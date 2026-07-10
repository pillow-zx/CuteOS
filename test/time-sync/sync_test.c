#include <kernel/atomic.h>
#include <kernel/irq.h>
#include <kernel/refcount.h>
#include <kernel/sync.h>
#include <kernel/test.h>

#include "../ktest.h"

int test_atomic_basic(void)
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
		TEST_ASSERT_EQ(atomic_cmpxchg(&value, 0, 9), 0);
		TEST_ASSERT_EQ(atomic_read(&value), 9);
		TEST_ASSERT_EQ(atomic_cmpxchg(&value, 0, 1), 9);
		TEST_ASSERT_EQ(atomic_read(&value), 9);

		refcount_t refs = REFCOUNT_INIT(1);
		TEST_ASSERT_EQ(refcount_read(&refs), 1);
		refcount_inc(&refs);
		TEST_ASSERT_EQ(refcount_read(&refs), 2);
		TEST_ASSERT(!refcount_dec_and_test(&refs));
		TEST_ASSERT(refcount_dec_and_test(&refs));
		TEST_ASSERT_EQ(refcount_read(&refs), 0);
		TEST_ASSERT(!refcount_inc_not_zero(&refs));
		TEST_ASSERT(!refcount_dec_if_positive(&refs));
		refcount_inc_allow_zero(&refs);
		TEST_ASSERT_EQ(refcount_read(&refs), 1);
		TEST_ASSERT(refcount_inc_not_zero(&refs));
		TEST_ASSERT_EQ(refcount_read(&refs), 2);
		TEST_ASSERT(!refcount_dec_if_positive(&refs));
		TEST_ASSERT(refcount_dec_if_positive(&refs));
		TEST_ASSERT_EQ(refcount_read(&refs), 0);
	}
	TEST_END("sync: atomic basic");
	return __test_ret;
fail:
	TEST_FAIL("sync: atomic basic", "see above");

	return __test_ret;
}

int test_spinlock_irqsave(void)
{
	TEST_BEGIN("sync: spinlock irqsave");
	{
		spinlock_t lock = SPINLOCK_INIT;
		irq_flags_t flags;
		bool was_disabled = irqs_disabled();

		spin_lock_irqsave(&lock, &flags);
		TEST_ASSERT(lock.locked);
		TEST_ASSERT(irqs_disabled());
		spin_unlock_irqrestore(&lock, flags);
		TEST_ASSERT(!lock.locked);
		TEST_ASSERT_EQ(irqs_disabled(), was_disabled);
	}
	TEST_END("sync: spinlock irqsave");
	return __test_ret;
fail:
	TEST_FAIL("sync: spinlock irqsave", "see above");

	return __test_ret;
}
