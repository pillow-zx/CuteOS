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
 *   send_signal(sig, target)       - 向目标进程发送信号（置 pending 位）。
 *   setup_signal_frame(tf, ka)     - 在用户栈上构建 signal_frame，
 *                                     保存 trap_frame 和 blocked，
 *                                     修改 trap_frame（sepc=handler, a0=signo,
 * ra=trampoline）。 sys_sigreturn()                - 从信号处理器返回，恢复
 * trap_frame + blocked。 sys_kill(pid, sig)             - kill 系统调用实现。
 *   sys_sigaction(sig, act, oldact)- sigaction 系统调用（注册/查询处理器）。
 */

#include <kernel/errno.h>
#include <kernel/exit.h>
#include <kernel/buddy.h>
#include <kernel/mm.h>
#include <kernel/sched.h>
#include <kernel/signal.h>
#include <kernel/string.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <asm/csr.h>
#include <asm/page.h>
#include <asm/pte.h>
#include <asm/trap.h>

static void *trampoline_page;

static_assert(SYS_sigreturn >= 0 && SYS_sigreturn < 2048,
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

static __always_inline bool signal_is_catchable(int sig)
{
	return sig != SIGKILL && sig != SIGSTOP;
}

static __always_inline uint64_t unblockable_mask(void)
{
	return signal_mask(SIGKILL) | signal_mask(SIGSTOP);
}

static struct task_struct *find_task_recursive(struct task_struct *root,
					       pid_t pid)
{
	struct task_struct *child;

	if (!root)
		return NULL;
	if (root->pid == pid)
		return root;
	if (list_empty(&root->children))
		return NULL;

	list_for_each_entry (child, &root->children, sibling) {
		struct task_struct *found = find_task_recursive(child, pid);

		if (found)
			return found;
	}

	return NULL;
}

struct task_struct *find_task_by_pid(pid_t pid)
{
	return find_task_recursive(&idle_task, pid);
}

int send_signal(int sig, struct task_struct *task)
{
	if (!signal_is_valid(sig))
		return -EINVAL;
	if (!task || task->state == TASK_DEAD || task->state == TASK_ZOMBIE)
		return -ESRCH;

	task->pending |= signal_mask(sig);

	if (sig == SIGKILL || sig == SIGCONT) {
		if (task->state == TASK_STOPPED ||
		    task->state == TASK_SLEEPING) {
			task->state = TASK_RUNNING;
			if (task != current)
				sched_wakeup(task);
		}
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
	if (task->sigactions[sig].sa_handler == SIG_IGN)
		task->sigactions[sig].sa_handler = SIG_DFL;

	ret = send_signal(sig, task);
	if (ret < 0)
		return ret;

	return 0;
}

/*
 * signals_clone - 为新创建的子进程继承可继承的信号状态
 * @child: 刚由 task_alloc() 分配、尚未入就绪队列的子进程
 *
 * sigactions 与 blocked 从父进程继承；pending 清零（不继承待处理信号）；
 * in_handler 保持 task_alloc() 给出的 0（子进程不在任何 handler 中）。
 * 这是 fork 唯一应当知道的信号语义——至于这些字段如何表示、还有哪些字段，
 * 是 signal.c 的私有实现细节。
 */
void signals_clone(struct task_struct *child)
{
	memcpy(child->sigactions, current->sigactions,
	       sizeof(child->sigactions));
	child->blocked = current->blocked;
	child->pending = 0;
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
		RISCV_ADDI(RISCV_REG_A7, RISCV_REG_ZERO, SYS_sigreturn),
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
			      __sighandler_t handler)
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
	current->blocked |= current->sigactions[sig].sa_mask;
	current->blocked &= ~unblockable_mask();
	current->in_handler |= signal_mask(sig);

	tf->sepc = (uintptr_t)handler;
	tf->ra = SIGNAL_TRAMPOLINE_ADDR;
	tf->sp = sp;
	tf->a0 = (uintptr_t)sig;
	return 0;
}

static int next_signal(void)
{
	uint64_t pending = current->pending;
	uint64_t deliverable;

	pending &= (1UL << (NSIG - 1)) - 1;
	deliverable = pending & ~(current->blocked & ~unblockable_mask());
	if (!deliverable)
		return 0;

	for (int sig = 1; sig < NSIG; sig++) {
		if (deliverable & signal_mask(sig))
			return sig;
	}

	return 0;
}

void do_signal(struct trap_frame *tf)
{
	while (true) {
		int sig = next_signal();

		if (sig == 0)
			return;

		uint64_t mask = signal_mask(sig);
		__sighandler_t handler = current->sigactions[sig].sa_handler;

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

		if (setup_signal_frame(tf, sig, handler) < 0)
			do_exit(SIGNAL_EXIT_CODE(SIGSEGV));
		return;
	}
}

ssize_t sys_kill(struct trap_frame *tf)
{
	pid_t pid = (pid_t)tf->a0;
	int sig = (int)tf->a1;
	struct task_struct *task;

	if (sig != 0 && !signal_is_valid(sig))
		return -EINVAL;
	if (pid <= 0)
		return -EINVAL;

	task = find_task_by_pid(pid);
	if (!task)
		return -ESRCH;
	if (sig == 0)
		return 0;

	return send_signal(sig, task);
}

ssize_t sys_tgkill(struct trap_frame *tf)
{
	pid_t tgid = (pid_t)tf->a0;
	pid_t tid = (pid_t)tf->a1;
	int sig = (int)tf->a2;
	struct task_struct *task;

	if (sig != 0 && !signal_is_valid(sig))
		return -EINVAL;
	if (tgid <= 0 || tid <= 0)
		return -EINVAL;

	/*
	 * TODO(signal): 当前内核没有线程组，tid 与 pid 等价。等 clone
	 * 支持线程后，应按 tgid 校验线程组成员关系。
	 */
	if (tgid != tid)
		return -ESRCH;

	task = find_task_by_pid(tid);
	if (!task)
		return -ESRCH;
	if (sig == 0)
		return 0;

	return send_signal(sig, task);
}

ssize_t sys_sigaltstack(struct trap_frame *tf)
{
	(void)tf;
	/*
	 * TODO(signal): 需要在 task_struct 中保存用户备用信号栈，并在
	 * setup_signal_frame 中支持 SA_ONSTACK 后再实现。
	 */
	return -ENOSYS;
}

ssize_t sys_sigaction(struct trap_frame *tf)
{
	int sig = (int)tf->a0;
	const struct sigaction *act = (const struct sigaction *)tf->a1;
	struct sigaction *oldact = (struct sigaction *)tf->a2;
	size_t sigsetsize = (size_t)tf->a3;
	struct sigaction kact;

	if (!signal_is_valid(sig))
		return -EINVAL;
	if (!signal_is_catchable(sig) && act)
		return -EINVAL;
	if (sigsetsize != 0 && sigsetsize != sizeof(uint64_t))
		return -EINVAL;

	if (oldact) {
		struct sigaction old = current->sigactions[sig];

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
	current->sigactions[sig] = kact;
	return 0;
}

ssize_t sys_sigprocmask(struct trap_frame *tf)
{
	int how = (int)tf->a0;
	const uint64_t *set = (const uint64_t *)tf->a1;
	uint64_t *oldset = (uint64_t *)tf->a2;
	size_t sigsetsize = (size_t)tf->a3;
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

ssize_t sys_sigreturn(struct trap_frame *tf)
{
	struct signal_frame frame;
	struct signal_frame *user_frame = (struct signal_frame *)tf->sp;

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
