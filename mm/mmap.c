/*
 * mm/mmap.c - 用户地址空间管理
 *
 * 功能：
 *   管理用户进程的虚拟地址空间。mm_struct 包含 pgd、brk、代码段范围
 *   以及 vma[NR_VMA] 固定数组。每个 vm_area_struct 描述一段连续虚拟区域。
 *
 * 主要函数：
 *   mm_alloc()           - 分配并初始化 mm_struct
 *   mm_destroy()         - 销毁用户地址空间
 *   mm_create_user_pgd() - 创建用户页表 + 复制内核映射 + 映射 MMIO
 *   find_vma()           - 查找包含指定地址的 VMA
 *   mm_brk()             - brk 内部实现（lazy allocation，不缩小）
 */

#include <kernel/mm.h>
#include <kernel/printk.h>
#include <kernel/string.h>
#include <kernel/slab.h>
#include <kernel/buddy.h>
#include <kernel/task.h>
#include <asm/page.h>
#include <asm/pte.h>
#include <asm/csr.h>
#include <drivers/uart.h>

/* ---- 内部辅助函数 ---- */

/*
 * vma_alloc_slot - 在 mm->vma[] 中找一个空闲槽位
 *
 * 线性扫描，返回空闲槽位指针，满则返回 NULL。
 */
static struct vm_area_struct *vma_alloc_slot(struct mm_struct *mm)
{
	for (int i = 0; i < NR_VMA; i++) {
		if (!mm->vma[i].used)
			return &mm->vma[i];
	}
	return NULL;
}

/*
 * free_user_page_tables - 释放用户页表中的所有页表页和映射的物理页
 * @pgd: 用户 PGD
 *
 * 遍历低 256 个 PGD 项（用户地址空间部分），释放每个有效 PMD 下
 * 的所有 PTE 页和映射的物理页，最后释放 PMD 页和 PGD 页本身。
 * 高 256 项是内核映射的共享引用，不释放。
 */
static void free_user_page_tables(pte_t *pgd)
{
	for (int i = 0; i < 256; i++) {
		if (!(pgd[i] & PTE_V))
			continue;

		pte_t *pmd = (pte_t *)__va(PTE_TO_PA(pgd[i]));
		for (int j = 0; j < 512; j++) {
			if (!(pmd[j] & PTE_V))
				continue;

			pte_t *pt = (pte_t *)__va(PTE_TO_PA(pmd[j]));
			for (int k = 0; k < 512; k++) {
				if (!(pt[k] & PTE_V))
					continue;

				paddr_t pa = PTE_TO_PA(pt[k]);
				/*
				 * mm_create_user_pgd() currently injects a low
				 * UART MMIO mapping so trap-time printk works
				 * while still running on the user page table.
				 * Only DRAM pages are owned by this mm; MMIO and
				 * other shared mappings must not be returned to
				 * the buddy allocator.
				 */
				if (pa >= DRAM_BASE && pa < DRAM_BASE + DRAM_SIZE)
					free_page(__va(pa), 0);
			}
			free_page(pt, 0);
		}
		free_page(pmd, 0);
	}
	free_page(pgd, 0);
}

/* ---- 公共接口 ---- */

struct mm_struct *mm_alloc(void)
{
	struct mm_struct *mm = kmalloc(sizeof(struct mm_struct));
	if (!mm)
		return NULL;

	memset(mm, 0, sizeof(struct mm_struct));
	return mm;
}

void mm_destroy(struct mm_struct *mm)
{
	if (!mm)
		return;

	/* 释放用户地址空间中所有映射的物理页和页表页 */
	if (mm->pgd)
		free_user_page_tables(mm->pgd);

	kfree(mm);
}

pte_t *mm_create_user_pgd(void)
{
	/* 1. 分配 PGD 页 */
	pte_t *user_pgd = (pte_t *)get_free_page(0);
	if (!user_pgd)
		return NULL;
	memset(user_pgd, 0, PAGE_SIZE);

	/* 2. 复制内核高地址映射（PGD[256-511]）
	 *    确保 trap 进内核后内核代码/数据仍可访问 */
	pte_t *kern_pgd = current_pgd();
	for (int i = 256; i < 512; i++)
		user_pgd[i] = kern_pgd[i];

	/* 3. 映射 UART MMIO（trap 处理中 printk 可能用到） */
	map_page(user_pgd, UART_BASE, UART_BASE, PTE_KERN_RW);

	return user_pgd;
}

struct vm_area_struct *find_vma(struct mm_struct *mm, uintptr_t addr)
{
	for (int i = 0; i < NR_VMA; i++) {
		if (mm->vma[i].used &&
		    addr >= mm->vma[i].vm_start &&
		    addr < mm->vma[i].vm_end)
			return &mm->vma[i];
	}
	return NULL;
}

/*
 * mm_brk - brk 内部实现
 * @mm:   进程地址空间描述符
 * @addr: 新的 brk 地址，0 表示查询
 *
 * 不允许缩小堆。仅更新 VMA 边界和 brk 指针，不分配物理页。
 * 实际的物理页在缺页时由 do_page_fault() 按需分配（lazy allocation）。
 * 如果当前没有堆 VMA，则创建一个新的。
 *
 * 返回新的 brk 值，失败返回当前 brk 值。
 */
uintptr_t mm_brk(struct mm_struct *mm, uintptr_t addr)
{
	/* 无地址空间 */
	if (!mm)
		return 0;

	/* 查询当前 brk */
	if (addr == 0)
		return mm->brk;

	/* 不允许缩小 */
	if (addr <= mm->brk)
		return mm->brk;

	/* 不允许超过用户地址空间上限 */
	if (addr > TASK_SIZE)
		return mm->brk;

	/* 查找堆 VMA（通过类型标记精确匹配） */
	uintptr_t old_brk = mm->brk;
	struct vm_area_struct *heap_vma = NULL;
	for (int i = 0; i < NR_VMA; i++) {
		if (mm->vma[i].used &&
		    mm->vma[i].vm_type == VMA_HEAP) {
			heap_vma = &mm->vma[i];
			break;
		}
	}

	if (!heap_vma) {
		/* 首次扩展：创建堆 VMA */
		heap_vma = vma_alloc_slot(mm);
		if (!heap_vma)
			return old_brk;
		heap_vma->vm_start = old_brk;
		heap_vma->vm_end = addr;
		heap_vma->vm_flags = VM_READ | VM_WRITE;
		heap_vma->vm_type = VMA_HEAP;
		heap_vma->used = true;
	} else {
		/* 扩展已有堆 VMA */
		heap_vma->vm_end = addr;
	}

	mm->brk = addr;
	return addr;
}
