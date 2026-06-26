/*
 * user/bin/pthread_test.c - pthread 接口测试
 *
 * 测试内容：
 *   1. pthread_create / pthread_join 基本往返
 *   2. 多线程通过 pthread_mutex 保护共享计数器
 *   3. pthread_mutex_lock/unlock/destroy 错误路径
 *   4. pthread_self / gettid 一致性
 */

#include <pthread.h>
#include <ulib.h>

/* ---- test 1: basic create/join ---- */

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

/* ---- test 2: mutex-protected shared counter ---- */

#define N_THREADS 4
#define N_ITERS   500

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

/* ---- test 3: mutex error paths ---- */

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

/* ---- test 4: pthread_self consistency ---- */

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

	long recorded = __atomic_load_n((long *)&child_self_tid, __ATOMIC_ACQUIRE);

	if (recorded != (long)t) {
		printf("FAIL: self tid %ld != thread handle %ld\n", recorded,
		       (long)t);
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	int failed = 0;

	printf("pthread_test: create/join ... ");
	if (test_create_join())
		failed++;
	else
		printf("PASS\n");

	printf("pthread_test: mutex counter (%d threads x %d iters) ... ",
	       N_THREADS, N_ITERS);
	if (test_mutex_counter())
		failed++;
	else
		printf("PASS\n");

	printf("pthread_test: mutex error paths ... ");
	if (test_mutex_errors())
		failed++;
	else
		printf("PASS\n");

	printf("pthread_test: pthread_self consistency ... ");
	if (test_self_consistency())
		failed++;
	else
		printf("PASS\n");

	if (failed)
		printf("pthread_test: %d test(s) FAILED\n", failed);
	else
		printf("pthread_test: all tests PASSED\n");

	return failed ? 1 : 0;
}
