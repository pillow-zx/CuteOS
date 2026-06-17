/*
 * kernel/exit.c - 进程退出与回收
 *
 * 功能：
 *   处理进程终止请求和相关系统调用。当进程调用 exit 或收到致命信号时，
 *   逐步释放其占用的资源，最终由父进程调用 wait4 完成 task_struct 回收。
 *   若父进程先于子进程退出，子进程会被过继（reparent）给 init 进程。
 *
 *   do_exit 执行流程：
 *     1. 将进程状态设为 ZOMBIE。
 *     2. 关闭所有已打开的文件描述符（close all fds）。
 *     3. 释放用户地址空间（mm_struct、VMA、物理页）。
 *     4. 将当前进程的子进程过继给 init 进程（reparent orphans to init）。
 *     5. 向父进程发送 SIGCHLD 信号通知。
 *     6. 调用 schedule() 切换到下一个可运行进程，永不返回。
 *
 *   sys_wait4 支持：
 *     - pid > 0 ：等待指定 PID 的子进程。
 *     - pid == -1：等待任意子进程（等价于 waitpid(-1, ...)）。
 *     - 使用 sleep_on/wake_up 在子进程等待队列上睡眠/唤醒。
 *
 * 主要函数：
 *   do_exit(code)               - 核心退出逻辑：设置 ZOMBIE 状态，
 *                                 关闭所有 fd，释放用户空间，
 *                                 过继孤儿给 init，发送 SIGCHLD 给父进程，
 *                                 调用 schedule() 永不返回。
 *   sys_wait4(pid, wstatus, options, rusage) - 等待子进程状态变化，
 *                                 支持 pid>0 和 pid==-1，
 *                                 使用 sleep_on 在子进程等待队列上阻塞。
 *   release_task(task)          - 最终的 task_struct 回收（供 wait4 调用）。
 *   reparent_children(dead_task)- 将死进程的子进程过继给 init 进程。
 */

#include <kernel/exit.h>
#include <kernel/errno.h>
#include <kernel/fdtable.h>
#include <kernel/futex.h>
#include <kernel/fs_struct.h>
#include <kernel/list.h>
#include <kernel/mm.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <kernel/signal.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <kernel/wait.h>
#include <asm/csr.h>
#include <asm/pte.h>
#include <asm/trap.h>

#define WEXITCODE(code) ((code) << 8)

static LIST_HEAD(exited_threads);
static bool exited_threads_reap_pending;

static struct task_struct *find_child(pid_t pid)
{
	struct task_struct *child;

	list_for_each_entry (child, &current->children, sibling) {
		if (!task_is_group_leader(child))
			continue;
		if (child->pid == pid)
			return child;
	}

	return NULL;
}

static struct task_struct *find_any_zombie_child(void)
{
	struct task_struct *child;

	list_for_each_entry (child, &current->children, sibling) {
		if (!task_is_group_leader(child))
			continue;
		if (child->state == TASK_ZOMBIE)
			return child;
	}

	return NULL;
}

static struct task_struct *find_waitable_child(pid_t pid)
{
	if (pid == (pid_t)-1)
		return find_any_zombie_child();

	struct task_struct *child = find_child(pid);
	if (child && child->state == TASK_ZOMBIE)
		return child;

	return NULL;
}

static bool has_wait_target(pid_t pid)
{
	struct task_struct *child;

	if (pid == (pid_t)-1) {
		list_for_each_entry (child, &current->children, sibling) {
			if (task_is_group_leader(child))
				return true;
		}
		return false;
	}

	return find_child(pid) != NULL;
}

static void reparent_children(struct task_struct *dead)
{
	struct list_head *pos;
	struct list_head *next;

	list_for_each_safe (pos, next, &dead->children) {
		struct task_struct *child =
			list_entry(pos, struct task_struct, sibling);

		list_del_init(&child->sibling);
		child->parent = init_task ? init_task : &idle_task;
		list_add_tail(&child->sibling, &child->parent->children);

		if (child->state == TASK_ZOMBIE)
			wake_up(&child->parent->wait_child_queue);
	}
}

static void clear_child_tid(struct task_struct *task)
{
	int zero = 0;

	if (!task->clear_child_tid)
		return;

	if (copy_to_user(task->clear_child_tid, &zero, sizeof(zero)) == 0)
		futex_wake_mm(task->mm, task->clear_child_tid, 1);
	task->clear_child_tid = NULL;
}

static void release_task_mm(struct task_struct *task)
{
	struct mm_struct *mm = task->mm;

	if (!mm)
		return;

	task->mm = NULL;
	task->satp = 0;

	if (task == current) {
		csr_write(satp, kernel_satp());
		sfence_vma_all();
	}

	mm_put(mm);
}

static void detach_task_queues(struct task_struct *task)
{
	if (!task || task == current)
		return;

	if (!list_empty(&task->run_list))
		sched_dequeue(task);
	if (!list_empty(&task->wait_list))
		list_del_init(&task->wait_list);
}

static void finish_task_exit(struct task_struct *task, int code,
			     bool notify_parent)
{
	if (!task || task->state == TASK_ZOMBIE || task->state == TASK_DEAD)
		return;

	detach_task_queues(task);

	task->exit_code = code;
	clear_child_tid(task);
	close_files(task);
	exit_fs(task);
	release_task_mm(task);

	if (task_is_group_leader(task))
		reparent_children(task);
	else if (!list_empty(&task->thread_node))
		list_del_init(&task->thread_node);

	task->state = TASK_ZOMBIE;
	if (!task_is_group_leader(task) && task == current) {
		list_add_tail(&task->thread_node, &exited_threads);
		exited_threads_reap_pending = true;
	}

	if (notify_parent && task->parent && task->exit_signal > 0) {
		send_signal(task->exit_signal, task->parent);
		wake_up(&task->parent->wait_child_queue);
	}
}

bool exited_threads_pending(void)
{
	return exited_threads_reap_pending;
}

void reap_exited_threads(void)
{
	struct list_head *pos;
	struct list_head *next;

	if (!exited_threads_reap_pending)
		return;

	list_for_each_safe (pos, next, &exited_threads) {
		struct task_struct *thread =
			list_entry(pos, struct task_struct, thread_node);

		if (thread == current)
			continue;

		release_task(thread);
	}

	if (list_empty(&exited_threads))
		exited_threads_reap_pending = false;
}

static void reap_other_threads(struct task_struct *leader, int code)
{
	struct list_head *pos;
	struct list_head *next;

	list_for_each_safe (pos, next, &leader->thread_group) {
		struct task_struct *thread =
			list_entry(pos, struct task_struct, thread_node);

		if (thread == current)
			continue;

		finish_task_exit(thread, code, false);
		release_task(thread);
	}
}

/*
 * do_exit - 终止当前进程
 * @code: 退出码
 */
void do_exit(int code)
{
	printk("do_exit: pid=%d exit_code=%d\n", current->pid, code);

	if (!task_is_group_leader(current)) {
		finish_task_exit(current, code, false);
	} else {
		reap_other_threads(current, code);
		finish_task_exit(current, code, true);
	}

	/*
	 * From here until schedule() switches away, current is a zombie with
	 * current->mm == NULL and current->satp == 0. Do not add code here
	 * that dereferences current->mm or assumes a user address space exists.
	 *
	 * 运行中的进程不在 runqueue 中（schedule 在取队首时已 dequeue），
	 * 无需再次 sched_dequeue。schedule() 也不会把 ZOMBIE 进程重新入队。
	 */

	/* 永不返回 */
	schedule();

	unreachable();
}

void do_exit_group(int code)
{
	struct task_struct *leader = current->group_leader;

	printk("exit_group: pid=%d tgid=%d exit_code=%d\n", current->pid,
	       current->tgid, code);

	if (leader && leader != current) {
		finish_task_exit(leader, code, true);
	}

	if (leader)
		reap_other_threads(leader, code);

	finish_task_exit(current, code, current == leader);
	schedule();

	unreachable();
}

void release_task(struct task_struct *task)
{
	if (!task)
		return;

	BUG_ON(task == current);
	BUG_ON(task == &idle_task);
	BUG_ON(task->state != TASK_ZOMBIE);
	BUG_ON(!list_empty(&task->children));
	BUG_ON(task_is_group_leader(task) && !list_empty(&task->thread_group));

	if (!list_empty(&task->sibling))
		list_del_init(&task->sibling);
	if (!list_empty(&task->thread_node))
		list_del_init(&task->thread_node);
	if (!list_empty(&task->run_list))
		sched_dequeue(task);
	if (!list_empty(&task->wait_list))
		list_del_init(&task->wait_list);

	task->state = TASK_DEAD;
	task_free(task);
}

ssize_t sys_wait4(struct trap_frame *tf)
{
	pid_t pid = (pid_t)tf->a0;
	int *wstatus = (int *)tf->a1;
	int options = (int)tf->a2;

	if (pid != (pid_t)-1 && pid <= 0)
		return -EINVAL;
	if (options != 0)
		return -EINVAL;
	if (wstatus && !access_ok(wstatus, sizeof(*wstatus)))
		return -EFAULT;

	while (true) {
		if (!has_wait_target(pid))
			return -ECHILD;

		struct task_struct *child = find_waitable_child(pid);
		if (!child) {
			sleep_on(&current->wait_child_queue);
			continue;
		}

		pid_t child_pid = child->pid;
		int status = WEXITCODE(child->exit_code);

		if (wstatus) {
			if (copy_to_user(wstatus, &status, sizeof(status)) != 0)
				return -EFAULT;
		}

		release_task(child);
		return child_pid;
	}
}
