#include <kernel/test.h>
#include <kernel/mm.h>
#include <kernel/page.h>
#include <kernel/trap.h>
#include <kernel/task.h>
#include <kernel/user_return.h>
#include <uapi/mman.h>
#include <uapi/syscall.h>

static int user_return_test_count;
static struct trap_frame *user_return_test_tf;

static void user_return_test_hook(struct trap_frame *tf)
{
	user_return_test_count++;
	user_return_test_tf = tf;
}

int test_user_return_work_ecall_path(void)
{
	struct trap_frame tf;
	struct trap_frame *saved_tf = task_trap_frame(current_task());

	TEST_BEGIN("user-return: ecall path uses generic work");
	{
		memset(&tf, 0, sizeof(tf));
		trap_setup_user_return(&tf, 0x1000, 0x2000);
		tf.scause = EXC_ECALL_U;
		tf.a7 = SYS_getpid;

		user_return_test_count = 0;
		user_return_test_tf = NULL;
		user_return_set_test_hook(user_return_test_hook);

		trap_handler(&tf);

		user_return_set_test_hook(NULL);
		task_set_trap_frame(current_task(), saved_tf);

		TEST_ASSERT_EQ(user_return_test_count, 1);
		TEST_ASSERT_EQ((uintptr_t)user_return_test_tf, (uintptr_t)&tf);
		TEST_ASSERT_EQ(trap_user_pc(&tf), (uintptr_t)0x1004);
		TEST_ASSERT_EQ(trap_return_value(&tf),
			       (uintptr_t)task_tgid(current_task()));
	}
	TEST_END("user-return: ecall path uses generic work");
	return __test_ret;
fail:
	user_return_set_test_hook(NULL);
	task_set_trap_frame(current_task(), saved_tf);
	TEST_FAIL("user-return: ecall path uses generic work", "see above");

	return __test_ret;
}

int test_user_return_work_page_fault_path(void)
{
	const uintptr_t fault_addr = 0x400000UL;
	struct task_struct *saved_task = current_task();
	struct task_struct *task = NULL;
	struct mm_struct *mm = NULL;
	struct trap_frame tf;

	TEST_BEGIN("user-return: page fault path uses generic work");
	{
		task = task_alloc();
		TEST_ASSERT_NOT_NULL(task);
		mm = mm_create_user();
		TEST_ASSERT_NOT_NULL(mm);
		TEST_ASSERT_EQ(mm_mmap(mm, fault_addr, PAGE_SIZE, PROT_READ,
				       MAP_PRIVATE | MAP_ANONYMOUS),
			       (ssize_t)fault_addr);

		task_set_mm(task, mm);
		task_set_satp(task, mm_user_satp(mm));
		set_current_task(task);

		memset(&tf, 0, sizeof(tf));
		trap_setup_user_return(&tf, fault_addr, 0x2000);
		tf.scause = EXC_LOAD_PAGE_FAULT;
		tf.stval = fault_addr;

		user_return_test_count = 0;
		user_return_test_tf = NULL;
		user_return_set_test_hook(user_return_test_hook);

		trap_handler(&tf);

		user_return_set_test_hook(NULL);
		TEST_ASSERT_EQ(user_return_test_count, 1);
		TEST_ASSERT_EQ((uintptr_t)user_return_test_tf, (uintptr_t)&tf);
	}
	TEST_END("user-return: page fault path uses generic work");
	goto cleanup;
fail:
	user_return_set_test_hook(NULL);
	TEST_FAIL("user-return: page fault path uses generic work",
		  "see above");
cleanup:
	set_current_task(saved_task);
	if (task) {
		task_set_mm(task, NULL);
		task_set_satp(task, 0);
		task_free(task);
	}
	if (mm)
		mm_put(mm);

	return __test_ret;
}

int test_user_return_work_timer_path(void)
{
	struct trap_frame tf;
	struct trap_frame *saved_tf = task_trap_frame(current_task());

	TEST_BEGIN("user-return: timer path uses generic work");
	{
		memset(&tf, 0, sizeof(tf));
		trap_setup_user_return(&tf, 0x1000, 0x2000);
		tf.scause = SCAUSE_IRQ_FLAG | IRQ_S_TIMER;

		user_return_test_count = 0;
		user_return_test_tf = NULL;
		user_return_set_test_hook(user_return_test_hook);

		trap_handler(&tf);

		user_return_set_test_hook(NULL);
		task_set_trap_frame(current_task(), saved_tf);

		TEST_ASSERT_EQ(user_return_test_count, 1);
		TEST_ASSERT_EQ((uintptr_t)user_return_test_tf, (uintptr_t)&tf);
	}
	TEST_END("user-return: timer path uses generic work");
	return __test_ret;
fail:
	user_return_set_test_hook(NULL);
	task_set_trap_frame(current_task(), saved_tf);
	TEST_FAIL("user-return: timer path uses generic work", "see above");

	return __test_ret;
}
