/*
 * mm/uaccess.c - 用户空间内存访问
 *
 * 功能：
 *   提供内核与用户空间之间安全的数据拷贝函数。
 *   access_ok() 检查地址范围是否合法（无溢出 + 不超过 TASK_SIZE）。
 *   copy_to_user / copy_from_user 在 SUM 位保护下进行 memcpy。
 *
 * 当前实现：
 *   copy_to_user / copy_from_user 先经 user_range_probe 预探整个范围：
 *   逐页校验 VMA 权限，合法但未映射的页通过 do_page_fault 按需 fault-in；
 *   范围非法（无 VMA / 权限不符 / 无法映射）时返回未拷贝字节数 n，
 *   调用方据此返回 -EFAULT，而非触发内核崩溃。
 *
 * 后续计划：
 *   预探是 O(页数 × VMA) 的保守方案；Stage 6 可改为异常表 fixup
 *   （先访问、触发异常再回滚），省去逐页预探的开销。
 *
 * 主要函数：
 *   access_ok(addr, size)        - 检查用户地址范围是否合法
 *   user_range_probe(addr, n, w) - 预探范围：校验 VMA 权限并 fault-in
 *   copy_to_user(to, from, n)    - 从内核空间复制数据到用户空间
 *   copy_from_user(to, from, n)  - 从用户空间复制数据到内核空间
 */

#include <kernel/mm.h>
#include <kernel/errno.h>
#include <kernel/string.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <asm/csr.h>
#include <asm/page.h>
#include <asm/trap.h>

bool access_ok(const void *addr, size_t size)
{
	vaddr_t a = (vaddr_t)addr;

	if (size == 0)
		return true;

	if (a + size < a) /* 溢出检查 */
		return false;

	if (a + size > TASK_SIZE)
		return false;

	return true;
}

int user_range_probe(const void *addr, size_t size, bool write)
{
	uintptr_t start;
	uintptr_t end;
	uint64_t scause;

	if (size == 0)
		return 0;
	if (!access_ok(addr, size))
		return -EFAULT;
	if (!current || !current->mm)
		return -EFAULT;

	start = (uintptr_t)addr & PAGE_MASK;
	end = ((uintptr_t)addr + size + PAGE_SIZE - 1) & PAGE_MASK;
	scause = write ? EXC_STORE_PAGE_FAULT : EXC_LOAD_PAGE_FAULT;

	for (uintptr_t va = start; va < end; va += PAGE_SIZE) {
		struct vm_area_struct *vma = find_vma(current->mm, va);
		pte_t *pte;

		if (!vma)
			return -EFAULT;
		if (write) {
			if (!(vma->vm_flags & VM_WRITE))
				return -EFAULT;
		} else if (!(vma->vm_flags & VM_READ)) {
			return -EFAULT;
		}

		pte = walk_page_table(current->mm->pgd, va, false);
		if (pte && pte_present(*pte))
			continue;

		struct trap_frame probe = {
			.scause = scause,
			.stval = va,
			.sstatus = SSTATUS_SPIE,
		};
		do_page_fault(&probe);

		pte = walk_page_table(current->mm->pgd, va, false);
		if (!pte || !pte_present(*pte))
			return -EFAULT;
	}

	return 0;
}

size_t copy_to_user(void *to, const void *from, size_t n)
{
	if (user_range_probe(to, n, true) < 0)
		return n;

	bool had_sum = user_access_begin();
	memcpy(to, from, n);
	user_access_end(had_sum);

	return 0;
}

size_t copy_from_user(void *to, const void *from, size_t n)
{
	if (user_range_probe(from, n, false) < 0)
		return n;

	bool had_sum = user_access_begin();
	memcpy(to, from, n);
	user_access_end(had_sum);

	return 0;
}
