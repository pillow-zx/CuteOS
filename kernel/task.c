/*
 * kernel/task.c - 进程控制块管理
 */

#include <kernel/task.h>
#include <kernel/cpu.h>
#include <kernel/errno.h>
#include <kernel/pid.h>
#include <kernel/slab.h>
#include <kernel/buddy.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <kernel/signal.h>
#include <kernel/fdtable.h>
#include <kernel/fs_struct.h>
#include <kernel/vfs.h>

struct task_struct idle_task;

struct cpu cpu_table[NR_CPUS];
uint32_t nr_cpu_ids;

struct task_struct *init_task;

void cpu_boot_init(struct task_struct *idle)
{
	BUG_ON(!idle);

	for (uint32_t id = 0; id < NR_CPUS; id++) {
		cpu_table[id].id = id;
		cpu_table[id].hartid = id;
		cpu_table[id].state = CPU_OFFLINE;
		cpu_table[id].flags = 0;
		cpu_table[id].idle_task = NULL;
		cpu_table[id].current_task = NULL;
		cpu_table[id].preempt_count = 0;
	}

	nr_cpu_ids = 1;
	cpu_table[0].hartid = 0;
	cpu_table[0].state = CPU_ONLINE;
	cpu_table[0].idle_task = idle;
	cpu_table[0].current_task = idle;
}

struct task_struct *task_alloc(void)
{
	struct task_struct *task = kmalloc(sizeof(struct task_struct));
	if (!task)
		return NULL;

	void *kstack = get_free_page(KSTACK_ORDER);
	if (!kstack) {
		kfree(task);
		return NULL;
	}

	int32_t pid = alloc_pid();
	if (pid < 0) {
		free_page(kstack, KSTACK_ORDER);
		kfree(task);
		return NULL;
	}

	memset(task, 0, sizeof(struct task_struct));
	task->ids.pid = (pid_t)pid;
	task->lifecycle.state = TASK_RUNNING;
	arch_task_init(task);
	task_set_kernel_stack(task, kstack);
	task->resources.mm = NULL;
	task->ids.tgid = task->ids.pid;
	task->ids.pgid = task->ids.pid;
	task->ids.sid = task->ids.pid;
	task->ids.group_leader = task;
	task->lifecycle.exit_signal = SIGCHLD;
	task->resources.uid = 0;
	task->resources.gid = 0;
	task->sigctx.sas.ss_flags = SS_DISABLE;
	sched_task_init(task);

	INIT_LIST_HEAD(&task->links.children);
	INIT_LIST_HEAD(&task->links.sibling);
	INIT_LIST_HEAD(&task->links.thread_group);
	INIT_LIST_HEAD(&task->links.thread_node);
	INIT_LIST_HEAD(&task->sched.run_list);
	wait_channel_init(&task->links.wait_child_queue);

	memset(kstack, 0, KSTACK_SIZE);

	pid_attach_task(task->ids.pid, task);

	return task;
}

int task_init_resources(struct task_struct *task)
{
	int ret;

	if (!task)
		return -EINVAL;

	ret = init_files(task);
	if (ret < 0)
		return ret;

	ret = init_fs(task);
	if (ret < 0)
		goto fail;

	ret = signals_init(task);
	if (ret < 0)
		goto fail;

	return 0;

fail:
	task_release_resources(task);
	return ret;
}

void task_release_resources(struct task_struct *task)
{
	if (!task)
		return;

	close_files(task);
	exit_fs(task);
	signals_release(task);
}

void task_free(struct task_struct *task)
{
	if (!task)
		return;
	BUG_ON(task->active_wait);

	pid_detach_task(task->ids.pid, task);
	free_pid(task->ids.pid);

	task_release_resources(task);

	if (task_kernel_stack_safe(task)) {
		free_page(task_kernel_stack(task), KSTACK_ORDER);
		task_set_kernel_stack(task, NULL);
	}

	kfree(task);
}

void task_init(void)
{
	pid_init();

	memset(&idle_task, 0, sizeof(struct task_struct));
	idle_task.ids.pid = 0;
	idle_task.lifecycle.state = TASK_RUNNING;
	arch_task_init(&idle_task);
	idle_task.resources.mm = NULL;
	idle_task.ids.tgid = idle_task.ids.pid;
	idle_task.ids.pgid = idle_task.ids.pid;
	idle_task.ids.sid = idle_task.ids.pid;
	idle_task.ids.group_leader = &idle_task;
	idle_task.lifecycle.exit_signal = SIGCHLD;
	idle_task.resources.uid = 0;
	idle_task.resources.gid = 0;
	idle_task.sigctx.sas.ss_flags = SS_DISABLE;
	sched_task_init(&idle_task);

	INIT_LIST_HEAD(&idle_task.links.children);
	INIT_LIST_HEAD(&idle_task.links.sibling);
	INIT_LIST_HEAD(&idle_task.links.thread_group);
	INIT_LIST_HEAD(&idle_task.links.thread_node);
	INIT_LIST_HEAD(&idle_task.sched.run_list);
	wait_channel_init(&idle_task.links.wait_child_queue);
	BUG_ON(task_init_resources(&idle_task) < 0);
	pid_attach_task(idle_task.ids.pid, &idle_task);

	cpu_boot_init(&idle_task);
	set_current_task(&idle_task);

	pr_info("task: idle (PID 0) created\n");
}

struct task_struct *kernel_thread(void (*fn)(void *), void *arg)
{
	struct task_struct *parent = current_task();
	struct task_struct *task = task_alloc();

	if (!task)
		return NULL;
	if (task_init_resources(task) < 0) {
		task_free(task);
		return NULL;
	}

	arch_task_setup_kernel_thread(task, fn, arg);

	task->links.parent = parent;
	list_add_tail(&task->links.sibling, &parent->links.children);

	sched_enqueue(task);

	return task;
}

void set_init_task(struct task_struct *task)
{
	BUG_ON(!task);
	BUG_ON(task->ids.pid != 1);
	BUG_ON(init_task && init_task != task);

	init_task = task;
}

bool task_is_group_leader(const struct task_struct *task)
{
	return task && task->ids.group_leader == task;
}

bool task_group_has_other_threads(const struct task_struct *task)
{
	if (!task || !task->ids.group_leader)
		return false;

	return !list_empty(&task->ids.group_leader->links.thread_group);
}

struct task_struct *task_find_thread(pid_t tid)
{
	return pid_task(tid);
}

struct task_struct *task_find_group_leader(pid_t tgid)
{
	struct task_struct *task = pid_task(tgid);

	if (!task || !task_is_group_leader(task) || task->ids.tgid != tgid)
		return NULL;

	return task;
}

bool task_in_thread_group(const struct task_struct *task, pid_t tgid)
{
	return task && task->ids.tgid == tgid;
}

bool task_pgid_exists(pid_t pgid)
{
	for (pid_t pid = 1; pid <= PID_MAX; pid++) {
		struct task_struct *task = pid_task(pid);

		if (task && task_pgid(task) == pgid)
			return true;
	}

	return false;
}

bool task_sid_exists(pid_t sid)
{
	for (pid_t pid = 1; pid <= PID_MAX; pid++) {
		struct task_struct *task = pid_task(pid);

		if (task && task_sid(task) == sid)
			return true;
	}

	return false;
}

bool task_pgid_in_session(pid_t pgid, pid_t sid)
{
	for (pid_t pid = 1; pid <= PID_MAX; pid++) {
		struct task_struct *task = pid_task(pid);

		if (task && task_pgid(task) == pgid && task_sid(task) == sid)
			return true;
	}

	return false;
}

void __nonnull(1) task_set_pgid_all(struct task_struct *leader, pid_t pgid)
{
	struct task_struct *thread;

	leader = task_group_leader(leader);
	task_set_pgid(leader, pgid);
	list_for_each_entry (thread, task_thread_group(leader),
			     links.thread_node)
		task_set_pgid(thread, pgid);
}

void __nonnull(1) task_set_sid_all(struct task_struct *leader, pid_t sid)
{
	struct task_struct *thread;

	leader = task_group_leader(leader);
	task_set_sid(leader, sid);
	list_for_each_entry (thread, task_thread_group(leader),
			     links.thread_node)
		task_set_sid(thread, sid);
}
