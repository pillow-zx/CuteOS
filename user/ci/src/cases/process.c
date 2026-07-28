#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include <utest.h>

struct process_counter {
	pthread_mutex_t lock;
	int value;
};

static void *process_increment(void *argument)
{
	struct process_counter *counter = argument;
	int index;

	for (index = 0; index < 100; index++) {
		if (pthread_mutex_lock(&counter->lock) != 0)
			return (void *)(intptr_t)-1;
		counter->value++;
		if (pthread_mutex_unlock(&counter->lock) != 0)
			return (void *)(intptr_t)-1;
	}
	return NULL;
}

static void *process_die_holding_mutex(void *argument)
{
	pthread_mutex_t *mutex = argument;

	if (pthread_mutex_lock(mutex) != 0)
		return (void *)(intptr_t)-1;
	return NULL;
}

UT_CASE(process_fork_vfork_wait_and_pid_reuse, 5000)
{
	pid_t first;
	pid_t second;
	pid_t child;

	child = UT_FORK();
	if (child == 0)
		_exit(23);
	UT_EXPECT_EXIT(child, 23);
	child = vfork();
	UT_ASSERT(child >= 0);
	if (child == 0)
		_exit(29);
	UT_EXPECT_EXIT(child, 29);
	first = UT_FORK();
	if (first == 0)
		_exit(0);
	UT_EXPECT_EXIT(first, 0);
	second = UT_FORK();
	if (second == 0)
		_exit(0);
	UT_EXPECT_EQ(second, first);
	UT_EXPECT_EXIT(second, 0);
}

UT_CASE(process_pthread_mutex_futex_and_robust_list, 5000)
{
	struct process_counter counter = {
		.lock = PTHREAD_MUTEX_INITIALIZER,
	};
	pthread_mutexattr_t attribute;
	pthread_mutex_t robust_mutex;
	pthread_t threads[2];
	void *thread_result;
	int index;
	int result;

	for (index = 0; index < 2; index++)
		UT_ASSERT_EQ(pthread_create(&threads[index], NULL, process_increment,
						&counter), 0);
	for (index = 0; index < 2; index++) {
		UT_ASSERT_EQ(pthread_join(threads[index], &thread_result), 0);
		UT_EXPECT_EQ((intptr_t)thread_result, 0);
	}
	UT_EXPECT_EQ(counter.value, 200);
	UT_ASSERT_EQ(pthread_mutexattr_init(&attribute), 0);
	UT_ASSERT_EQ(pthread_mutexattr_setrobust(&attribute, PTHREAD_MUTEX_ROBUST),
		     0);
	UT_ASSERT_EQ(pthread_mutex_init(&robust_mutex, &attribute), 0);
	UT_ASSERT_EQ(pthread_mutexattr_destroy(&attribute), 0);
	UT_ASSERT_EQ(pthread_create(&threads[0], NULL, process_die_holding_mutex,
					&robust_mutex), 0);
	UT_ASSERT_EQ(pthread_join(threads[0], &thread_result), 0);
	UT_EXPECT_EQ((intptr_t)thread_result, 0);
	result = pthread_mutex_lock(&robust_mutex);
	UT_EXPECT_EQ(result, EOWNERDEAD);
	if (result == EOWNERDEAD) {
		UT_EXPECT_EQ(pthread_mutex_consistent(&robust_mutex), 0);
		UT_EXPECT_EQ(pthread_mutex_unlock(&robust_mutex), 0);
	}
	UT_EXPECT_EQ(pthread_mutex_destroy(&robust_mutex), 0);
}

UT_CASE(process_fd_exhaustion_and_reuse, 1500)
{
	int fds[64];
	char *path = ut_path("fd-source");
	int count = 0;
	int replacement;

	UT_ASSERT(path != NULL);
	UT_ASSERT_EQ(ut_write_file("fd-source", "x", 1, 0600), 0);
	while (count < (int)(sizeof(fds) / sizeof(fds[0]))) {
		fds[count] = open(path, O_RDONLY);
		if (fds[count] < 0)
			break;
		count++;
	}
	UT_EXPECT(count > 0);
	UT_EXPECT_EQ(errno, EMFILE);
	UT_ASSERT_EQ(close(fds[0]), 0);
	replacement = open(path, O_RDONLY);
	UT_ASSERT(replacement >= 0);
	UT_EXPECT_EQ(replacement, fds[0]);
	UT_ASSERT_EQ(close(replacement), 0);
	while (--count > 0)
		UT_EXPECT_EQ(close(fds[count]), 0);
	free(path);
}
