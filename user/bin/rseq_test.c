/*
 * user/bin/rseq_test.c - restartable sequence user ABI tests
 */

#include <ulib.h>
#include <uapi/futex.h>
#include <uapi/mman.h>
#include <uapi/sched.h>

#define RSEQ_SIG 0x53053053U
#define THREAD_STACK_SIZE (16 * 1024UL)

struct rseq_test_area {
	unsigned int cpu_id_start;
	unsigned int cpu_id;
	unsigned long rseq_cs;
	unsigned int flags;
	unsigned int node_id;
	unsigned int mm_cid;
	unsigned int pad;
} __attribute__((aligned(32)));

STATIC_ASSERT(sizeof(struct rseq_test_area) == 32,
	      "rseq test area size mismatch");
STATIC_ASSERT(OFFSETOF(struct rseq_test_area, cpu_id_start) == 0,
	      "rseq cpu_id_start offset mismatch");
STATIC_ASSERT(OFFSETOF(struct rseq_test_area, cpu_id) == 4,
	      "rseq cpu_id offset mismatch");
STATIC_ASSERT(OFFSETOF(struct rseq_test_area, rseq_cs) == 8,
	      "rseq_cs offset mismatch");
STATIC_ASSERT(OFFSETOF(struct rseq_test_area, flags) == 16,
	      "rseq flags offset mismatch");
STATIC_ASSERT(OFFSETOF(struct rseq_test_area, node_id) == 20,
	      "rseq node_id offset mismatch");
STATIC_ASSERT(OFFSETOF(struct rseq_test_area, mm_cid) == 24,
	      "rseq mm_cid offset mismatch");

static struct rseq_test_area rseq_area;
static struct rseq_test_area thread_area;
static volatile int thread_tid = -1;
static volatile int thread_done;
static volatile int thread_result;
static volatile int signal_seen;
static volatile int abort_seen;
static volatile int commit_seen;
static volatile int spin_sink;
static struct rseq_cs rseq_signal_cs __attribute__((aligned(32), used));
static struct rseq_cs rseq_preempt_cs __attribute__((aligned(32), used));

static int rseq_expect_ret(const char *name, long got, long want)
{
	if (got != want) {
		printf("FAIL: %s expected %ld got %ld\n", name, want, got);
		return 1;
	}

	return 0;
}

static int test_rseq_register_initializes_area(void)
{
	long ret;

	memset(&rseq_area, 0xff, sizeof(rseq_area));
	ret = rseq(&rseq_area, sizeof(rseq_area), 0, RSEQ_SIG);
	if (rseq_expect_ret("register", ret, 0))
		return 1;

	if (rseq_area.cpu_id_start != 0 || rseq_area.cpu_id != 0 ||
	    rseq_area.rseq_cs != 0 || rseq_area.flags != 0 ||
	    rseq_area.node_id != 0 || rseq_area.mm_cid != 0) {
		printf("FAIL: rseq area not initialized: cpu_start=%u "
		       "cpu=%u cs=%lu flags=%u node=%u cid=%u\n",
		       rseq_area.cpu_id_start, rseq_area.cpu_id,
		       rseq_area.rseq_cs, rseq_area.flags, rseq_area.node_id,
		       rseq_area.mm_cid);
		return 1;
	}

	return 0;
}

static int test_rseq_unregister_and_error_paths(void)
{
	static struct rseq_test_area other_area;
	int failed = 0;

	failed += rseq_expect_ret("duplicate register",
				  rseq(&rseq_area, sizeof(rseq_area), 0,
				       RSEQ_SIG),
				  -EBUSY);
	failed += rseq_expect_ret("duplicate wrong sig",
				  rseq(&rseq_area, sizeof(rseq_area), 0,
				       RSEQ_SIG + 1),
				  -EPERM);
	failed += rseq_expect_ret("duplicate wrong area",
				  rseq(&other_area, sizeof(other_area), 0,
				       RSEQ_SIG),
				  -EINVAL);
	failed += rseq_expect_ret(
		"unregister wrong sig",
		rseq(&rseq_area, sizeof(rseq_area), RSEQ_FLAG_UNREGISTER,
		     RSEQ_SIG + 1),
		-EPERM);
	failed += rseq_expect_ret(
		"unregister",
		rseq(&rseq_area, sizeof(rseq_area), RSEQ_FLAG_UNREGISTER,
		     RSEQ_SIG),
		0);
	failed += rseq_expect_ret("cpu uninitialized after unregister",
				  rseq_area.cpu_id,
				  RSEQ_CPU_ID_UNINITIALIZED);

	failed += rseq_expect_ret("bad len",
				  rseq(&rseq_area, sizeof(rseq_area) - 1, 0,
				       RSEQ_SIG),
				  -EINVAL);
	failed += rseq_expect_ret(
		"bad alignment",
		rseq((char *)&rseq_area + 4, sizeof(rseq_area), 0, RSEQ_SIG),
		-EINVAL);
	failed += rseq_expect_ret("bad flags",
				  rseq(&rseq_area, sizeof(rseq_area), 2,
				       RSEQ_SIG),
				  -EINVAL);
	failed += rseq_expect_ret("fault address",
				  rseq((void *)0x80000000UL,
				       sizeof(rseq_area), 0, RSEQ_SIG),
				  -EFAULT);

	return failed;
}

static int rseq_thread_register(void *arg)
{
	(void)arg;

	thread_result = rseq(&thread_area, sizeof(thread_area), 0, RSEQ_SIG);
	if (thread_result == 0)
		(void)rseq(&thread_area, sizeof(thread_area),
			   RSEQ_FLAG_UNREGISTER, RSEQ_SIG);
	__atomic_store_n((int *)&thread_done, 1, __ATOMIC_RELEASE);
	futex((int *)&thread_done, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, 1, NULL, 0,
	      0);
	return 0;
}

static int test_rseq_lifecycle(void)
{
	char *argv[] = {"rseq_test", "--check-exec-rseq", 0};
	char *envp[] = {"PATH=/bin", 0};
	unsigned long flags;
	void *stack;
	long child;
	long waited;
	int status = 0;
	int failed = 0;

	failed += rseq_expect_ret("lifecycle register",
				  rseq(&rseq_area, sizeof(rseq_area), 0,
				       RSEQ_SIG),
				  0);
	if (failed)
		return failed;

	child = fork();
	if (child == 0) {
		long ret = rseq(&rseq_area, sizeof(rseq_area), 0, RSEQ_SIG);

		exit(ret == -EBUSY ? 0 : 10);
	}
	if (child < 0)
		return rseq_expect_ret("fork lifecycle", child, 0);
	waited = wait4(child, &status, 0, NULL);
	if (waited != child || status != 0) {
		printf("FAIL: fork rseq inherit waited=%ld child=%ld "
		       "status=%d\n",
		       waited, child, status);
		failed++;
	}

	thread_done = 0;
	thread_result = 0;
	thread_tid = -1;
	stack = mmap(NULL, THREAD_STACK_SIZE, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)stack < 0) {
		printf("FAIL: mmap thread stack: %ld\n", (long)stack);
		failed++;
		goto unregister;
	}
	flags = CLONE_VM | CLONE_SIGHAND | CLONE_THREAD | CLONE_FILES |
		CLONE_FS | CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID;
	child = clone_thread(flags, (char *)stack + THREAD_STACK_SIZE, NULL, 0,
			     (int *)&thread_tid, rseq_thread_register, NULL);
	if (child < 0) {
		printf("FAIL: clone_thread lifecycle: %ld\n", child);
		munmap(stack, THREAD_STACK_SIZE);
		failed++;
		goto unregister;
	}
	while (__atomic_load_n((int *)&thread_done, __ATOMIC_ACQUIRE) == 0)
		futex((int *)&thread_done, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, 0,
		      NULL, 0, 0);
	while (__atomic_load_n((int *)&thread_tid, __ATOMIC_ACQUIRE) != 0)
		futex((int *)&thread_tid, FUTEX_WAIT | FUTEX_PRIVATE_FLAG,
		      thread_tid, NULL, 0, 0);
	munmap(stack, THREAD_STACK_SIZE);
	failed += rseq_expect_ret("thread independent register",
				  thread_result, 0);

	child = fork();
	if (child == 0) {
		execve("/bin/rseq_test", argv, envp);
		exit(11);
	}
	if (child < 0)
		return rseq_expect_ret("fork exec lifecycle", child, 0);
	waited = wait4(child, &status, 0, NULL);
	if (waited != child || status != 0) {
		printf("FAIL: exec rseq clear waited=%ld child=%ld status=%d\n",
		       waited, child, status);
		failed++;
	}

unregister:
	failed += rseq_expect_ret(
		"lifecycle unregister",
		rseq(&rseq_area, sizeof(rseq_area), RSEQ_FLAG_UNREGISTER,
		     RSEQ_SIG),
		0);
	return failed;
}

static int check_exec_rseq(void)
{
	long ret;

	memset(&rseq_area, 0, sizeof(rseq_area));
	ret = rseq(&rseq_area, sizeof(rseq_area), 0, RSEQ_SIG);
	if (ret != 0) {
		printf("FAIL: exec rseq register expected 0 got %ld\n", ret);
		return 1;
	}

	return rseq(&rseq_area, sizeof(rseq_area), RSEQ_FLAG_UNREGISTER,
		    RSEQ_SIG) == 0 ?
		       0 :
		       1;
}

static void rseq_signal_handler(int sig)
{
	(void)sig;
	signal_seen = 1;
}

static __attribute__((noinline)) void run_signal_abort_section(void)
{
	long pid = getpid();
	long tid = gettid();

	__asm__ volatile("lla t0, rseq_signal_cs\n"
			 "sd zero, 0(t0)\n"
			 "lla t1, 1f\n"
			 "sd t1, 8(t0)\n"
			 "lla t2, 2f\n"
			 "sub t2, t2, t1\n"
			 "sd t2, 16(t0)\n"
			 "lla t1, 3f\n"
			 "sd t1, 24(t0)\n"
			 "lla t1, rseq_area\n"
			 "sd t0, 8(t1)\n"
			 "mv a0, %[pid]\n"
			 "mv a1, %[tid]\n"
			 "li a2, %[sig]\n"
			 "li a7, %[tgkill]\n"
			 "1:\n"
			 "ecall\n"
			 "lla t0, commit_seen\n"
			 "li t1, 1\n"
			 "sw t1, 0(t0)\n"
			 "2:\n"
			 "lla t0, rseq_area\n"
			 "sd zero, 8(t0)\n"
			 "j 4f\n"
			 ".balign 4\n"
			 ".word 0x53053053\n"
			 "3:\n"
			 "lla t0, abort_seen\n"
			 "li t1, 1\n"
			 "sw t1, 0(t0)\n"
			 "4:\n"
			 :
			 : [pid] "r"(pid), [tid] "r"(tid), [sig] "i"(SIGUSR1),
			   [tgkill] "i"(SYS_tgkill)
			 : "a0", "a1", "a2", "a7", "t0", "t1", "t2",
			   "memory");
}

static int test_rseq_signal_abort(void)
{
	struct sigaction sa;
	int failed = 0;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = rseq_signal_handler;
	if (sigaction(SIGUSR1, &sa, NULL) != 0) {
		printf("FAIL: sigaction rseq signal\n");
		return 1;
	}

	signal_seen = 0;
	abort_seen = 0;
	commit_seen = 0;
	memset(&rseq_area, 0, sizeof(rseq_area));

	failed += rseq_expect_ret("signal register",
				  rseq(&rseq_area, sizeof(rseq_area), 0,
				       RSEQ_SIG),
				  0);
	if (failed)
		return failed;

	run_signal_abort_section();
	if (!signal_seen) {
		printf("FAIL: rseq signal handler did not run\n");
		failed++;
	}
	if (!abort_seen) {
		printf("FAIL: rseq signal abort label did not run\n");
		failed++;
	}
	if (commit_seen) {
		printf("FAIL: rseq signal section committed\n");
		failed++;
	}
	if (rseq_area.rseq_cs != 0) {
		printf("FAIL: rseq_cs not cleared after signal abort\n");
		failed++;
	}

	failed += rseq_expect_ret(
		"signal unregister",
		rseq(&rseq_area, sizeof(rseq_area), RSEQ_FLAG_UNREGISTER,
		     RSEQ_SIG),
		0);
	return failed;
}

static __attribute__((noinline)) void run_preempt_abort_section(void)
{
	__asm__ volatile("lla t0, rseq_preempt_cs\n"
			 "sd zero, 0(t0)\n"
			 "lla t1, 1f\n"
			 "sd t1, 8(t0)\n"
			 "lla t2, 2f\n"
			 "sub t2, t2, t1\n"
			 "sd t2, 16(t0)\n"
			 "lla t1, 3f\n"
			 "sd t1, 24(t0)\n"
			 "lla t1, rseq_area\n"
			 "sd t0, 8(t1)\n"
			 "li t1, 0\n"
			 "li t2, 20000000\n"
			 "lla t3, spin_sink\n"
			 "1:\n"
			 "sw t1, 0(t3)\n"
			 "addi t1, t1, 1\n"
			 "blt t1, t2, 1b\n"
			 "lla t0, commit_seen\n"
			 "li t1, 1\n"
			 "sw t1, 0(t0)\n"
			 "2:\n"
			 "lla t0, rseq_area\n"
			 "sd zero, 8(t0)\n"
			 "j 4f\n"
			 ".balign 4\n"
			 ".word 0x53053053\n"
			 "3:\n"
			 "lla t0, abort_seen\n"
			 "li t1, 1\n"
			 "sw t1, 0(t0)\n"
			 "4:\n"
			 :
			 :
			 : "t0", "t1", "t2", "t3", "memory");
}

static int test_rseq_preempt_abort(void)
{
	long child;
	long waited;
	int status = 0;
	int failed = 0;

	abort_seen = 0;
	commit_seen = 0;
	memset(&rseq_area, 0, sizeof(rseq_area));

	failed += rseq_expect_ret("preempt register",
				  rseq(&rseq_area, sizeof(rseq_area), 0,
				       RSEQ_SIG),
				  0);
	if (failed)
		return failed;

	child = fork();
	if (child == 0) {
		for (volatile int i = 0; i < 40000000; i++)
			spin_sink = i;
		exit(0);
	}
	if (child < 0) {
		printf("FAIL: preempt fork: %ld\n", child);
		failed++;
		goto unregister;
	}

	run_preempt_abort_section();
	waited = wait4(child, &status, 0, NULL);
	if (waited != child || status != 0) {
		printf("FAIL: preempt child waited=%ld child=%ld status=%d\n",
		       waited, child, status);
		failed++;
	}

	if (!abort_seen) {
		printf("FAIL: rseq preempt abort label did not run\n");
		failed++;
	}
	if (commit_seen) {
		printf("FAIL: rseq preempt section committed\n");
		failed++;
	}
	if (rseq_area.rseq_cs != 0) {
		printf("FAIL: rseq_cs not cleared after preempt abort\n");
		failed++;
	}

unregister:
	failed += rseq_expect_ret(
		"preempt unregister",
		rseq(&rseq_area, sizeof(rseq_area), RSEQ_FLAG_UNREGISTER,
		     RSEQ_SIG),
		0);
	return failed;
}

static void report_group(const char *name, int ret, int *failed)
{
	printf("rseq_test: %s ... ", name);
	if (ret)
		(*failed)++;
	else
		printf("PASS\n");
}

int main(int argc, char **argv)
{
	int failed = 0;

	if (argc > 1 && streq(argv[1], "--check-exec-rseq"))
		return check_exec_rseq();

	report_group("register initializes area",
		     test_rseq_register_initializes_area(), &failed);
	report_group("unregister and error paths",
		     test_rseq_unregister_and_error_paths(), &failed);
	report_group("fork clone exec lifecycle", test_rseq_lifecycle(),
		     &failed);
	report_group("signal abort", test_rseq_signal_abort(), &failed);
	report_group("preempt abort", test_rseq_preempt_abort(), &failed);

	if (failed)
		printf("rseq_test: %d test group(s) FAILED\n", failed);
	else
		printf("rseq_test: all tests PASSED\n");

	return failed ? 1 : 0;
}
