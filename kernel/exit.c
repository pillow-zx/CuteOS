/*
 * kernel/exit.c - 进程退出与回收
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
#include <kernel/processor.h>
#include <kernel/pgtable.h>

#define WEXITCODE(code) ((code) << 8)

static LIST_HEAD(exited_threads);
static bool exited_threads_reap_pending;

static struct task_struct *find_child(pid_t pid)
{
	struct task_struct *child;

	task_for_each_child (child, current_task()) {
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

	task_for_each_child (child, current_task()) {
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
		task_for_each_child (child, current_task()) {
			if (task_is_group_leader(child))
				return true;
		}
		return false;
	}

	return find_child(pid) != NULL;
}

static int wait4_probe(struct wait_registrar *registrar, void *arg)
{
	pid_t pid = *(pid_t *)arg;
	int ret;

	if (!has_wait_target(pid) || find_waitable_child(pid))
		return 1;

	ret = wait_register(registrar,
			    task_wait_child_queue(current_task()));
	if (ret < 0)
		return ret;

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
		list_add_tail(&child->links.sibling,
			      &child->links.parent->links.children);

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

	if (task == current_task())
		pgtable_activate_kernel();

	mm_put(mm);
}

static void detach_task_queues(struct task_struct *task)
{
	if (!task || task == current_task())
		return;

	if (!list_empty(&task->sched.run_list))
		sched_dequeue(task);
}

static void __nonnull(1)
	finish_task_exit(struct task_struct *task, int code, bool notify_parent)
{
	if (task_state(task) == TASK_ZOMBIE || task_state(task) == TASK_DEAD)
		return;

	detach_task_queues(task);
	wait_cancel_task(task);

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
	if (!task_is_group_leader(task) && task == current_task()) {
		list_add_tail(&task->links.thread_node, &exited_threads);
		exited_threads_reap_pending = true;
	}

	if (notify_parent && task->links.parent &&
	    task->lifecycle.exit_signal > 0) {
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

		if (thread == current_task())
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

		if (thread == current_task())
			continue;

		finish_task_exit(thread, code, false);
		release_task(thread);
	}
}

void __noreturn do_exit(int code)
{
	struct task_struct *task = current_task();

	BUG_ON(!task);
	if (!task_is_group_leader(task)) {
		finish_task_exit(task, code, false);
	} else {
		reap_other_threads(task, code);
		finish_task_exit(task, code, true);
	}

	schedule();

	unreachable();
}

void __noreturn do_exit_group(int code)
{
	struct task_struct *task = current_task();
	struct task_struct *leader;

	BUG_ON(!task);
	leader = task_group_leader(task);

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

	BUG_ON(task == current_task());
	BUG_ON(task == &idle_task);
	BUG_ON(task_state(task) != TASK_ZOMBIE);
	BUG_ON(!list_empty(&task->links.children));
	BUG_ON(task_is_group_leader(task) &&
	       !list_empty(&task->links.thread_group));

	if (!list_empty(&task->links.sibling))
		list_del_init(&task->links.sibling);
	if (!list_empty(&task->links.thread_node))
		list_del_init(&task->links.thread_node);
	if (!list_empty(&task->sched.run_list))
		sched_dequeue(task);
	task_set_state(task, TASK_DEAD);
	task_free(task);
}

int kernel_wait4(pid_t pid, int options, struct wait4_result *result)
{
	const struct wait_deadline deadline = {
		.active = false,
	};
	struct wait_source source = {
		.probe = wait4_probe,
		.arg = &pid,
		.registration_limit = 1,
	};
	wait_completion_t completion;

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
			int ret = wait_complete(&source, 0, &deadline,
						&completion);
			if (ret < 0)
				return ret;
			BUG_ON(completion != WAIT_COMPLETION_EVENT);
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

	task_add_child_time(current_task(), &result->cputime);
	release_task(result->task);
	result->task = NULL;
}
