/*
 * init/user_trap_test.c - 用户态 trap 往返测试
 *
 * 真实执行一段用户 stub（写满寄存器后 ecall），在 trap hook 中校验
 * trap_frame 是否完整保存了用户寄存器值，然后改写返回目标切回 idle。
 *
 * 测试路径：
 *   kernel_test() -> schedule() -> __trapret -> sret(U-mode)
 *   -> user ecall -> __alltraps(U->S) -> trap_handler(test hook)
 *   -> __trapret(S-mode) -> user_trap_test_resume() -> switch_to(idle)
 */

#include <kernel/test.h>
#include <kernel/printk.h>
#include <kernel/string.h>
#include <kernel/buddy.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <asm/page.h>
#include <asm/pte.h>
#include <asm/trap.h>
#include <asm/csr.h>

#include "ktest.h"

/* entry.S 中的 trap 返回入口 */
extern void __trapret(void);
extern char user_trap_test_start[];
extern char user_trap_test_ecall[];
extern char user_trap_test_end[];

/*
 * UTEST_* 哨兵值 — 必须与 init/user_trap_test_stub.S 中的 li 立即数保持同步，
 * 修改其中一处时必须同步更新另一处。
 *
 * gp (x3) 和 tp (x4) 故意不设哨兵值：
 *   gp — 用于 GP 相对寻址，加载假值会导致访问异常
 *   tp — 保存线程指针 (TLS)，覆写会破坏 TP 相对访问
 */
#define UTEST_RA  0x0101010101010101UL
#define UTEST_T0  0x0202020202020202UL
#define UTEST_T1  0x0303030303030303UL
#define UTEST_T2  0x0404040404040404UL
#define UTEST_S0  0x0505050505050505UL
#define UTEST_S1  0x0606060606060606UL
#define UTEST_A0  0x0707070707070707UL
#define UTEST_A1  0x0808080808080808UL
#define UTEST_A2  0x0909090909090909UL
#define UTEST_A3  0x0A0A0A0A0A0A0A0AUL
#define UTEST_A4  0x0B0B0B0B0B0B0B0BUL
#define UTEST_A5  0x0C0C0C0C0C0C0C0CUL
#define UTEST_A6  0x0D0D0D0D0D0D0D0DUL
#define UTEST_A7  0x0E0E0E0E0E0E0E0EUL
#define UTEST_S2  0x0F0F0F0F0F0F0F0FUL
#define UTEST_S3  0x1010101010101010UL
#define UTEST_S4  0x1111111111111111UL
#define UTEST_S5  0x1212121212121212UL
#define UTEST_S6  0x1313131313131313UL
#define UTEST_S7  0x1414141414141414UL
#define UTEST_S8  0x1515151515151515UL
#define UTEST_S9  0x1616161616161616UL
#define UTEST_S10 0x1717171717171717UL
#define UTEST_S11 0x1818181818181818UL
#define UTEST_T3  0x1919191919191919UL
#define UTEST_T4  0x1A1A1A1A1A1A1A1AUL
#define UTEST_T5  0x1B1B1B1B1B1B1B1BUL
#define UTEST_T6  0x1C1C1C1C1C1C1C1CUL

static volatile bool user_trap_test_trapped;
static volatile bool user_trap_test_resumed;
static volatile const char *user_trap_test_fail_msg;
static struct task_struct *user_trap_test_task;
static uintptr_t user_trap_test_user_sp;
static uintptr_t user_trap_test_ecall_va;

static void __noreturn user_trap_test_resume(void);

static bool user_trap_test_hook(struct trap_frame *tf)
{
#define USER_TRAP_CHECK(cond, msg)                                             \
	do {                                                                   \
		if (!(cond)) {                                                 \
			user_trap_test_fail_msg = (msg);                       \
			goto intercept;                                        \
		}                                                              \
	} while (0)

	if (current != user_trap_test_task)
		return false;

	user_trap_test_trapped = true;

	USER_TRAP_CHECK(from_user(tf), "trap did not originate from user mode");
	USER_TRAP_CHECK(tf->scause == EXC_ECALL_U,
			"unexpected trap cause while running user test");
	USER_TRAP_CHECK(tf->stval == 0, "ecall stval should be zero");
	USER_TRAP_CHECK(tf->sepc == user_trap_test_ecall_va,
			"saved sepc does not point to user ecall");
	USER_TRAP_CHECK(tf->sp == user_trap_test_user_sp,
			"user sp was not restored correctly");
	USER_TRAP_CHECK(tf->ra == UTEST_RA, "ra was not preserved");
	USER_TRAP_CHECK(tf->t0 == UTEST_T0, "t0 was not preserved");
	USER_TRAP_CHECK(tf->t1 == UTEST_T1, "t1 was not preserved");
	USER_TRAP_CHECK(tf->t2 == UTEST_T2, "t2 was not preserved");
	USER_TRAP_CHECK(tf->s0 == UTEST_S0, "s0 was not preserved");
	USER_TRAP_CHECK(tf->s1 == UTEST_S1, "s1 was not preserved");
	USER_TRAP_CHECK(tf->a0 == UTEST_A0, "a0 was not preserved");
	USER_TRAP_CHECK(tf->a1 == UTEST_A1, "a1 was not preserved");
	USER_TRAP_CHECK(tf->a2 == UTEST_A2, "a2 was not preserved");
	USER_TRAP_CHECK(tf->a3 == UTEST_A3, "a3 was not preserved");
	USER_TRAP_CHECK(tf->a4 == UTEST_A4, "a4 was not preserved");
	USER_TRAP_CHECK(tf->a5 == UTEST_A5, "a5 was not preserved");
	USER_TRAP_CHECK(tf->a6 == UTEST_A6, "a6 was not preserved");
	USER_TRAP_CHECK(tf->a7 == UTEST_A7, "a7 was not preserved");
	USER_TRAP_CHECK(tf->s2 == UTEST_S2, "s2 was not preserved");
	USER_TRAP_CHECK(tf->s3 == UTEST_S3, "s3 was not preserved");
	USER_TRAP_CHECK(tf->s4 == UTEST_S4, "s4 was not preserved");
	USER_TRAP_CHECK(tf->s5 == UTEST_S5, "s5 was not preserved");
	USER_TRAP_CHECK(tf->s6 == UTEST_S6, "s6 was not preserved");
	USER_TRAP_CHECK(tf->s7 == UTEST_S7, "s7 was not preserved");
	USER_TRAP_CHECK(tf->s8 == UTEST_S8, "s8 was not preserved");
	USER_TRAP_CHECK(tf->s9 == UTEST_S9, "s9 was not preserved");
	USER_TRAP_CHECK(tf->s10 == UTEST_S10, "s10 was not preserved");
	USER_TRAP_CHECK(tf->s11 == UTEST_S11, "s11 was not preserved");
	USER_TRAP_CHECK(tf->t3 == UTEST_T3, "t3 was not preserved");
	USER_TRAP_CHECK(tf->t4 == UTEST_T4, "t4 was not preserved");
	USER_TRAP_CHECK(tf->t5 == UTEST_T5, "t5 was not preserved");
	USER_TRAP_CHECK(tf->t6 == UTEST_T6, "t6 was not preserved");

intercept:
	/*
	 * 改写返回目标：不再回用户页，而是直接从 __trapret 的 S 分支
	 * 回到一个内核 continuation，然后手动 switch 回 idle 上下文。
	 */
	tf->sepc = (size_t)user_trap_test_resume;
	tf->sstatus |= SSTATUS_SPP | SSTATUS_SPIE;
	return true;

#undef USER_TRAP_CHECK
}

static void __noreturn user_trap_test_resume(void)
{
	struct task_struct *prev = current;

	user_trap_test_resumed = true;
	prev->state = TASK_DEAD;
	current = &idle_task;
	switch_to(&prev->ctx, &idle_task.ctx);
	panic("user trap test resume returned unexpectedly");
}

/**
 * forge_user_return_task - 伪造一个首次调度时将返回到 U-mode 的任务
 * @user_pc: 用户入口 PC（写入 tf->sepc）
 * @user_sp: 用户栈顶 SP（写入 tf->sp）
 *
 * 该 helper 复用 kernel_thread 的启动机制：
 *   ctx.ra = __trapret
 *   ctx.sp = tf
 * 但 trap_frame 的 sstatus 不设置 SPP，因此 __trapret 会走用户态返回路径。
 */
static struct task_struct *
forge_user_return_task(size_t user_pc, size_t user_sp, size_t user_sstatus)
{
	struct task_struct *task = task_alloc();
	if (!task)
		return NULL;

	struct trap_frame *tf =
		(struct trap_frame *)((uint8_t *)task->kstack + KSTACK_SIZE -
				      sizeof(struct trap_frame));

	memset(tf, 0, sizeof(*tf));

	tf->sepc = user_pc;
	tf->sp = user_sp;
	tf->sstatus = user_sstatus;

	task->tf = tf;
	task->ctx.ra = (size_t)__trapret;
	task->ctx.sp = (size_t)tf;

	return task;
}

/**
 * test_trap_user_return_task_setup - 真实运行 U->S trap 往返
 *
 * 关键点：
 *   - 临时分配代码页/用户栈页，并把现有 identity mapping 改成 PTE_U
 *   - 真实执行一段用户 stub，写满寄存器后触发 ecall
 *   - 在 trap hook 中校验 trap_frame 是否完整保存用户寄存器
 */
void test_trap_user_return_task_setup(void)
{
	struct task_struct *t = NULL;
	void *code_page = NULL;
	void *stack_page = NULL;
	bool enqueued = false;
	bool hook_installed = false;
	bool timer_quiesced = false;
	unsigned long saved_sie = 0;
	uintptr_t user_pc = 0;
	uintptr_t user_sp = 0;
	uintptr_t code_pa = 0;
	uintptr_t stack_pa = 0;
	size_t stub_size = 0;

	TEST_BEGIN("trap: runtime user entry and return");
	{
		TEST_ASSERT(list_empty(&runqueue));

		code_page = get_free_page(0);
		TEST_ASSERT_NOT_NULL(code_page);
		stack_page = get_free_page(0);
		TEST_ASSERT_NOT_NULL(stack_page);

		memset(stack_page, 0, PAGE_SIZE);

		stub_size = (size_t)(user_trap_test_end - user_trap_test_start);
		TEST_ASSERT(stub_size > 0);
		TEST_ASSERT(stub_size <= PAGE_SIZE);
		memcpy(code_page, user_trap_test_start, stub_size);

		code_pa = __pa((uintptr_t)code_page);
		stack_pa = __pa((uintptr_t)stack_page);
		user_pc = code_pa;
		user_sp = stack_pa + PAGE_SIZE;

		/*
		 * 复用现有 DRAM identity mapping：临时把测试页改成用户页。
		 * 这样无需在 3.1 阶段就引入完整的用户页表分配逻辑。
		 */
		page_table_write_current(code_pa, code_pa, PTE_USER_RX);
		page_table_write_current(stack_pa, stack_pa, PTE_USER_RW);

		t = forge_user_return_task(user_pc, user_sp, 0);
		TEST_ASSERT_NOT_NULL(t);

		user_trap_test_trapped = false;
		user_trap_test_resumed = false;
		user_trap_test_fail_msg = NULL;
		user_trap_test_task = t;
		user_trap_test_user_sp = user_sp;
		user_trap_test_ecall_va =
			user_pc + (uintptr_t)(user_trap_test_ecall -
					      user_trap_test_start);

		trap_set_test_hook(user_trap_test_hook);
		hook_installed = true;

		/*
		 * 关闭时钟中断，使整段手工编排的 U->S 往返对 timer 原子。
		 *
		 * 本测试的 test hook 会把"测试任务发出的任意 trap"一律改写到
		 * resume 并手工 switch_to(idle)。若 timer 在 schedule() /
		 * __trapret / resume 等 S 态脚手架中（此时 SIE=1）打断，hook
		 * 会在上下文切换中途强行 switch，破坏手工切换的不变量，随机
		 * 表现为 panic 退出或卡死。注意伪造任务的 sstatus=0 已保证 U
		 * 态中断关闭，但 S 态脚手架仍暴露在 timer 下，故在此显式屏蔽。
		 */
		saved_sie = csr_read(sie);
		csr_clear(sie, SIE_STIE);
		timer_quiesced = true;

		sched_enqueue(t);
		enqueued = true;
		schedule();
		enqueued = false;

		csr_write(sie, saved_sie);
		timer_quiesced = false;

		trap_set_test_hook(NULL);
		hook_installed = false;

		TEST_ASSERT_EQ((size_t)current, (size_t)&idle_task);
		TEST_ASSERT(user_trap_test_trapped == true);
		TEST_ASSERT(user_trap_test_resumed == true);
		TEST_ASSERT_NULL((void *)user_trap_test_fail_msg);
		TEST_ASSERT(list_empty(&runqueue));
	}

	TEST_END("trap: runtime user entry and return");
	goto cleanup;
fail:
	TEST_FAIL("trap: runtime user entry and return", "see above");
cleanup:
	if (hook_installed)
		trap_set_test_hook(NULL);
	if (timer_quiesced) {
		csr_write(sie, saved_sie);
		timer_quiesced = false;
	}
	if (enqueued)
		sched_dequeue(t);
	if (t)
		task_free(t);
	if (code_pa)
		page_table_write_current(code_pa, code_pa, PTE_KERN_RWX);
	if (stack_pa)
		page_table_write_current(stack_pa, stack_pa, PTE_KERN_RWX);
	if (code_page)
		free_page(code_page, 0);
	if (stack_page)
		free_page(stack_page, 0);
	return;
}
