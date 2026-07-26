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
#include <kernel/sync.h>
#include <kernel/fdtable.h>
#include <kernel/fs_struct.h>
#include <kernel/vfs.h>

#include "pid_internal.h"
#include "task_internal.h"

struct task_pgid_query {
	pid_t pgid;
	pid_t sid;
	const struct task_struct *ignored;
};

struct task_struct idle_task;

struct cpu cpu_table[NR_CPUS];
uint32_t nr_cpu_ids;

struct task_struct *init_task;
static DEFINE_MUTEX(process_lock);

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
	refcount_set(&task->lifecycle.refs, 1);
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

static void task_destroy(struct task_struct *task)
{
	BUG_ON(task->active_wait);
	task_release_resources(task);

	free_pid(task->ids.pid);

	if (task_kernel_stack_safe(task)) {
		free_page(task_kernel_stack(task), KSTACK_ORDER);
		task_set_kernel_stack(task, NULL);
	}

	kfree(task);
}

void task_free(struct task_struct *task)
{
	if (!task)
		return;
	BUG_ON(task->active_wait);
	BUG_ON(task->lifecycle.published);
	BUG_ON(refcount_read(&task->lifecycle.refs) != 1);

	task_destroy(task);
}

void task_publish(struct task_struct *task)
{
	BUG_ON(refcount_read(&task->lifecycle.refs) <= 0);

	pid_attach_task(task->ids.pid, task);
}

void task_unpublish(struct task_struct *task)
{
	pid_detach_task(task->ids.pid, task);
}

bool task_try_get_published(struct task_struct *task)
{
	if (!task->lifecycle.published)
		return false;

	return refcount_inc_not_zero(&task->lifecycle.refs);
}

void task_put(struct task_struct *task)
{
	if (!task || task == &idle_task)
		return;

	if (refcount_dec_and_test(&task->lifecycle.refs)) {
		BUG_ON(task->lifecycle.published);
		task_destroy(task);
	}
}

void task_init(void)
{
	pid_init();

	memset(&idle_task, 0, sizeof(struct task_struct));
	refcount_set(&idle_task.lifecycle.refs, 1);
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
	task_publish(&idle_task);

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

	task_publish(task);
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
	return pid_task_get(tid);
}

struct task_struct *task_find_group_leader(pid_t tgid)
{
	struct task_struct *task = pid_task_get(tgid);

	if (!task || !task_is_group_leader(task) || task->ids.tgid != tgid) {
		task_put(task);
		return NULL;
	}

	return task;
}

bool task_in_thread_group(const struct task_struct *task, pid_t tgid)
{
	return task && task->ids.tgid == tgid;
}

bool task_is_user_process(const struct task_struct *task)
{
	return task && task_is_group_leader(task) &&
	       atomic_read_acquire(&task->lifecycle.user_process) != 0;
}

void task_inherit_process_role(struct task_struct *child,
			       const struct task_struct *parent)
{
	BUG_ON(!child || !parent);
	atomic_set_release(
		&child->lifecycle.user_process,
		atomic_read_acquire(&parent->lifecycle.user_process));
}

void task_mark_user_process(struct task_struct *task)
{
	BUG_ON(!task);
	atomic_set_release(&task->lifecycle.user_process, 1);
}

static bool task_pgid_exists_visit(struct task_struct *task, void *arg)
{
	const struct task_pgid_query *query = arg;

	return task->ids.pgid == query->pgid;
}

static bool task_pgid_exists_locked(pid_t pgid)
{
	struct task_pgid_query query = {
		.pgid = pgid,
	};

	return pid_visit_published(task_pgid_exists_visit, &query);
}

static bool task_pgid_in_session_visit(struct task_struct *task, void *arg)
{
	const struct task_pgid_query *query = arg;

	return task->ids.pgid == query->pgid && task->ids.sid == query->sid;
}

static bool task_pgid_in_session_locked(pid_t pgid, pid_t sid)
{
	struct task_pgid_query query = {
		.pgid = pgid,
		.sid = sid,
	};

	return pid_visit_published(task_pgid_in_session_visit, &query);
}

static bool task_pgid_live_member_visit(struct task_struct *task, void *arg)
{
	const struct task_pgid_query *query = arg;

	return task != query->ignored && task->ids.pgid == query->pgid &&
	       task->ids.sid == query->sid && task_state(task) != TASK_ZOMBIE &&
	       task_state(task) != TASK_DEAD;
}

static bool
task_pgid_has_live_member_except_locked(pid_t pgid, pid_t sid,
					const struct task_struct *ignored)
{
	struct task_pgid_query query = {
		.pgid = pgid,
		.sid = sid,
		.ignored = ignored,
	};

	return pid_visit_published(task_pgid_live_member_visit, &query);
}

bool task_pgid_has_live_member_except(pid_t pgid, pid_t sid,
				      const struct task_struct *ignored)
{
	bool found;

	mutex_lock(&process_lock);
	found = task_pgid_has_live_member_except_locked(pgid, sid, ignored);
	mutex_unlock(&process_lock);
	return found;
}

int task_process_snapshot(const struct task_struct *task,
			  struct task_process_identity *identity)
{
	if (!task || !identity)
		return -EINVAL;

	mutex_lock(&process_lock);
	identity->pgid = task->ids.pgid;
	identity->sid = task->ids.sid;
	mutex_unlock(&process_lock);
	return 0;
}

int task_process_clone_identity(struct task_struct *child,
				const struct task_struct *parent)
{
	if (!child || !parent)
		return -EINVAL;
	if (child->lifecycle.published)
		return -EINVAL;

	mutex_lock(&process_lock);
	child->ids.pgid = parent->ids.pgid;
	child->ids.sid = parent->ids.sid;
	mutex_unlock(&process_lock);
	return 0;
}

static void task_set_pgid_all(struct task_struct *leader, pid_t pgid)
{
	struct task_struct *thread;

	leader = task_group_leader(leader);
	leader->ids.pgid = pgid;
	list_for_each_entry (thread, task_thread_group(leader),
			     links.thread_node)
		thread->ids.pgid = pgid;
}

static void task_set_sid_all(struct task_struct *leader, pid_t sid)
{
	struct task_struct *thread;

	leader = task_group_leader(leader);
	leader->ids.sid = sid;
	list_for_each_entry (thread, task_thread_group(leader),
			     links.thread_node)
		thread->ids.sid = sid;
}

int task_process_setsid(struct task_struct *task,
			struct task_process_identity *previous)
{
	struct task_struct *leader = task_group_leader_safe(task);
	pid_t sid;
	int ret = 0;

	if (!previous)
		return -EINVAL;
	if (!leader)
		return -ESRCH;

	mutex_lock(&process_lock);
	previous->pgid = leader->ids.pgid;
	previous->sid = leader->ids.sid;
	sid = task_pid(leader);
	if (task_pgid_exists_locked(sid)) {
		ret = -EPERM;
	} else {
		task_set_sid_all(leader, sid);
		task_set_pgid_all(leader, sid);
		ret = sid;
	}
	mutex_unlock(&process_lock);
	return ret;
}

int task_process_setpgid(struct task_struct *caller, pid_t pid, pid_t pgid,
			 struct task_process_identity *previous)
{
	struct task_struct *self = task_group_leader_safe(caller);
	struct task_struct *target;
	pid_t new_pgid;
	bool put_target = false;
	int ret = 0;

	if (!previous)
		return -EINVAL;
	if (!self)
		return -ESRCH;

	mutex_lock(&process_lock);
	if (pid == 0) {
		target = self;
	} else {
		target = task_find_thread(pid);
		put_target = true;
	}
	if (!target || !task_is_group_leader(target) ||
	    task_tgid(target) != task_pid(target)) {
		ret = -ESRCH;
		goto out;
	}
	if (target != self && task_parent(target) != self) {
		ret = -EPERM;
		goto out;
	}
	if (target != self && target->ids.sid != self->ids.sid) {
		ret = -EPERM;
		goto out;
	}
	if (target->ids.sid == task_pid(target)) {
		ret = -EPERM;
		goto out;
	}

	new_pgid = pgid == 0 ? task_pid(target) : pgid;
	if (new_pgid != task_pid(target) &&
	    !task_pgid_in_session_locked(new_pgid, target->ids.sid)) {
		ret = -EPERM;
		goto out;
	}

	previous->pgid = target->ids.pgid;
	previous->sid = target->ids.sid;
	task_set_pgid_all(target, new_pgid);
out:
	mutex_unlock(&process_lock);
	if (put_target && target)
		task_put(target);
	return ret;
}

#ifdef KERNEL_SELFTEST
pid_t task_test_pgid(const struct task_struct *task)
{
	struct task_process_identity identity;

	BUG_ON(task_process_snapshot(task, &identity) < 0);
	return identity.pgid;
}

pid_t task_test_sid(const struct task_struct *task)
{
	struct task_process_identity identity;

	BUG_ON(task_process_snapshot(task, &identity) < 0);
	return identity.sid;
}

void task_test_set_process_identity(struct task_struct *task, pid_t pgid,
				    pid_t sid)
{
	BUG_ON(!task);

	mutex_lock(&process_lock);
	task->ids.pgid = pgid;
	task->ids.sid = sid;
	mutex_unlock(&process_lock);
}

void task_test_mark_user_process(struct task_struct *task)
{
	task_mark_user_process(task);
}

void task_test_inherit_process_role(struct task_struct *child,
				    const struct task_struct *parent)
{
	task_inherit_process_role(child, parent);
}
#endif
