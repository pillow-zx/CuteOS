#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#include <utest.h>

static volatile sig_atomic_t signal_count;
static volatile sig_atomic_t signal_child_count;
static char *signal_altstack_base;
static size_t signal_altstack_size;

enum {
	RISCV_UCONTEXT_A0 = 10,
};

static const uintptr_t signal_restored_a0 = 0x1234567800000000UL;

static void signal_count_handler(int signal)
{
	(void)signal;
	signal_count++;
}

static void signal_altstack_handler(int signal)
{
	char marker;

	(void)signal;
	if (&marker >= signal_altstack_base &&
	    &marker < signal_altstack_base + signal_altstack_size)
		signal_count++;
}

static void signal_child_handler(int signal)
{
	(void)signal;
	signal_child_count++;
}

static void signal_restore_a0_handler(int signal, siginfo_t *info,
				      void *context)
{
	ucontext_t *ucontext = context;

	(void)signal;
	(void)info;
	ucontext->uc_mcontext.__gregs[RISCV_UCONTEXT_A0] = signal_restored_a0;
}

static void signal_sleep_ms(long milliseconds)
{
	struct timespec delay = {
		.tv_sec = milliseconds / 1000,
		.tv_nsec = (milliseconds % 1000) * 1000000L,
	};

	while (nanosleep(&delay, &delay) < 0)
		;
}

UT_CASE(signal_handler_mask_pending_and_altstack, 1500)
{
	struct sigaction action = {
		.sa_handler = signal_count_handler,
	};
	struct sigaction old_action;
	sigset_t block;
	sigset_t pending;
	stack_t stack;
	stack_t old_stack;
	char alternate_stack[SIGSTKSZ];

	UT_ASSERT_EQ(sigemptyset(&action.sa_mask), 0);
	UT_ASSERT_EQ(sigaction(SIGUSR1, &action, &old_action), 0);
	UT_ASSERT_EQ(sigemptyset(&block), 0);
	UT_ASSERT_EQ(sigaddset(&block, SIGUSR1), 0);
	signal_count = 0;
	UT_ASSERT_EQ(sigprocmask(SIG_BLOCK, &block, NULL), 0);
	UT_ASSERT_EQ(kill(getpid(), SIGUSR1), 0);
	UT_ASSERT_EQ(sigpending(&pending), 0);
	UT_EXPECT(sigismember(&pending, SIGUSR1));
	UT_EXPECT_ERRNO(syscall(SYS_rt_sigpending, NULL, sizeof(pending)),
			EINVAL);
	UT_EXPECT_ERRNO(syscall(SYS_rt_sigpending, NULL, sizeof(unsigned long)),
			EFAULT);
	UT_ASSERT_EQ(sigpending(&pending), 0);
	UT_EXPECT(sigismember(&pending, SIGUSR1));
	UT_ASSERT_EQ(sigprocmask(SIG_UNBLOCK, &block, NULL), 0);
	UT_EXPECT_EQ(signal_count, 1);
	stack = (stack_t){
		.ss_sp = alternate_stack,
		.ss_size = sizeof(alternate_stack),
	};
	signal_altstack_base = alternate_stack;
	signal_altstack_size = sizeof(alternate_stack);
	UT_ASSERT_EQ(sigaltstack(&stack, &old_stack), 0);
	action.sa_handler = signal_altstack_handler;
	action.sa_flags = SA_ONSTACK;
	UT_ASSERT_EQ(sigaction(SIGUSR1, &action, NULL), 0);
	signal_count = 0;
	UT_ASSERT_EQ(kill(getpid(), SIGUSR1), 0);
	UT_EXPECT_EQ(signal_count, 1);
	UT_ASSERT_EQ(sigaltstack(&old_stack, NULL), 0);
	UT_ASSERT_EQ(sigaction(SIGUSR1, &old_action, NULL), 0);
}

UT_CASE(signal_sigchld_process_group_and_fatal_status, 5000)
{
	struct sigaction action = {
		.sa_handler = signal_child_handler,
	};
	struct sigaction old_action;
	int report_pipe[2];
	pid_t child;

	UT_ASSERT_EQ(sigemptyset(&action.sa_mask), 0);
	UT_ASSERT_EQ(sigaction(SIGCHLD, &action, &old_action), 0);
	signal_child_count = 0;
	child = UT_FORK();
	if (child == 0)
		_exit(0);
	UT_EXPECT_EXIT(child, 0);
	UT_EXPECT(signal_child_count > 0);
	UT_ASSERT_EQ(sigaction(SIGCHLD, &old_action, NULL), 0);
	UT_ASSERT_EQ(pipe(report_pipe), 0);
	action.sa_handler = signal_count_handler;
	UT_ASSERT_EQ(sigaction(SIGUSR2, &action, &old_action), 0);
	signal_count = 0;
	child = UT_FORK();
	if (child == 0) {
		char report = 0;

		(void)close(report_pipe[0]);
		while (!signal_count)
			pause();
		report = 'g';
		(void)write(report_pipe[1], &report, 1);
		_exit(0);
	}
	UT_ASSERT_EQ(close(report_pipe[1]), 0);
	signal_sleep_ms(20);
	UT_ASSERT_EQ(kill(-getpgrp(), SIGUSR2), 0);
	UT_EXPECT_EQ(signal_count, 1);
	UT_EXPECT_EQ(read(report_pipe[0], &(char){0}, 1), 1);
	UT_EXPECT_EXIT(child, 0);
	UT_ASSERT_EQ(close(report_pipe[0]), 0);
	UT_ASSERT_EQ(sigaction(SIGUSR2, &old_action, NULL), 0);
	child = UT_FORK();
	if (child == 0) {
		volatile int *invalid = (volatile int *)0;

		*invalid = 1;
		_exit(127);
	}
	UT_EXPECT_SIGNAL(child, SIGSEGV);
}

UT_CASE(signal_eintr_restart_and_sigreturn_restore, 5000)
{
	struct sigaction action = {
		.sa_handler = signal_count_handler,
	};
	struct sigaction old_action;
	int pipefd[2];
	pid_t child;
	char byte;

	UT_ASSERT_EQ(sigemptyset(&action.sa_mask), 0);
	UT_ASSERT_EQ(sigaction(SIGUSR1, &action, &old_action), 0);
	UT_ASSERT_EQ(pipe(pipefd), 0);
	child = UT_FORK();
	if (child == 0) {
		signal_sleep_ms(30);
		(void)kill(getppid(), SIGUSR1);
		_exit(0);
	}
	UT_EXPECT_ERRNO(read(pipefd[0], &byte, 1), EINTR);
	UT_EXPECT_EXIT(child, 0);
	action = (struct sigaction){
		.sa_sigaction = signal_restore_a0_handler,
		.sa_flags = SA_SIGINFO,
	};
	UT_ASSERT_EQ(sigemptyset(&action.sa_mask), 0);
	UT_ASSERT_EQ(sigaction(SIGUSR1, &action, NULL), 0);
	child = UT_FORK();
	if (child == 0) {
		signal_sleep_ms(30);
		(void)kill(getppid(), SIGUSR1);
		_exit(0);
	}
	UT_EXPECT_EQ((uintptr_t)read(pipefd[0], &byte, 1), signal_restored_a0);
	UT_EXPECT_EXIT(child, 0);
	action = (struct sigaction){
		.sa_handler = signal_count_handler,
		.sa_flags = SA_RESTART,
	};
	UT_ASSERT_EQ(sigemptyset(&action.sa_mask), 0);
	UT_ASSERT_EQ(sigaction(SIGUSR1, &action, NULL), 0);
	child = UT_FORK();
	if (child == 0) {
		signal_sleep_ms(30);
		(void)kill(getppid(), SIGUSR1);
		signal_sleep_ms(30);
		(void)write(pipefd[1], "r", 1);
		_exit(0);
	}
	UT_ASSERT_EQ(read(pipefd[0], &byte, 1), 1);
	UT_EXPECT_EQ(byte, 'r');
	UT_EXPECT_EXIT(child, 0);
	UT_ASSERT_EQ(close(pipefd[0]), 0);
	UT_ASSERT_EQ(close(pipefd[1]), 0);
	UT_ASSERT_EQ(sigaction(SIGUSR1, &old_action, NULL), 0);
}
