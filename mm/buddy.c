/*
 * mm/buddy.c - 伙伴系统（物理页分配）
 *
 * 功能：
 *   实现经典的伙伴分配算法管理物理内存页框。max_order=9（最大块 2MB），
 *   即 free_area[10]。struct page 包含 flags、order、refcount、lru 字段。
 *   mem_map 数组位于内核映像 _end 之后，覆盖全部物理页。分配失败（OOM）
 *   时返回 NULL。
 *
 * 数据结构：
 *   struct page {flags, order, refcount, lru}
 *   struct free_area {free_list, nr_free}  free_area[MAX_ORDER + 1]
 *   mem_map[]  - 全局 struct page 数组，紧接 early allocator 结束位置
 *
 * 主要函数：
 *   buddy_init()             - 从 arch_bootmem_end() 到 DRAM 末尾初始化，
 *                               将可用物理内存区域按最大可能阶数加入空闲链表。
 *   get_free_page(order)     - 分配 2^order 个连续物理页，返回首页虚拟地址。
 *   free_page(addr, order)   - 释放指定地址的页块，并尝试伙伴合并。
 *
 * 合并策略：
 *   释放时计算伙伴地址，若伙伴同样空闲且 order 相同则合并，递归向上
 *   直到无法合并或达到 max_order 为止。
 */

#include <kernel/buddy.h>
#include <kernel/page.h>
#include <kernel/printk.h>
#include <kernel/bitops.h>
#include <kernel/tools.h>
#include <asm/page.h>

/* ---- 全局数据 ---- */

struct page *mem_map;
struct free_area free_area[MAX_ORDER + 1];
static size_t total_pages;
static size_t nr_free_pages;

/* ---- 内联辅助 ---- */

/**
 * page_to_pfn - 由 mem_map 下标得到页帧号（即下标本身）
 */
static inline size_t page_to_pfn(const struct page *page)
{
	return (size_t)(page - mem_map);
}

/**
 * pfn_to_page - 由页帧号得到 struct page 指针
 */
static inline struct page *pfn_to_page(size_t pfn)
{
	return &mem_map[pfn];
}

/**
 * pfn_to_virt - 页帧号转内核虚拟地址
 */
static inline void *pfn_to_virt(size_t pfn)
{
	return __va(DRAM_BASE + pfn * PAGE_SIZE);
}

/**
 * virt_to_pfn - 内核虚拟地址转页帧号
 */
static inline size_t virt_to_pfn(void *addr)
{
	return (__pa((uintptr_t)addr) - DRAM_BASE) / PAGE_SIZE;
}

static bool virt_to_pfn_checked(const void *addr, size_t *pfn)
{
	uintptr_t va = (uintptr_t)addr;
	uintptr_t direct_start = (uintptr_t)__va(DRAM_BASE);
	uintptr_t direct_end = direct_start + DRAM_SIZE;

	if (!addr || !IS_ALIGNED(va, PAGE_SIZE))
		return false;
	if (va < direct_start || va >= direct_end)
		return false;

	*pfn = (va - direct_start) / PAGE_SIZE;
	return *pfn < total_pages;
}

static void buddy_add_free_block(size_t pfn, uint32_t order, bool tail)
{
	struct page *page = pfn_to_page(pfn);

	page->flags = BIT(PG_BUDDY);
	page->order = order;
	page->refcount = 0;
	if (tail)
		list_add_tail(&page->lru, &free_area[order].free_list);
	else
		list_add(&page->lru, &free_area[order].free_list);
	free_area[order].nr_free++;
}

static void buddy_remove_free_block(struct page *page)
{
	BUG_ON(!page_test_flag(page, PG_BUDDY));
	list_del(&page->lru);
	page_clear_flag(page, PG_BUDDY);
	free_area[page->order].nr_free--;
}

/* ---- 公共接口 ---- */

/**
 * buddy_init - 初始化伙伴系统
 *
 * 内存布局（物理视角）：
 *   DRAM_BASE				← 内核映像 + 页表
 *   arch_bootmem_end()		← mem_map 起始
 *   mem_map + total_pages*sizeof(page) ← 可用页起始（4KB 对齐）
 *   DRAM_BASE + DRAM_SIZE		← DRAM 结束
 *
 * 步骤：
 *   1. 在 arch_bootmem_end() 处放置 mem_map 数组
 *   2. 将所有页标记为 PRESERVED（不可分配）
 *   3. 初始化 free_area 空闲链表
 *   4. 从首个可用页开始，按最大对齐阶数将连续块加入空闲链表
 */
void buddy_init(void)
{
	void *mem_start = arch_bootmem_end();

	total_pages = DRAM_SIZE / PAGE_SIZE;

	/* 1. 放置 mem_map */
	mem_map = (struct page *)mem_start;

	/* 2. 初始化所有页描述符为 reserved */
	for (size_t i = 0; i < total_pages; i++) {
		mem_map[i].flags = BIT(PG_RESERVED);
		mem_map[i].order = 0;
		mem_map[i].refcount = 0;
		INIT_LIST_HEAD(&mem_map[i].lru);
	}

	/* 3. 初始化 free_area */
	for (int i = 0; i <= MAX_ORDER; i++) {
		INIT_LIST_HEAD(&free_area[i].free_list);
		free_area[i].nr_free = 0;
	}

	/* 4. 计算可用页范围 */
	uintptr_t mem_map_bytes = total_pages * sizeof(struct page);
	vaddr_t free_start_va =
		ALIGN_UP((uintptr_t)mem_start + mem_map_bytes, PAGE_SIZE);
	paddr_t free_start_pa = __pa(free_start_va);
	size_t free_idx = (free_start_pa - DRAM_BASE) / PAGE_SIZE;
	size_t remaining = total_pages - free_idx;

	/* 5. 将可用页按最大对齐阶数分块加入空闲链表 */
	nr_free_pages = 0;
	size_t idx = free_idx;

	while (remaining > 0) {
		/* 找当前 idx 处可放入的最大阶数 */
		uint32_t order = MAX_ORDER;
		while (order > 0 && ((idx & ((1UL << order) - 1)) != 0 ||
				     remaining < (1UL << order)))
			order--;

		buddy_add_free_block(idx, order, true);

		nr_free_pages += (1UL << order);
		idx += (1UL << order);
		remaining -= (1UL << order);
	}

	pr_info("buddy: %d pages free (%d MB)\n", (int)nr_free_pages,
		(int)(nr_free_pages * PAGE_SIZE >> 20));
}

/**
 * get_free_page - 分配 2^order 个连续物理页
 * @order: 分配阶数
 *
 * 从 free_area[order] 取一个空闲块；若为空则从更高阶拆分。
 * 返回首页的内核虚拟地址，OOM 返回 NULL。
 */
void *get_free_page(uint32_t order)
{
	if (order > MAX_ORDER)
		return NULL;

	/* 在 order 及以上找一个非空链表 */
	uint32_t cur = order;
	while (cur <= MAX_ORDER && list_empty(&free_area[cur].free_list))
		cur++;

	if (cur > MAX_ORDER)
		return NULL; /* OOM */

	/* 从链表取出一个块 */
	struct page *page =
		list_entry(free_area[cur].free_list.next, struct page, lru);
	buddy_remove_free_block(page);

	/* 向下拆分直到目标阶数 */
	while (cur > order) {
		cur--;
		/* 后半块作为新的空闲块加入 free_area[cur] */
		size_t buddy_pfn = page_to_pfn(page) + (1UL << cur);

		buddy_add_free_block(buddy_pfn, cur, false);
	}

	/* 标记为已分配 */
	page->flags = 0;
	page->order = order;
	page->refcount = 1;

	nr_free_pages -= (1UL << order);

	return pfn_to_virt(page_to_pfn(page));
}

/**
 * free_page - 释放页块并尝试伙伴合并
 * @addr:  get_free_page 返回的内核虚拟地址
 * @order: 释放的阶数，须与分配时一致
 *
 * 释放后递归尝试与伙伴合并，直到伙伴不空闲或达到 MAX_ORDER。
 */
void free_page(void *addr, uint32_t order)
{
	size_t freed_pages;
	size_t pfn;
	struct page *page;

	if (order > MAX_ORDER)
		panic("free_page: order %d > MAX_ORDER", order);

	freed_pages = 1UL << order;

	if (!virt_to_pfn_checked(addr, &pfn))
		panic("free_page: invalid page address %p", addr);
	if (pfn + freed_pages > total_pages)
		panic("free_page: pfn %zu order %u out of range", pfn, order);
	if (pfn & (freed_pages - 1))
		panic("free_page: pfn %zu is not order %u aligned", pfn, order);

	page = pfn_to_page(pfn);
	if (page_test_flag(page, PG_BUDDY))
		panic("free_page: double free pfn %zu", pfn);
	if (page_test_flag(page, PG_RESERVED))
		panic("free_page: reserved pfn %zu", pfn);
	if (page_test_flag(page, PG_SLAB))
		panic("free_page: slab pfn %zu", pfn);
	if (page->refcount == 0)
		panic("free_page: unallocated pfn %zu", pfn);
	if (page->order != order)
		panic("free_page: pfn %zu order %u != %u", pfn, page->order,
		      order);

	/* 标记为空闲 */
	page->flags = 0;
	page->refcount = 0;

	/* 尝试向上合并 */
	while (order < MAX_ORDER) {
		size_t buddy_pfn = pfn ^ (1UL << order);

		/* buddy 超出范围，停止合并 */
		if (buddy_pfn >= total_pages)
			break;

		struct page *buddy = pfn_to_page(buddy_pfn);

		/* buddy 必须空闲且同阶才能合并 */
		if (!page_test_flag(buddy, PG_BUDDY))
			break;
		if (buddy->order != order)
			break;

		/* 合并：从空闲链表中摘除 buddy */
		buddy_remove_free_block(buddy);

		/* 取较低 pfn 作为合并后的块首 */
		pfn = pfn < buddy_pfn ? pfn : buddy_pfn;
		order++;
	}

	/* 将合并后的块加入对应阶的空闲链表 */
	buddy_add_free_block(pfn, order, false);

	nr_free_pages += freed_pages;
}

size_t buddy_free_pages(void)
{
	return nr_free_pages;
}

struct page *virt_to_page(const void *addr)
{
	size_t pfn;

	if (!virt_to_pfn_checked(addr, &pfn))
		return NULL;
	return pfn_to_page(pfn);
}

void *page_to_virt(const struct page *page)
{
	BUG_ON(page < mem_map || page >= mem_map + total_pages);
	return pfn_to_virt(page_to_pfn(page));
}
