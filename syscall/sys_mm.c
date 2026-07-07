/*
 * syscall/sys_mm.c - 内存相关系统调用
 */

#include <kernel/errno.h>
#include <kernel/syscall.h>
#include <kernel/mm.h>
#include <kernel/page.h>
#include <kernel/task.h>
#include <uapi/mman.h>
#include <kernel/trap.h>

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

ssize_t sys_mincore(struct trap_frame *tf)
{
	uintptr_t addr = (uintptr_t)syscall_arg(tf, 0);
	size_t len = (size_t)syscall_arg(tf, 1);
	unsigned char *uvec = (unsigned char *)syscall_arg(tf, 2);
	struct mm_struct *mm;
	uintptr_t end, va;
	size_t npages, i;

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
