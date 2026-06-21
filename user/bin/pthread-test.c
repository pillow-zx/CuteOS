#include <pthread.h>
#include <ulib.h>

#define WORKERS 3
#define LOOPS	 64

struct worker_args {
	pthread_mutex_t *lock;
	volatile int *counter;
	int loops;
	pthread_t self;
};

struct detached_args {
	volatile int state;
};

struct detached_wait_args {
	volatile int state;
	volatile int release;
};

struct main_exit_args {
	int write_fd;
};

static int wait_for_state(volatile int *addr, int value)
{
	for (int i = 0; i < 100000; i++) {
		if (*addr == value)
			return 0;
		yield();
	}
	return -1;
}

static int wait_for_detached_reaped(pthread_t thread)
{
	for (int i = 0; i < 100000; i++) {
		int ret = pthread_detach(thread);

		if (ret == ESRCH)
			return 0;
		if (ret != EINVAL)
			return -1;
		yield();
	}
	return -1;
}

static void *counter_worker(void *arg)
{
	struct worker_args *args = arg;

	args->self = pthread_self();
	for (int i = 0; i < args->loops; i++) {
		if (pthread_mutex_lock(args->lock) != 0)
			return NULL;
		(*args->counter)++;
		if (pthread_mutex_unlock(args->lock) != 0)
			return NULL;
	}

	return arg;
}

static void *exit_worker(void *arg)
{
	pthread_exit(arg);
}

static void *detached_worker(void *arg)
{
	struct detached_args *args = arg;

	args->state = 1;
	return NULL;
}

static void *detached_wait_worker(void *arg)
{
	struct detached_wait_args *args = arg;

	args->state = 1;
	while (!args->release)
		yield();
	args->state = 2;
	return NULL;
}

static void *main_exit_worker(void *arg)
{
	struct main_exit_args *args = arg;
	char ch = 'x';

	(void)write(args->write_fd, &ch, 1);
	return NULL;
}

static long test_pipe2(int fds[2])
{
	return syscall(SYS_pipe2, (long)fds, 0);
}

static void run_main_exit_child(int write_fd)
{
	struct main_exit_args args = { write_fd };
	pthread_t thread;

	if (pthread_create(&thread, NULL, main_exit_worker, &args) != 0)
		exit(2);
	pthread_exit(NULL);
	exit(3);
}

static int test_main_thread_pthread_exit(void)
{
	int fds[2];
	long child;
	char ch = 0;
	long nread;
	int status = -1;
	long waited;

	if (test_pipe2(fds) < 0) {
		printf("pthread-test: pipe2 failed\n");
		return -1;
	}

	child = fork();
	if (child < 0) {
		printf("pthread-test: fork failed: %ld\n", child);
		close(fds[0]);
		close(fds[1]);
		return -1;
	}
	if (child == 0) {
		close(fds[0]);
		run_main_exit_child(fds[1]);
	}

	close(fds[1]);
	nread = read(fds[0], &ch, 1);
	close(fds[0]);
	waited = wait4(child, &status, 0, NULL);
	if (nread != 1 || ch != 'x') {
		printf("pthread-test: main pthread_exit did not wait worker\n");
		return -1;
	}
	if (waited != child || status != 0) {
		printf("pthread-test: main pthread_exit child status=%d wait=%ld\n",
		       status, waited);
		return -1;
	}
	return 0;
}

int main(void)
{
	pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
	pthread_t threads[WORKERS];
	struct worker_args args[WORKERS];
	volatile int counter = 0;
	void *retval = NULL;
	long parent_tid = gettid();

	if (pthread_mutex_init(&lock, NULL) != 0) {
		printf("pthread-test: mutex init failed\n");
		return 1;
	}
	if (pthread_mutex_destroy(&lock) != 0) {
		printf("pthread-test: mutex destroy unlocked failed\n");
		return 1;
	}
	if (pthread_mutex_init(&lock, NULL) != 0) {
		printf("pthread-test: mutex reinit failed\n");
		return 1;
	}
	if (pthread_mutex_lock(&lock) != 0) {
		printf("pthread-test: mutex lock failed\n");
		return 1;
	}
	if (pthread_mutex_destroy(&lock) != EBUSY) {
		printf("pthread-test: destroying locked mutex did not EBUSY\n");
		return 1;
	}
	if (pthread_mutex_unlock(&lock) != 0) {
		printf("pthread-test: mutex unlock failed\n");
		return 1;
	}

	for (int i = 0; i < WORKERS; i++) {
		args[i].lock = &lock;
		args[i].counter = &counter;
		args[i].loops = LOOPS;
		args[i].self = 0;
		int ret = pthread_create(&threads[i], NULL, counter_worker,
					 &args[i]);
		if (ret != 0) {
			printf("pthread-test: create worker %d failed: %d\n", i,
			       ret);
			return 1;
		}
	}

	for (int i = 0; i < WORKERS; i++) {
		retval = NULL;
		if (pthread_join(threads[i], &retval) != 0) {
			printf("pthread-test: join worker %d failed\n", i);
			return 1;
		}
		if (retval != &args[i]) {
			printf("pthread-test: worker %d bad retval\n", i);
			return 1;
		}
		if (args[i].self == 0 || args[i].self == (pthread_t)parent_tid) {
			printf("pthread-test: worker %d bad self=%lu\n", i,
			       args[i].self);
			return 1;
		}
	}

	if (counter != WORKERS * LOOPS) {
		printf("pthread-test: counter=%d expected=%d\n", counter,
		       WORKERS * LOOPS);
		return 1;
	}

	pthread_t exit_thread;
	void *exit_value = (void *)0x12345678UL;
	if (pthread_create(&exit_thread, NULL, exit_worker, exit_value) != 0) {
		printf("pthread-test: create exit worker failed\n");
		return 1;
	}
	retval = NULL;
	if (pthread_join(exit_thread, &retval) != 0 || retval != exit_value) {
		printf("pthread-test: pthread_exit retval failed\n");
		return 1;
	}
	if (pthread_join(exit_thread, NULL) != ESRCH) {
		printf("pthread-test: double join did not ESRCH\n");
		return 1;
	}
	if (pthread_join((pthread_t)0, NULL) != ESRCH) {
		printf("pthread-test: join zero tid did not ESRCH\n");
		return 1;
	}
	if (pthread_detach((pthread_t)0) != ESRCH) {
		printf("pthread-test: detach zero tid did not ESRCH\n");
		return 1;
	}

	struct detached_wait_args before = { 0, 0 };
	pthread_t detached_before;
	if (pthread_create(&detached_before, NULL, detached_wait_worker,
			   &before) != 0) {
		printf("pthread-test: create detach-before worker failed\n");
		return 1;
	}
	if (wait_for_state(&before.state, 1) < 0) {
		printf("pthread-test: detach-before worker did not start\n");
		return 1;
	}
	if (pthread_detach(detached_before) != 0) {
		printf("pthread-test: detach-before failed\n");
		return 1;
	}
	if (pthread_detach(detached_before) != EINVAL) {
		printf("pthread-test: double detach did not EINVAL\n");
		return 1;
	}
	if (pthread_join(detached_before, NULL) != EINVAL) {
		printf("pthread-test: join detached did not EINVAL\n");
		return 1;
	}
	before.release = 1;
	if (wait_for_state(&before.state, 2) < 0 ||
	    wait_for_detached_reaped(detached_before) < 0) {
		printf("pthread-test: detach-before was not reaped\n");
		return 1;
	}

	struct detached_args after = { 0 };
	pthread_t detached_after;
	if (pthread_create(&detached_after, NULL, detached_worker, &after) != 0) {
		printf("pthread-test: create detach-after worker failed\n");
		return 1;
	}
	if (wait_for_state(&after.state, 1) < 0) {
		printf("pthread-test: detach-after worker did not run\n");
		return 1;
	}
	for (int i = 0; i < 16; i++)
		yield();
	if (pthread_detach(detached_after) != 0 ||
	    wait_for_detached_reaped(detached_after) < 0) {
		printf("pthread-test: detach-after was not reaped\n");
		return 1;
	}
	if (pthread_join(detached_after, NULL) != ESRCH) {
		printf("pthread-test: join reaped detached did not ESRCH\n");
		return 1;
	}

	if (test_main_thread_pthread_exit() < 0)
		return 1;

	if (pthread_mutex_destroy(&lock) != 0) {
		printf("pthread-test: mutex final destroy failed\n");
		return 1;
	}

	printf("pthread-test: ok\n");
	return 0;
}
