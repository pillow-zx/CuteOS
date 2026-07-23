/*
 * user/bin/signal_test.c - signal user ABI tests
 */

#include <ulib.h>
#include <uapi/mman.h>
#include <uapi/signal.h>

#define ALT_STACK_SIZE (SIGSTKSZ * 2)
#define PAGE_SIZE      4096UL
#define RESTART_PIPE_SIZE    4096
#define RESTART_PARTIAL_SIZE 8192

static char *alt_base;
static volatile int handler_ran;
static volatile int handler_on_altstack;
static volatile int handler_saw_onstack;
static volatile int handler_change_denied;
static volatile int flag_handler_count;
static volatile int flag_handler_depth;
static volatile int flag_handler_max_depth;
static volatile int siginfo_handler_count;
static volatile int siginfo_handler_valid;
static volatile int restart_handler_count;
static volatile int restart_futex_word;
static char restart_pipe_fill[RESTART_PARTIAL_SIZE];
static int restart_sync_fd = -1;

struct test_trap_frame {
	unsigned long sepc;
	unsigned long ra, sp, gp, tp;
	unsigned long t0, t1, t2;
	unsigned long s0, s1;
	unsigned long a0, a1, a2, a3, a4, a5, a6, a7;
	unsigned long s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
	unsigned long t3, t4, t5, t6;
	unsigned long scause;
	unsigned long stval;
	unsigned long sstatus;
};

struct test_signal_frame {
	struct test_trap_frame tf;
	unsigned long blocked;
	unsigned long sig;
	unsigned long on_altstack;
};

static void handler_check_stack(int sig)
{
	unsigned long sp;
	struct stack_t old;
	struct stack_t disabled;

	(void)sig;

	__asm__ volatile("mv %0, sp" : "=r"(sp));

	handler_ran = 1;

	if (sp >= (unsigned long)alt_base &&
	    sp < (unsigned long)alt_base + ALT_STACK_SIZE)
		handler_on_altstack = 1;

	if (sigaltstack(NULL, &old) == 0 && (old.ss_flags & SS_ONSTACK))
		handler_saw_onstack = 1;

	disabled.ss_sp = NULL;
	disabled.ss_flags = SS_DISABLE;
	disabled.ss_size = 0;
	if (sigaltstack(&disabled, NULL) == -1)
		handler_change_denied = 1;
}

static void restart_handler(int sig)
{
	if (sig == SIGUSR2) {
		restart_handler_count++;
		if (restart_sync_fd >= 0)
			write(restart_sync_fd, "s", 1);
	}
}

static void restart_futex_handler(int sig)
{
	restart_handler(sig);
	restart_futex_word = 1;
}

static int install_restart_handler(bool restart, void (*handler)(int))
{
	struct sigaction sa = {0};

	sa.sa_handler = handler;
	sa.sa_flags = restart ? SA_RESTART : 0;
	return sigaction(SIGUSR2, &sa, NULL);
}

static int wait_restart_child(long child)
{
	int status = 0;

	if (wait4(child, &status, 0, NULL) != child || status != 0) {
		printf("FAIL: restart child wait/status\n");
		return 1;
	}
	return 0;
}

static int test_read_signal_restart(bool restart)
{
	int fds[2];
	int sync_fds[2];
	char byte = 0;
	long parent_tid = gettid();
	long child;
	long ret;

	restart_handler_count = 0;
	if (install_restart_handler(restart, restart_handler) != 0 ||
	    pipe(fds) != 0 || pipe(sync_fds) != 0)
		return 1;
	restart_sync_fd = sync_fds[1];
	child = fork();
	if (child == 0) {
		tkill(parent_tid, SIGUSR2);
		read(sync_fds[0], &byte, 1);
		write(fds[1], "r", 1);
		exit(0);
	}
	if (child < 0) {
		restart_sync_fd = -1;
		return 1;
	}
	ret = read(fds[0], &byte, 1);
	restart_sync_fd = -1;
	if (wait_restart_child(child))
		return 1;
	if (restart)
		return ret != 1 || byte != 'r' || restart_handler_count != 1;
	return ret != -EINTR || restart_handler_count != 1;
}

static int test_write_signal_restart(bool restart)
{
	int fds[2];
	char byte = 0;
	long parent_tid = gettid();
	long child;
	long ret;

	restart_handler_count = 0;
	if (install_restart_handler(restart, restart_handler) != 0 ||
	    pipe(fds) != 0 ||
		write(fds[1], restart_pipe_fill, RESTART_PIPE_SIZE) !=
			RESTART_PIPE_SIZE)
		return 1;
	child = fork();
	if (child == 0) {
		tkill(parent_tid, SIGUSR2);
		yield();
		read(fds[0], &byte, 1);
		exit(0);
	}
	if (child < 0)
		return 1;
	ret = write(fds[1], "w", 1);
	if (wait_restart_child(child))
		return 1;
	if (restart)
		return ret != 1 || restart_handler_count != 1;
	return ret != -EINTR || restart_handler_count != 1;
}

static int test_wait4_signal_restart(bool restart)
{
	long parent_tid = gettid();
	long child;
	long ret;
	int status = 0;

	restart_handler_count = 0;
	if (install_restart_handler(restart, restart_handler) != 0)
		return 1;
	child = fork();
	if (child == 0) {
		tkill(parent_tid, SIGUSR2);
		yield();
		exit(0);
	}
	if (child < 0)
		return 1;
	ret = wait4(child, &status, 0, NULL);
	if (!restart && ret == -EINTR)
		ret = wait4(child, &status, 0, NULL);
	if (ret != child || status != 0 || restart_handler_count != 1)
		return 1;
	return 0;
}

static int test_futex_signal_restart(bool restart)
{
	long parent_tid = gettid();
	long child;
	long ret;

	restart_handler_count = 0;
	restart_futex_word = 0;
	if (install_restart_handler(restart, restart_futex_handler) != 0)
		return 1;
	child = fork();
	if (child == 0) {
		tkill(parent_tid, SIGUSR2);
		yield();
		exit(0);
	}
	if (child < 0)
		return 1;
	ret = futex((int *)&restart_futex_word, FUTEX_WAIT, 0, NULL, NULL, 0);
	if (wait_restart_child(child))
		return 1;
	if (restart)
		return ret != -EAGAIN || restart_handler_count != 1;
	return ret != -EINTR || restart_handler_count != 1;
}

static int test_partial_write_signal(void)
{
	int fds[2];
	long parent_tid = gettid();
	long child;
	long ret;

	restart_handler_count = 0;
	if (install_restart_handler(true, restart_handler) != 0 || pipe(fds) != 0)
		return 1;
	child = fork();
	if (child == 0) {
		tkill(parent_tid, SIGUSR2);
		yield();
		exit(0);
	}
	if (child < 0)
		return 1;
	ret = write(fds[1], restart_pipe_fill, RESTART_PARTIAL_SIZE);
	if (wait_restart_child(child))
		return 1;
	return ret != RESTART_PIPE_SIZE || restart_handler_count != 1;
}

static int test_install_altstack(void)
{
	struct stack_t ss;
	long ret;

	alt_base = mmap(NULL, ALT_STACK_SIZE, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)alt_base < 0) {
		printf("FAIL: mmap altstack: %ld\n", (long)alt_base);
		return 1;
	}

	ss.ss_sp = alt_base;
	ss.ss_flags = 0;
	ss.ss_size = ALT_STACK_SIZE;

	ret = sigaltstack(&ss, NULL);
	if (ret != 0) {
		printf("FAIL: sigaltstack install: %ld\n", ret);
		return 1;
	}
	return 0;
}

static int test_query_altstack(void)
{
	struct stack_t old;
	long ret;

	old.ss_sp = NULL;
	old.ss_flags = -1;
	old.ss_size = 0;

	ret = sigaltstack(NULL, &old);
	if (ret != 0) {
		printf("FAIL: sigaltstack query: %ld\n", ret);
		return 1;
	}
	if (old.ss_sp != alt_base) {
		printf("FAIL: query ss_sp mismatch: got %p want %p\n",
		       old.ss_sp, alt_base);
		return 1;
	}
	if (old.ss_size != ALT_STACK_SIZE) {
		printf("FAIL: query ss_size mismatch: got %lu want %lu\n",
		       old.ss_size, (unsigned long)ALT_STACK_SIZE);
		return 1;
	}
	return 0;
}

static int test_handler_on_altstack(void)
{
	struct sigaction sa;
	struct stack_t old;

	handler_ran = 0;
	handler_on_altstack = 0;
	handler_saw_onstack = 0;
	handler_change_denied = 0;

	sa.sa_handler = handler_check_stack;
	sa.sa_flags = SA_ONSTACK;
	sa.sa_mask = 0;
	sa.sa_restorer = NULL;

	if (sigaction(SIGUSR1, &sa, NULL) != 0) {
		printf("FAIL: sigaction SA_ONSTACK\n");
		return 1;
	}

	raise(SIGUSR1);

	if (!handler_ran) {
		printf("FAIL: handler did not run\n");
		return 1;
	}
	if (!handler_on_altstack) {
		printf("FAIL: handler ran but not on alternate stack\n");
		return 1;
	}
	if (!handler_saw_onstack) {
		printf("FAIL: sigaltstack query did not report SS_ONSTACK\n");
		return 1;
	}
	if (!handler_change_denied) {
		printf("FAIL: sigaltstack change on stack was not denied\n");
		return 1;
	}
	if (sigaltstack(NULL, &old) != 0) {
		printf("FAIL: sigaltstack query after handler\n");
		return 1;
	}
	if (old.ss_flags & SS_ONSTACK) {
		printf("FAIL: SS_ONSTACK still set after handler\n");
		return 1;
	}
	return 0;
}

static int test_fork_inherits_altstack(void)
{
	struct sigaction sa;
	long pid;
	int status;

	sa.sa_handler = handler_check_stack;
	sa.sa_flags = SA_ONSTACK;
	sa.sa_mask = 0;
	sa.sa_restorer = NULL;
	if (sigaction(SIGUSR1, &sa, NULL) != 0) {
		printf("FAIL: sigaction fork inherit\n");
		return 1;
	}

	pid = fork();
	if (pid == 0) {
		handler_ran = 0;
		handler_on_altstack = 0;
		handler_saw_onstack = 0;
		handler_change_denied = 0;

		raise(SIGUSR1);
		if (!handler_ran)
			exit(10);
		if (!handler_on_altstack)
			exit(11);
		if (!handler_saw_onstack)
			exit(12);
		if (!handler_change_denied)
			exit(13);
		exit(0);
	}
	if (pid < 0) {
		printf("FAIL: fork: %ld\n", pid);
		return 1;
	}
	if (wait4(pid, &status, 0, NULL) != pid) {
		printf("FAIL: wait4 fork altstack\n");
		return 1;
	}
	if (status != 0) {
		printf("FAIL: fork altstack child status=0x%x\n", status);
		return 1;
	}

	return 0;
}

static int test_disable_altstack(void)
{
	struct stack_t ss, old;
	long ret;

	ss.ss_sp = NULL;
	ss.ss_flags = SS_DISABLE;
	ss.ss_size = 0;

	ret = sigaltstack(&ss, &old);
	if (ret != 0) {
		printf("FAIL: sigaltstack SS_DISABLE: %ld\n", ret);
		return 1;
	}

	sigaltstack(NULL, &old);
	if (old.ss_flags != SS_DISABLE) {
		printf("FAIL: after disable, flags=%d (want SS_DISABLE=%d)\n",
		       old.ss_flags, SS_DISABLE);
		return 1;
	}
	return 0;
}

static int test_invalid_flags(void)
{
	struct stack_t ss;
	long ret;

	ss.ss_sp = alt_base;
	ss.ss_flags = 0xff;
	ss.ss_size = ALT_STACK_SIZE;

	ret = sigaltstack(&ss, NULL);
	if (ret != -22) {
		printf("FAIL: invalid flags: expected -22 got %ld\n", ret);
		return 1;
	}
	return 0;
}

static __attribute__((noinline, noreturn)) void
sigreturn_from_frame(struct test_signal_frame *frame)
{
	__asm__ volatile("mv sp, %0\n"
			 "li a7, 139\n"
			 "ecall\n"
			 :
			 : "r"(frame)
			 : "a7", "memory");
	__builtin_unreachable();
}

static __attribute__((naked, noreturn, used)) void
sigreturn_truncated_pc_exit(void)
{
	__asm__ volatile("li a0, 77\n"
			 "li a7, 93\n"
			 "ecall\n"
			 "1: j 1b\n");
}

static __attribute__((naked, noreturn, used)) void sigreturn_bad_sp_fault(void)
{
	__asm__ volatile("sd zero, 0(sp)\n"
			 "li a0, 78\n"
			 "li a7, 93\n"
			 "ecall\n"
			 "1: j 1b\n");
}

static int signal_expect_child_status(const char *name, long child,
				      int want)
{
	long waited;
	int status = 0;

	if (child < 0) {
		printf("FAIL: %s fork=%ld\n", name, child);
		return 1;
	}
	waited = wait4(child, &status, 0, NULL);
	if (waited != child || status != want) {
		printf("FAIL: %s waited=%ld status=0x%x want=0x%x\n", name,
		       waited, status, want);
		return 1;
	}

	return 0;
}

static int test_sigreturn_inaccessible_frame(void)
{
	long child = fork();

	if (child == 0)
		sigreturn_from_frame((struct test_signal_frame *)0x80000000UL);

	return signal_expect_child_status(
		"sigreturn inaccessible frame", child,
		SIGNAL_EXIT_CODE(SIGSEGV) << 8);
}

static int test_sigreturn_bad_stack_faults_safely(void)
{
	struct test_signal_frame frame;
	long child = fork();

	if (child == 0) {
		memset(&frame, 0, sizeof(frame));
		frame.tf.sepc = (unsigned long)sigreturn_bad_sp_fault;
		frame.tf.sp = 0x80000000UL;
		frame.sig = SIGUSR1;
		sigreturn_from_frame(&frame);
	}

	return signal_expect_child_status("sigreturn bad stack", child,
					  SIGNAL_EXIT_CODE(SIGSEGV) << 8);
}

static int test_sigreturn_rejects_privileged_status(void)
{
	struct test_signal_frame frame;
	unsigned long sp;
	long child = fork();

	if (child == 0) {
		memset(&frame, 0, sizeof(frame));
		__asm__ volatile("mv %0, sp" : "=r"(sp));
		frame.tf.sepc = (unsigned long)sigreturn_truncated_pc_exit;
		frame.tf.sp = sp;
		frame.tf.sstatus = 1UL << 8;
		frame.sig = SIGUSR1;
		sigreturn_from_frame(&frame);
	}

	return signal_expect_child_status("sigreturn privileged status", child,
					  SIGNAL_EXIT_CODE(SIGSEGV) << 8);
}

static void sigreturn_mask_handler(int sig, siginfo_t *info, void *context)
{
	struct ucontext *uc = context;

	if (sig == SIGUSR1 && info && info->si_signo == SIGUSR1 && uc)
		uc->uc_sigmask = ~0UL;
}

static int test_sigreturn_unblocks_kill_and_stop(void)
{
	struct sigaction sa = {0};
	unsigned long mask = 0;

	sa.sa_sigaction = sigreturn_mask_handler;
	sa.sa_flags = SA_SIGINFO;
	if (sigaction(SIGUSR1, &sa, NULL) != 0 || raise(SIGUSR1) != 0)
		return 1;
	if (sigprocmask(SIG_SETMASK, NULL, &mask) != 0)
		return 1;
	if (mask & ((1UL << (SIGKILL - 1)) | (1UL << (SIGSTOP - 1)))) {
		printf("FAIL: sigreturn restored unblockable mask=0x%lx\n", mask);
		return 1;
	}

	return 0;
}

static int test_sigreturn_preserves_invalid_64bit_pc(void)
{
	struct test_signal_frame frame;
	unsigned long sp;
	long child;
	long waited;
	int status = 0;

	child = fork();
	if (child == 0) {
		memset(&frame, 0, sizeof(frame));
		__asm__ volatile("mv %0, sp" : "=r"(sp));
		frame.tf.sepc = (1UL << 32) |
				(unsigned int)(unsigned long)
					sigreturn_truncated_pc_exit;
		frame.tf.sp = sp;
		frame.sig = SIGUSR1;

		sigreturn_from_frame(&frame);
	}
	if (child < 0) {
		printf("FAIL: fork sigreturn pc: %ld\n", child);
		return 1;
	}

	waited = wait4(child, &status, 0, NULL);
	if (waited != child) {
		printf("FAIL: sigreturn pc wait got %ld child %ld\n", waited,
		       child);
		return 1;
	}
	if (status != (SIGNAL_EXIT_CODE(SIGSEGV) << 8)) {
		printf("FAIL: sigreturn pc status expected 0x%x got 0x%x\n",
		       SIGNAL_EXIT_CODE(SIGSEGV) << 8, status);
		return 1;
	}

	return 0;
}

static volatile int usr1_count;

static void usr1_handler(int sig)
{
	if (sig == SIGUSR1)
		usr1_count++;
}

static void siginfo_handler(int sig, siginfo_t *info, void *context)
{
	siginfo_handler_count++;
	if (sig == SIGUSR1 && info && info->si_signo == SIGUSR1 &&
	    info->si_code == SI_USER && context)
		siginfo_handler_valid = 1;
}

static int signal_expect_ret(const char *name, long got, long want)
{
	if (got != want) {
		printf("FAIL: %s expected %ld got %ld\n", name, want, got);
		return 1;
	}

	return 0;
}

static void nodefer_handler(int sig)
{
	(void)sig;

	flag_handler_depth++;
	if (flag_handler_depth > flag_handler_max_depth)
		flag_handler_max_depth = flag_handler_depth;
	flag_handler_count++;
	if (flag_handler_count == 1)
		raise(SIGUSR1);
	flag_handler_depth--;
}

static void reset_hand_handler(int sig)
{
	(void)sig;
	flag_handler_count++;
}

static int test_sigaction_flag_policy(void)
{
	struct sigaction sa;
	struct sigaction old;
	int failed = 0;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = usr1_handler;
	sa.sa_flags = SA_ONSTACK | SA_NODEFER | SA_RESETHAND | SA_RESTART;
	failed += signal_expect_ret("supported action flags",
				    sigaction(SIGUSR1, &sa, NULL), 0);
	memset(&old, 0, sizeof(old));
	failed += signal_expect_ret("query supported action flags",
				    sigaction(SIGUSR1, NULL, &old), 0);
	failed += signal_expect_ret("supported action flags preserved",
				    old.sa_flags, sa.sa_flags);

	memset(&sa, 0, sizeof(sa));
	siginfo_handler_count = 0;
	siginfo_handler_valid = 0;
	sa.sa_sigaction = siginfo_handler;
	sa.sa_flags = SA_SIGINFO;
	failed += signal_expect_ret("install SA_SIGINFO",
				    sigaction(SIGUSR1, &sa, NULL), 0);
	failed += signal_expect_ret("deliver SA_SIGINFO", raise(SIGUSR1), 0);
	if (siginfo_handler_count != 1 || !siginfo_handler_valid) {
		printf("FAIL: SA_SIGINFO count=%d valid=%d\n",
		       siginfo_handler_count, siginfo_handler_valid);
		failed++;
	}
	sa.sa_flags = SA_NOCLDSTOP;
	failed += signal_expect_ret("reject SA_NOCLDSTOP",
				    sigaction(SIGUSR1, &sa, NULL), -EINVAL);
	sa.sa_flags = SA_NOCLDWAIT;
	failed += signal_expect_ret("reject SA_NOCLDWAIT",
				    sigaction(SIGUSR1, &sa, NULL), -EINVAL);
	sa.sa_flags = 1UL << 20;
	failed += signal_expect_ret("reject unknown action flag",
				    sigaction(SIGUSR1, &sa, NULL), -EINVAL);

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = SA_RESTART;
	failed += signal_expect_ret("ignored action preserves supported flags",
				    sigaction(SIGUSR1, &sa, &old), 0);
	failed += signal_expect_ret("ignored action query",
				    sigaction(SIGUSR1, NULL, &old), 0);
	failed += signal_expect_ret("ignored action flag preserved",
				    old.sa_flags, SA_RESTART);

	return failed;
}

static int test_sa_nodefer(void)
{
	struct sigaction sa;

	flag_handler_count = 0;
	flag_handler_depth = 0;
	flag_handler_max_depth = 0;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = nodefer_handler;
	sa.sa_flags = SA_NODEFER;
	if (sigaction(SIGUSR1, &sa, NULL) != 0)
		return 1;
	raise(SIGUSR1);

	if (flag_handler_count != 2 || flag_handler_max_depth != 2) {
		printf("FAIL: SA_NODEFER count=%d max_depth=%d\n",
		       flag_handler_count, flag_handler_max_depth);
		return 1;
	}

	return 0;
}

static int test_sa_nodefer_respects_mask(void)
{
	struct sigaction sa;

	flag_handler_count = 0;
	flag_handler_depth = 0;
	flag_handler_max_depth = 0;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = nodefer_handler;
	sa.sa_flags = SA_NODEFER;
	sa.sa_mask = 1UL << (SIGUSR1 - 1);
	if (sigaction(SIGUSR1, &sa, NULL) != 0)
		return 1;
	raise(SIGUSR1);

	if (flag_handler_count != 2 || flag_handler_max_depth != 1) {
		printf("FAIL: SA_NODEFER mask count=%d max_depth=%d\n",
		       flag_handler_count, flag_handler_max_depth);
		return 1;
	}

	return 0;
}

static int test_sa_resethand(void)
{
	struct sigaction sa;
	long pid;
	long waited;
	int status = 0;

	pid = fork();
	if (pid == 0) {
		flag_handler_count = 0;
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = reset_hand_handler;
		sa.sa_flags = SA_RESETHAND;
		if (sigaction(SIGUSR1, &sa, NULL) != 0)
			exit(1);
		raise(SIGUSR1);
		if (flag_handler_count != 1)
			exit(2);
		raise(SIGUSR1);
		exit(3);
	}
	if (pid < 0)
		return 1;

	waited = wait4(pid, &status, 0, NULL);
	if (waited != pid ||
	    status != (SIGNAL_EXIT_CODE(SIGUSR1) << 8)) {
		printf("FAIL: SA_RESETHAND waited=%ld status=%d\n", waited,
		       status);
		return 1;
	}

	return 0;
}

static int test_tkill_self_signal(void)
{
	struct sigaction sa;
	long tid = gettid();
	long ret;

	usr1_count = 0;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = usr1_handler;
	ret = sigaction(SIGUSR1, &sa, NULL);
	if (ret != 0)
		return signal_expect_ret("sigaction", ret, 0);

	ret = tkill(tid, 0);
	if (signal_expect_ret("self sig 0", ret, 0))
		return 1;
	if (usr1_count != 0) {
		printf("FAIL: sig 0 delivered signal\n");
		return 1;
	}

	ret = tkill(tid, SIGUSR1);
	if (signal_expect_ret("self SIGUSR1", ret, 0))
		return 1;
	if (usr1_count != 1) {
		printf("FAIL: SIGUSR1 count expected 1 got %d\n", usr1_count);
		return 1;
	}

	return 0;
}

static int test_tkill_errors(void)
{
	long tid = gettid();
	int failed = 0;

	failed += signal_expect_ret("bad signal", tkill(tid, NSIG), -EINVAL);
	failed += signal_expect_ret("bad tid", tkill(-1, 0), -EINVAL);
	failed += signal_expect_ret("missing tid", tkill(999999, 0), -ESRCH);

	return failed;
}

static void report_group(const char *name, int ret, int *failed)
{
	printf("signal_test: %s ... ", name);
	if (ret)
		(*failed)++;
	else
		printf("PASS\n");
}

int main(void)
{
	int failed = 0;

	report_group("install altstack", test_install_altstack(), &failed);
	report_group("query altstack", test_query_altstack(), &failed);
	report_group("handler on altstack", test_handler_on_altstack(),
		     &failed);
	report_group("fork inherits altstack", test_fork_inherits_altstack(),
		     &failed);
	report_group("disable altstack", test_disable_altstack(), &failed);
	report_group("invalid altstack flags", test_invalid_flags(), &failed);
	report_group("tkill self signal", test_tkill_self_signal(), &failed);
	report_group("tkill error paths", test_tkill_errors(), &failed);
	report_group("read without SA_RESTART",
		     test_read_signal_restart(false), &failed);
	report_group("read with SA_RESTART", test_read_signal_restart(true),
		     &failed);
	report_group("write without SA_RESTART",
		     test_write_signal_restart(false), &failed);
	report_group("write with SA_RESTART", test_write_signal_restart(true),
		     &failed);
	report_group("partial write preserves count", test_partial_write_signal(),
		     &failed);
	report_group("wait4 without SA_RESTART",
		     test_wait4_signal_restart(false), &failed);
	report_group("wait4 with SA_RESTART", test_wait4_signal_restart(true),
		     &failed);
	report_group("futex without SA_RESTART",
		     test_futex_signal_restart(false), &failed);
	report_group("futex with SA_RESTART", test_futex_signal_restart(true),
		     &failed);
	report_group("sigaction flag policy", test_sigaction_flag_policy(),
		     &failed);
	report_group("SA_NODEFER", test_sa_nodefer(), &failed);
	report_group("SA_NODEFER respects mask",
		     test_sa_nodefer_respects_mask(), &failed);
	report_group("SA_RESETHAND", test_sa_resethand(), &failed);
	report_group("sigreturn invalid pc",
		     test_sigreturn_preserves_invalid_64bit_pc(), &failed);
	report_group("sigreturn inaccessible frame",
		     test_sigreturn_inaccessible_frame(), &failed);
	report_group("sigreturn bad stack",
		     test_sigreturn_bad_stack_faults_safely(), &failed);
	report_group("sigreturn privileged status",
		     test_sigreturn_rejects_privileged_status(), &failed);
	report_group("sigreturn unblockable mask",
		     test_sigreturn_unblocks_kill_and_stop(), &failed);

	if (failed)
		printf("signal_test: %d test group(s) FAILED\n", failed);
	else
		printf("signal_test: all tests PASSED\n");

	return failed ? 1 : 0;
}
