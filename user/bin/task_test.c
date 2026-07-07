/*
 * user/bin/task_test.c - task, thread, clone, and futex user ABI tests
 */

#include <pthread.h>
#include <ulib.h>
#include <uapi/mman.h>
#include <uapi/sched.h>

#define THREAD_STACK_SIZE (16 * 1024UL)
#define FUTEX_STACK_SIZE  (16 * 1024UL)

static int task_pid_expect_ret(const char *name, long got, long want)
{
	if (got != want) {
		printf("FAIL: %s expected %ld got %ld\n", name, want, got);
		return 1;
	}

	return 0;
}

static int test_wait_any_child(void)
{
	int status = 0;
	long pid = fork();
	long waited;

	if (pid < 0)
		return task_pid_expect_ret("fork", pid, 0);
	if (pid == 0)
		exit(7);

	waited = wait4(-1, &status, 0, NULL);
	if (waited != pid) {
		printf("FAIL: wait4(-1) expected child %ld got %ld\n", pid,
		       waited);
		return 1;
	}
	if (status != (7 << 8)) {
		printf("FAIL: wait status expected %d got %d\n", 7 << 8,
		       status);
		return 1;
	}

	return 0;
}

static int test_pid_error_paths(void)
{
	unsigned long mask = 0;
	struct rlimit64 limit;
	int failed = 0;

	failed += task_pid_expect_ret("wait4 zero", wait4(0, NULL, 0, NULL),
				      -EINVAL);
	failed += task_pid_expect_ret("wait4 negative",
				      wait4(-2, NULL, 0, NULL), -EINVAL);
	failed += task_pid_expect_ret(
		"sched_getaffinity negative",
		sched_getaffinity(-1, sizeof(mask), &mask), -ESRCH);
	failed += task_pid_expect_ret(
		"prlimit64 negative",
		prlimit64(-1, RLIMIT_NOFILE, NULL, &limit), -ESRCH);

	return failed;
}

static int test_getpgid_self(void)
{
	long pid = getpid();
	long pgid = getpgid(0);
	long missing = getpgid(9999);
	int failed = 0;

	if (pid < 0)
		return task_pid_expect_ret("getpid", pid, 0);

	if (pgid <= 0) {
		printf("FAIL: getpgid self expected positive got %ld\n", pgid);
		failed++;
	}
	failed += task_pid_expect_ret("getpgid by pid", getpgid(pid), pgid);
	failed += task_pid_expect_ret("getpgid missing", missing, -ESRCH);

	return failed;
}

static int test_getpgid_fork_inherits(void)
{
	long parent_pgid = getpgid(0);
	int status = 0;
	long pid;
	long waited;
	int failed = 0;

	if (parent_pgid < 0)
		return task_pid_expect_ret("parent getpgid", parent_pgid, 0);

	pid = fork();
	if (pid < 0)
		return task_pid_expect_ret("fork pgid inherit", pid, 0);
	if (pid == 0)
		exit(getpgid(0) == parent_pgid ? 0 : 5);

	failed += task_pid_expect_ret("child getpgid",
				      getpgid(pid), parent_pgid);
	waited = wait4(pid, &status, 0, NULL);
	if (waited != pid) {
		printf("FAIL: wait4 pgid inherit expected %ld got %ld\n", pid,
		       waited);
		return 1;
	}
	if (status != 0) {
		printf("FAIL: child pgid inherit status got %d\n", status);
		failed++;
	}

	return failed;
}

static int test_setpgid_child_leader(void)
{
	int status = 0;
	long pid;
	long waited;
	int failed = 0;

	pid = fork();
	if (pid < 0)
		return task_pid_expect_ret("fork setpgid child", pid, 0);
	if (pid == 0) {
		for (int i = 0; i < 16; i++)
			yield();
		exit(0);
	}

	failed += task_pid_expect_ret("setpgid child", setpgid(pid, pid), 0);
	failed += task_pid_expect_ret("child pgid after setpgid",
				      getpgid(pid), pid);

	waited = wait4(pid, &status, 0, NULL);
	if (waited != pid) {
		printf("FAIL: wait4 setpgid child expected %ld got %ld\n", pid,
		       waited);
		return 1;
	}
	if (status != 0) {
		printf("FAIL: setpgid child status got %d\n", status);
		failed++;
	}

	return failed;
}

static int test_setpgid_edges(void)
{
	int status = 0;
	long self = getpid();
	long child1;
	long child2;
	long waited;
	int failed = 0;

	failed += task_pid_expect_ret("setpgid self zero",
				      setpgid(0, 0), 0);
	failed += task_pid_expect_ret("self pgid after setpgid",
				      getpgid(0), self);
	failed += task_pid_expect_ret("setpgid negative pid",
				      setpgid(-1, 0), -EINVAL);
	failed += task_pid_expect_ret("setpgid negative pgid",
				      setpgid(0, -1), -EINVAL);
	failed += task_pid_expect_ret("setpgid missing target",
				      setpgid(9999, 9999), -ESRCH);
	failed += task_pid_expect_ret("setpgid missing group",
				      setpgid(0, 9999), -EPERM);

	child1 = fork();
	if (child1 < 0)
		return task_pid_expect_ret("fork setpgid group leader",
					   child1, 0);
	if (child1 == 0) {
		for (int i = 0; i < 16; i++)
			yield();
		exit(0);
	}

	failed += task_pid_expect_ret("setpgid group leader",
				      setpgid(child1, child1), 0);

	child2 = fork();
	if (child2 < 0)
		return task_pid_expect_ret("fork setpgid join group", child2,
					   0);
	if (child2 == 0)
		exit(setpgid(getppid(), getppid()) == -EPERM ? 0 : 6);

	failed += task_pid_expect_ret("setpgid join existing",
				      setpgid(child2, child1), 0);
	failed += task_pid_expect_ret("child1 pgid", getpgid(child1),
				      child1);
	failed += task_pid_expect_ret("child2 pgid", getpgid(child2),
				      child1);

	waited = wait4(child1, &status, 0, NULL);
	if (waited != child1) {
		printf("FAIL: wait4 child1 expected %ld got %ld\n", child1,
		       waited);
		return 1;
	}
	if (status != 0) {
		printf("FAIL: child1 status got %d\n", status);
		failed++;
	}

	waited = wait4(child2, &status, 0, NULL);
	if (waited != child2) {
		printf("FAIL: wait4 child2 expected %ld got %ld\n", child2,
		       waited);
		return 1;
	}
	if (status != 0) {
		printf("FAIL: child2 status got %d\n", status);
		failed++;
	}

	return failed;
}

static int rusage_unsupported_zero(const struct rusage *usage)
{
	return usage->ru_maxrss == 0 && usage->ru_ixrss == 0 &&
	       usage->ru_idrss == 0 && usage->ru_isrss == 0 &&
	       usage->ru_minflt == 0 && usage->ru_majflt == 0 &&
	       usage->ru_nswap == 0 && usage->ru_inblock == 0 &&
	       usage->ru_oublock == 0 && usage->ru_msgsnd == 0 &&
	       usage->ru_msgrcv == 0 && usage->ru_nsignals == 0 &&
	       usage->ru_nvcsw == 0 && usage->ru_nivcsw == 0;
}

static long rusage_cpu_usec(const struct rusage *usage)
{
	return usage->ru_utime.tv_sec * 1000000L + usage->ru_utime.tv_usec +
	       usage->ru_stime.tv_sec * 1000000L + usage->ru_stime.tv_usec;
}

static long tms_cpu_ticks(const struct tms *tm)
{
	return tm->tms_utime + tm->tms_stime;
}

static void child_burn_cpu(void)
{
	struct tms start;
	struct tms now;
	volatile long sink = 0;
	long base;

	if (times(&start) < 0)
		exit(2);
	base = tms_cpu_ticks(&start);

	for (long i = 0; i < 20000000L; i++) {
		sink += i;
		if ((i & 0xffffL) != 0)
			continue;
		if (times(&now) < 0)
			exit(2);
		if (tms_cpu_ticks(&now) > base)
			exit(0);
	}

	(void)sink;
	exit(3);
}

static int test_getrusage_self(void)
{
	struct rusage usage;
	int failed = 0;

	memset(&usage, 0xff, sizeof(usage));
	failed += task_pid_expect_ret("getrusage self",
				      getrusage(RUSAGE_SELF, &usage), 0);

	if (usage.ru_utime.tv_sec < 0 || usage.ru_utime.tv_usec < 0 ||
	    usage.ru_utime.tv_usec >= 1000000L) {
		printf("FAIL: invalid user time %ld usec %ld\n",
		       usage.ru_utime.tv_sec, usage.ru_utime.tv_usec);
		failed++;
	}
	if (usage.ru_stime.tv_sec < 0 || usage.ru_stime.tv_usec < 0 ||
	    usage.ru_stime.tv_usec >= 1000000L) {
		printf("FAIL: invalid system time %ld usec %ld\n",
		       usage.ru_stime.tv_sec, usage.ru_stime.tv_usec);
		failed++;
	}
	if (!rusage_unsupported_zero(&usage)) {
		printf("FAIL: unsupported rusage fields not zero\n");
		failed++;
	}

	failed += task_pid_expect_ret("getrusage null",
				      getrusage(RUSAGE_SELF, NULL), -EFAULT);
	failed += task_pid_expect_ret("getrusage thread",
				      getrusage(RUSAGE_THREAD, &usage),
				      -EINVAL);
	failed += task_pid_expect_ret("getrusage both",
				      getrusage(RUSAGE_BOTH, &usage),
				      -EINVAL);

	return failed;
}

static int test_getrusage_children(void)
{
	struct rusage before;
	struct rusage child;
	struct rusage after;
	long before_cpu;
	long child_cpu;
	long after_cpu;
	long waited;
	long pid;
	int status = 0;
	int failed = 0;

	memset(&before, 0xff, sizeof(before));
	failed += task_pid_expect_ret("getrusage children before",
				      getrusage(RUSAGE_CHILDREN, &before), 0);
	if (failed)
		return failed;
	before_cpu = rusage_cpu_usec(&before);

	pid = fork();
	if (pid < 0)
		return task_pid_expect_ret("fork child usage", pid, 0);
	if (pid == 0)
		child_burn_cpu();

	memset(&child, 0xff, sizeof(child));
	waited = wait4(pid, &status, 0, &child);
	if (waited != pid) {
		printf("FAIL: wait4 rusage expected child %ld got %ld\n", pid,
		       waited);
		return 1;
	}
	if (status != 0) {
		printf("FAIL: child usage status expected 0 got %d\n", status);
		return 1;
	}

	child_cpu = rusage_cpu_usec(&child);
	if (child_cpu <= 0) {
		printf("FAIL: child cpu usage expected >0 got %ld\n",
		       child_cpu);
		failed++;
	}
	if (!rusage_unsupported_zero(&child)) {
		printf("FAIL: child rusage unsupported fields not zero\n");
		failed++;
	}

	memset(&after, 0xff, sizeof(after));
	failed += task_pid_expect_ret("getrusage children after",
				      getrusage(RUSAGE_CHILDREN, &after), 0);
	if (failed)
		return failed;

	after_cpu = rusage_cpu_usec(&after);
	if (after_cpu - before_cpu < child_cpu) {
		printf("FAIL: children usage delta %ld smaller than child %ld\n",
		       after_cpu - before_cpu, child_cpu);
		failed++;
	}
	if (!rusage_unsupported_zero(&after)) {
		printf("FAIL: children rusage unsupported fields not zero\n");
		failed++;
	}

	return failed;
}

static int test_wait4_bad_rusage_preserves_child(void)
{
	int status = 0;
	long waited;
	long pid = fork();

	if (pid < 0)
		return task_pid_expect_ret("fork bad wait4 rusage", pid, 0);
	if (pid == 0)
		exit(9);

	waited = wait4(pid, NULL, 0, (void *)~0UL);
	if (waited != -EFAULT) {
		printf("FAIL: wait4 bad rusage expected -EFAULT got %ld\n",
		       waited);
		return 1;
	}

	waited = wait4(pid, &status, 0, NULL);
	if (waited != pid) {
		printf("FAIL: wait4 after bad rusage expected %ld got %ld\n",
		       pid, waited);
		return 1;
	}
	if (status != (9 << 8)) {
		printf("FAIL: wait4 after bad rusage status got %d\n", status);
		return 1;
	}

	return 0;
}

static volatile int ready_flag;
static volatile int tid_word = -1;
static volatile int result;

static int thread_fn(void *arg)
{
	(void)arg;
	result = 42;

	__atomic_store_n((int *)&ready_flag, 1, __ATOMIC_RELEASE);
	futex((int *)&ready_flag, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, 1, NULL, 0,
	      0);
	return 0;
}

static int test_basic_thread_wake(void)
{
	void *stack;
	long child;
	unsigned long flags;

	ready_flag = 0;
	result = 0;
	tid_word = -1;

	stack = mmap(NULL, THREAD_STACK_SIZE, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)stack < 0) {
		printf("FAIL: mmap stack: %ld\n", (long)stack);
		return 1;
	}

	flags = CLONE_VM | CLONE_SIGHAND | CLONE_THREAD | CLONE_FILES |
		CLONE_FS | CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID;

	child = clone_thread(flags, (char *)stack + THREAD_STACK_SIZE, NULL, 0,
			     (int *)&tid_word, thread_fn, NULL);
	if (child < 0) {
		printf("FAIL: clone_thread: %ld\n", child);
		munmap(stack, THREAD_STACK_SIZE);
		return 1;
	}


	while (__atomic_load_n((int *)&ready_flag, __ATOMIC_ACQUIRE) == 0)
		futex((int *)&ready_flag, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, 0,
		      NULL, 0, 0);

	if (result != 42) {
		printf("FAIL: result expected 42, got %d\n", result);
		munmap(stack, THREAD_STACK_SIZE);
		return 1;
	}


	int tid;

	while ((tid = __atomic_load_n((int *)&tid_word, __ATOMIC_ACQUIRE)) != 0)
		futex((int *)&tid_word, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, tid,
		      NULL, 0, 0);

	munmap(stack, THREAD_STACK_SIZE);
	return 0;
}

static volatile int counter;
static volatile int counter_done;
static volatile int counter_tid = -1;

static int thread_counter(void *arg)
{
	int n = (int)(long)arg;

	for (int i = 0; i < n; i++)
		__atomic_fetch_add((int *)&counter, 1, __ATOMIC_SEQ_CST);

	__atomic_store_n((int *)&counter_done, 1, __ATOMIC_RELEASE);
	futex((int *)&counter_done, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, 1, NULL, 0,
	      0);
	return 0;
}

static int test_shared_counter(void)
{
	void *stack;
	long child;
	unsigned long flags;
	const int N = 1000;

	counter = 0;
	counter_done = 0;
	counter_tid = -1;

	stack = mmap(NULL, THREAD_STACK_SIZE, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)stack < 0)
		return 1;

	flags = CLONE_VM | CLONE_SIGHAND | CLONE_THREAD | CLONE_FILES |
		CLONE_FS | CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID;

	child = clone_thread(flags, (char *)stack + THREAD_STACK_SIZE, NULL, 0,
			     (int *)&counter_tid, thread_counter,
			     (void *)(long)N);
	if (child < 0) {
		munmap(stack, THREAD_STACK_SIZE);
		return 1;
	}


	for (int i = 0; i < N; i++)
		__atomic_fetch_add((int *)&counter, 1, __ATOMIC_SEQ_CST);

	while (__atomic_load_n((int *)&counter_done, __ATOMIC_ACQUIRE) == 0)
		futex((int *)&counter_done, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, 0,
		      NULL, 0, 0);

	int tid;

	while ((tid = __atomic_load_n((int *)&counter_tid, __ATOMIC_ACQUIRE)) !=
	       0)
		futex((int *)&counter_tid, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, tid,
		      NULL, 0, 0);

	munmap(stack, THREAD_STACK_SIZE);

	if (counter != 2 * N) {
		printf("FAIL: counter expected %d, got %d\n", 2 * N, counter);
		return 1;
	}
	return 0;
}

static void *thread_return_value(void *arg)
{
	return (void *)((long)arg + 1);
}

static int test_create_join(void)
{
	pthread_t t;
	void *retval = NULL;
	int rc;

	rc = pthread_create(&t, NULL, thread_return_value, (void *)41L);
	if (rc != 0) {
		printf("FAIL: pthread_create: %d\n", rc);
		return 1;
	}

	rc = pthread_join(t, &retval);
	if (rc != 0) {
		printf("FAIL: pthread_join: %d\n", rc);
		return 1;
	}

	if ((long)retval != 42L) {
		printf("FAIL: retval expected 42, got %ld\n", (long)retval);
		return 1;
	}
	return 0;
}

#define N_THREADS 4
#define N_ITERS	  500

static pthread_mutex_t cnt_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int shared_cnt;

static void *thread_increment(void *arg)
{
	int n = (int)(long)arg;

	for (int i = 0; i < n; i++) {
		pthread_mutex_lock(&cnt_mutex);
		shared_cnt++;
		pthread_mutex_unlock(&cnt_mutex);
	}
	return NULL;
}

static int test_mutex_counter(void)
{
	pthread_t threads[N_THREADS];
	int rc;

	shared_cnt = 0;

	for (int i = 0; i < N_THREADS; i++) {
		rc = pthread_create(&threads[i], NULL, thread_increment,
				    (void *)(long)N_ITERS);
		if (rc != 0) {
			printf("FAIL: pthread_create[%d]: %d\n", i, rc);
			return 1;
		}
	}

	for (int i = 0; i < N_THREADS; i++) {
		rc = pthread_join(threads[i], NULL);
		if (rc != 0) {
			printf("FAIL: pthread_join[%d]: %d\n", i, rc);
			return 1;
		}
	}

	if (shared_cnt != N_THREADS * N_ITERS) {
		printf("FAIL: counter expected %d, got %d\n",
		       N_THREADS * N_ITERS, shared_cnt);
		return 1;
	}
	return 0;
}

static int test_mutex_errors(void)
{
	pthread_mutex_t m;

	if (pthread_mutex_init(&m, NULL) != 0)
		return 1;
	if (pthread_mutex_lock(&m) != 0)
		return 1;
	if (pthread_mutex_destroy(&m) != EBUSY) {
		printf("FAIL: expected EBUSY on destroy of locked mutex\n");
		return 1;
	}
	if (pthread_mutex_unlock(&m) != 0)
		return 1;
	if (pthread_mutex_destroy(&m) != 0)
		return 1;
	return 0;
}

static volatile long child_self_tid;

static void *thread_record_self(void *arg)
{
	(void)arg;
	__atomic_store_n((long *)&child_self_tid, (long)pthread_self(),
			 __ATOMIC_RELEASE);
	return NULL;
}

static int test_self_consistency(void)
{
	pthread_t t;
	int rc;

	child_self_tid = 0;

	rc = pthread_create(&t, NULL, thread_record_self, NULL);
	if (rc != 0)
		return 1;

	rc = pthread_join(t, NULL);
	if (rc != 0)
		return 1;

	long recorded =
		__atomic_load_n((long *)&child_self_tid, __ATOMIC_ACQUIRE);

	if (recorded != (long)t) {
		printf("FAIL: self tid %ld != thread handle %ld\n", recorded,
		       (long)t);
		return 1;
	}
	return 0;
}

static int futex_clone_expect_eq(const char *name, long got, long want)
{
	if (got == want)
		return 0;

	printf("FAIL: %s: got %ld, want %ld\n", name, got, want);
	return 1;
}

static int test_futex_error_paths(void)
{
	int failed = 0;
	int word = 1;
	struct timespec timeout;

	failed += futex_clone_expect_eq(
		"futex wait mismatch",
		futex(&word, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, 0, NULL, NULL, 0),
		-EAGAIN);

	word = 0;
	failed += futex_clone_expect_eq("futex bad timeout pointer",
					futex(&word,
					      FUTEX_WAIT | FUTEX_PRIVATE_FLAG,
					      0, (void *)~0UL, NULL, 0),
					-EFAULT);

	timeout.tv_sec = 0;
	timeout.tv_nsec = 1000000000L;
	failed += futex_clone_expect_eq("futex invalid timeout nsec",
					futex(&word,
					      FUTEX_WAIT | FUTEX_PRIVATE_FLAG,
					      0, &timeout, NULL, 0),
					-EINVAL);

	timeout.tv_sec = 0;
	timeout.tv_nsec = 0;
	failed += futex_clone_expect_eq("futex zero timeout",
					futex(&word,
					      FUTEX_WAIT | FUTEX_PRIVATE_FLAG,
					      0, &timeout, NULL, 0),
					-ETIMEDOUT);

	return failed;
}

static int test_robust_list_roundtrip(void)
{
	struct robust_list_head head;
	struct robust_list_head *got = NULL;
	long len = 0;
	int failed = 0;

	memset(&head, 0, sizeof(head));
	head.list.next = &head.list;

	failed += futex_clone_expect_eq(
		"set_robust_list", set_robust_list(&head, sizeof(head)), 0);
	failed += futex_clone_expect_eq("get_robust_list",
					get_robust_list(0, &got, &len), 0);

	if (got != &head) {
		printf("FAIL: robust head mismatch\n");
		failed++;
	}
	if (len != (long)sizeof(head)) {
		printf("FAIL: robust len got %ld, want %ld\n", len,
		       (long)sizeof(head));
		failed++;
	}

	return failed;
}

static int test_clone_bad_parent_tid(void)
{
	void *stack;
	long ret;
	unsigned long flags;

	stack = mmap(NULL, FUTEX_STACK_SIZE, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)stack < 0) {
		printf("FAIL: mmap stack: %ld\n", (long)stack);
		return 1;
	}

	flags = CLONE_VM | CLONE_SIGHAND | CLONE_THREAD | CLONE_FILES |
		CLONE_FS | CLONE_PARENT_SETTID;
	ret = clone(flags, (char *)stack + FUTEX_STACK_SIZE, (int *)~0UL, 0,
		    NULL);
	munmap(stack, FUTEX_STACK_SIZE);

	return futex_clone_expect_eq("clone bad parent_tid", ret, -EFAULT);
}

static void report_group(const char *name, int ret, int *failed)
{
	printf("task_test: %s ... ", name);
	if (ret)
		(*failed)++;
	else
		printf("PASS\n");
}

int main(void)
{
	int failed = 0;

	report_group("wait any child", test_wait_any_child(), &failed);
	report_group("pid error paths", test_pid_error_paths(), &failed);
	report_group("getpgid self", test_getpgid_self(), &failed);
	report_group("getpgid fork inherits", test_getpgid_fork_inherits(),
		     &failed);
	report_group("setpgid child leader", test_setpgid_child_leader(),
		     &failed);
	report_group("setpgid edges", test_setpgid_edges(), &failed);
	report_group("getrusage self", test_getrusage_self(), &failed);
	report_group("getrusage children", test_getrusage_children(), &failed);
	report_group("wait4 bad rusage preserves child",
		     test_wait4_bad_rusage_preserves_child(), &failed);
	report_group("clone thread basic wake", test_basic_thread_wake(),
		     &failed);
	report_group("clone thread shared counter", test_shared_counter(),
		     &failed);
	report_group("pthread create/join", test_create_join(), &failed);
	report_group("pthread mutex counter", test_mutex_counter(), &failed);
	report_group("pthread mutex error paths", test_mutex_errors(), &failed);
	report_group("pthread self consistency", test_self_consistency(),
		     &failed);
	report_group("futex error paths", test_futex_error_paths(), &failed);
	report_group("robust list roundtrip", test_robust_list_roundtrip(),
		     &failed);
	report_group("clone bad parent_tid", test_clone_bad_parent_tid(),
		     &failed);

	if (failed)
		printf("task_test: %d test group(s) FAILED\n", failed);
	else
		printf("task_test: all tests PASSED\n");

	return failed ? 1 : 0;
}
