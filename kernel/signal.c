/*
 * kernel/signal.c - 信号机制
 */

#include <kernel/errno.h>
#include <kernel/exit.h>
#include <kernel/buddy.h>
#include <kernel/fs.h>
#include <kernel/mm.h>
#include <kernel/sched.h>
#include <kernel/rseq.h>
#include <kernel/signal.h>
#include <kernel/slab.h>
#include <kernel/task.h>
#include <kernel/user_map.h>
#include <uapi/syscall.h>
#include <kernel/processor.h>
#include <kernel/page.h>
#include <kernel/pgtable.h>
#include <kernel/trap.h>

static void *trampoline_page;

static_assert(SYS_rt_sigreturn >= 0 && SYS_rt_sigreturn < 2048,
	      "SYS_sigreturn must fit in a RISC-V addi immediate");

#define RISCV_REG_ZERO 0
#define RISCV_REG_A7   17
#define RISCV_OP_IMM   0x13
#define RISCV_ECALL    0x00000073
#define RISCV_J_SELF   0x0000006f

#define RISCV_ADDI(rd, rs1, imm)                                               \
	((((uint32_t)(imm) & 0xfff) << 20) | ((uint32_t)(rs1) << 15) |         \
	 ((uint32_t)(rd) << 7) | RISCV_OP_IMM)

bool signal_is_valid(int sig)
{
	return sig > 0 && sig < NSIG;
}

uint64_t signal_mask(int sig)
{
	return 1UL << (sig - 1);
}

static bool signal_is_fatal_default(int sig)
{
	switch (sig) {
	case SIGCHLD:
	case SIGCONT:
		return false;
	default:
		return true;
	}
}

__always_inline bool signal_is_catchable(int sig)
{
	return sig != SIGKILL && sig != SIGSTOP;
}

__always_inline uint64_t unblockable_mask(void)
{
	return signal_mask(SIGKILL) | signal_mask(SIGSTOP);
}

static struct sighand_struct *sighand_alloc(void)
{
	struct sighand_struct *sighand = kmalloc(sizeof(*sighand));

	if (!sighand)
		return NULL;

	memset(sighand, 0, sizeof(*sighand));
	refcount_set(&sighand->refcount, 1);
	mutex_init(&sighand->lock);
	return sighand;
}

static struct sighand_struct *sighand_dup(struct sighand_struct *old)
{
	struct sighand_struct *sighand = sighand_alloc();

	if (!sighand)
		return NULL;
	if (!old)
		return sighand;

	mutex_lock(&old->lock);
	memcpy(sighand->sigactions, old->sigactions,
	       sizeof(sighand->sigactions));
	mutex_unlock(&old->lock);
	return sighand;
}

static void sighand_get(struct sighand_struct *sighand)
{
	if (sighand)
		refcount_inc(&sighand->refcount);
}

static void sighand_put(struct sighand_struct *sighand)
{
	if (!sighand)
		return;

	if (refcount_dec_and_test(&sighand->refcount))
		kfree(sighand);
}

static struct signal_struct *signal_state_alloc(void)
{
	struct signal_struct *signal = kmalloc(sizeof(*signal));

	if (!signal)
		return NULL;

	memset(signal, 0, sizeof(*signal));
	refcount_set(&signal->refcount, 1);
	mutex_init(&signal->lock);
	for (size_t i = 0; i < ITIMER_COUNT; i++)
		itimer_state_init(&signal->itimers[i]);
	posix_timer_table_init(&signal->posix_timers);
	rlimits_init(signal->rlimits);
	return signal;
}

static void signal_state_get(struct signal_struct *signal)
{
	if (signal)
		refcount_inc(&signal->refcount);
}

static void signal_state_put(struct signal_struct *signal)
{
	if (!signal)
		return;

	if (refcount_dec_and_test(&signal->refcount)) {
		for (size_t i = 0; i < ITIMER_COUNT; i++)
			itimer_state_destroy(&signal->itimers[i]);
		posix_timer_table_destroy(&signal->posix_timers);
		kfree(signal);
	}
}

static bool task_signal_target_dead(struct task_struct *task)
{
	if (!task)
		return true;

	return task_state(task) == TASK_DEAD || task_state(task) == TASK_ZOMBIE;
}

static void reset_task_altstack(struct task_struct *task)
{
	struct stack_t *sas = task_altstack_safe(task);

	if (!sas)
		return;
	sas->ss_sp = NULL;
	sas->ss_flags = SS_DISABLE;
	sas->ss_size = 0;
}

int signals_init(struct task_struct *task)
{
	struct sighand_struct *sighand;
	struct signal_struct *signal;

	if (!task)
		return -EINVAL;

	sighand = sighand_alloc();
	if (!sighand)
		return -ENOMEM;
	task_set_sighand(task, sighand);

	signal = signal_state_alloc();
	if (!signal) {
		sighand_put(sighand);
		task_set_sighand(task, NULL);
		return -ENOMEM;
	}
	task_set_signal_state(task, signal);

	signal_set_blocked_mask(task, 0);
	signal_clear_pending(task, ~0UL);
	signal_clear_handlers(task);
	reset_task_altstack(task);
	return 0;
}

void signals_release(struct task_struct *task)
{
	if (!task)
		return;

	sighand_put(task_sighand(task));
	signal_state_put(task_signal_state(task));
	task_set_sighand(task, NULL);
	task_set_signal_state(task, NULL);
	signal_set_blocked_mask(task, 0);
	signal_clear_pending(task, ~0UL);
	signal_clear_handlers(task);
	reset_task_altstack(task);
}

static void signal_copy_rlimits(struct signal_struct *dst,
				struct signal_struct *src)
{
	if (!dst || !src)
		return;

	mutex_lock(&src->lock);
	memcpy(dst->rlimits, src->rlimits, sizeof(dst->rlimits));
	mutex_unlock(&src->lock);
}

int signals_clone(struct task_struct *child, bool share_sighand,
		  bool share_signal, bool disable_altstack)
{
	struct sighand_struct *sighand;
	struct signal_struct *signal;

	if (!child)
		return -EINVAL;

	if (share_sighand) {
		sighand = task_sighand(current_task());
		if (!sighand)
			return -EINVAL;
		sighand_get(sighand);
	} else {
		sighand = sighand_dup(task_sighand(current_task()));
		if (!sighand)
			return -ENOMEM;
	}

	if (share_signal) {
		signal = task_signal_state(current_task());
		if (!signal) {
			sighand_put(sighand);
			return -EINVAL;
		}
		signal_state_get(signal);
	} else {
		signal = signal_state_alloc();
		if (!signal) {
			sighand_put(sighand);
			return -ENOMEM;
		}
		signal_copy_rlimits(signal, task_signal_state(current_task()));
	}

	signals_release(child);
	task_set_sighand(child, sighand);
	task_set_signal_state(child, signal);
	signal_set_blocked_mask(child, signal_blocked_mask(current_task()));
	signal_clear_pending(child, ~0UL);
	signal_clear_handlers(child);
	if (current_task() && !disable_altstack) {
		struct stack_t *child_sas = task_altstack(child);

		*child_sas = *task_altstack(current_task());
		child_sas->ss_flags &= ~SS_ONSTACK;
	} else {
		reset_task_altstack(child);
	}
	return 0;
}

static struct task_struct *find_task_by_pid(pid_t pid)
{
	return task_find_thread(pid);
}

bool signal_pending(struct task_struct *task)
{
	uint64_t pending;
	uint64_t blocked;

	if (!task)
		return false;

	pending = task_pending_mask(task);
	if (task_signal_state(task)) {
		struct signal_struct *signal = task_signal_state(task);

		mutex_lock(&signal->lock);
		pending |= signal->shared_pending;
		mutex_unlock(&signal->lock);
	}
	blocked = task_blocked_mask(task) & ~unblockable_mask();
	return (pending & ~blocked) != 0;
}

uint64_t signal_blocked_mask(struct task_struct *task)
{
	return task_blocked_mask(task);
}

void signal_block_mask(struct task_struct *task, uint64_t mask)
{
	task_or_blocked_mask(task, mask);
	task_and_blocked_mask(task, ~unblockable_mask());
}

void signal_unblock_mask(struct task_struct *task, uint64_t mask)
{
	task_and_blocked_mask(task, ~mask);
	task_and_blocked_mask(task, ~unblockable_mask());
}

void signal_set_blocked_mask(struct task_struct *task, uint64_t mask)
{
	task_set_blocked_mask(task, mask & ~unblockable_mask());
}

void signal_mark_pending(struct task_struct *task, uint64_t mask)
{
	task_or_pending_mask(task, mask);
}

void signal_clear_pending(struct task_struct *task, uint64_t mask)
{
	task_and_pending_mask(task, ~mask);
}

void signal_enter_handler(struct task_struct *task, int sig)
{
	if (!signal_is_valid(sig))
		return;

	task_or_in_handler_mask(task, signal_mask(sig));
}

void signal_leave_handler(struct task_struct *task, int sig)
{
	if (!signal_is_valid(sig))
		return;

	task_and_in_handler_mask(task, ~signal_mask(sig));
}

void signal_clear_handlers(struct task_struct *task)
{
	task_set_in_handler_mask(task, 0);
}

static void wake_signal_target(struct task_struct *task, int sig)
{
	uint32_t state;

	if (!task)
		return;

	state = task_state(task);
	if (state == TASK_INTERRUPTIBLE &&
	    ((signal_mask(sig) & ~task_blocked_mask(task)) ||
	     !signal_is_catchable(sig))) {
		sched_wake_task(task);
		return;
	}

	if (sig == SIGKILL || sig == SIGCONT) {
		if (state == TASK_STOPPED || state == TASK_UNINTERRUPTIBLE)
			sched_wake_task(task);
	}
}

int send_signal(int sig, struct task_struct *task)
{
	if (!signal_is_valid(sig))
		return -EINVAL;
	if (task_signal_target_dead(task))
		return -ESRCH;

	signal_mark_pending(task, signal_mask(sig));
	wake_signal_target(task, sig);

	return 0;
}

int send_current_signal(int sig)
{
	return send_signal(sig, current_task());
}

int send_group_signal(int sig, struct task_struct *leader)
{
	if (!signal_is_valid(sig))
		return -EINVAL;
	if (task_signal_target_dead(leader))
		return -ESRCH;

	if (!task_signal_state(leader))
		return send_signal(sig, leader);

	struct signal_struct *signal = task_signal_state(leader);

	mutex_lock(&signal->lock);
	signal->shared_pending |= signal_mask(sig);
	mutex_unlock(&signal->lock);

	wake_signal_target(leader, sig);
	if (task_is_group_leader(leader)) {
		struct task_struct *thread;

		list_for_each_entry (thread, &leader->links.thread_group,
				     links.thread_node)
			wake_signal_target(thread, sig);
	}

	return 0;
}

int force_signal(int sig, struct task_struct *task)
{
	int ret;

	if (!signal_is_valid(sig))
		return -EINVAL;
	if (!task)
		return -ESRCH;


	if (task_in_handler_mask(task) & signal_mask(sig)) {
		if (task == current_task())
			do_exit(SIGNAL_EXIT_CODE(sig));
		return 0;
	}

	signal_unblock_mask(task, signal_mask(sig));
	if (task_sighand(task)) {
		struct sighand_struct *sighand = task_sighand(task);

		mutex_lock(&sighand->lock);
		if (sighand->sigactions[sig].sa_handler == SIG_IGN)
			sighand->sigactions[sig].sa_handler = SIG_DFL;
		mutex_unlock(&sighand->lock);
	}

	ret = send_signal(sig, task);
	if (ret < 0)
		return ret;

	return 0;
}

static int signal_map_trampoline(pte_t *pgd)
{
	static const uint32_t code[] = {
		RISCV_ADDI(RISCV_REG_A7, RISCV_REG_ZERO, SYS_rt_sigreturn),
		RISCV_ECALL,
		RISCV_J_SELF,
	};

	if (!trampoline_page) {
		trampoline_page = get_free_page(0);
		if (!trampoline_page)
			return -ENOMEM;
		memset(trampoline_page, 0, PAGE_SIZE);
		memcpy(trampoline_page, code, sizeof(code));
		flush_icache();
	}

	return map_page(pgd, SIGNAL_TRAMPOLINE_ADDR,
			__pa((uintptr_t)trampoline_page),
			pgprot_user(true, false, true));
}

void signal_user_map_init(void)
{
	int ret;

	ret = user_map_register_reserved(
		"signal_trampoline", SIGNAL_TRAMPOLINE_ADDR,
		SIGNAL_TRAMPOLINE_ADDR + PAGE_SIZE, signal_map_trampoline);
	BUG_ON(ret < 0);
}

static void stop_current(void)
{
	task_mark_stopped(current_task());
	schedule();
}

static int setup_signal_frame(struct trap_frame *tf, int sig,
			      const struct sigaction *action)
{
	uintptr_t sp;
	struct signal_frame frame;
	struct stack_t *sas = task_altstack(current_task());
	bool on_altstack = false;

	if ((action->sa_flags & SA_ONSTACK) && sas &&
	    !(sas->ss_flags & (SS_DISABLE | SS_ONSTACK))) {
		uintptr_t top = (uintptr_t)sas->ss_sp + sas->ss_size;
		sp = (top - sizeof(struct signal_frame)) & ~(uintptr_t)0xf;
		on_altstack = true;
	} else {
		sp = (trap_user_sp(tf) - sizeof(struct signal_frame)) &
		     ~(uintptr_t)0xf;
	}

	if (!access_ok((void *)sp, sizeof(frame)))
		return -EFAULT;

	trap_save_signal_state(&frame.state, tf);
	frame.blocked = signal_blocked_mask(current_task());
	frame.sig = sig;
	frame.on_altstack = on_altstack;

	if (copy_to_user((void *)sp, &frame, sizeof(frame)) != 0)
		return -EFAULT;

	if (on_altstack)
		sas->ss_flags |= SS_ONSTACK;

	signal_block_mask(current_task(), signal_mask(sig));
	signal_block_mask(current_task(), action->sa_mask);
	signal_enter_handler(current_task(), sig);

	trap_setup_signal_handler(tf, (uintptr_t)action->sa_handler,
				  SIGNAL_TRAMPOLINE_ADDR, sp, (uintptr_t)sig);
	return 0;
}

static uint64_t current_shared_pending(void)
{
	struct signal_struct *signal = task_signal_state(current_task());
	uint64_t pending = 0;

	if (!signal)
		return 0;

	mutex_lock(&signal->lock);
	pending = signal->shared_pending;
	mutex_unlock(&signal->lock);
	return pending;
}

static void clear_shared_pending(int sig)
{
	struct signal_struct *signal = task_signal_state(current_task());

	if (!signal)
		return;

	mutex_lock(&signal->lock);
	signal->shared_pending &= ~signal_mask(sig);
	mutex_unlock(&signal->lock);
}

static int next_signal(bool *shared)
{
	uint64_t shared_pending = current_shared_pending();
	uint64_t pending = task_pending_mask(current_task()) | shared_pending;
	uint64_t deliverable;

	*shared = false;
	pending &= (1UL << (NSIG - 1)) - 1;
	deliverable = pending & ~(signal_blocked_mask(current_task()) &
				  ~unblockable_mask());
	if (!deliverable)
		return 0;

	for (int sig = 1; sig < NSIG; sig++) {
		if (deliverable & signal_mask(sig)) {
			*shared = (task_pending_mask(current_task()) &
				   signal_mask(sig)) == 0 &&
				  (shared_pending & signal_mask(sig)) != 0;
			return sig;
		}
	}

	return 0;
}

static struct sigaction get_signal_action(int sig)
{
	struct sigaction action;
	struct sighand_struct *sighand = task_sighand(current_task());

	memset(&action, 0, sizeof(action));
	if (!sighand)
		return action;

	mutex_lock(&sighand->lock);
	action = sighand->sigactions[sig];
	mutex_unlock(&sighand->lock);
	return action;
}

void do_signal(struct trap_frame *tf)
{
	while (true) {
		bool shared;
		int sig = next_signal(&shared);

		if (sig == 0)
			return;

		uint64_t mask = signal_mask(sig);
		struct sigaction action = get_signal_action(sig);
		__sighandler_t handler = action.sa_handler;

		if (shared)
			clear_shared_pending(sig);
		else
			signal_clear_pending(current_task(), mask);

		if (sig == SIGCONT) {
			if (handler == SIG_DFL || handler == SIG_IGN)
				continue;
		}

		if (sig == SIGSTOP) {
			stop_current();
			continue;
		}

		if (sig == SIGKILL)
			do_exit(SIGNAL_EXIT_CODE(sig));

		if (handler == SIG_IGN)
			continue;

		if (handler == SIG_DFL) {
			if (signal_is_fatal_default(sig))
				do_exit(SIGNAL_EXIT_CODE(sig));
			continue;
		}

		if (rseq_signal_deliver(tf) < 0)
			do_exit(SIGNAL_EXIT_CODE(SIGSEGV));
		if (setup_signal_frame(tf, sig, &action) < 0)
			do_exit(SIGNAL_EXIT_CODE(SIGSEGV));
		return;
	}
}

int do_kill(pid_t pid, int sig)
{
	struct task_struct *task;

	if (sig != 0 && !signal_is_valid(sig))
		return -EINVAL;
	if (pid <= 0)
		return -EINVAL;

	task = task_find_group_leader(pid);
	if (!task)
		return -ESRCH;
	if (sig == 0)
		return 0;

	return send_group_signal(sig, task);
}

int do_tkill(pid_t tid, int sig)
{
	struct task_struct *task;

	if (sig != 0 && !signal_is_valid(sig))
		return -EINVAL;
	if (tid <= 0)
		return -EINVAL;

	task = task_find_thread(tid);
	if (!task)
		return -ESRCH;
	if (sig == 0)
		return 0;

	return send_signal(sig, task);
}

int do_tgkill(pid_t tgid, pid_t tid, int sig)
{
	struct task_struct *task;

	if (sig != 0 && !signal_is_valid(sig))
		return -EINVAL;
	if (tgid <= 0 || tid <= 0)
		return -EINVAL;

	task = find_task_by_pid(tid);
	if (!task || !task_in_thread_group(task, tgid))
		return -ESRCH;
	if (sig == 0)
		return 0;

	return send_signal(sig, task);
}

int do_sigaltstack(const struct stack_t *ss, struct stack_t *old_ss)
{
	struct stack_t *sas = task_altstack(current_task());

	if (old_ss)
		*old_ss = *sas;

	if (ss) {

		if (sas->ss_flags & SS_ONSTACK)
			return -EPERM;

		if (ss->ss_flags != 0 && ss->ss_flags != SS_DISABLE)
			return -EINVAL;

		if (ss->ss_flags & SS_DISABLE) {
			reset_task_altstack(current_task());
		} else {
			if (ss->ss_size < MINSIGSTKSZ)
				return -ENOMEM;
			*sas = *ss;
			sas->ss_flags &= ~SS_ONSTACK;
		}
	}

	return 0;
}

int do_sigaction(int sig, const struct sigaction *act, struct sigaction *oldact)
{
	struct sighand_struct *sighand = task_sighand(current_task());
	struct sigaction kact;

	if (!signal_is_valid(sig))
		return -EINVAL;
	if (!signal_is_catchable(sig) && act)
		return -EINVAL;

	if (oldact) {
		if (!sighand)
			return -EINVAL;

		mutex_lock(&sighand->lock);
		*oldact = sighand->sigactions[sig];
		mutex_unlock(&sighand->lock);
	}

	if (!act)
		return 0;

	kact = *act;
	if (kact.sa_handler == SIG_ERR)
		return -EINVAL;

	kact.sa_mask &= ~unblockable_mask();
	if (!sighand)
		return -EINVAL;

	mutex_lock(&sighand->lock);
	sighand->sigactions[sig] = kact;
	mutex_unlock(&sighand->lock);
	return 0;
}

int do_sigprocmask(int how, const uint64_t *set, uint64_t *oldset)
{
	uint64_t newset;

	if (oldset)
		*oldset = signal_blocked_mask(current_task());

	if (!set)
		return 0;

	newset = *set & ~unblockable_mask();

	switch (how) {
	case SIG_BLOCK:
		signal_block_mask(current_task(), newset);
		break;
	case SIG_UNBLOCK:
		signal_unblock_mask(current_task(), newset);
		break;
	case SIG_SETMASK:
		signal_set_blocked_mask(current_task(), newset);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int do_sigreturn(struct trap_frame *tf, uintptr_t sp)
{
	struct signal_frame frame;
	struct signal_frame *user_frame = (struct signal_frame *)sp;

	if (copy_from_user(&frame, user_frame, sizeof(frame)) != 0)
		do_exit(SIGNAL_EXIT_CODE(SIGSEGV));
	if (!signal_is_valid((int)frame.sig))
		do_exit(SIGNAL_EXIT_CODE(SIGSEGV));

	trap_restore_signal_state(tf, &frame.state);
	signal_set_blocked_mask(current_task(), frame.blocked);
	signal_leave_handler(current_task(), (int)frame.sig);
	if (frame.on_altstack) {
		struct stack_t *sas = task_altstack(current_task());

		if (sas)
			sas->ss_flags &= ~SS_ONSTACK;
	}
	task_set_trap_frame(current_task(), tf);
	return (ssize_t)trap_return_value(tf);
}
