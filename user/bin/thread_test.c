/*
 * user/bin/thread_test.c - clone/futex 线程原语测试
 *
 * 测试内容：
 *   1. clone_thread 创建线程，子线程写入共享内存后 futex_wake 唤醒父线程
 *   2. 父线程通过 futex_wait 阻塞等待
 *   3. 子线程退出时 CHILD_CLEARTID 自动清零 tid_word 并唤醒等待者
 *   4. 验证 futex_wait/wake 跨线程同步语义
 */

#include <ulib.h>
#include <uapi/mman.h>
#include <uapi/sched.h>

#define STACK_SIZE (16 * 1024UL)

/* Shared state between parent and child thread. */
static volatile int ready_flag;
static volatile int tid_word = -1;
static volatile int result;

static int thread_fn(void *arg)
{
	(void)arg;
	result = 42;
	/* Signal parent that we're done. */
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

	stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)stack < 0) {
		printf("FAIL: mmap stack: %ld\n", (long)stack);
		return 1;
	}

	flags = CLONE_VM | CLONE_SIGHAND | CLONE_THREAD | CLONE_FILES |
		CLONE_FS | CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID;

	child = clone_thread(flags,
			     (char *)stack + STACK_SIZE,
			     NULL, 0, (int *)&tid_word,
			     thread_fn, NULL);
	if (child < 0) {
		printf("FAIL: clone_thread: %ld\n", child);
		munmap(stack, STACK_SIZE);
		return 1;
	}

	/* Wait for the child to signal readiness. */
	while (__atomic_load_n((int *)&ready_flag, __ATOMIC_ACQUIRE) == 0)
		futex((int *)&ready_flag, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, 0,
		      NULL, 0, 0);

	if (result != 42) {
		printf("FAIL: result expected 42, got %d\n", result);
		munmap(stack, STACK_SIZE);
		return 1;
	}

	/* Wait for thread to fully exit (tid_word cleared by CHILD_CLEARTID). */
	int tid;

	while ((tid = __atomic_load_n((int *)&tid_word, __ATOMIC_ACQUIRE)) != 0)
		futex((int *)&tid_word, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, tid,
		      NULL, 0, 0);

	munmap(stack, STACK_SIZE);
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
	futex((int *)&counter_done, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, 1, NULL,
	      0, 0);
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

	stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)stack < 0)
		return 1;

	flags = CLONE_VM | CLONE_SIGHAND | CLONE_THREAD | CLONE_FILES |
		CLONE_FS | CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID;

	child = clone_thread(flags, (char *)stack + STACK_SIZE,
			     NULL, 0, (int *)&counter_tid,
			     thread_counter, (void *)(long)N);
	if (child < 0) {
		munmap(stack, STACK_SIZE);
		return 1;
	}

	/* Parent also increments. */
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

	munmap(stack, STACK_SIZE);

	if (counter != 2 * N) {
		printf("FAIL: counter expected %d, got %d\n", 2 * N, counter);
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	int failed = 0;

	printf("thread_test: basic wake ... ");
	if (test_basic_thread_wake()) {
		failed++;
	} else {
		printf("PASS\n");
	}

	printf("thread_test: shared counter ... ");
	if (test_shared_counter()) {
		failed++;
	} else {
		printf("PASS\n");
	}

	if (failed)
		printf("thread_test: %d test(s) FAILED\n", failed);
	else
		printf("thread_test: all tests PASSED\n");

	return failed ? 1 : 0;
}
