/*
 * syscall/sys_stub.c - 当前内核子系统尚不足的系统调用占位实现
 *
 * 每个函数保留 Linux riscv64 ABI 入口和明确 TODO，等对应子系统成熟后
 * 再补完整语义。
 */

#include <kernel/errno.h>
#include <kernel/syscall.h>
#include <asm/trap.h>

ssize_t sys_epoll_create1(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(epoll): 需要 pollable file、等待队列聚合和 epoll fd 对象。 */
	return -ENOSYS;
}

ssize_t sys_epoll_ctl(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(epoll): 需要 epoll interest list 和 ready list。 */
	return -ENOSYS;
}

ssize_t sys_epoll_pwait(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(epoll): 需要可被信号中断的等待和事件复制语义。 */
	return -ENOSYS;
}

ssize_t sys_umount(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(vfs): 当前根文件系统固定挂载，尚无 mount namespace。 */
	return -ENOSYS;
}

ssize_t sys_mount(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(vfs): 需要动态挂载表、挂载点和设备/类型解析。 */
	return -ENOSYS;
}

ssize_t sys_statfs64(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(vfs): 需要 super_block 统计字段和 statfs64 ABI 结构。 */
	return -ENOSYS;
}

ssize_t sys_fstatfs64(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(vfs): 需要从 file->super_block 导出 statfs64 统计。 */
	return -ENOSYS;
}

ssize_t sys_ppoll(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(poll): 需要 file poll 操作、超时睡眠和信号掩码切换。 */
	return -ENOSYS;
}

ssize_t sys_readlinkat(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(vfs): 当前 VFS/EXT2 不支持符号链接 inode 语义。 */
	return -ENOSYS;
}

ssize_t sys_futex(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(futex): 需要用户地址哈希等待队列和原子比较唤醒。 */
	return -ENOSYS;
}

ssize_t sys_set_robust_list(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(thread): 需要线程退出时的 robust futex 清理。 */
	return -ENOSYS;
}

ssize_t sys_get_robust_list(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(thread): 需要在 task_struct 中保存 robust_list 指针。 */
	return -ENOSYS;
}

ssize_t sys_sched_setaffinity(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(smp): 当前单核且没有 CPU mask 状态。 */
	return -ENOSYS;
}

ssize_t sys_sched_getaffinity(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(smp): 当前单核；补 cpumask ABI 后可返回 CPU0。 */
	return -ENOSYS;
}

ssize_t sys_mremap(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(mm): 需要 VMA 拆分/合并和页表范围迁移。 */
	return -ENOSYS;
}

ssize_t sys_mprotect(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(mm): 需要按 VMA 范围修改权限并刷新 TLB。 */
	return -ENOSYS;
}

ssize_t sys_mlock(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(mm): 需要 page pin/unevictable 语义；当前也没有换页。 */
	return -ENOSYS;
}

ssize_t sys_munlock(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(mm): 需要 page pin/unevictable 语义；当前也没有换页。 */
	return -ENOSYS;
}

ssize_t sys_mincore(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(mm): 需要查询用户页表和 VMA resident 状态。 */
	return -ENOSYS;
}

ssize_t sys_madvise(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(mm): 需要 VMA advice 状态和页回收策略。 */
	return -ENOSYS;
}

ssize_t sys_prlimit64(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(resource): 需要进程资源限制表。 */
	return -ENOSYS;
}

ssize_t sys_renameat2(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(vfs): 需要 VFS rename 操作和 EXT2 目录项原子更新。 */
	return -ENOSYS;
}

ssize_t sys_getrandom(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(random): 需要熵源或确定性的内核 PRNG。 */
	return -ENOSYS;
}

ssize_t sys_rseq(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(thread): 需要 per-thread restartable sequence 状态。 */
	return -ENOSYS;
}
