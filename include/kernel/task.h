#ifndef _CUTEOS_KERNEL_TASK_H
#define _CUTEOS_KERNEL_TASK_H

/**
 * @file task.h
 * @brief 进程控制块、线程组关系与 task 生命周期公共接口。
 */

#include <kernel/types.h>
#include <kernel/list.h>
#include <kernel/wait.h>
#include <kernel/compiler.h>
#include <kernel/cpu.h>
#include <kernel/rseq_types.h>
#include <arch/task.h>
#include <uapi/signal.h>

/**
 * @def TASK_RUNNING
 * @brief task is runnable or currently executing on the single CPU.
 */
constexpr uint32_t TASK_RUNNING = 0x00u;

/**
 * @def TASK_UNINTERRUPTIBLE
 * @brief task sleeps until an explicit wakeup, ignoring pending signals.
 */
constexpr uint32_t TASK_UNINTERRUPTIBLE = 0x01u;

/**
 * @def TASK_INTERRUPTIBLE
 * @brief task sleeps until wakeup or signal delivery makes it runnable.
 */
constexpr uint32_t TASK_INTERRUPTIBLE = 0x02u;

/**
 * @def TASK_ZOMBIE
 * @brief task has exited and keeps waitable exit status for its parent.
 */
constexpr uint32_t TASK_ZOMBIE = 0x04u;

/**
 * @def TASK_DEAD
 * @brief task resources have been released and the task is no longer runnable.
 */
constexpr uint32_t TASK_DEAD = 0x08u;

/**
 * @def TASK_STOPPED
 * @brief task is stopped by job-control style state, not eligible to run.
 */
constexpr uint32_t TASK_STOPPED = 0x10u;

/**
 * @def TASK_ANY_SLEEP
 * @brief Mask matching both interruptible and uninterruptible sleep states.
 */
constexpr uint32_t TASK_ANY_SLEEP = TASK_UNINTERRUPTIBLE | TASK_INTERRUPTIBLE;

/**
 * @def KSTACK_ORDER
 * @brief Architecture-selected allocation order for every task kernel stack.
 */
constexpr uint32_t KSTACK_ORDER = ARCH_KSTACK_ORDER;

/**
 * @def KSTACK_SIZE
 * @brief Size in bytes of the per-task kernel stack.
 */
constexpr size_t KSTACK_SIZE = ARCH_KSTACK_SIZE;

struct files_struct;
struct fs_struct;
struct mm_struct;
struct sighand_struct;
struct signal_struct;
struct robust_list_head;
struct task_struct;
struct signal_frame_state;

/**
 * @struct task_identity
 * @brief Linux-visible task identifiers and thread-group leadership.
 *
 * @par Fields
 * - @c pid: Kernel thread id; userspace observes it through gettid.
 * - @c tgid: Thread-group id; userspace observes it through getpid.
 * - @c pgid: Process-group id used by setpgid/getpgid semantics.
 * - @c sid: Session id used by setsid/getsid and controlling tty policy.
 * - @c group_leader: Leader whose pid equals @ref tgid.
 */
struct task_identity {
	pid_t pid;
	pid_t tgid;
	pid_t pgid;
	pid_t sid;
	struct task_struct *group_leader;
};

/**
 * @struct task_lifecycle
 * @brief Run state and exit status owned by scheduler and wait paths.
 *
 * @par Fields
 * - @c state: TASK_* state sampled by wake/schedule paths.
 * - @c exit_code: Wait-visible process status recorded at exit.
 * - @c exit_signal: Signal delivered to parent when this task exits.
 */
struct task_lifecycle {
	volatile uint32_t state;
	int exit_code;
	int exit_signal;
};

/**
 * @struct task_links
 * @brief Intrusive links connecting parent/child and thread-group topology.
 *
 * @par Fields
 * - @c parent: Reaper/wait parent, or NULL for root.
 * - @c children: Head of children whose parent is this task.
 * - @c sibling: Node in parent's children list.
 * - @c thread_group: Head of threads when this task is leader.
 * - @c thread_node: Node in group leader's thread list.
 * - @c wait_child_queue: wait4 sleepers for children.
 */
struct task_links {
	struct task_struct *parent;
	struct list_head children;
	struct list_head sibling;
	struct list_head thread_group;
	struct list_head thread_node;
	struct wait_channel wait_child_queue;
};

/**
 * @struct task_resources
 * @brief Shared subsystem resources referenced by a task.
 *
 * @par Fields
 * - @c mm: User address space; NULL for pure kernel tasks.
 * - @c files: File descriptor table.
 * - @c fs: cwd/root/umask state.
 * - @c sighand: Installed signal actions.
 * - @c signal: Thread-group shared signal state.
 * - @c uid: Current real/effective uid in the simplified cred model.
 * - @c gid: Current real/effective gid in the simplified cred model.
 */
struct task_resources {
	struct mm_struct *mm;
	struct files_struct *files;
	struct fs_struct *fs;
	struct sighand_struct *sighand;
	struct signal_struct *signal;
	uid_t uid;
	gid_t gid;
};

/**
 * @struct task_signal_context
 * @brief Per-thread signal, futex robust-list, and alt-stack state.
 *
 * @par Fields
 * - @c blocked: Linux signal mask; bit n represents signal n+1.
 * - @c pending: Per-thread pending signal mask.
 * - @c signal_frames: Signal-owned LIFO state for active user frames.
 * - @c restore_mask: Signal mask restored after a temporary wait mask.
 * - @c restore_mask_pending: Whether restore_mask must be consumed.
 * - @c sas: sigaltstack state copied to/from userspace ABI.
 * - @c clear_child_tid: User futex word cleared by set_tid_addr exit.
 * - @c robust_list: User robust futex list head.
 * - @c robust_list_len: Userspace-reported robust-list head size.
 */
struct task_signal_context {
	uint64_t blocked;
	uint64_t pending;
	siginfo_t pending_info[NSIG];
	struct signal_frame_state *signal_frames;
	uint64_t restore_mask;
	bool restore_mask_pending;
	struct stack_t sas;
	int *clear_child_tid;
	struct robust_list_head *robust_list;
	size_t robust_list_len;
};

/**
 * @struct restart_context
 * @brief Dispatcher-owned user context for an interrupted syscall restart.
 *
 * A valid context is retained only while a restartable syscall has returned
 * -EINTR and is awaiting signal delivery.  The signal module consumes it
 * through the syscall restart interface; no syscall handler rewrites the
 * trap frame itself.
 */
struct restart_context {
	uintptr_t pc;
	uintptr_t args[6];
	uintptr_t nr;
	bool valid;
	bool restartable;
};

/**
 * @struct task_sched_entity
 * @brief Scheduler-private runnable metadata embedded in a task.
 *
 * @par Fields
 * - @c run_list: Node in the selected run queue.
 * - @c need_resched: User-return path should call schedule.
 * - @c sched_level: Current MLFQ priority level.
 * - @c time_slice: Remaining ticks in the current queue level.
 * - @c sched_ticks: Ticks consumed in the current accounting window.
 * - @c enqueue_jiffies: Time when the task entered a run queue.
 */
struct task_sched_entity {
	struct list_head run_list;
	volatile uint8_t need_resched;
	uint8_t sched_level;
	uint8_t time_slice;
	uint8_t sched_ticks;
	uint64_t enqueue_jiffies;
};

/**
 * @struct task_cputime
 * @brief Tick counters reported through times/getrusage-style interfaces.
 *
 * @par Fields
 * - @c utime_ticks: Timer ticks charged while executing user code.
 * - @c stime_ticks: Timer ticks charged while executing kernel code.
 */
struct task_cputime {
	uint64_t utime_ticks;
	uint64_t stime_ticks;
};

/**
 * @struct task_struct
 * @brief Task lifecycle aggregate and subsystem ownership root.
 *
 * Field groups mirror subsystem ownership. Task owns lifecycle assembly and
 * cross-subsystem identity/resource wiring. Complex per-task semantics belong
 * to the owning subsystem: signal state through signal.h, robust futex and
 * clear_child_tid state through futex.h, rseq state through rseq.h, scheduling
 * policy through sched/, and architecture state through arch task accessors.
 *
 * Helpers in this header are limited to lifecycle aggregation, simple identity
 * and resource wiring, and hot cross-subsystem accessors that do not expose a
 * single subsystem's policy surface.
 *
 * @par Fields
 * - @c arch: RISC-V context, trap frame, stack, satp.
 * - @c ids: PID/TGID/PGID/SID and leader identity.
 * - @c lifecycle: Runnable, sleep, stopped, exit state.
 * - @c links: Parent/child and thread-group intrusive links.
 * - @c resources: MM, fd, fs, signal, and credential refs.
 * - @c sigctx: Per-thread signal/futex ABI state.
 * - @c restart: Dispatcher-owned interrupted-syscall context.
 * - @c rseq: Restartable sequences registration.
 * - @c sched: Scheduler queueing and tick state.
 * - @c cputime: CPU time charged to this task.
 * - @c child_cputime: Reaped child CPU time totals.
 * - @c active_wait: Opaque wait session cancelled during exit.
 */
struct task_struct {
	struct task_arch_state arch;
	struct task_identity ids;
	struct task_lifecycle lifecycle;
	struct task_links links;
	struct task_resources resources;
	struct task_signal_context sigctx;
	struct restart_context restart;
	struct rseq_task_context rseq;
	struct task_sched_entity sched;
	struct task_cputime cputime;
	struct task_cputime child_cputime;
	struct wait_session *active_wait;
};

extern struct task_struct idle_task;

extern struct task_struct *init_task;

#include <arch/task_access.h>

/**
 * @brief Return a task's user address space.
 * @param task Task to inspect, or NULL.
 * @return The task mm, or NULL for NULL/kernel-only tasks.
 */
static inline __must_check __pure struct mm_struct *
task_mm(struct task_struct *task)
{
	return task ? task->resources.mm : NULL;
}

/**
 * @brief Replace a task's user address-space pointer.
 * @param task Task to update, or NULL.
 * @param mm New mm pointer; may be NULL for kernel tasks or after teardown.
 */
static inline void task_set_mm(struct task_struct *task, struct mm_struct *mm)
{
	if (task)
		task->resources.mm = mm;
}

static inline __must_check __pure
	__nonnull(1) struct files_struct *task_files(struct task_struct *task)
{
	return task->resources.files;
}

static inline __must_check __pure struct files_struct *
task_files_safe(struct task_struct *task)
{
	return task ? task_files(task) : NULL;
}

static inline void task_set_files(struct task_struct *task,
				  struct files_struct *files)
{
	if (task)
		task->resources.files = files;
}

static inline __must_check __pure struct fs_struct *
task_fs(struct task_struct *task)
{
	return task ? task->resources.fs : NULL;
}

static inline void task_set_fs(struct task_struct *task,
					struct fs_struct *fs)
{
	if (task)
		task->resources.fs = fs;
}

static inline __must_check __pure __nonnull(1) uid_t
	task_uid(const struct task_struct *task)
{
	return task->resources.uid;
}

static inline __must_check __pure __nonnull(1) gid_t
	task_gid(const struct task_struct *task)
{
	return task->resources.gid;
}

static inline __must_check __pure __nonnull(1) pid_t
	task_pid(const struct task_struct *task)
{
	return task->ids.pid;
}

/**
 * @brief Return the Linux thread-group id observed by getpid().
 * @param task Non-NULL task.
 * @return Thread-group id; equal to the group leader pid.
 */
static inline __must_check __pure __nonnull(1) pid_t
	task_tgid(const struct task_struct *task)
{
	return task->ids.tgid;
}

/**
 * @brief Return the POSIX process-group id attached to a task.
 * @param task Non-NULL task.
 * @return Process-group id used by getpgid/setpgid paths.
 */
static inline __must_check __pure __nonnull(1) pid_t
	task_pgid(const struct task_struct *task)
{
	return task->ids.pgid;
}

/**
 * @brief Return the POSIX session id attached to a task.
 * @param task Non-NULL task.
 * @return Session id used by getsid/setsid and controlling tty paths.
 */
static inline __must_check __pure __nonnull(1) pid_t
	task_sid(const struct task_struct *task)
{
	return task->ids.sid;
}

/**
 * @brief Update one task's process-group id.
 * @param task Non-NULL task to update.
 * @param pgid New process-group id.
 */
static inline __nonnull(1) void task_set_pgid(struct task_struct *task,
						       pid_t pgid)
{
	task->ids.pgid = pgid;
}

/**
 * @brief Update one task's session id.
 * @param task Non-NULL task to update.
 * @param sid New session id.
 */
static inline __nonnull(1) void task_set_sid(struct task_struct *task,
						      pid_t sid)
{
	task->ids.sid = sid;
}

static inline void task_set_uid(struct task_struct *task, uid_t uid)
{
	BUG_ON(!task);
	task->resources.uid = uid;
}

static inline void task_set_gid(struct task_struct *task, gid_t gid)
{
	BUG_ON(!task);
	task->resources.gid = gid;
}

static inline __must_check __pure __nonnull(1) uint32_t
	task_state(const struct task_struct *task)
{
	return task->lifecycle.state;
}

static inline __must_check __pure uint32_t
task_state_safe(const struct task_struct *task)
{
	return task ? task_state(task) : TASK_DEAD;
}

static inline void task_set_state(struct task_struct *task,
					   uint32_t state)
{
	if (task)
		task->lifecycle.state = state;
}

static inline void task_mark_running(struct task_struct *task)
{
	task_set_state(task, TASK_RUNNING);
}

static inline void
task_mark_interruptible_sleep(struct task_struct *task)
{
	task_set_state(task, TASK_INTERRUPTIBLE);
}

static inline void
task_mark_uninterruptible_sleep(struct task_struct *task)
{
	task_set_state(task, TASK_UNINTERRUPTIBLE);
}

static inline void task_mark_stopped(struct task_struct *task)
{
	task_set_state(task, TASK_STOPPED);
}

static inline __must_check __pure __nonnull(1)
struct task_struct *task_group_leader(struct task_struct *task)
{
	return task->ids.group_leader;
}

static inline __must_check __pure struct task_struct *
task_group_leader_safe(struct task_struct *task)
{
	return task ? task_group_leader(task) : NULL;
}

static inline __must_check __pure struct task_struct *
task_parent(struct task_struct *task)
{
	return task ? task->links.parent : NULL;
}

static inline __must_check __pure __nonnull(1) __returns_nonnull
	struct list_head *task_children(struct task_struct *task)
{
	return &task->links.children;
}

static inline __must_check __pure struct list_head *
task_children_safe(struct task_struct *task)
{
	return task ? task_children(task) : NULL;
}

static inline __must_check __pure struct wait_channel *
task_wait_child_queue(struct task_struct *task)
{
	return task ? &task->links.wait_child_queue : NULL;
}

static inline __must_check __pure bool
task_has_parent_link(const struct task_struct *task)
{
	return task && !list_empty(&task->links.sibling);
}

static inline void task_set_parent(struct task_struct *task,
					    struct task_struct *parent)
{
	if (task)
		task->links.parent = parent;
}

static inline void task_link_child(struct task_struct *parent,
					    struct task_struct *child)
{
	if (!parent || !child)
		return;
	child->links.parent = parent;
	list_add_tail(&child->links.sibling, &parent->links.children);
}

static inline void task_unlink_child(struct task_struct *task)
{
	if (!task || list_empty(&task->links.sibling))
		return;
	list_del_init(&task->links.sibling);
	task->links.parent = NULL;
}

/**
 * @def task_for_each_child
 * @brief Iterate over a parent's direct children.
 * @param pos Cursor of type `struct task_struct *`.
 * @param parent Parent task whose children list is traversed.
 *
 * The cursor is recovered from the embedded @c links.sibling node with
 * container-of logic inherited from @ref list_for_each_entry.
 */
#define task_for_each_child(pos, parent)                                       \
	list_for_each_entry (pos, task_children(parent), links.sibling)

static inline __must_check __pure struct list_head *
task_thread_group(struct task_struct *task)
{
	return task ? &task->links.thread_group : NULL;
}

static inline __must_check __pure struct list_head *
task_thread_node(struct task_struct *task)
{
	return task ? &task->links.thread_node : NULL;
}

static inline void task_link_thread(struct task_struct *leader,
					     struct task_struct *thread)
{
	if (!leader || !thread)
		return;
	list_add_tail(&thread->links.thread_node, &leader->links.thread_group);
}

static inline void task_unlink_thread(struct task_struct *task)
{
	if (!task || list_empty(&task->links.thread_node))
		return;
	list_del_init(&task->links.thread_node);
}

static inline __must_check __pure uint64_t
task_user_ticks(const struct task_struct *task)
{
	return task ? task->cputime.utime_ticks : 0;
}

static inline __must_check __pure uint64_t
task_system_ticks(const struct task_struct *task)
{
	return task ? task->cputime.stime_ticks : 0;
}

static inline __must_check __pure int
task_exit_code(struct task_struct *task)
{
	return task ? task->lifecycle.exit_code : 0;
}

static inline void task_set_exit_code(struct task_struct *task,
					       int code)
{
	if (task)
		task->lifecycle.exit_code = code;
}

static inline __must_check __pure int
task_exit_signal(struct task_struct *task)
{
	return task ? task->lifecycle.exit_signal : 0;
}

static inline void task_set_exit_signal(struct task_struct *task,
						 int sig)
{
	if (task)
		task->lifecycle.exit_signal = sig;
}

static inline __must_check __pure struct list_head *
task_run_list(struct task_struct *task)
{
	return task ? &task->sched.run_list : NULL;
}

static inline __must_check __pure bool
task_is_queued(struct task_struct *task)
{
	return task && !list_empty(&task->sched.run_list);
}

static inline __must_check __pure uint8_t
task_need_resched(struct task_struct *task)
{
	return task ? task->sched.need_resched : 0;
}

static inline void task_set_need_resched(struct task_struct *task,
						  uint8_t val)
{
	if (task)
		task->sched.need_resched = val;
}

/**
 * @brief Initialize global task-management state.
 */
void task_init(void);

/**
 * @brief Allocate a zeroed task_struct with architecture stack storage.
 * @return New task on success, or NULL when allocation fails.
 */
struct task_struct *__must_check task_alloc(void);

/**
 * @brief Initialize reference-counted resources for a new task.
 * @param task Task returned by task_alloc().
 * @return 0 on success, or a negative errno.
 */
int __must_check task_init_resources(struct task_struct *task);

/**
 * @brief Drop all resources held by a task.
 * @param task Task whose resources are no longer externally reachable.
 */
void task_release_resources(struct task_struct *task);

/**
 * @brief Free a task_struct and its architecture-owned storage.
 * @param task Task to free; may be NULL.
 */
void task_free(struct task_struct *task);

/**
 * @brief Initialize architecture-owned task fields.
 * @param task Non-NULL task being prepared for execution.
 */
void __nonnull(1) arch_task_init(struct task_struct *task);

/**
 * @brief Build the initial kernel-thread return frame.
 * @param task Task being initialized.
 * @param fn Kernel function to run.
 * @param arg Opaque argument passed to @p fn.
 */
void __nonnull(1, 2)
	arch_task_setup_kernel_thread(struct task_struct *task,
				      void (*fn)(void *), void *arg);

/**
 * @brief Build the child trap frame for clone/fork.
 * @param child Child task being initialized.
 * @param parent_tf Parent trap frame used as the ABI template.
 * @param flags Linux clone flags selected by syscall layer.
 * @param child_stack Optional userspace stack pointer override.
 * @param tls Optional thread-local storage value for clone.
 */
void __nonnull(1, 2)
	arch_task_setup_clone_frame(struct task_struct *child,
				    const struct trap_frame *parent_tf,
				    unsigned long flags, uintptr_t child_stack,
				    uintptr_t tls);

/**
 * @brief Switch active user page-table context between tasks.
 * @param prev Task being switched out.
 * @param next Task being switched in.
 */
void __nonnull(1, 2)
	arch_task_switch_address_space(const struct task_struct *prev,
				       const struct task_struct *next);

/**
 * @brief Switch callee-saved CPU context from @p prev to @p next.
 * @param prev Current task.
 * @param next Next scheduled task.
 */
void __nonnull(1, 2)
	arch_task_switch(struct task_struct *prev, struct task_struct *next);

/**
 * @brief Check whether the saved trap frame came from user mode.
 * @param task Task whose architecture state is inspected.
 * @return true when the saved trap frame represents user context.
 */
bool __must_check __pure
arch_task_trap_from_user(const struct task_struct *task);

#ifdef KERNEL_SELFTEST
bool __must_check __pure arch_task_test_kernel_thread_setup(
	const struct task_struct *task, void (*fn)(void *), void *arg);
bool __must_check __pure arch_task_test_layout_contract(void);
void __nonnull(1)
	arch_task_test_setup_user_return(struct task_struct *task,
					 size_t user_pc, size_t user_sp,
					 size_t user_sstatus);
#endif

/**
 * @brief Create a runnable kernel task.
 * @param fn Kernel function to execute.
 * @param arg Opaque argument passed to @p fn.
 * @return New task on success, or NULL on allocation/setup failure.
 */
struct task_struct *__must_check kernel_thread(void (*fn)(void *), void *arg);

/**
 * @brief Publish the process that becomes PID 1 after exec.
 * @param task Task selected as the init task.
 */
void set_init_task(struct task_struct *task);

/**
 * @brief Test whether a task is its thread-group leader.
 * @param task Task to inspect.
 * @return true when task->pid equals task->tgid.
 */
bool __must_check __pure task_is_group_leader(const struct task_struct *task);

/**
 * @brief Check whether a thread group contains tasks besides its leader.
 * @param task Any task in the group.
 * @return true when another task shares the same tgid.
 */
bool __must_check __pure
task_group_has_other_threads(const struct task_struct *task);

/**
 * @brief Find a thread-group leader by TGID.
 * @param tgid Thread-group id to search for.
 * @return Matching leader, or NULL.
 */
struct task_struct *__must_check __pure task_find_group_leader(pid_t tgid);

/**
 * @brief Find a task by Linux TID.
 * @param tid Thread id to search for.
 * @return Matching task, or NULL.
 */
struct task_struct *__must_check __pure task_find_thread(pid_t tid);

/**
 * @brief Check whether a task belongs to a thread group.
 * @param task Task to inspect.
 * @param tgid Thread-group id.
 * @return true when @p task is in @p tgid.
 */
bool __must_check __pure task_in_thread_group(const struct task_struct *task,
					      pid_t tgid);

/**
 * @brief Check whether any task currently uses a process-group id.
 * @param pgid Process-group id.
 * @return true if at least one task has @p pgid.
 */
bool __must_check __pure task_pgid_exists(pid_t pgid);

/**
 * @brief Check whether any task currently belongs to a session.
 * @param sid Session id.
 * @return true if at least one task has @p sid.
 */
bool __must_check __pure task_sid_exists(pid_t sid);

/**
 * @brief Check whether a process group has a member in one session.
 * @param pgid Process-group id.
 * @param sid Session id.
 * @return true if at least one task has both @p pgid and @p sid.
 */
bool __must_check __pure task_pgid_in_session(pid_t pgid, pid_t sid);

/**
 * @brief Set one thread group's process-group id.
 * @param leader Non-NULL thread-group leader.
 * @param pgid Process-group id applied to all group members.
 */
void __nonnull(1) task_set_pgid_all(struct task_struct *leader, pid_t pgid);

/**
 * @brief Set one thread group's session id.
 * @param leader Non-NULL thread-group leader.
 * @param sid Session id applied to all group members.
 */
void __nonnull(1) task_set_sid_all(struct task_struct *leader, pid_t sid);

#endif
