/*
 * mm/vmalloc.c - vmalloc 区虚拟内存分配
 *
 * 功能：
 *   管理内核虚拟地址空间中的 vmalloc 区域（128MB，位于直接映射区之后）。
 *   vmalloc 分配的虚拟地址在物理上不要求连续，通过修改内核页表建立
 *   非连续物理页到连续虚拟地址的映射。
 *
 *   本模块使用场景有限，采用简单实现。适用于需要大块虚拟连续内存但
 *   物理上可离散的场景。
 *
 * 主要函数：
 *   vmalloc_init()   - 初始化 vmalloc 区域管理结构
 *   vmalloc(size)    - 分配指定大小的虚拟连续内存
 *   vfree(ptr)       - 释放 vmalloc 分配的内存及相关页表映射
 */

#include <kernel/vmalloc.h>
#include <kernel/bitops.h>
#include <kernel/buddy.h>
#include <kernel/errno.h>
#include <kernel/list.h>
#include <kernel/printk.h>
#include <kernel/slab.h>
#include <asm/csr.h>
#include <asm/page.h>
#include <asm/pte.h>

#define VMALLOC_SIZE (128UL << 20)

struct vmalloc_area {
	uintptr_t start;
	uintptr_t end;
	bool free;
	struct list_head node;
};

static uintptr_t vmalloc_start;
static uintptr_t vmalloc_end;
static bool vmalloc_ready;
static LIST_HEAD(vmalloc_areas);

static void vmalloc_unmap_pages(uintptr_t start, uintptr_t end)
{
	pte_t *root = kernel_pt();

	for (uintptr_t va = start; va < end; va += PAGE_SIZE) {
		pte_t *pte = arch_pt_lookup(root, va);
		paddr_t pa;

		if (!pte || !(*pte & PTE_V))
			continue;

		pa = PTE_TO_PA(*pte);
		*pte = 0;
		arch_tlb_flush_page(va);
		free_page(__va(pa), 0);
	}
}

static struct vmalloc_area *vmalloc_find_area(uintptr_t start)
{
	struct vmalloc_area *area;

	list_for_each_entry (area, &vmalloc_areas, node) {
		if (area->start == start)
			return area;
	}

	return NULL;
}

static size_t vmalloc_area_size(const struct vmalloc_area *area)
{
	return area->end - area->start;
}

static struct vmalloc_area *vmalloc_find_free_area(size_t size)
{
	struct vmalloc_area *area;

	list_for_each_entry (area, &vmalloc_areas, node) {
		if (area->free && vmalloc_area_size(area) >= size)
			return area;
	}

	return NULL;
}

static int vmalloc_split_area(struct vmalloc_area *area, size_t size)
{
	struct vmalloc_area *tail;

	if (vmalloc_area_size(area) == size)
		return 0;

	tail = kmalloc(sizeof(*tail));
	if (!tail)
		return -ENOMEM;

	tail->start = area->start + size;
	tail->end = area->end;
	tail->free = true;
	INIT_LIST_HEAD(&tail->node);

	area->end = tail->start;
	list_add(&tail->node, &area->node);
	return 0;
}

static struct vmalloc_area *vmalloc_prev_area(struct vmalloc_area *area)
{
	if (area->node.prev == &vmalloc_areas)
		return NULL;
	return list_entry(area->node.prev, struct vmalloc_area, node);
}

static struct vmalloc_area *vmalloc_next_area(struct vmalloc_area *area)
{
	if (area->node.next == &vmalloc_areas)
		return NULL;
	return list_entry(area->node.next, struct vmalloc_area, node);
}

static void vmalloc_merge_area(struct vmalloc_area *area)
{
	struct vmalloc_area *prev = vmalloc_prev_area(area);
	struct vmalloc_area *next;

	if (prev && prev->free && prev->end == area->start) {
		prev->end = area->end;
		list_del(&area->node);
		kfree(area);
		area = prev;
	}

	next = vmalloc_next_area(area);
	if (next && next->free && area->end == next->start) {
		area->end = next->end;
		list_del(&next->node);
		kfree(next);
	}
}

void vmalloc_init(void)
{
	struct vmalloc_area *area;

	if (vmalloc_ready)
		return;

	area = kmalloc(sizeof(*area));
	BUG_ON(!area);

	vmalloc_start =
		ALIGN_UP(KERNEL_VBASE + DRAM_BASE + DRAM_SIZE, PAGE_SIZE);
	vmalloc_end = vmalloc_start + VMALLOC_SIZE;
	INIT_LIST_HEAD(&vmalloc_areas);
	area->start = vmalloc_start;
	area->end = vmalloc_end;
	area->free = true;
	INIT_LIST_HEAD(&area->node);
	list_add_tail(&area->node, &vmalloc_areas);
	vmalloc_ready = true;

	pr_info("vmalloc: area [%p, %p)\n", (void *)vmalloc_start,
		(void *)vmalloc_end);
}

void *vmalloc(size_t size)
{
	struct vmalloc_area *area;
	uintptr_t start;
	uintptr_t end;
	int ret;

	if (size == 0)
		return NULL;
	BUG_ON(!vmalloc_ready);
	if (size > VMALLOC_SIZE)
		return NULL;

	size = ALIGN_UP(size, PAGE_SIZE);
	if (size == 0 || size > VMALLOC_SIZE)
		return NULL;

	area = vmalloc_find_free_area(size);
	if (!area)
		return NULL;
	ret = vmalloc_split_area(area, size);
	if (ret < 0)
		return NULL;

	start = area->start;
	end = area->end;

	for (uintptr_t va = start; va < end; va += PAGE_SIZE) {
		void *page = get_free_page(0);

		if (!page)
			goto fail;

		memset(page, 0, PAGE_SIZE);
		ret = map_page(kernel_pt(), va, __pa((uintptr_t)page),
			       PTE_KERN_RW);
		if (ret < 0) {
			free_page(page, 0);
			goto fail;
		}
	}

	area->free = false;
	return (void *)start;

fail:
	vmalloc_unmap_pages(start, end);
	area->free = true;
	vmalloc_merge_area(area);
	return NULL;
}

void vfree(void *ptr)
{
	struct vmalloc_area *area;
	uintptr_t start;

	start = (uintptr_t)ptr;
	area = vmalloc_find_area(start);
	if (!area)
		panic("vfree: invalid address %p", ptr);
	if (area->free)
		panic("vfree: double free %p", ptr);

	vmalloc_unmap_pages(area->start, area->end);
	area->free = true;
	vmalloc_merge_area(area);
}
