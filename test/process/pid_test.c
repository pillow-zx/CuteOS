#include <kernel/pid.h>
#include <kernel/test.h>

#include "../ktest.h"

int test_pid_basic(void)
{
	int32_t p1 = -1;
	int32_t p1b = -1;
	int32_t p0 = -1;
	int32_t after_negative = -1;
	int32_t first_pid = -1;

	TEST_BEGIN("pid: basic alloc/free");
	{
		p1 = alloc_pid();
		TEST_ASSERT(p1 > 0);
		first_pid = p1;

		free_pid((pid_t)p1);
		p1 = -1;
		p1b = alloc_pid();
		TEST_ASSERT_EQ(p1b, first_pid);

		free_pid((pid_t)p1b);
		p1b = -1;

		free_pid(0);

		p0 = alloc_pid();
		TEST_ASSERT_EQ(p0, first_pid);
		free_pid((pid_t)p0);
		p0 = -1;

		free_pid((pid_t)-1);
		TEST_ASSERT(pid_task((pid_t)-1) == NULL);

		after_negative = alloc_pid();
		TEST_ASSERT_EQ(after_negative, first_pid);
		free_pid((pid_t)after_negative);
		after_negative = -1;
	}
	TEST_END("pid: basic alloc/free");
	goto cleanup;
fail:
	TEST_FAIL("pid: basic alloc/free", "see above");
cleanup:
	if (p1 > 0)
		free_pid((pid_t)p1);
	if (p1b > 0)
		free_pid((pid_t)p1b);
	if (p0 > 0)
		free_pid((pid_t)p0);
	if (after_negative > 0)
		free_pid((pid_t)after_negative);
	return __test_ret;
}

int test_pid_exhaust(void)
{
	int32_t pids[PID_COUNT];
	int count = 0;
	int expected;

	TEST_BEGIN("pid: exhaust and recover");
	{
		expected = PID_MAX - pid_count_tasks();

		for (int i = 0; i < expected; i++) {
			int32_t pid = alloc_pid();
			if (pid < 0)
				break;
			pids[count++] = pid;
		}

		TEST_ASSERT_EQ(count, expected);

		int32_t oom = alloc_pid();
		TEST_ASSERT(oom < 0);

		int slot = count > 100 ? 100 : 0;
		free_pid((pid_t)pids[slot]);
		int32_t recovered = alloc_pid();
		TEST_ASSERT_EQ(recovered, pids[slot]);

		free_pid((pid_t)recovered);
		for (int i = 0; i < count; i++) {
			if (i != slot)
				free_pid((pid_t)pids[i]);
		}
		count = 0;
	}
	TEST_END("pid: exhaust and recover");
	goto cleanup;
fail:
	TEST_FAIL("pid: exhaust and recover", "see above");
cleanup:
	for (int i = 0; i < count; i++)
		free_pid((pid_t)pids[i]);
	return __test_ret;
}
