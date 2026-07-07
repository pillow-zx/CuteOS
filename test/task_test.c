#include <kernel/test.h>
#include <kernel/task.h>
#include <kernel/cpu.h>
#include <kernel/pid.h>

void test_task_layout_contract(void)
{
	TEST_BEGIN("task: layout contract");
	{
		TEST_ASSERT(arch_task_test_layout_contract());
	}
	TEST_END("task: layout contract");
	return;
fail:
	TEST_FAIL("task: layout contract", "see above");
}

void test_cpu_boot_topology(void)
{
	TEST_BEGIN("cpu: boot topology");
	{
		TEST_ASSERT_EQ((uint32_t)NR_CPUS, (uint32_t)CONFIG_QEMU_CPUS);
		TEST_ASSERT_EQ(nr_cpu_ids, (uint32_t)1);
		TEST_ASSERT_EQ(current_cpu(), cpu_by_id(0));
		TEST_ASSERT(cpu_is_online(0));

		for (uint32_t id = 1; id < NR_CPUS; id++)
			TEST_ASSERT(!cpu_is_online(id));
	}
	TEST_END("cpu: boot topology");
	return;
fail:
	TEST_FAIL("cpu: boot topology", "see above");
}

void test_cpu_current_task_accessors(void)
{
	struct task_struct *saved = current_task();
	struct task_struct *task = NULL;

	TEST_BEGIN("cpu: active task accessors");
	{
		TEST_ASSERT_EQ(cpu_current_task(current_cpu()), saved);

		task = task_alloc();
		TEST_ASSERT_NOT_NULL(task);

		set_current_task(task);
		TEST_ASSERT_EQ(current_task(), task);
		TEST_ASSERT_EQ(cpu_current_task(current_cpu()), task);
	}
	TEST_END("cpu: active task accessors");
	goto cleanup;
fail:
	TEST_FAIL("cpu: active task accessors", "see above");
cleanup:
	set_current_task(saved);
	if (task)
		task_free(task);
}

void test_task_alloc_free(void)
{
	TEST_BEGIN("task: alloc/free");
	{
		struct task_struct *task = task_alloc();
		TEST_ASSERT_NOT_NULL(task);


		TEST_ASSERT(task_pid(task) > 0);
		TEST_ASSERT(task_pid(task) <= PID_MAX);


		TEST_ASSERT_EQ(task_state(task), (uint32_t)TASK_RUNNING);


		TEST_ASSERT_NOT_NULL(task_kernel_stack(task));


		TEST_ASSERT_ALIGNED(task_kernel_stack(task), PAGE_SIZE);


		TEST_ASSERT_NULL(task_mm(task));


		TEST_ASSERT_NULL(task_trap_frame(task));


		check_canary(task);


		TEST_ASSERT(list_empty(task_children(task)));
		TEST_ASSERT(!task_has_parent_link(task));
		TEST_ASSERT(!task_is_queued(task));

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


		uint64_t *canary_ptr = (uint64_t *)task_kernel_stack(task);
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
			pids[i] = task_pid(tasks[i]);
		}


		for (int i = 0; i < TASK_N_TASKS; i++) {
			for (int j = i + 1; j < TASK_N_TASKS; j++) {
				TEST_ASSERT_NE(pids[i], pids[j]);
			}
		}


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


		task_link_child(parent, child1);

		task_link_child(parent, child2);


		TEST_ASSERT(!list_empty(task_children(parent)));

		int child_count = 0;
		struct task_struct *pos;
		task_for_each_child (pos, parent)
			child_count++;
		TEST_ASSERT_EQ(child_count, 2);


		task_unlink_child(child1);
		task_unlink_child(child2);
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
		TEST_ASSERT_NOT_NULL(current_task());
		TEST_ASSERT_EQ(task_pid(current_task()), (pid_t)0);
		TEST_ASSERT_EQ(task_pid(&idle_task), (pid_t)0);
		TEST_ASSERT_EQ(current_task(), &idle_task);
		TEST_ASSERT_EQ(task_state(&idle_task), (uint32_t)TASK_RUNNING);
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

	}
	TEST_END("task: free(NULL) safe");
}
