#include <kernel/test.h>
#include <kernel/trap.h>

/* ================================================================
 *  Trap 测试
 * ================================================================ */

void test_trap_frame_layout(void)
{
	TEST_BEGIN("trap: frame layout");
	{
		/* 结构体应有 35 个 size_t 字段 */
		TEST_ASSERT_EQ(trap_frame_size(),
			       (size_t)(35 * sizeof(size_t)));
	}
	TEST_END("trap: frame layout");
	return;
fail:
	TEST_FAIL("trap: frame layout", "see above");
}

void test_trap_from_user(void)
{
	TEST_BEGIN("trap: arch_from_user helper");
	{
		struct trap_frame tf;

		memset(&tf, 0, sizeof(tf));

		/* SPP=0 (用户模式) → arch_from_user 返回 true */
		trap_setup_user_return(&tf, 0, 0);
		TEST_ASSERT(trap_frame_from_user(&tf) == true);

		/* SPP=1 (内核模式) → arch_from_user 返回 false */
		trap_set_kernel_return(&tf, 0);
		TEST_ASSERT(trap_frame_from_user(&tf) == false);

		/* SPP=0 但其他位有值 → arch_from_user 仍返回 true */
		trap_setup_user_return(&tf, 0, 0);
		TEST_ASSERT(trap_frame_from_user(&tf) == true);
	}
	TEST_END("trap: arch_from_user helper");
	return;
fail:
	TEST_FAIL("trap: arch_from_user helper", "see above");
}

void test_trap_context_layout(void)
{
	TEST_BEGIN("trap: context layout");
	{
		/* 14 个 callee-saved 寄存器 */
		TEST_ASSERT_EQ(trap_context_size(),
			       (size_t)(14 * sizeof(size_t)));
	}
	TEST_END("trap: context layout");
	return;
fail:
	TEST_FAIL("trap: context layout", "see above");
}

void test_trap_irq_codes(void)
{
	TEST_BEGIN("trap: IRQ/exception codes");
	{
		/* 中断码 */
		TEST_ASSERT_EQ(IRQ_S_SOFT, 1UL);
		TEST_ASSERT_EQ(IRQ_S_TIMER, 5UL);
		TEST_ASSERT_EQ(IRQ_S_EXT, 9UL);

		/* 异常码 */
		TEST_ASSERT_EQ(EXC_INST_ILLEGAL, 2UL);
		TEST_ASSERT_EQ(EXC_ECALL_U, 8UL);
		TEST_ASSERT_EQ(EXC_ECALL_S, 9UL);
		TEST_ASSERT_EQ(EXC_LOAD_PAGE_FAULT, 13UL);
		TEST_ASSERT_EQ(EXC_STORE_PAGE_FAULT, 15UL);

		/* 中断标志位 */
		TEST_ASSERT_EQ(SCAUSE_IRQ_FLAG, (1UL << 63));
	}
	TEST_END("trap: IRQ/exception codes");
	return;
fail:
	TEST_FAIL("trap: IRQ/exception codes", "see above");
}
