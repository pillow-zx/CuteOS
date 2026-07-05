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
#include <kernel/page.h>
#include <kernel/task.h>
#include <uapi/mman.h>
#include <kernel/trap.h>

/*
 * sys_brk - brk 系统调用入口
 * @tf: trap_frame，a0 = 新的 brk 地址，0 表示查询
 */
ssize_t sys_brk(struct trap_frame *tf)
{
	uintptr_t addr = (uintptr_t)syscall_arg(tf, 0);

	return (ssize_t)mm_brk(task_mm(current_task()), addr);
}

ssize_t sys_mmap(struct trap_frame *tf)
{
	uintptr_t addr = (uintptr_t)syscall_arg(tf, 0);
	size_t length = (size_t)syscall_arg(tf, 1);
	int prot = (int)syscall_arg(tf, 2);
	int flags = (int)syscall_arg(tf, 3);
	int fd = (int)syscall_arg(tf, 4);
	uint64_t offset = (uint64_t)syscall_arg(tf, 5);

	return mm_mmap_file(task_mm(current_task()), addr, length, prot, flags,
			    fd, offset);
}

ssize_t sys_munmap(struct trap_frame *tf)
{
	uintptr_t addr = (uintptr_t)syscall_arg(tf, 0);
	size_t length = (size_t)syscall_arg(tf, 1);

	return mm_munmap(task_mm(current_task()), addr, length);
}

ssize_t sys_mprotect(struct trap_frame *tf)
{
	uintptr_t addr = (uintptr_t)syscall_arg(tf, 0);
	size_t length = (size_t)syscall_arg(tf, 1);
	int prot = (int)syscall_arg(tf, 2);

	return mm_mprotect(task_mm(current_task()), addr, length, prot);
}

ssize_t sys_mremap(struct trap_frame *tf)
{
	uintptr_t old_addr = (uintptr_t)syscall_arg(tf, 0);
	size_t old_size = (size_t)syscall_arg(tf, 1);
	size_t new_size = (size_t)syscall_arg(tf, 2);
	int flags = (int)syscall_arg(tf, 3);
	uintptr_t new_addr = (uintptr_t)syscall_arg(tf, 4);

	return mm_mremap(task_mm(current_task()), old_addr, old_size, new_size,
			 flags, new_addr);
}

ssize_t sys_msync(struct trap_frame *tf)
{
	uintptr_t addr = (uintptr_t)syscall_arg(tf, 0);
	size_t length = (size_t)syscall_arg(tf, 1);
	int flags = (int)syscall_arg(tf, 2);

	return mm_msync(task_mm(current_task()), addr, length, flags);
}

ssize_t sys_mlock(struct trap_frame *tf)
{
	uintptr_t addr = (uintptr_t)syscall_arg(tf, 0);
	size_t len = (size_t)syscall_arg(tf, 1);
	struct mm_struct *mm = task_mm(current_task());

	if (!mm)
		return -EINVAL;

	return mm_mlock(mm, addr, len);
}

ssize_t sys_munlock(struct trap_frame *tf)
{
	uintptr_t addr = (uintptr_t)syscall_arg(tf, 0);
	size_t len = (size_t)syscall_arg(tf, 1);
	struct mm_struct *mm = task_mm(current_task());

	if (!mm)
		return -EINVAL;

	return mm_munlock(mm, addr, len);
}

/*
 * sys_madvise - madvise 系统调用
 *
 * 接受 Linux ABI advice 值。NORMAL/RANDOM/SEQUENTIAL/WILLNEED/FREE 在当前
 * 内核中只验证范围；DONTNEED 委托 MM 层释放匿名 resident 页。
 */
ssize_t sys_madvise(struct trap_frame *tf)
{
	uintptr_t addr = (uintptr_t)syscall_arg(tf, 0);
	size_t len = (size_t)syscall_arg(tf, 1);
	int advice = (int)syscall_arg(tf, 2);

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

	return mm_madvise(task_mm(current_task()), addr, len, advice);
}

/*
 * sys_mincore - 查询地址范围内各页的驻留状态
 *
 * 对范围内每一页，通过页表遍历检查有效用户页映射，结果写入用户字节向量
 * (bit0=1 表示该页已映射)。
 *
 * 注意：copy_to_user 内部调用 user_range_probe，后者也需要获取 mm_lock。
 * 为避免死锁，在持有锁时收集所有结果到内核栈缓冲区，再释放锁后
 * 一次性写入用户空间。
 */
ssize_t sys_mincore(struct trap_frame *tf)
{
	uintptr_t addr = (uintptr_t)syscall_arg(tf, 0);
	size_t len = (size_t)syscall_arg(tf, 1);
	unsigned char *uvec = (unsigned char *)syscall_arg(tf, 2);
	struct mm_struct *mm;
	uintptr_t end, va;
	size_t npages, i;
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

	mm = task_mm(current_task());
	if (!mm)
		return -EINVAL;

	while (done < npages) {
		batch = npages - done;
		if (batch > sizeof(kbuf))
			batch = sizeof(kbuf);

		for (i = 0; i < batch; i++) {
			bool resident;
			int ret;

			va = addr + (done + i) * PAGE_SIZE;
			ret = mm_user_page_resident(mm, va, &resident);
			if (ret < 0)
				return ret;
			kbuf[i] = resident ? 1 : 0;
		}

		if (copy_to_user(uvec + done, kbuf, batch) != 0)
			return -EFAULT;

		done += batch;
	}
	return 0;
}
