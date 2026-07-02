/*
 * mm/slab.c - SLAB 对象缓存
 *
 * 功能：
 *   在伙伴系统之上实现简化版 SLAB 分配器。提供 8 个固定大小级别：
 *   16 / 32 / 64 / 128 / 256 / 512 / 1024 / 2048 字节。每个 kmem_cache
 *   维护一个 free_list 空闲对象链表。无 full/partial/empty 链表跟踪。
 *
 * 数据结构：
 *   kmem_cache {obj_size, free_list}  - 共 8 个缓存，对应 8 个大小级别
 *
 * Slot 布局（每个 slot = 8 字节头 + obj_size 字节）：
 *   +0  uint32_t cache_idx  — 所属缓存索引，kfree 时用于定位缓存
 *   +4  uint32_t _pad       — 对齐填充
 *   +8  用户数据 / list_head — 空闲时前 16 字节作为 list_head
 *
 * 分配流程：
 *   kmalloc(size) 遍历 8 个缓存，找到第一个 obj_size >= 请求大小的缓存，
 *   从其 free_list 取出对象返回。若 free_list 为空，则向伙伴系统
 *   请求一个物理页，按 slot 大小切割为多个对象挂入 free_list。
 *
 * 回收流程：
 *   kfree(ptr) 读取 ptr 前 8 字节处的 cache_idx，确定所属缓存，
 *   将对象归还 free_list。不回收物理页回伙伴系统（简单实现）。
 */

#include <kernel/slab.h>
#include <kernel/buddy.h>
#include <kernel/printk.h>
#include <kernel/list.h>
#include <asm/page.h>

/* ---- 常量 ---- */

#define NR_CACHES     8
#define SLOT_HDR_SIZE 8 /* cache_idx(4) + pad(4) */

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

struct slab_slot_header {
	uint32_t cache_idx;
	uint32_t pad;
};

static_assert(sizeof(struct slab_slot_header) == SLOT_HDR_SIZE,
	      "slab slot header size changed");
static_assert(SLOT_HDR_SIZE % __alignof__(struct list_head) == 0,
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
	if (!page)
		return;

	size_t slot_size = SLOT_HDR_SIZE + cache->obj_size;
	size_t nr_objs = PAGE_SIZE / slot_size;

	for (size_t i = 0; i < nr_objs; i++) {
		char *slot = (char *)page + i * slot_size;
		struct slab_slot_header *hdr =
			(struct slab_slot_header *)(uintptr_t)slot;
		struct list_head *node =
			(struct list_head *)(uintptr_t)(slot + SLOT_HDR_SIZE);

		/* 在 slot 头部写入缓存索引，供 kfree 定位 */
		hdr->cache_idx = cache_idx;
		/* 用户数据区起始 = slot + SLOT_HDR_SIZE，
		 * 空闲时作为 list_head 使用 */
		INIT_LIST_HEAD(node);
		list_add_tail(node, &cache->free_list);
	}
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
	if (idx < 0)
		return NULL;

	struct kmem_cache *cache = &caches[idx];

	if (list_empty(&cache->free_list)) {
		refill_cache(cache, (uint32_t)idx);
		if (list_empty(&cache->free_list))
			return NULL;
	}

	/* free_list.next 指向第一个空闲 slot 的用户数据区 */
	struct list_head *node = cache->free_list.next;
	list_del(node);

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

	struct slab_slot_header *hdr =
		(struct slab_slot_header *)(uintptr_t)((char *)ptr - SLOT_HDR_SIZE);
	uint32_t cache_idx = hdr->cache_idx;

	if (cache_idx >= NR_CACHES)
		panic("kfree: invalid cache index %d", (int)cache_idx);

	struct list_head *node = (struct list_head *)(uintptr_t)ptr;
	list_add(node, &caches[cache_idx].free_list);
}
