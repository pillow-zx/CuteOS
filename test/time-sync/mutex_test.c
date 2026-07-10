#include <kernel/exit.h>
#include <kernel/sched.h>
#include <kernel/sync.h>
#include <kernel/task.h>
#include <kernel/test.h>

#include "../ktest.h"

static mutex_t mutex_test_lock;
static volatile int mutex_test_stage;

static void mutex_waiter(void *arg)
{
	(void)arg;

	mutex_test_stage = 1;
	mutex_lock(&mutex_test_lock);
	mutex_test_stage = 2;
	mutex_unlock(&mutex_test_lock);
	do_exit(0);
}

int test_mutex_blocking(void)
{
	TEST_BEGIN("sync: mutex blocking wake");
	{
		struct task_struct *waiter;

		mutex_test_stage = 0;
		mutex_init(&mutex_test_lock);
		mutex_lock(&mutex_test_lock);
		TEST_ASSERT(!mutex_trylock(&mutex_test_lock));

		waiter = kernel_thread(mutex_waiter, NULL);
		TEST_ASSERT_NOT_NULL(waiter);

		schedule();
		TEST_ASSERT_EQ(mutex_test_stage, 1);
		TEST_ASSERT_EQ(waiter->lifecycle.state,
			       (uint32_t)TASK_UNINTERRUPTIBLE);

		mutex_unlock(&mutex_test_lock);
		schedule();
		TEST_ASSERT_EQ(mutex_test_stage, 2);
		TEST_ASSERT_EQ(waiter->lifecycle.state, (uint32_t)TASK_ZOMBIE);

		release_task(waiter);
	}
	TEST_END("sync: mutex blocking wake");
	return __test_ret;
fail:
	TEST_FAIL("sync: mutex blocking wake", "see above");

	return __test_ret;
}

int test_mutex_uncontended_preserves_sleep_state(void)
{
	mutex_t mutex;

	TEST_BEGIN("sync: mutex uncontended preserves sleep state");
	{
		mutex_init(&mutex);
		task_mark_interruptible_sleep(current_task());
		mutex_lock(&mutex);
		TEST_ASSERT_EQ(task_state(current_task()),
			       (uint32_t)TASK_INTERRUPTIBLE);
		mutex_unlock(&mutex);
		task_mark_running(current_task());
	}
	TEST_END("sync: mutex uncontended preserves sleep state");
	return __test_ret;
fail:
	task_mark_running(current_task());
	TEST_FAIL("sync: mutex uncontended preserves sleep state", "see above");
	return __test_ret;
}
