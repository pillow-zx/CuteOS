/*
 * syscall/sys_mm.c - 内存相关系统调用
 *
 * 功能：
 *   实现进程地址空间操作的系统调用。作为 syscall 层入口，
 *   从 trap_frame 提取参数，调用 mm 层的内部实现函数。
 *
 * 主要函数：
 *   sys_brk(tf) - 调整堆边界，不缩小，按需分配物理页
 *   sys_mmap(tf) - 创建匿名 mmap VMA
 *   sys_munmap(tf) - 解除 mmap VMA 并释放已映射物理页
 *   sys_madvise(tf) - 验证 advice 并委托 MM 层处理内存建议
 *   sys_mincore(tf) - 查询页面驻留状态
 */

#include <kernel/errno.h>
#include <kernel/syscall.h>
#include <kernel/mm.h>
#include <kernel/task.h>
#include <uapi/mman.h>
#include <asm/pte.h>
#include <asm/trap.h>

/*
 * sys_brk - brk 系统调用入口
 * @tf: trap_frame，a0 = 新的 brk 地址，0 表示查询
 */
ssize_t sys_brk(struct trap_frame *tf)
{
	vaddr_t addr = (vaddr_t)tf->a0;
	return (ssize_t)mm_brk(task_mm(current), addr);
}

ssize_t sys_mmap(struct trap_frame *tf)
{
	return mm_mmap(task_mm(current), (vaddr_t)tf->a0, (size_t)tf->a1,
		       (int)tf->a2, (int)tf->a3);
}

ssize_t sys_munmap(struct trap_frame *tf)
{
	return mm_munmap(task_mm(current), (vaddr_t)tf->a0, (size_t)tf->a1);
}

ssize_t sys_mprotect(struct trap_frame *tf)
{
	return mm_mprotect(task_mm(current), (uintptr_t)tf->a0,
			   (size_t)tf->a1, (int)tf->a2);
}

/*
 * sys_madvise - madvise 系统调用
 *
 * 接受 Linux ABI advice 值。NORMAL/RANDOM/SEQUENTIAL/WILLNEED/FREE 在当前
 * 内核中只验证范围；DONTNEED 委托 MM 层释放匿名 resident 页。
 */
ssize_t sys_madvise(struct trap_frame *tf)
{
	uintptr_t addr  = (uintptr_t)tf->a0;
	size_t    len   = (size_t)tf->a1;
	int       advice = (int)tf->a2;

	switch (advice) {
	case MADV_NORMAL:
	case MADV_RANDOM:
	case MADV_SEQUENTIAL:
	case MADV_WILLNEED:
	case MADV_DONTNEED:
	case MADV_FREE:
		break;
	default:
		return -EINVAL;
	}

	return mm_madvise(task_mm(current), addr, len, advice);
}

/*
 * sys_mincore - 查询地址范围内各页的驻留状态
 *
 * 对范围内每一页，通过页表遍历检查 PTE_V 位，结果写入用户字节向量
 * (bit0=1 表示该页已映射)。
 *
 * 注意：copy_to_user 内部调用 user_range_probe，后者也需要获取 mm_lock。
 * 为避免死锁，在持有锁时收集所有结果到内核栈缓冲区，再释放锁后
 * 一次性写入用户空间。
 */
ssize_t sys_mincore(struct trap_frame *tf)
{
	uintptr_t      addr  = (uintptr_t)tf->a0;
	size_t         len   = (size_t)tf->a1;
	unsigned char *uvec  = (unsigned char *)tf->a2;
	struct mm_struct *mm;
	uintptr_t end, va;
	size_t    npages, i;
	/* Stack buffer: covers up to 256 pages per batch without heap alloc. */
	unsigned char kbuf[256];
	size_t batch, done = 0;

	if (addr & (PAGE_SIZE - 1))
		return -EINVAL;
	if (len == 0)
		return 0;
	if (!uvec)
		return -EFAULT;
	if (len > TASK_SIZE)
		return -EINVAL;

	len = (len + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
	end = addr + len;
	if (end < addr || end > TASK_SIZE)
		return -EINVAL;

	npages = len / PAGE_SIZE;

	mm = task_mm(current);
	if (!mm)
		return -EINVAL;

	while (done < npages) {
		batch = npages - done;
		if (batch > sizeof(kbuf))
			batch = sizeof(kbuf);

		mm_lock(mm);
		for (i = 0; i < batch; i++) {
			pte_t *pte;

			va = addr + (done + i) * PAGE_SIZE;
			if (!find_vma(mm, va)) {
				mm_unlock(mm);
				return -ENOMEM;
			}
			pte = walk_page_table(mm->pgd, va, false);
			kbuf[i] = (pte && pte_user_page(*pte)) ? 1 : 0;
		}
		mm_unlock(mm);

		if (copy_to_user(uvec + done, kbuf, batch) != 0)
			return -EFAULT;

		done += batch;
	}
	return 0;
}
