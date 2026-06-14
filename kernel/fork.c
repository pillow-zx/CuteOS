/*
 * kernel/fork.c - 进程创建（完整物理复制）
 *
 * 功能：
 *   实现 sys_fork 系统调用。通过完整物理复制创建子进程——
 *   为子进程的每个 VMA 分配新的物理页并复制父进程的页面内容。
 *   这是朴素但正确的 fork 语义，后续可引入 COW 优化。
 *
 * 主要函数：
 *   sys_fork()          - fork 系统调用入口，调用 do_fork 创建子进程，
 *                         父进程返回子进程 PID。
 *   dup_mm(oldmm)       - 完整复制父进程的用户地址空间。
 *   copy_files(child)   - 复制文件描述符表。
 *   copy_sighand(child) - 复制信号处理器表，清零 pending。
 */

#include <kernel/task.h>
#include <kernel/mm.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <kernel/string.h>
#include <kernel/errno.h>
#include <kernel/fdtable.h>
#include <kernel/fs_struct.h>
#include <asm/page.h>
#include <asm/trap.h>
#include <asm/csr.h>

/*
 * copy_sighand - 复制信号处理表
 * @child: 子进程 task_struct
 *
 * 复制 sighand 数组和 blocked 掩码。
 * 子进程的 pending 清零（不继承待处理信号）。
 */
static void copy_sighand(struct task_struct *child)
{
	/* TODO(signal): Stage 6 完整信号机制落地时，把 sighand/blocked/
	 * pending 的复制封装成 signals_clone()，避免 fork 直接知道信号
	 * 状态布局。 */
	memcpy(child->sighand, current->sighand,
	       sizeof(current->sighand));
	child->blocked = current->blocked;
	child->pending = 0;
}

/* ---- 公共接口 ---- */

/*
 * sys_fork - fork 系统调用实现
 * @tf: 父进程的 trap_frame
 *
 * 创建子进程，完整复制父进程的地址空间。
 * 父进程返回子进程 PID，子进程返回 0。
 *
 * 流程：
 *   1. task_alloc 分配子进程 task_struct + 内核栈 + PID
 *   2. dup_mm 深拷贝用户地址空间
 *   3. 在子进程栈顶构造 trap_frame（复制父进程 + a0=0）
 *   4. 设置子进程 context（ra=__trapret, sp→trap_frame）
 *   5. 复制 fd_array、sighand
 *   6. 建立进程树关系
 *   7. 子进程入就绪队列，父进程继续运行
 */
ssize_t sys_fork(struct trap_frame *tf)
{
	/* 1. 分配 task_struct */
	struct task_struct *child = task_alloc();
	if (!child)
		return -ENOMEM;

	/* 2. 复制用户地址空间 */
	child->mm = dup_mm(current->mm);
	if (!child->mm && current->mm) {
		task_free(child);
		return -ENOMEM;
	}

	/* 3. 计算子进程 satp */
	if (child->mm) {
		paddr_t pgd_pa = __pa((uintptr_t)child->mm->pgd);
		child->satp = SATP_MODE_SV39 | (pgd_pa >> PAGE_SHIFT);
	}

	/* 4. 在子进程内核栈顶构造 trap_frame */
	struct trap_frame *child_tf =
		(struct trap_frame *)((uint8_t *)child->kstack + KSTACK_SIZE -
				      sizeof(struct trap_frame));
	memcpy(child_tf, tf, sizeof(struct trap_frame));
	child_tf->a0 = 0; /* fork 在子进程中返回 0 */
	child->tf = child_tf;

	/* 5. 设置子进程 context：首次调度时走 __trapret 返回用户态 */
	child->ctx.ra = (size_t)__trapret;
	child->ctx.sp = (size_t)child_tf;

	/* 6. 复制文件描述符和信号处理表 */
	int ret = copy_files(child);
	if (ret < 0) {
		close_files(child);
		if (child->mm)
			mm_destroy(child->mm);
		task_free(child);
		return ret;
	}
	copy_fs(child);
	copy_sighand(child);

	/* 7. 建立进程树关系 */
	child->parent = current;
	list_add(&child->sibling, &current->children);

	/* 8. 子进程入就绪队列 */
	sched_enqueue(child);

	printk("fork: parent=%d child=%d\n", current->pid, child->pid);

	/* 9. 父进程返回子进程 PID */
	return child->pid;
}
