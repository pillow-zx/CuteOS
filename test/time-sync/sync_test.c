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
		int expected;

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

		atomic_set_relaxed(&value, 2);
		TEST_ASSERT_EQ(atomic_read_relaxed(&value), 2);
		atomic_store_order(&value, 3, ATOMIC_ORDER_RELEASE);
		TEST_ASSERT_EQ(atomic_load_order(&value, ATOMIC_ORDER_ACQUIRE), 3);
		TEST_ASSERT_EQ(atomic_exchange_order(&value, 4,
							    ATOMIC_ORDER_ACQ_REL),
				      3);

		TEST_ASSERT_EQ(atomic_fetch_add_order(&value, 2,
							 ATOMIC_ORDER_RELAXED),
				      4);
		TEST_ASSERT_EQ(atomic_add_fetch_order(&value, 1,
							 ATOMIC_ORDER_RELAXED),
				      7);
		TEST_ASSERT_EQ(atomic_fetch_sub_order(&value, 2,
							 ATOMIC_ORDER_RELAXED),
				      7);
		TEST_ASSERT_EQ(atomic_sub_fetch_order(&value, 1,
							 ATOMIC_ORDER_RELAXED),
				      4);

		atomic_set(&value, 0xf0);
		TEST_ASSERT_EQ(atomic_fetch_and_order(&value, 0x0f,
							 ATOMIC_ORDER_RELAXED),
				      0xf0);
		TEST_ASSERT_EQ(atomic_or_fetch_order(&value, 0x30,
							ATOMIC_ORDER_RELAXED),
				      0x30);
		TEST_ASSERT_EQ(atomic_fetch_xor_order(&value, 0x10,
							  ATOMIC_ORDER_RELAXED),
				      0x30);
		TEST_ASSERT_EQ(atomic_xor_fetch_order(&value, 0x20,
							 ATOMIC_ORDER_RELAXED),
				      0x00);
		atomic_set(&value, 0xff);
		TEST_ASSERT_EQ(atomic_fetch_andnot_order(&value, 0x0f,
								ATOMIC_ORDER_RELAXED),
				      0xff);
		TEST_ASSERT_EQ(atomic_read(&value), 0xf0);

		expected = 0xf0;
		TEST_ASSERT(atomic_compare_exchange_order(
			&value, &expected, 5, false, ATOMIC_ORDER_ACQUIRE,
			ATOMIC_ORDER_RELAXED));
		TEST_ASSERT_EQ(expected, 0xf0);
		expected = 0;
		TEST_ASSERT(!atomic_compare_exchange_order(
			&value, &expected, 6, false, ATOMIC_ORDER_ACQUIRE,
			ATOMIC_ORDER_RELAXED));
		TEST_ASSERT_EQ(expected, 5);

		atomic_set(&value, 0);
		expected = 0;
		TEST_ASSERT(atomic_try_cmpxchg_acquire(&value, &expected, 7));
		TEST_ASSERT_EQ(expected, 0);
		TEST_ASSERT_EQ(atomic_read(&value), 7);

		expected = 0;
		TEST_ASSERT(!atomic_try_cmpxchg_acquire(&value, &expected, 8));
		TEST_ASSERT_EQ(expected, 7);
		TEST_ASSERT_EQ(atomic_xchg_release(&value, 9), 7);
		TEST_ASSERT_EQ(atomic_read(&value), 9);

		atomic64_t wide = ATOMIC64_INIT(1);
		TEST_ASSERT_EQ(atomic64_add_return(&wide, 2), 3);
		TEST_ASSERT_EQ(atomic64_read_acquire(&wide), 3);
		TEST_ASSERT_EQ(atomic64_fetch_sub(&wide, 1), 3);
		TEST_ASSERT_EQ(atomic64_read(&wide), 2);

		atomic_isize_t word = ATOMIC_LONG_INIT(1);
		TEST_ASSERT_EQ(atomic_isize_inc_return(&word), 2);
		TEST_ASSERT_EQ(atomic_isize_xchg_release(&word, 3), 2);
		TEST_ASSERT_EQ(atomic_isize_read(&word), 3);

		atomic_thread_fence(ATOMIC_ORDER_SEQ_CST);
		atomic_signal_fence(ATOMIC_ORDER_SEQ_CST);

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
		irq_flags_t initial_flags = local_irq_save();

		spin_lock_init(&lock);
		local_irq_enable();

		spin_lock_irqsave(&lock, &flags);
		TEST_ASSERT_EQ(atomic_read(&lock.locked), 1);
		TEST_ASSERT(irqs_disabled());
		spin_unlock_irqrestore(&lock, flags);
		TEST_ASSERT_EQ(atomic_read(&lock.locked), 0);
		TEST_ASSERT(!irqs_disabled());

		local_irq_disable();
		spin_lock_irqsave(&lock, &flags);
		TEST_ASSERT(irqs_disabled());
		spin_unlock_irqrestore(&lock, flags);
		TEST_ASSERT(irqs_disabled());
		local_irq_restore(initial_flags);
	}
	TEST_END("sync: spinlock irqsave");
	return __test_ret;
fail:
	TEST_FAIL("sync: spinlock irqsave", "see above");

	return __test_ret;
}

int test_spinlock_held_tracking(void)
{
	spinlock_t outer = SPINLOCK_INIT;
	spinlock_t inner = SPINLOCK_INIT;
	irq_flags_t saved_flags = local_irq_save();
	irq_flags_t outer_flags;
	irq_flags_t inner_flags;
	bool outer_held = false;
	bool inner_held = false;

	TEST_BEGIN("sync: spinlock held-lock tracking");
	{
		spin_lock_init(&outer);
		spin_lock_init(&inner);
		TEST_ASSERT_EQ(lock_depth(), (uint32_t)0);
		TEST_ASSERT(!spinlock_held());

		spin_lock_irqsave(&outer, &outer_flags);
		outer_held = true;
		TEST_ASSERT_EQ(lock_depth(), (uint32_t)1);
		TEST_ASSERT(spinlock_held());
#ifdef CONFIG_DEBUG_CONTEXT
		TEST_ASSERT(spinlock_is_held_by_current(&outer));
		TEST_ASSERT(!spinlock_is_held_by_current(&inner));
#endif

		spin_lock_irqsave(&inner, &inner_flags);
		inner_held = true;
		TEST_ASSERT_EQ(lock_depth(), (uint32_t)2);
#ifdef CONFIG_DEBUG_CONTEXT
		TEST_ASSERT(spinlock_is_held_by_current(&outer));
		TEST_ASSERT(spinlock_is_held_by_current(&inner));
#endif

		/* The tracking set permits non-LIFO release and compacts the gap. */
		spin_unlock_irqrestore(&outer, outer_flags);
		outer_held = false;
		TEST_ASSERT_EQ(lock_depth(), (uint32_t)1);
		TEST_ASSERT(spinlock_held());
#ifdef CONFIG_DEBUG_CONTEXT
		TEST_ASSERT(!spinlock_is_held_by_current(&outer));
		TEST_ASSERT(spinlock_is_held_by_current(&inner));
#endif

		spin_unlock_irqrestore(&inner, inner_flags);
		inner_held = false;
		TEST_ASSERT_EQ(lock_depth(), (uint32_t)0);
		TEST_ASSERT(!spinlock_held());

		/* Non-LIFO release must keep IRQs off until the last lock is gone. */
		local_irq_enable();
		TEST_ASSERT(!irqs_disabled());
		spin_lock_irqsave(&outer, &outer_flags);
		outer_held = true;
		spin_lock_irqsave(&inner, &inner_flags);
		inner_held = true;
		TEST_ASSERT(irqs_disabled());

		spin_unlock_irqrestore(&outer, outer_flags);
		outer_held = false;
		TEST_ASSERT(irqs_disabled());
		TEST_ASSERT_EQ(lock_depth(), (uint32_t)1);

		spin_unlock_irqrestore(&inner, inner_flags);
		inner_held = false;
		TEST_ASSERT(!irqs_disabled());
		TEST_ASSERT_EQ(lock_depth(), (uint32_t)0);
	}
	TEST_END("sync: spinlock held-lock tracking");
	goto cleanup;
fail:
	TEST_FAIL("sync: spinlock held-lock tracking", "see above");
cleanup:
	if (inner_held)
		spin_unlock_irqrestore(&inner, inner_flags);
	if (outer_held)
		spin_unlock_irqrestore(&outer, outer_flags);
	local_irq_restore(saved_flags);

	return __test_ret;
}
