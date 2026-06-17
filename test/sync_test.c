#include <kernel/atomic.h>
#include <kernel/exit.h>
#include <kernel/sched.h>
#include <kernel/sync.h>
#include <kernel/task.h>
#include <kernel/test.h>
#include <asm/csr.h>

void test_atomic_basic(void)
{
	TEST_BEGIN("sync: atomic basic");
	{
		atomic_t value = ATOMIC_INIT(1);

		TEST_ASSERT_EQ(atomic_read(&value), 1);
		atomic_set(&value, 3);
		TEST_ASSERT_EQ(atomic_read(&value), 3);
		TEST_ASSERT_EQ(atomic_inc_return(&value), 4);
		atomic_inc(&value);
		TEST_ASSERT_EQ(atomic_read(&value), 5);
		TEST_ASSERT_EQ(atomic_dec_return(&value), 4);
		atomic_add(&value, -3);
		TEST_ASSERT_EQ(atomic_read(&value), 1);
		TEST_ASSERT(atomic_dec_and_test(&value));
		TEST_ASSERT_EQ(atomic_read(&value), 0);
	}
	TEST_END("sync: atomic basic");
	return;
fail:
	TEST_FAIL("sync: atomic basic", "see above");
}

void test_spinlock_irqsave(void)
{
	TEST_BEGIN("sync: spinlock irqsave");
	{
		spinlock_t lock = SPINLOCK_INIT;
		irq_flags_t flags;
		irq_flags_t before = csr_read(sstatus);

		spin_lock_irqsave(&lock, &flags);
		TEST_ASSERT(lock.locked);
		TEST_ASSERT(irqs_disabled());
		spin_unlock_irqrestore(&lock, flags);
		TEST_ASSERT(!lock.locked);
		TEST_ASSERT_EQ(csr_read(sstatus) & SSTATUS_SIE,
			       before & SSTATUS_SIE);
	}
	TEST_END("sync: spinlock irqsave");
	return;
fail:
	TEST_FAIL("sync: spinlock irqsave", "see above");
}

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

void test_mutex_blocking(void)
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
		TEST_ASSERT_EQ(waiter->state, (uint32_t)TASK_SLEEPING);

		mutex_unlock(&mutex_test_lock);
		schedule();
		TEST_ASSERT_EQ(mutex_test_stage, 2);
		TEST_ASSERT_EQ(waiter->state, (uint32_t)TASK_ZOMBIE);

		release_task(waiter);
	}
	TEST_END("sync: mutex blocking wake");
	return;
fail:
	TEST_FAIL("sync: mutex blocking wake", "see above");
}
