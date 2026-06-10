#include <kernel/test.h>
#include <kernel/string.h>
#include <asm/trap.h>

/* ================================================================
 *  Trap 测试
 * ================================================================ */

void test_trap_frame_layout(void)
{
	TEST_BEGIN("trap: frame layout");
	{
		struct trap_frame tf;

		(void)tf; /* 抑制未使用警告 */

		/* 结构体应有 35 个 size_t 字段 */
		TEST_ASSERT_EQ(sizeof(tf), (size_t)(35 * sizeof(size_t)));

		/* 验证 sepc 在最前面 */
		TEST_ASSERT_EQ(offsetof(struct trap_frame, sepc), (size_t)0);

		/* 验证 scause 偏移 = 32 * sizeof(size_t) */
		TEST_ASSERT_EQ(offsetof(struct trap_frame, scause),
			       (size_t)(32 * sizeof(size_t)));

		/* 验证 stval 紧跟 scause */
		TEST_ASSERT_EQ(offsetof(struct trap_frame, stval),
			       (size_t)(33 * sizeof(size_t)));

		/* 验证 sstatus 在最后 */
		TEST_ASSERT_EQ(offsetof(struct trap_frame, sstatus),
			       (size_t)(34 * sizeof(size_t)));
	}
	TEST_END("trap: frame layout");
	return;
fail:
	TEST_FAIL("trap: frame layout", "see above");
}

void test_trap_from_user(void)
{
	TEST_BEGIN("trap: from_user helper");
	{
		struct trap_frame tf;

		memset(&tf, 0, sizeof(tf));

		/* SPP=0 (用户模式) → from_user 返回 true */
		tf.sstatus = 0;
		TEST_ASSERT(from_user(&tf) == true);

		/* SPP=1 (内核模式) → from_user 返回 false */
		tf.sstatus = SSTATUS_SPP;
		TEST_ASSERT(from_user(&tf) == false);

		/* SPP=0 但其他位有值 → from_user 仍返回 true */
		tf.sstatus = SSTATUS_SIE;
		TEST_ASSERT(from_user(&tf) == true);
	}
	TEST_END("trap: from_user helper");
	return;
fail:
	TEST_FAIL("trap: from_user helper", "see above");
}

void test_trap_context_layout(void)
{
	TEST_BEGIN("trap: context layout");
	{
		struct context ctx;
		(void)ctx;

		/* 14 个 callee-saved 寄存器 */
		TEST_ASSERT_EQ(sizeof(ctx), (size_t)(14 * sizeof(size_t)));

		/* ra 在最前 */
		TEST_ASSERT_EQ(offsetof(struct context, ra), (size_t)0);

		/* sp 紧跟 ra */
		TEST_ASSERT_EQ(offsetof(struct context, sp),
			       (size_t)sizeof(size_t));
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
