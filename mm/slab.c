/*
 * mm/slab.c - SLAB 对象缓存
 *
 * 功能：
 *   在伙伴系统之上实现简化版 SLAB 分配器。提供 8 个固定大小级别：
 *   16 / 32 / 64 / 128 / 256 / 512 / 1024 / 2048 字节。每个 kmem_cache
 *   维护一个 free_list 空闲对象链表。slab 页记录空闲对象计数，整页空闲
 *   时返回 buddy。
 *
 * 数据结构：
 *   kmem_cache {obj_size, free_list}  - 共 8 个缓存，对应 8 个大小级别
 *
 * 页布局：
 *   slab_page_header | slot[]，每个 slot = kmalloc_header + obj_size。
 *   slot 空闲时用户数据区作为 list_head 挂入 cache free_list。
 *
 * 分配流程：
 *   kmalloc(size) 遍历 8 个缓存，找到第一个 obj_size >= 请求大小的缓存，
 *   从其 free_list 取出对象返回。若 free_list 为空，则向伙伴系统
 *   请求一个物理页，按 slot 大小切割为多个对象挂入 free_list。
 *
 * 回收流程：
 *   kfree(ptr) 读取 ptr 前面的 slot header，确定所属 slab 页并增加空闲
 *   计数。整页空闲时先摘除该页所有 free_list 节点，再释放页回 buddy。
 */

#include <kernel/slab.h>
#include <kernel/buddy.h>
#include <kernel/bitops.h>
#include <kernel/printk.h>
#include <kernel/list.h>
#include <kernel/compiler.h>
#include <asm/page.h>

/* ---- 常量 ---- */

#define NR_CACHES     8
#define KMALLOC_MAGIC 0x6b6d616cU /* "kmal" */
#define KMALLOC_SLAB  0x51abU
#define KMALLOC_LARGE 0x1a9eU

static const size_t cache_sizes[NR_CACHES] = {16,  32,	64,   128,
					      256, 512, 1024, 2048};

/* ---- 数据结构 ---- */

/**
 * struct kmem_cache - 固定大小对象缓存
 * @obj_size:  单个对象的用户可用字节数
 * @free_list: 空闲对象链表（节点嵌入在 slot 的用户数据区域）
 */
struct kmem_cache {
	size_t obj_size;
	struct list_head free_list;
};

struct slab_page_header {
	struct kmem_cache *cache;
	size_t total_objs;
	size_t free_objs;
	uint32_t cache_idx;
};

struct kmalloc_header {
	uint32_t magic;
	uint32_t kind;
	uint32_t order;
	union {
		struct {
			struct slab_page_header *slab;
			bool free;
		} slab;
		struct {
			size_t size;
			bool free;
		} large;
	};
};

static_assert(sizeof(struct kmalloc_header) % alignof(struct list_head) == 0,
	      "slab free-list nodes must be naturally aligned");

static struct kmem_cache caches[NR_CACHES];

/* ---- 内部辅助 ---- */

/**
 * find_cache - 为请求大小找到最小的合适缓存索引
 * @size: 请求字节数
 *
 * 返回缓存索引（0~NR_CACHES-1），无匹配返回 -1。
 */
static int find_cache(size_t size)
{
	if (size == 0)
		return -1;
	for (int i = 0; i < NR_CACHES; i++) {
		if (cache_sizes[i] >= size)
			return i;
	}
	return -1;
}

/**
 * refill_cache - 从 buddy 分配一页并切割为对象挂入空闲链表
 * @cache:     目标缓存
 * @cache_idx: 缓存在 caches[] 中的索引
 */
static void refill_cache(struct kmem_cache *cache, uint32_t cache_idx)
{
	void *page = get_free_page(0);
	struct slab_page_header *slab;
	struct page *meta;
	uintptr_t cursor;
	size_t slot_size;
	size_t nr_objs;

	if (!page)
		return;

	slab = page;
	meta = virt_to_page(page);
	BUG_ON(!meta);
	page_set_flag(meta, PG_SLAB);
	slot_size = sizeof(struct kmalloc_header) + cache->obj_size;
	cursor = ALIGN_UP((uintptr_t)page + sizeof(*slab),
			  alignof(struct kmalloc_header));
	nr_objs = ((uintptr_t)page + PAGE_SIZE - cursor) / slot_size;
	if (nr_objs == 0) {
		page_clear_flag(meta, PG_SLAB);
		free_page(page, 0);
		return;
	}

	slab->cache = cache;
	slab->total_objs = nr_objs;
	slab->free_objs = nr_objs;
	slab->cache_idx = cache_idx;

	for (size_t i = 0; i < nr_objs; i++) {
		char *slot = (char *)cursor + i * slot_size;
		struct kmalloc_header *hdr =
			(struct kmalloc_header *)(uintptr_t)slot;
		struct list_head *node =
			(struct list_head *)(uintptr_t)(slot + sizeof(*hdr));

		hdr->magic = KMALLOC_MAGIC;
		hdr->kind = KMALLOC_SLAB;
		hdr->order = 0;
		hdr->slab.slab = slab;
		hdr->slab.free = true;
		INIT_LIST_HEAD(node);
		list_add_tail(node, &cache->free_list);
	}
}

static void slab_reclaim_page(struct slab_page_header *slab)
{
	struct kmem_cache *cache;
	struct page *meta;
	uintptr_t cursor;
	size_t slot_size;

	BUG_ON(!slab);
	cache = slab->cache;
	meta = virt_to_page(slab);
	cursor = ALIGN_UP((uintptr_t)slab + sizeof(*slab),
			  alignof(struct kmalloc_header));
	slot_size = sizeof(struct kmalloc_header) + cache->obj_size;

	BUG_ON(!meta);
	for (size_t i = 0; i < slab->total_objs; i++) {
		struct kmalloc_header *hdr =
			(struct kmalloc_header *)(cursor + i * slot_size);
		struct list_head *node = (struct list_head *)(hdr + 1);

		BUG_ON(hdr->magic != KMALLOC_MAGIC ||
		       hdr->kind != KMALLOC_SLAB);
		BUG_ON(!hdr->slab.free);
		list_del(node);
	}

	page_clear_flag(meta, PG_SLAB);
	free_page(slab, 0);
}

static uint32_t kmalloc_large_order(size_t size)
{
	if (size > UINT64_MAX - sizeof(struct kmalloc_header))
		return MAX_ORDER + 1;

	size += sizeof(struct kmalloc_header);
	for (uint32_t order = 0; order <= MAX_ORDER; order++) {
		if (((size_t)PAGE_SIZE << order) >= size)
			return order;
	}

	return MAX_ORDER + 1;
}

static void *kmalloc_large(size_t size)
{
	uint32_t order = kmalloc_large_order(size);
	struct kmalloc_header *hdr;

	if (order > MAX_ORDER)
		return NULL;

	hdr = get_free_page(order);
	if (!hdr)
		return NULL;

	hdr->magic = KMALLOC_MAGIC;
	hdr->kind = KMALLOC_LARGE;
	hdr->order = order;
	hdr->large.size = size;
	hdr->large.free = false;
	return hdr + 1;
}

/* ---- 公共接口 ---- */

/**
 * slab_init - 初始化 SLAB 分配器
 *
 * 创建 8 个 kmem_cache，初始化空闲链表，执行自测。
 */
void slab_init(void)
{
	for (int i = 0; i < NR_CACHES; i++) {
		caches[i].obj_size = cache_sizes[i];
		INIT_LIST_HEAD(&caches[i].free_list);
	}

	pr_info("slab: %d caches initialized (16..2048 bytes)\n", NR_CACHES);
}

/**
 * kmalloc - 分配内核对象
 * @size: 请求字节数
 *
 * 找到满足 size 的最小缓存，从 free_list 取一个空闲对象。
 * free_list 为空时向 buddy 请求新页并切割。
 * 返回对象指针，失败返回 NULL。
 */
void *kmalloc(size_t size)
{
	int idx = find_cache(size);
	if (size == 0)
		return NULL;
	if (idx < 0)
		return kmalloc_large(size);

	struct kmem_cache *cache = &caches[idx];

	if (list_empty(&cache->free_list)) {
		refill_cache(cache, (uint32_t)idx);
		if (list_empty(&cache->free_list))
			return NULL;
	}

	/* free_list.next 指向第一个空闲 slot 的用户数据区 */
	struct list_head *node = cache->free_list.next;
	list_del(node);
	struct kmalloc_header *hdr =
		(struct kmalloc_header *)(uintptr_t)((char *)node -
						     sizeof(*hdr));

	BUG_ON(hdr->magic != KMALLOC_MAGIC || hdr->kind != KMALLOC_SLAB);
	BUG_ON(!hdr->slab.free);
	hdr->slab.free = false;
	hdr->slab.slab->free_objs--;

	return (void *)node;
}

/**
 * kfree - 释放 kmalloc 分配的对象
 * @ptr: kmalloc 返回的指针
 *
 * 从 ptr 前 8 字节读取 cache_idx，归还到对应缓存的 free_list。
 * 传入 NULL 安全返回。
 */
void kfree(void *ptr)
{
	if (!ptr)
		return;

	struct kmalloc_header *hdr =
		(struct kmalloc_header *)(uintptr_t)((char *)ptr -
						     sizeof(*hdr));
	if (hdr->magic != KMALLOC_MAGIC)
		panic("kfree: invalid object");
	if (hdr->kind == KMALLOC_LARGE) {
		if (hdr->large.free)
			panic("kfree: double free");
		hdr->large.free = true;
		free_page(hdr, hdr->order);
		return;
	}
	if (hdr->kind != KMALLOC_SLAB)
		panic("kfree: invalid object kind %u", hdr->kind);

	struct slab_page_header *slab = hdr->slab.slab;
	uint32_t cache_idx = slab->cache_idx;

	if (cache_idx >= NR_CACHES)
		panic("kfree: invalid cache index %d", (int)cache_idx);
	if (hdr->slab.free)
		panic("kfree: double free");

	struct list_head *node = (struct list_head *)(uintptr_t)ptr;
	hdr->slab.free = true;
	slab->free_objs++;
	list_add(node, &caches[cache_idx].free_list);

	if (slab->free_objs == slab->total_objs)
		slab_reclaim_page(slab);
}
