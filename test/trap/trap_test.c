#include <kernel/test.h>
#include <kernel/signal.h>
#include <kernel/trap.h>
#include <uapi/signal.h>

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

int test_trap_user_exception_classification(void)
{
	struct trap_exception exception;
	struct trap_frame tf;
	static const struct {
		uintptr_t cause;
		int sig;
		int code;
		uintptr_t addr;
	} cases[] = {
		{EXC_INST_MISALIGNED, SIGBUS, BUS_ADRALN, 0x3003},
		{EXC_LOAD_MISALIGNED, SIGBUS, BUS_ADRALN, 0x3003},
		{EXC_STORE_MISALIGNED, SIGBUS, BUS_ADRALN, 0x3003},
		{EXC_INST_ACCESS, SIGSEGV, SEGV_ACCERR, 0x3003},
		{EXC_LOAD_ACCESS, SIGSEGV, SEGV_ACCERR, 0x3003},
		{EXC_STORE_ACCESS, SIGSEGV, SEGV_ACCERR, 0x3003},
		{EXC_INST_ILLEGAL, SIGILL, ILL_ILLOPC, 0x1000},
		{EXC_BREAKPOINT, SIGTRAP, TRAP_BRKPT, 0x1000},
		{EXC_ECALL_S, SIGILL, ILL_ILLTRP, 0x1000},
		{63, SIGILL, SI_KERNEL, 0x1000},
	};

	TEST_BEGIN("trap: user exception classification");
	{
		memset(&tf, 0, sizeof(tf));
		trap_setup_user_return(&tf, 0x1000, 0x2000);
		tf.stval = 0x3003;

		for (size_t index = 0; index < ARRLEN(cases); index++) {
			tf.scause = cases[index].cause;
			exception = trap_classify_exception(&tf);
			TEST_ASSERT_EQ(exception.disposition,
				       TRAP_EXCEPTION_USER_SIGNAL);
			TEST_ASSERT_EQ(exception.info.si_signo,
				       cases[index].sig);
			TEST_ASSERT_EQ(exception.info.si_code,
				       cases[index].code);
			TEST_ASSERT_EQ((uintptr_t)exception.info.si_addr,
				       cases[index].addr);
		}

		tf.scause = EXC_LOAD_PAGE_FAULT;
		exception = trap_classify_exception(&tf);
		TEST_ASSERT_EQ(exception.disposition,
			       TRAP_EXCEPTION_PAGE_FAULT);

		trap_set_kernel_return(&tf, 0x1000);
		tf.scause = EXC_INST_ILLEGAL;
		exception = trap_classify_exception(&tf);
		TEST_ASSERT_EQ(exception.disposition,
			       TRAP_EXCEPTION_KERNEL_FATAL);
	}
	TEST_END("trap: user exception classification");
	return __test_ret;
fail:
	TEST_FAIL("trap: user exception classification", "see above");
	return __test_ret;
}

int test_signal_riscv_frame_abi(void)
{
	struct trap_frame tf;

	TEST_BEGIN("signal: riscv rt frame ABI");
	{
		TEST_ASSERT_EQ(sizeof(siginfo_t), (size_t)128);
		TEST_ASSERT_EQ(sizeof(struct user_regs_struct), (size_t)256);
		TEST_ASSERT_EQ(offsetof(struct user_regs_struct, a0),
			       (size_t)(10 * sizeof(unsigned long)));
		TEST_ASSERT_EQ(offsetof(struct ucontext, uc_mcontext),
			       (size_t)176);
		TEST_ASSERT_EQ(sizeof(struct rt_sigframe), (size_t)1088);

		memset(&tf, 0, sizeof(tf));
		trap_setup_signal_handler(&tf, 0x1000, 0x2000, 0x3000, 4,
					  0x4000, 0x5000);
		TEST_ASSERT_EQ(tf.sepc, 0x1000UL);
		TEST_ASSERT_EQ(tf.ra, 0x2000UL);
		TEST_ASSERT_EQ(tf.sp, 0x3000UL);
		TEST_ASSERT_EQ(tf.a0, 4UL);
		TEST_ASSERT_EQ(tf.a1, 0x4000UL);
		TEST_ASSERT_EQ(tf.a2, 0x5000UL);
	}
	TEST_END("signal: riscv rt frame ABI");
	return __test_ret;
fail:
	TEST_FAIL("signal: riscv rt frame ABI", "see above");
	return __test_ret;
}
