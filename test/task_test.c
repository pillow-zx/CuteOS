#include <kernel/test.h>
#include <kernel/task.h>
#include <kernel/pid.h>

void test_task_alloc_free(void)
{
	TEST_BEGIN("task: alloc/free");
	{
		struct task_struct *task = task_alloc();
		TEST_ASSERT_NOT_NULL(task);

		/* PID 应 > 0（idle 占用了 0） */
		TEST_ASSERT(task->pid > 0);
		TEST_ASSERT(task->pid <= PID_MAX);

		/* 初始状态应为 RUNNING */
		TEST_ASSERT_EQ(task->state, (uint32_t)TASK_RUNNING);

		/* 内核栈应已分配 */
		TEST_ASSERT_NOT_NULL(task->kstack);

		/* 内核栈应按 KSTACK_SIZE 对齐 */
		TEST_ASSERT_ALIGNED(task->kstack, PAGE_SIZE);

		/* mm 应为 NULL（内核线程） */
		TEST_ASSERT_NULL(task->mm);

		/* tf 应为 NULL */
		TEST_ASSERT_NULL(task->tf);

		/* canary 应完好 */
		check_canary(task);

		/* 进程树链接应已初始化 */
		TEST_ASSERT(list_empty(&task->children));
		TEST_ASSERT(list_empty(&task->sibling));
		TEST_ASSERT(list_empty(&task->run_list));

		task_free(task);
	}
	TEST_END("task: alloc/free");
	return;
fail:
	TEST_FAIL("task: alloc/free", "see above");
}

void test_task_canary(void)
{
	TEST_BEGIN("task: canary integrity");
	{
		struct task_struct *task = task_alloc();
		TEST_ASSERT_NOT_NULL(task);

		/* 直接读取 canary 值 */
		uint64_t *canary_ptr = (uint64_t *)task->kstack;
		TEST_ASSERT_EQ(*canary_ptr, CANARY_MAGIC);

		check_canary(task);
		task_free(task);
	}
	TEST_END("task: canary integrity");
	return;
fail:
	TEST_FAIL("task: canary integrity", "see above");
}

void test_task_multiple(void)
{
	TEST_BEGIN("task: multiple tasks");
	{
#define TASK_N_TASKS 8
		struct task_struct *tasks[TASK_N_TASKS];
		pid_t pids[TASK_N_TASKS];

		for (int i = 0; i < TASK_N_TASKS; i++) {
			tasks[i] = task_alloc();
			TEST_ASSERT_NOT_NULL(tasks[i]);
			pids[i] = tasks[i]->pid;
		}

		/* 所有 PID 应互不相同 */
		for (int i = 0; i < TASK_N_TASKS; i++) {
			for (int j = i + 1; j < TASK_N_TASKS; j++) {
				TEST_ASSERT_NE(pids[i], pids[j]);
			}
		}

		/* 释放 */
		for (int i = 0; i < TASK_N_TASKS; i++)
			task_free(tasks[i]);
#undef TASK_N_TASKS
	}
	TEST_END("task: multiple tasks");
	return;
fail:
	TEST_FAIL("task: multiple tasks", "see above");
}

void test_task_process_tree(void)
{
	TEST_BEGIN("task: process tree linkage");
	{
		struct task_struct *parent = task_alloc();
		TEST_ASSERT_NOT_NULL(parent);

		struct task_struct *child1 = task_alloc();
		TEST_ASSERT_NOT_NULL(child1);

		struct task_struct *child2 = task_alloc();
		TEST_ASSERT_NOT_NULL(child2);

		/* 构建父子关系 */
		child1->parent = parent;
		list_add_tail(&child1->sibling, &parent->children);

		child2->parent = parent;
		list_add_tail(&child2->sibling, &parent->children);

		/* 验证 parent 有 2 个子进程 */
		TEST_ASSERT(!list_empty(&parent->children));

		int child_count = 0;
		struct task_struct *pos;
		list_for_each_entry (pos, &parent->children, sibling)
			child_count++;
		TEST_ASSERT_EQ(child_count, 2);

		/* 清理 */
		list_del(&child1->sibling);
		list_del(&child2->sibling);
		task_free(child2);
		task_free(child1);
		task_free(parent);
	}
	TEST_END("task: process tree linkage");
	return;
fail:
	TEST_FAIL("task: process tree linkage", "see above");
}

void test_task_idle(void)
{
	TEST_BEGIN("task: idle task init");
	{
		TEST_ASSERT_NOT_NULL(current);
		TEST_ASSERT_EQ(current->pid, (pid_t)0);
		TEST_ASSERT_EQ(idle_task.pid, (pid_t)0);
		TEST_ASSERT_EQ(current, &idle_task);
		TEST_ASSERT_EQ(idle_task.state, (uint32_t)TASK_RUNNING);
	}
	TEST_END("task: idle task init");
	return;
fail:
	TEST_FAIL("task: idle task init", "see above");
}

void test_task_free_null(void)
{
	TEST_BEGIN("task: free(NULL) safe");
	{
		task_free(NULL);
		/* 如果没崩溃就算通过 */
	}
	TEST_END("task: free(NULL) safe");
}
