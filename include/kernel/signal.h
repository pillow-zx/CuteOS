#ifndef _CUTEOS_KERNEL_SIGNAL_H
#define _CUTEOS_KERNEL_SIGNAL_H

/**
 * @file signal.h
 * @brief 内核信号状态、投递、用户返回与 sigaltstack 接口。
 */

#include <kernel/types.h>
#include <kernel/refcount.h>
#include <kernel/sync.h>
#include <kernel/resource.h>
#include <kernel/task.h>
#include <kernel/time.h>
#include <kernel/page.h>
#include <kernel/pgtable.h>
#include <kernel/trap.h>
#include <uapi/signal.h>

/**
 * @struct sighand_struct
 * @brief Shared signal-handler table referenced by a thread group.
 *
 * @par Fields
 * - @c refcount: References from tasks sharing handlers.
 * - @c lock: Serializes sigaction updates.
 * - @c sigactions: Linux signal action table.
 */
struct sighand_struct {
	refcount_t refcount;
	mutex_t lock;
	struct sigaction sigactions[NSIG];
};

/**
 * @struct signal_struct
 * @brief Thread-group shared signal, timer, and resource-limit state.
 *
 * @par Fields
 * - @c refcount: References from tasks in the group.
 * - @c lock: Serializes shared signal-state updates.
 * - @c shared_pending: Pending process-directed signal mask.
 * - @c itimers: setitimer state.
 * - @c posix_timers: POSIX timer table.
 * - @c rlimits: prlimit64 state.
 */
struct signal_struct {
	refcount_t refcount;
	mutex_t lock;
	uint64_t shared_pending;
	siginfo_t shared_pending_info[NSIG];
	struct itimer_state itimers[ITIMER_COUNT];
	struct posix_timer_table posix_timers;
	struct rlimit64 rlimits[RLIM_NLIMITS];
};

/**
 * @def SIGNAL_TRAMPOLINE_ADDR
 * @brief Fixed user virtual address of the signal trampoline mapping.
 */
constexpr vaddr_t SIGNAL_TRAMPOLINE_ADDR = USER_STACK_GUARD_BASE - PAGE_SIZE;

/**
 * @struct signal_frame
 * @brief Kernel-built user stack frame consumed by rt_sigreturn.
 *
 * @par Fields
 * The signal module records only this frame's user stack address and signal
 * number in a kernel-owned LIFO chain.  The user frame remains the source of
 * restored registers, mask, and alternate-stack state.
 */
struct rt_sigframe {
	siginfo_t info;
	struct ucontext uc;
};

static_assert(sizeof(struct rt_sigframe) == 1088,
	      "Linux riscv64 rt_sigframe size mismatch");

struct task_struct;

__must_check __pure
static inline struct signal_struct *task_signal_state(const struct task_struct *task)
{
	return task ? task->resources.signal : NULL;
}

__must_check __pure
static inline struct sighand_struct *task_sighand(const struct task_struct *task)
{
	return task ? task->resources.sighand : NULL;
}

static inline void task_set_sighand(struct task_struct *task,
					     struct sighand_struct *sighand)
{
	if (task)
		task->resources.sighand = sighand;
}

static inline void task_set_signal_state(struct task_struct *task,
						  struct signal_struct *signal)
{
	if (task)
		task->resources.signal = signal;
}

__must_check __pure
static inline uint64_t task_blocked_mask(const struct task_struct *task)
{
	return task ? task->sigctx.blocked : 0;
}

static inline void task_set_blocked_mask(struct task_struct *task,
						  uint64_t mask)
{
	if (task)
		task->sigctx.blocked = mask;
}

static inline void task_or_blocked_mask(struct task_struct *task,
						 uint64_t mask)
{
	if (task)
		task->sigctx.blocked |= mask;
}

static inline void task_and_blocked_mask(struct task_struct *task,
						  uint64_t mask)
{
	if (task)
		task->sigctx.blocked &= mask;
}

__must_check __pure
static inline uint64_t task_pending_mask(const struct task_struct *task)
{
	return task ? task->sigctx.pending : 0;
}

static inline void task_set_pending_mask(struct task_struct *task,
						  uint64_t mask)
{
	if (task)
		task->sigctx.pending = mask;
}

static inline void task_or_pending_mask(struct task_struct *task,
						 uint64_t mask)
{
	if (task)
		task->sigctx.pending |= mask;
}

static inline void task_and_pending_mask(struct task_struct *task,
						  uint64_t mask)
{
	if (task)
		task->sigctx.pending &= mask;
}

__must_check __pure __nonnull(1) __returns_nonnull
static inline struct stack_t *task_altstack(struct task_struct *task)
{
	return &task->sigctx.sas;
}

__must_check __pure
static inline struct stack_t *task_altstack_safe(struct task_struct *task)
{
	return task ? task_altstack(task) : NULL;
}

bool signal_is_valid(int sig);
uint64_t signal_mask(int sig);
bool signal_is_catchable(int sig);
uint64_t unblockable_mask(void);
uint64_t signal_blocked_mask(struct task_struct *task);
void signal_block_mask(struct task_struct *task, uint64_t mask);
void signal_unblock_mask(struct task_struct *task, uint64_t mask);
void signal_set_blocked_mask(struct task_struct *task, uint64_t mask);
void signal_mark_pending(struct task_struct *task, uint64_t mask);
void signal_clear_pending(struct task_struct *task, uint64_t mask);
uint64_t signal_pending_mask(const struct task_struct *task);
/**
 * @brief Restore a temporary wait mask after the next user signal delivery.
 * @param task Task returning from an interruptible masked wait.
 * @param mask Signal mask active before the temporary wait mask.
 */
void signal_defer_mask_restore(struct task_struct *task, uint64_t mask);
int send_signal(int sig, struct task_struct *task);
int send_signal_info(int sig, const siginfo_t *info, struct task_struct *task);
int send_group_signal(int sig, struct task_struct *leader);
int send_group_signal_info(int sig, const siginfo_t *info,
			   struct task_struct *leader);
int send_pgrp_signal(int sig, pid_t pgid);
int send_session_pgrp_signal(int sig, pid_t pgid, pid_t sid);
int send_current_signal(int sig);
int force_signal(int sig, struct task_struct *task);
int force_signal_info(int sig, const siginfo_t *info, struct task_struct *task);
int signal_pending_info(const struct task_struct *task, int sig,
			siginfo_t *info);
bool signal_pending(struct task_struct *task);
bool signal_fatal_pending(struct task_struct *task);
int signals_init(struct task_struct *task);
int signals_clone(struct task_struct *child, bool share_sighand,
		  bool share_signal, bool disable_altstack);

/**
 * @brief Apply the signal-disposition transition required by exec.
 * @param task Task that successfully installed a new user image.
 *
 * Caught dispositions are reset to @c SIG_DFL, while @c SIG_IGN remains
 * ignored. The signal mask and pending signals are left unchanged.
 */
void signals_execve(struct task_struct *task);
void signals_release(struct task_struct *task);
/**
 * @brief Discard all active signal-frame validation records for a task.
 * @param task Task whose signal-frame chain is cleared.
 *
 * This is used when an address space is replaced and during signal teardown.
 */
void signal_clear_frames(struct task_struct *task);
/**
 * @brief Deliver one pending signal before returning to userspace.
 * @param tf User trap frame to rewrite for handler entry.
 */
void do_signal(struct trap_frame *tf);

/**
 * @brief Register the fixed signal trampoline user mapping.
 */
void signal_user_map_init(void);

/**
 * @brief Implement Linux kill() process and process-group signal semantics.
 * @param pid Positive process ID, zero for the caller's process group, -1 for
 *        the supported broadcast policy, or a negative process-group ID.
 * @param sig Signal number, or 0 for permission/existence probe.
 * @return 0 on success, or a negative errno.
 */
int do_kill(pid_t pid, int sig);

/**
 * @brief Implement tkill() thread-directed signal semantics.
 * @param tid Target thread id.
 * @param sig Signal number.
 * @return 0 on success, or a negative errno.
 */
int do_tkill(pid_t tid, int sig);

/**
 * @brief Implement tgkill() thread-group-qualified signal semantics.
 * @param tgid Target thread-group id.
 * @param tid Target thread id.
 * @param sig Signal number.
 * @return 0 on success, or a negative errno.
 */
int do_tgkill(pid_t tgid, pid_t tid, int sig);

/**
 * @brief Register or query the current task alternate signal stack.
 * @param ss Optional new userspace stack_t.
 * @param old_ss Optional output for previous stack_t.
 * @return 0 on success, or a negative errno.
 */
int do_sigaltstack(const struct stack_t *ss, struct stack_t *old_ss);

/**
 * @brief Install or query one signal action.
 * @param sig Signal number.
 * @param act Optional new action.
 * @param oldact Optional output for previous action.
 * @return 0 on success, or a negative errno.
 */
int do_sigaction(int sig, const struct sigaction *act,
		 struct sigaction *oldact);

/**
 * @brief Apply Linux rt_sigprocmask operation to the current task.
 * @param how SIG_BLOCK, SIG_UNBLOCK, or SIG_SETMASK.
 * @param set Optional new mask.
 * @param oldset Optional output for previous mask.
 * @return 0 on success, or a negative errno.
 */
int do_sigprocmask(int how, const uint64_t *set, uint64_t *oldset);

/**
 * @brief Temporarily replace the signal mask and wait for interruption.
 * @param mask Replacement blocked signal mask.
 * @return Always -EINTR after an unblocked signal is observed, or a negative
 *         errno from the wait subsystem.
 *
 * The previous mask is restored through the signal-frame path after a caught
 * signal handler returns to userspace.
 */
int signal_sigsuspend(uint64_t mask);

/**
 * @brief Synchronously consume or wait for a pending signal in a set.
 * @param set Signals accepted by the wait.
 * @param timeout Optional relative timeout; NULL waits indefinitely.
 * @param info Consumed signal information.
 * @return Signal number on success, or a negative errno.
 */
__must_check __nonnull(3)
__access_no_size(read_only, 2) __access_no_size(write_only, 3)
int signal_wait_pending(uint64_t set, const struct timespec *timeout,
		siginfo_t *info);

/**
 * @brief Restore a userspace context from a signal frame.
 * @param tf Current syscall trap frame.
 * @param sp User stack pointer pointing at the current frame-stack top.
 * @return The restored a0 value for syscall dispatch to write back.
 */
ssize_t do_sigreturn(struct trap_frame *tf, uintptr_t sp);

#endif
