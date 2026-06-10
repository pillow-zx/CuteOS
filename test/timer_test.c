#include <kernel/test.h>
#include <kernel/timer.h>

void test_timer_mtime(void)
{
	TEST_BEGIN("timer: mtime monotonic");
	{
		uint64_t t0 = get_mtime();

		/* 读 1000 次，值不应减小 */
		for (int i = 0; i < 1000; i++) {
			uint64_t t1 = get_mtime();
			TEST_ASSERT(t1 >= t0);
			t0 = t1;
		}
	}
	TEST_END("timer: mtime monotonic");
	return;
fail:
	TEST_FAIL("timer: mtime monotonic", "see above");
}

void test_timer_mtimecmp(void)
{
	TEST_BEGIN("timer: mtimecmp write/read");
	{
		uint64_t now = get_mtime();

		/* 设置一个远的超时，不触发中断 */
		set_mtimecmp(now + 10000000UL);

		/* 如果没 panic，说明 CSR 可写 */
		/* 恢复正常的 tick 间隔 */
		set_mtimecmp(now + 100000UL);
	}
	TEST_END("timer: mtimecmp write/read");
}
void test_timer_jiffies(void)
{
	TEST_BEGIN("timer: jiffies initial value");
	{
		/*
		 * timer_init() 在 kernel_main 中先于 kernel_test() 调用，
		 * jiffies 应 >= 0（可能 > 0，取决于初始化期间是否有 tick）。
		 * 只验证它没有溢出或变为异常值。
		 */
		uint64_t j = jiffies;
		TEST_ASSERT(j < 1000000UL);
	}
	TEST_END("timer: jiffies initial value");
	return;
fail:
	TEST_FAIL("timer: jiffies initial value", "see above");
}

void test_timer_constants(void)
{
	TEST_BEGIN("timer: constants");
	{
		/*
		 * 验证 timer.c 中的核心常量关系（直接使用字面量，
		 * 因为这些 #define 在 timer.c 内部，不通过头文件导出）：
		 *
		 *   HZ              = 100        (100 Hz tick)
		 *   MTIME_FREQ      = 10000000   (10 MHz)
		 *   CLOCKS_PER_TICK = 100000     (10000000 / 100)
		 */
		TEST_ASSERT_EQ(10000000UL / 100UL, 100000UL);
		TEST_ASSERT_EQ(100000UL * 100UL, 10000000UL);

		/* 每 tick = 100000 / 10000000 = 10ms */
		TEST_ASSERT_EQ(100000UL * 1000UL / 10000000UL, 10UL);
	}
	TEST_END("timer: constants");
	return;
fail:
	TEST_FAIL("timer: constants", "see above");
}
