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
 */

#include <kernel/syscall.h>
#include <kernel/mm.h>
#include <kernel/task.h>
#include <asm/trap.h>

/*
 * sys_brk - brk 系统调用入口
 * @tf: trap_frame，a0 = 新的 brk 地址，0 表示查询
 */
ssize_t sys_brk(struct trap_frame *tf)
{
	vaddr_t addr = (vaddr_t)tf->a0;
	return (ssize_t)mm_brk(current->mm, addr);
}

ssize_t sys_mmap(struct trap_frame *tf)
{
	return mm_mmap(current->mm, (vaddr_t)tf->a0, (size_t)tf->a1,
		       (int)tf->a2, (int)tf->a3);
}

ssize_t sys_munmap(struct trap_frame *tf)
{
	return mm_munmap(current->mm, (vaddr_t)tf->a0, (size_t)tf->a1);
}
