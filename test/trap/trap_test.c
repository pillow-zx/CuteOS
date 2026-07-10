#include <kernel/test.h>
#include <kernel/trap.h>

int test_trap_frame_layout(void)
{
	TEST_BEGIN("trap: frame layout");
	{

		TEST_ASSERT_EQ(trap_frame_size(),
			       (size_t)(35 * sizeof(size_t)));
	}
	TEST_END("trap: frame layout");
	return __test_ret;
fail:
	TEST_FAIL("trap: frame layout", "see above");

	return __test_ret;
}

int test_trap_from_user(void)
{
	TEST_BEGIN("trap: arch_from_user helper");
	{
		struct trap_frame tf;

		memset(&tf, 0, sizeof(tf));


		trap_setup_user_return(&tf, 0, 0);
		TEST_ASSERT(trap_frame_from_user(&tf) == true);


		trap_set_kernel_return(&tf, 0);
		TEST_ASSERT(trap_frame_from_user(&tf) == false);


		trap_setup_user_return(&tf, 0, 0);
		TEST_ASSERT(trap_frame_from_user(&tf) == true);
	}
	TEST_END("trap: arch_from_user helper");
	return __test_ret;
fail:
	TEST_FAIL("trap: arch_from_user helper", "see above");

	return __test_ret;
}

int test_trap_context_layout(void)
{
	TEST_BEGIN("trap: context layout");
	{

		TEST_ASSERT_EQ(trap_context_size(),
			       (size_t)(14 * sizeof(size_t)));
	}
	TEST_END("trap: context layout");
	return __test_ret;
fail:
	TEST_FAIL("trap: context layout", "see above");

	return __test_ret;
}

int test_trap_irq_codes(void)
{
	TEST_BEGIN("trap: IRQ/exception codes");
	{

		TEST_ASSERT_EQ(IRQ_S_SOFT, 1UL);
		TEST_ASSERT_EQ(IRQ_S_TIMER, 5UL);
		TEST_ASSERT_EQ(IRQ_S_EXT, 9UL);


		TEST_ASSERT_EQ(EXC_INST_ILLEGAL, 2UL);
		TEST_ASSERT_EQ(EXC_ECALL_U, 8UL);
		TEST_ASSERT_EQ(EXC_ECALL_S, 9UL);
		TEST_ASSERT_EQ(EXC_LOAD_PAGE_FAULT, 13UL);
		TEST_ASSERT_EQ(EXC_STORE_PAGE_FAULT, 15UL);


		TEST_ASSERT_EQ(SCAUSE_IRQ_FLAG, (1UL << 63));
	}
	TEST_END("trap: IRQ/exception codes");
	return __test_ret;
fail:
	TEST_FAIL("trap: IRQ/exception codes", "see above");

	return __test_ret;
}
