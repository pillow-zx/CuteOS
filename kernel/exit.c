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
 *   kernel_wait4 支持：
 *     - pid > 0 ：等待指定 PID 的子进程。
 *     - pid == -1：等待任意子进程（等价于 waitpid(-1, ...)）。
 *     - 使用 wait_event/wake_up 在子进程等待队列上睡眠/唤醒。
 *
 * 主要函数：
 *   do_exit(code)               - 核心退出逻辑：设置 ZOMBIE 状态，
 *                                 关闭所有 fd，释放用户空间，
 *                                 过继孤儿给 init，发送 SIGCHLD 给父进程，
 *                                 调用 schedule() 永不返回。
 *   kernel_wait4(pid, options, result) - 等待子进程状态变化，支持 pid>0
 *                                 和 pid==-1，使用 wait_event 在子进程
 *                                 等待队列上阻塞。
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
#include <kernel/resource.h>
#include <kernel/sched.h>
#include <kernel/signal.h>
#include <kernel/task.h>
#include <kernel/wait.h>
#include <asm/csr.h>
#include <asm/pte.h>

#define WEXITCODE(code) ((code) << 8)

static LIST_HEAD(exited_threads);
static bool exited_threads_reap_pending;

static struct task_struct *find_child(pid_t pid)
{
	struct task_struct *child;

	task_for_each_child(child, current) {
		if (!task_is_group_leader(child))
			continue;
		if (task_pid(child) == pid)
			return child;
	}

	return NULL;
}

static struct task_struct *find_any_zombie_child(void)
{
	struct task_struct *child;

	task_for_each_child(child, current) {
		if (!task_is_group_leader(child))
			continue;
		if (task_state(child) == TASK_ZOMBIE)
			return child;
	}

	return NULL;
}

static struct task_struct *find_waitable_child(pid_t pid)
{
	if (pid == (pid_t)-1)
		return find_any_zombie_child();

	struct task_struct *child = find_child(pid);
	if (child && task_state(child) == TASK_ZOMBIE)
		return child;

	return NULL;
}

static bool has_wait_target(pid_t pid)
{
	struct task_struct *child;

	if (pid == (pid_t)-1) {
		task_for_each_child(child, current) {
			if (task_is_group_leader(child))
				return true;
		}
		return false;
	}

	return find_child(pid) != NULL;
}

static bool wait4_ready(void *arg)
{
	pid_t pid = *(pid_t *)arg;

	return !has_wait_target(pid) || find_waitable_child(pid) != NULL;
}

static void reparent_children(struct task_struct *dead)
{
	struct list_head *pos;
	struct list_head *next;

	list_for_each_safe (pos, next, &dead->links.children) {
		struct task_struct *child =
			list_entry(pos, struct task_struct, links.sibling);

		list_del_init(&child->links.sibling);
		child->links.parent = init_task ? init_task : &idle_task;
		list_add_tail(&child->links.sibling, &child->links.parent->links.children);

		if (child->lifecycle.state == TASK_ZOMBIE)
			wake_up(&child->links.parent->links.wait_child_queue);
	}
}

static void clear_child_tid(struct task_struct *task)
{
	int *clear_tid = task_clear_child_tid(task);
	int zero = 0;

	if (!clear_tid)
		return;

	/*
	 * CLONE_CHILD_CLEARTID is a Linux exit-time ABI side effect: the
	 * exiting thread clears a user TID word and wakes futex waiters. Keep
	 * this uaccess here because it is tied to task teardown, not syscall
	 * argument copying.
	 */
	if (copy_to_user(clear_tid, &zero, sizeof(zero)) == 0)
		futex_wake_mm(task_mm(task), clear_tid, 1);
	task_set_clear_child_tid(task, NULL);
}

static void release_task_mm(struct task_struct *task)
{
	struct mm_struct *mm = task_mm(task);

	if (!mm)
		return;

	task_set_mm(task, NULL);
	task_set_satp(task, 0);

	if (task == current) {
		csr_write(satp, arch_kernel_satp());
		arch_tlb_flush_all();
	}

	mm_put(mm);
}

static void detach_task_queues(struct task_struct *task)
{
	if (!task || task == current)
		return;

	if (!list_empty(&task->sched.run_list))
		sched_dequeue(task);
	if (!list_empty(&task->sched.wait_entry.node))
		list_del_init(&task->sched.wait_entry.node);
}

static void __nonnull(1) finish_task_exit(struct task_struct *task, int code,
					  bool notify_parent)
{
	if (task_state(task) == TASK_ZOMBIE ||
	    task_state(task) == TASK_DEAD)
		return;

	detach_task_queues(task);

	task_set_exit_code(task, code);
	futex_exit_robust_list(task);
	clear_child_tid(task);
	close_files(task);
	exit_fs(task);
	signals_release(task);
	release_task_mm(task);

	if (task_is_group_leader(task))
		reparent_children(task);
	else if (!list_empty(&task->links.thread_node))
		list_del_init(&task->links.thread_node);

	task_set_state(task, TASK_ZOMBIE);
	if (!task_is_group_leader(task) && task == current) {
		list_add_tail(&task->links.thread_node, &exited_threads);
		exited_threads_reap_pending = true;
	}

	if (notify_parent && task->links.parent && task->lifecycle.exit_signal > 0) {
		send_signal(task->lifecycle.exit_signal, task->links.parent);
		wake_up(&task->links.parent->links.wait_child_queue);
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
			list_entry(pos, struct task_struct, links.thread_node);

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

	list_for_each_safe (pos, next, &leader->links.thread_group) {
		struct task_struct *thread =
			list_entry(pos, struct task_struct, links.thread_node);

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
void __noreturn do_exit(int code)
{
	struct task_struct *task = current;

	BUG_ON(!task);
	if (!task_is_group_leader(task)) {
		finish_task_exit(task, code, false);
	} else {
		reap_other_threads(task, code);
		finish_task_exit(task, code, true);
	}

	/*
	 * From here until schedule() switches away, current is a zombie with
	 * no mm and satp == 0. Do not add code here that dereferences a user
	 * address space.
	 *
	 * 运行中的进程不在 runqueue 中（schedule 在取队首时已 dequeue），
	 * 无需再次 sched_dequeue。schedule() 也不会把 ZOMBIE 进程重新入队。
	 */

	/* 永不返回 */
	schedule();

	unreachable();
}

void __noreturn do_exit_group(int code)
{
	struct task_struct *task = current;
	struct task_struct *leader;

	BUG_ON(!task);
	leader = task_group_leader(task);

	pr_info("exit_group: pid=%d tgid=%d exit_code=%d\n", task_pid(task),
		task_tgid(task), code);

	if (leader && leader != task) {
		finish_task_exit(leader, code, true);
	}

	if (leader)
		reap_other_threads(leader, code);

	finish_task_exit(task, code, task == leader);
	schedule();

	unreachable();
}

void release_task(struct task_struct *task)
{
	if (!task)
		return;

	BUG_ON(task == current);
	BUG_ON(task == &idle_task);
	BUG_ON(task_state(task) != TASK_ZOMBIE);
	BUG_ON(!list_empty(&task->links.children));
	BUG_ON(task_is_group_leader(task) && !list_empty(&task->links.thread_group));

	if (!list_empty(&task->links.sibling))
		list_del_init(&task->links.sibling);
	if (!list_empty(&task->links.thread_node))
		list_del_init(&task->links.thread_node);
	if (!list_empty(&task->sched.run_list))
		sched_dequeue(task);
	if (!list_empty(&task->sched.wait_entry.node))
		list_del_init(&task->sched.wait_entry.node);

	task_set_state(task, TASK_DEAD);
	task_free(task);
}

int kernel_wait4(pid_t pid, int options, struct wait4_result *result)
{
	if (pid != (pid_t)-1 && pid <= 0)
		return -EINVAL;
	if (options != 0)
		return -EINVAL;
	if (!result)
		return -EINVAL;

	while (true) {
		if (!has_wait_target(pid))
			return -ECHILD;

		struct task_struct *child = find_waitable_child(pid);
		if (!child) {
			int ret = wait_event(task_wait_child_queue(current),
					     wait4_ready, &pid);
			if (ret < 0)
				return ret;
			continue;
		}

		pid_t child_pid = task_pid(child);
		int status = WEXITCODE(task_exit_code(child));

		result->task = child;
		task_cputime_total(child, &result->cputime);
		result->pid = child_pid;
		result->status = status;
		return 0;
	}
}

void kernel_wait4_finish(struct wait4_result *result)
{
	if (!result || !result->task)
		return;

	task_add_child_time(current, &result->cputime);
	release_task(result->task);
	result->task = NULL;
}
