/*
 * kernel/signal.c - 信号机制
 *
 * 功能：
 *   实现符合 POSIX 语义的信号机制。每个 task_struct 中包含：
 *     - sigaction[32]：32 个信号的处理器描述。
 *     - blocked       ：被阻塞的信号掩码。
 *     - pending       ：待处理信号位图。
 *
 *   信号投递流程（do_signal）：
 *     - 在每次 trap 返回用户态（U-mode）前调用 do_signal。
 *     - 遍历 pending 位图，找到第一个未被 blocked 的待处理信号。
 *     - SIGKILL（9）和 SIGSTOP（19）不可捕获、不可阻塞，
 *       始终执行默认行为。
 *     - 投递动作：在用户栈上构建 signal_frame（保存当前 trap_frame
 *       和 blocked 掩码），修改 trap_frame 使返回后执行信号处理器
 *      （sepc = handler 地址，a0 = signo，ra = trampoline 地址）。
 *     - sigreturn 系统调用：从 signal_frame 恢复原始 trap_frame
 *       和 blocked 掩码，使信号处理器返回后继续执行被中断的程序。
 *
 * 主要函数：
 *   do_signal(tf)                  - 在所有 trap 返回 U-mode 前调用，
 *                                     检查并投递 pending 信号。
 *   do_kill(pid, sig)              - kill 系统调用内部实现。
 *   do_sigaction(sig, act, oldact) - sigaction 系统调用内部实现（注册/查询处理器）。
 *   do_sigreturn(tf, sp)           - 从信号处理器返回，恢复 trap_frame + blocked。
 *   send_signal(sig, target)       - 向目标进程发送信号（置 pending 位）。
 *   setup_signal_frame(tf, ka)     - 在用户栈上构建 signal_frame，
 *                                     保存 trap_frame 和 blocked，
 *                                     修改 trap_frame（sepc=handler, a0=signo,
 *                                     ra=trampoline）。
 */

#include <kernel/errno.h>
#include <kernel/exit.h>
#include <kernel/buddy.h>
#include <kernel/fs.h>
#include <kernel/mm.h>
#include <kernel/sched.h>
#include <kernel/signal.h>
#include <kernel/slab.h>
#include <kernel/string.h>
#include <kernel/task.h>
#include <uapi/syscall.h>
#include <asm/csr.h>
#include <asm/page.h>
#include <asm/pte.h>
#include <asm/trap.h>

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

	if (refcount_dec_and_test(&signal->refcount))
		kfree(signal);
}

int signals_init(struct task_struct *task)
{
	if (!task)
		return -EINVAL;

	task->sighand = sighand_alloc();
	if (!task->sighand)
		return -ENOMEM;

	task->signal = signal_state_alloc();
	if (!task->signal) {
		sighand_put(task->sighand);
		task->sighand = NULL;
		return -ENOMEM;
	}

	task->blocked = 0;
	task->pending = 0;
	task->in_handler = 0;
	return 0;
}

void signals_release(struct task_struct *task)
{
	if (!task)
		return;

	sighand_put(task->sighand);
	signal_state_put(task->signal);
	task->sighand = NULL;
	task->signal = NULL;
	task->blocked = 0;
	task->pending = 0;
	task->in_handler = 0;
}

int signals_clone(struct task_struct *child, bool share_sighand,
		  bool share_signal)
{
	struct sighand_struct *sighand;
	struct signal_struct *signal;

	if (!child)
		return -EINVAL;

	if (share_sighand) {
		sighand = current ? current->sighand : NULL;
		if (!sighand)
			return -EINVAL;
		sighand_get(sighand);
	} else {
		sighand = sighand_dup(current ? current->sighand : NULL);
		if (!sighand)
			return -ENOMEM;
	}

	if (share_signal) {
		signal = current ? current->signal : NULL;
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
		if (current && current->signal) {
			mutex_lock(&current->signal->lock);
			memcpy(signal->rlimits, current->signal->rlimits,
			       sizeof(signal->rlimits));
			mutex_unlock(&current->signal->lock);
		}
	}

	signals_release(child);
	child->sighand = sighand;
	child->signal = signal;
	child->blocked = current ? current->blocked : 0;
	child->pending = 0;
	child->in_handler = 0;
	return 0;
}

struct task_struct *find_task_by_pid(pid_t pid)
{
	return task_find_thread(pid);
}

bool signal_pending(struct task_struct *task)
{
	uint64_t pending;
	uint64_t blocked;

	if (!task)
		return false;

	pending = task->pending;
	if (task->signal) {
		mutex_lock(&task->signal->lock);
		pending |= task->signal->shared_pending;
		mutex_unlock(&task->signal->lock);
	}
	blocked = task->blocked & ~unblockable_mask();
	return (pending & ~blocked) != 0;
}

static void wake_signal_target(struct task_struct *task, int sig)
{
	if (!task)
		return;

	if (task->state == TASK_INTERRUPTIBLE &&
	    ((signal_mask(sig) & ~task->blocked) || !signal_is_catchable(sig))) {
		task->state = TASK_RUNNING;
		if (task != current)
			sched_wakeup(task);
		return;
	}

	if (sig == SIGKILL || sig == SIGCONT) {
		if (task->state == TASK_STOPPED ||
		    task->state == TASK_SLEEPING) {
			task->state = TASK_RUNNING;
			if (task != current)
				sched_wakeup(task);
		}
	}
}

int send_signal(int sig, struct task_struct *task)
{
	if (!signal_is_valid(sig))
		return -EINVAL;
	if (!task || task->state == TASK_DEAD || task->state == TASK_ZOMBIE)
		return -ESRCH;

	task->pending |= signal_mask(sig);
	wake_signal_target(task, sig);

	return 0;
}

int send_group_signal(int sig, struct task_struct *leader)
{
	if (!signal_is_valid(sig))
		return -EINVAL;
	if (!leader || leader->state == TASK_DEAD ||
	    leader->state == TASK_ZOMBIE)
		return -ESRCH;

	if (!leader->signal)
		return send_signal(sig, leader);

	mutex_lock(&leader->signal->lock);
	leader->signal->shared_pending |= signal_mask(sig);
	mutex_unlock(&leader->signal->lock);

	wake_signal_target(leader, sig);
	if (task_is_group_leader(leader)) {
		struct task_struct *thread;

		list_for_each_entry (thread, &leader->thread_group,
				     thread_node)
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

	/*
	 * 重入护栏：当该信号的 handler 正在运行时再次被强制投递（典型场景：
	 * 用户安装了 SIGSEGV 处理器，而该处理器自身又访问了非法地址）。
	 *
	 * "handler 运行期间本信号不可重入"这条不变量由两处共同维护：
	 * setup_signal_frame 在投递时把本信号加入 blocked 并置 in_handler 位，
	 * sys_sigreturn 返回时清除。若不检查而直接解除屏蔽再投递，会击穿这层
	 * 保护，导致 setup_signal_frame → 缺页 → force_signal 的无限递归。
	 * 因此一旦发现该信号已在投递中，对 current 直接按默认处置终止。
	 */
	if (task->in_handler & signal_mask(sig)) {
		if (task == current)
			do_exit(SIGNAL_EXIT_CODE(sig));
		return 0;
	}

	task->blocked &= ~signal_mask(sig);
	if (task->sighand) {
		mutex_lock(&task->sighand->lock);
		if (task->sighand->sigactions[sig].sa_handler == SIG_IGN)
			task->sighand->sigactions[sig].sa_handler = SIG_DFL;
		mutex_unlock(&task->sighand->lock);
	}

	ret = send_signal(sig, task);
	if (ret < 0)
		return ret;

	return 0;
}

vaddr_t signal_trampoline_start(void)
{
	return SIGNAL_TRAMPOLINE_ADDR;
}

vaddr_t signal_trampoline_end(void)
{
	return SIGNAL_TRAMPOLINE_ADDR + PAGE_SIZE;
}

bool signal_trampoline_contains(vaddr_t addr)
{
	return addr >= signal_trampoline_start() &&
	       addr < signal_trampoline_end();
}

bool signal_trampoline_overlaps(vaddr_t start, vaddr_t end)
{
	return start < signal_trampoline_end() &&
	       end > signal_trampoline_start();
}

int signal_map_trampoline(pte_t *pgd)
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
		fence_i();
	}

	map_page(pgd, SIGNAL_TRAMPOLINE_ADDR, __pa((uintptr_t)trampoline_page),
		 PTE_USER_RX);
	return 0;
}

static void stop_current(void)
{
	current->state = TASK_STOPPED;
	schedule();
}

static int setup_signal_frame(struct trap_frame *tf, int sig,
			      const struct sigaction *action)
{
	uintptr_t sp = (tf->sp - sizeof(struct signal_frame)) & ~(uintptr_t)0xf;
	struct signal_frame frame;

	if (!access_ok((void *)sp, sizeof(frame)))
		return -EFAULT;

	frame.tf = *tf;
	frame.blocked = current->blocked;
	frame.sig = sig;

	if (copy_to_user((void *)sp, &frame, sizeof(frame)) != 0)
		return -EFAULT;

	/*
	 * 投递不变量：handler 运行期间本信号必须被屏蔽，且 in_handler 置位。
	 * blocked 由 sys_sigreturn 用 frame.blocked 恢复，in_handler 由
	 * frame.sig 清除。force_signal 依赖 in_handler 判断是否重入，故二者
	 * 必须成对维护。详见 force_signal 的重入护栏注释。
	 */
	current->blocked |= signal_mask(sig);
	current->blocked |= action->sa_mask;
	current->blocked &= ~unblockable_mask();
	current->in_handler |= signal_mask(sig);

	tf->sepc = (uintptr_t)action->sa_handler;
	tf->ra = SIGNAL_TRAMPOLINE_ADDR;
	tf->sp = sp;
	tf->a0 = (uintptr_t)sig;
	return 0;
}

static uint64_t current_shared_pending(void)
{
	uint64_t pending = 0;

	if (!current || !current->signal)
		return 0;

	mutex_lock(&current->signal->lock);
	pending = current->signal->shared_pending;
	mutex_unlock(&current->signal->lock);
	return pending;
}

static void clear_shared_pending(int sig)
{
	if (!current || !current->signal)
		return;

	mutex_lock(&current->signal->lock);
	current->signal->shared_pending &= ~signal_mask(sig);
	mutex_unlock(&current->signal->lock);
}

static int next_signal(bool *shared)
{
	uint64_t shared_pending = current_shared_pending();
	uint64_t pending = current->pending | shared_pending;
	uint64_t deliverable;

	*shared = false;
	pending &= (1UL << (NSIG - 1)) - 1;
	deliverable = pending & ~(current->blocked & ~unblockable_mask());
	if (!deliverable)
		return 0;

	for (int sig = 1; sig < NSIG; sig++) {
		if (deliverable & signal_mask(sig)) {
			*shared = (current->pending & signal_mask(sig)) == 0 &&
				  (shared_pending & signal_mask(sig)) != 0;
			return sig;
		}
	}

	return 0;
}

static struct sigaction get_signal_action(int sig)
{
	struct sigaction action;

	memset(&action, 0, sizeof(action));
	if (!current || !current->sighand)
		return action;

	mutex_lock(&current->sighand->lock);
	action = current->sighand->sigactions[sig];
	mutex_unlock(&current->sighand->lock);
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
			current->pending &= ~mask;

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

int do_sigaltstack(void)
{
	/* TODO(signal): 需要在 task_struct 中保存用户备用信号栈，并在
	 * setup_signal_frame 中支持 SA_ONSTACK 后再实现。 */
	return -ENOSYS;
}

int do_sigaction(int sig, const struct sigaction *act,
		 struct sigaction *oldact, size_t sigsetsize)
{
	struct sigaction kact;

	if (!signal_is_valid(sig))
		return -EINVAL;
	if (!signal_is_catchable(sig) && act)
		return -EINVAL;
	if (sigsetsize != 0 && sigsetsize != sizeof(uint64_t))
		return -EINVAL;

	if (oldact) {
		struct sigaction old;

		if (!current->sighand)
			return -EINVAL;

		mutex_lock(&current->sighand->lock);
		old = current->sighand->sigactions[sig];
		mutex_unlock(&current->sighand->lock);

		if (copy_to_user(oldact, &old, sizeof(old)) != 0)
			return -EFAULT;
	}

	if (!act)
		return 0;

	if (copy_from_user(&kact, act, sizeof(kact)) != 0)
		return -EFAULT;
	if (kact.sa_handler == SIG_ERR)
		return -EINVAL;

	kact.sa_mask &= ~unblockable_mask();
	if (!current->sighand)
		return -EINVAL;

	mutex_lock(&current->sighand->lock);
	current->sighand->sigactions[sig] = kact;
	mutex_unlock(&current->sighand->lock);
	return 0;
}

int do_sigprocmask(int how, const uint64_t *set,
		   uint64_t *oldset, size_t sigsetsize)
{
	uint64_t newset;

	if (sigsetsize != 0 && sigsetsize != sizeof(uint64_t))
		return -EINVAL;

	if (oldset) {
		uint64_t old = current->blocked;

		if (copy_to_user(oldset, &old, sizeof(old)) != 0)
			return -EFAULT;
	}

	if (!set)
		return 0;

	if (copy_from_user(&newset, set, sizeof(newset)) != 0)
		return -EFAULT;
	newset &= ~unblockable_mask();

	switch (how) {
	case SIG_BLOCK:
		current->blocked |= newset;
		break;
	case SIG_UNBLOCK:
		current->blocked &= ~newset;
		break;
	case SIG_SETMASK:
		current->blocked = newset;
		break;
	default:
		return -EINVAL;
	}

	current->blocked &= ~unblockable_mask();
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

	*tf = frame.tf;
	current->blocked = frame.blocked & ~unblockable_mask();
	current->in_handler &= ~signal_mask(frame.sig);
	current->tf = tf;
	return (ssize_t)tf->a0;
}
