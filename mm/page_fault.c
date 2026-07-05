/*
 * mm/page_fault.c - 缺页异常处理
 *
 * 功能：
 *   处理由 MMU 触发的缺页异常（Page Fault）。仅处理两种情况：
 *   - 合法缺页（legal fault）：虚拟地址落在合法 VMA 内，分配物理页
 *     并建立映射。
 *   - 非法访问：向进程发送 SIGSEGV（当前直接 do_exit）。
 *
 *   支持三种缺页类型：
 *     - 指令缺页（scause=12）：检查 VMA 的 VM_EXEC 权限
 *     - 加载缺页（scause=13）：检查 VMA 的 VM_READ 权限
 *     - 存储缺页（scause=15）：检查 VMA 的 VM_WRITE 权限
 *
 * 内核态缺页：
 *   支持内核态下由 copy_to_user / copy_from_user 触发的缺页，
 *   此时自动为对应的用户页分配物理页（而非发送信号）。
 *   若内核态访问非法地址，打印警告后 do_exit 当前进程。
 *
 * 主要函数：
 *   do_page_fault(tf) - 缺页异常总入口。读取 stval 获取出错虚拟地址，
 *             读取 scause 区分读/写缺页类型，查找 VMA 判断合法性，
 *             调用对应处理（分配页面或终止进程）。
 */

#include <kernel/mm.h>
#include <kernel/buddy.h>
#include <kernel/errno.h>
#include <kernel/exit.h>
#include <kernel/page_cache.h>
#include <kernel/printk.h>
#include <kernel/signal.h>
#include <kernel/task.h>
#include <kernel/page.h>
#include <kernel/pgtable.h>
#include <kernel/trap.h>
#include <kernel/processor.h>

#include "internal.h"

/* ---- 内部辅助函数 ---- */

/*
 * check_vma_permission - 检查访问类型是否与 VMA 权限匹配
 * @access: 访问类型
 * @vma:    匹配到的 VMA
 *
 * 返回 true 表示权限匹配，false 表示权限冲突。
 */
static bool check_vma_permission(int access, struct vm_area_struct *vma)
{
	switch (access) {
	case USER_FAULT_EXEC:
		return (vma->vm_flags & VM_EXEC) != 0;
	case USER_FAULT_READ:
		return (vma->vm_flags & VM_READ) != 0;
	case USER_FAULT_WRITE:
		return (vma->vm_flags & VM_WRITE) != 0;
	default:
		return false;
	}
}

static bool pte_allows_fault(int access, pte_t pte)
{
	if (!pte_is_user_page(pte))
		return false;

	switch (access) {
	case USER_FAULT_EXEC:
		return pte_allows_user_exec(pte);
	case USER_FAULT_READ:
		return pte_allows_user_read(pte);
	case USER_FAULT_WRITE:
		return pte_allows_user_write(pte);
	default:
		return false;
	}
}

static void signal_or_exit_segv(bool from_user_mode)
{
	if (from_user_mode) {
		if (force_signal(SIGSEGV, current_task()) < 0)
			do_exit(SIGNAL_EXIT_CODE(SIGSEGV));
		return;
	}

	do_exit(SIGNAL_EXIT_CODE(SIGSEGV));
}

static int fault_in_user_page_locked(struct mm_struct *mm, uintptr_t fault_addr,
				     int access, pte_t *fault_pte)
{
	struct vm_area_struct *vma = find_vma(mm, fault_addr);
	uintptr_t page_addr;
	pte_t *existing;

	if (!vma)
		return -EFAULT;
	if (!check_vma_permission(access, vma))
		return -EFAULT;

	page_addr = fault_addr & PAGE_MASK;
	existing = pagetable_lookup(mm->pgd, page_addr);

		if (existing && pte_is_present(*existing)) {
		if (pte_allows_fault(access, *existing)) {
			flush_tlb_page(page_addr);
			return 0;
		}

		if (fault_pte)
			*fault_pte = *existing;
		return -EFAULT;
	}

	if (vma->vm_file) {
		struct page_cache *file_page;
		uint64_t page_index;
			pgprot_t pte_flags;

		page_index = vma_page_index(vma, page_addr);
		file_page = page_cache_read_page(
			&vma->vm_file->f_inode->i_pages, page_index);
		if (!file_page)
			return -EIO;

		pte_flags = vma_flags_to_pte(vma->vm_flags);
		if (vma->vm_shared) {
			int ret = map_page(
				mm->pgd, page_addr,
				__pa((uintptr_t)page_cache_data(file_page)),
				pte_flags);
			if (ret < 0) {
				page_cache_put_page(file_page);
				return ret;
			}
			flush_tlb_page(page_addr);
			return 0;
		}

		void *page = get_free_page(0);
		if (!page) {
			page_cache_put_page(file_page);
			return -ENOMEM;
		}

		memcpy(page, page_cache_data(file_page), PAGE_SIZE);
		if (page_addr < vma->vm_start)
			memset(page, 0, vma->vm_start - page_addr);
		if (page_addr + PAGE_SIZE > vma->vm_end) {
			uintptr_t keep = vma->vm_end - page_addr;

			memset((uint8_t *)page + keep, 0, PAGE_SIZE - keep);
		}
		page_cache_put_page(file_page);
		int ret = map_page(mm->pgd, page_addr, __pa((uintptr_t)page),
				   pte_flags);
		if (ret < 0) {
			free_page(page, 0);
			return ret;
		}
		flush_tlb_page(page_addr);
		return 0;
	}

	void *page = get_free_page(0);
	if (!page)
		return -ENOMEM;

	memset(page, 0, PAGE_SIZE);
	int ret = map_page(mm->pgd, page_addr, __pa((uintptr_t)page),
			   vma_flags_to_pte(vma->vm_flags));
	if (ret < 0) {
		free_page(page, 0);
		return ret;
	}
	flush_tlb_page(page_addr);
	return 0;
}

/* ---- 公共接口 ---- */

int fault_in_user_range(struct mm_struct *mm, uintptr_t addr, size_t size,
			int access)
{
	uintptr_t range_end;

	if (size == 0)
		return 0;
	if (!mm)
		return -EFAULT;
	if (!access_ok((const void *)addr, size))
		return -EFAULT;

	range_end = addr + size;
	for (uintptr_t cursor = addr; cursor < range_end;) {
		struct vm_area_struct *vma;
		uintptr_t segment_end;
		uintptr_t va;
		uintptr_t page_end;
		int ret = 0;

		mm_lock(mm);
		vma = find_vma(mm, cursor);
		if (!vma) {
			mm_unlock(mm);
			return -EFAULT;
		}
		if (!check_vma_permission(access, vma)) {
			mm_unlock(mm);
			return -EFAULT;
		}

		segment_end = vma->vm_end < range_end ? vma->vm_end : range_end;
		if (segment_end <= cursor) {
			mm_unlock(mm);
			return -EFAULT;
		}

		va = cursor & PAGE_MASK;
		page_end = mm_page_align_up(segment_end);
		while (va < page_end) {
			uintptr_t fault_addr = va < cursor ? cursor : va;

			ret = fault_in_user_page_locked(mm, fault_addr, access,
							NULL);
			if (ret < 0)
				break;
			va += PAGE_SIZE;
		}
		mm_unlock(mm);

		if (ret < 0)
			return ret;

		cursor = segment_end;
	}

	return 0;
}

/*
 * do_page_fault - 缺页异常处理总入口
 * @tf: 指向当前 trap_frame
 *
 * 处理流程：
 *   1. 通过 trap facade 获取故障虚拟地址
 *   2. 通过 trap facade 获取缺页类型（exec/read/write）
 *   3. 判断来源（用户态 / 内核态）
 *   4. find_vma 查找合法性
 *   5. 合法且权限匹配：分配物理页，清零，建立映射，刷新 TLB
 *   6. 非法或权限不匹配：do_exit（杀掉当前进程）
 *
 * 注意：不修改用户 PC，缺页指令在 trap return 后重新执行。
 */
void do_page_fault(struct trap_frame *tf)
{
	vaddr_t fault_addr = trap_fault_addr(tf);
	const char *fault_name = trap_fault_name(tf);
	bool from_user_mode = trap_frame_from_user(tf);
	struct mm_struct *mm = task_mm(current_task());
	int access = (int)trap_fault_access(tf);
	pte_t fault_pte = 0;
	int ret;

	/* 内核线程没有 mm，不应发生缺页 */
	if (!mm) {
		panic("page fault in kernel thread: type=%s addr=%p "
		      "sepc=%p",
		      fault_name, (void *)fault_addr,
		      (void *)trap_user_pc(tf));
	}

	mm_lock(mm);
	ret = fault_in_user_page_locked(mm, fault_addr, access, &fault_pte);
	mm_unlock(mm);

	if (ret == 0)
		return;

	if (ret == -ENOMEM) {
		pr_err("page fault: OOM at addr=%p pid=%d\n",
		       (void *)fault_addr, task_pid(current_task()));
		do_exit(1);
		unreachable();
	}

	struct vm_area_struct *vma;

	mm_lock(mm);
	vma = find_vma(mm, fault_addr);
	if (!vma) {
		mm_unlock(mm);
		pr_warn("page fault: illegal access (no VMA) "
			"type=%s addr=%p sepc=%p origin=%s pid=%d\n",
			fault_name, (void *)fault_addr,
			(void *)trap_user_pc(tf),
			from_user_mode ? "user" : "kernel",
			task_pid(current_task()));
		signal_or_exit_segv(from_user_mode);
		return;
	}

	/* 检查权限 */
	if (!check_vma_permission(access, vma)) {
		uint32_t vm_flags = vma->vm_flags;

		mm_unlock(mm);
		pr_warn("page fault: permission denied "
			"type=%s addr=%p vma_flags=0x%x sepc=%p "
			"origin=%s pid=%d\n",
			fault_name, (void *)fault_addr, vm_flags,
			(void *)trap_user_pc(tf),
			from_user_mode ? "user" : "kernel",
			task_pid(current_task()));
		signal_or_exit_segv(from_user_mode);
		return;
	}
	mm_unlock(mm);

	pr_warn("page fault: mapped page permission denied "
		"type=%s addr=%p pte=0x%lx sepc=%p origin=%s pid=%d\n",
		fault_name, (void *)fault_addr, (size_t)fault_pte,
		(void *)trap_user_pc(tf), from_user_mode ? "user" : "kernel",
		task_pid(current_task()));
	signal_or_exit_segv(from_user_mode);
}
