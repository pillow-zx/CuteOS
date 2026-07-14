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
 * - @c state: Architecture trap-frame payload.
 * - @c blocked: Signal mask to restore on sigreturn.
 * - @c sig: Delivered signal number.
 * - @c on_altstack: Whether delivery entered an alternate stack.
 */
struct rt_sigframe {
	siginfo_t info;
	struct ucontext uc;
};

static_assert(sizeof(struct rt_sigframe) == 1088,
	      "Linux riscv64 rt_sigframe size mismatch");

struct task_struct;

static inline __must_check __pure struct signal_struct *
task_signal_state(struct task_struct *task)
{
	return task ? task->resources.signal : NULL;
}

static inline __must_check __pure struct sighand_struct *
task_sighand(struct task_struct *task)
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

static inline __must_check __pure uint64_t
task_blocked_mask(const struct task_struct *task)
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

static inline __must_check __pure uint64_t
task_pending_mask(const struct task_struct *task)
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

static inline __must_check __pure uint64_t
task_in_handler_mask(const struct task_struct *task)
{
	return task ? task->sigctx.in_handler : 0;
}

static inline void task_set_in_handler_mask(struct task_struct *task,
						     uint64_t mask)
{
	if (task)
		task->sigctx.in_handler = mask;
}

static inline void task_or_in_handler_mask(struct task_struct *task,
						    uint64_t mask)
{
	if (task)
		task->sigctx.in_handler |= mask;
}

static inline void task_and_in_handler_mask(struct task_struct *task,
						     uint64_t mask)
{
	if (task)
		task->sigctx.in_handler &= mask;
}

static inline __must_check __pure __nonnull(1) __returns_nonnull
	struct stack_t *task_altstack(struct task_struct *task)
{
	return &task->sigctx.sas;
}

static inline __must_check __pure struct stack_t *
task_altstack_safe(struct task_struct *task)
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
void signal_enter_handler(struct task_struct *task, int sig);
void signal_leave_handler(struct task_struct *task, int sig);
void signal_clear_handlers(struct task_struct *task);
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
int send_current_signal(int sig);
int force_signal(int sig, struct task_struct *task);
int force_signal_info(int sig, const siginfo_t *info, struct task_struct *task);
int signal_pending_info(const struct task_struct *task, int sig,
			siginfo_t *info);
bool signal_pending(struct task_struct *task);
int signals_init(struct task_struct *task);
int signals_clone(struct task_struct *child, bool share_sighand,
		  bool share_signal, bool disable_altstack);
void signals_release(struct task_struct *task);
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
 * @brief Implement kill() pid-directed signal semantics.
 * @param pid Linux pid argument.
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
 * @brief Restore a userspace context from a signal frame.
 * @param tf Current syscall trap frame.
 * @param sp User stack pointer pointing at signal_frame.
 * @return Does not return normally to the syscall ABI on success.
 */
int do_sigreturn(struct trap_frame *tf, uintptr_t sp);

#endif
