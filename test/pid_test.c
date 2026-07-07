#include <kernel/pid.h>
#include <kernel/test.h>

#include "ktest.h"

void test_pid_basic(void)
{
	TEST_BEGIN("pid: basic alloc/free");
	{

		int32_t p1 = alloc_pid();
		TEST_ASSERT_EQ(p1, (int32_t)1);


		free_pid((pid_t)p1);
		int32_t p1b = alloc_pid();
		TEST_ASSERT_EQ(p1b, (int32_t)1);

		free_pid((pid_t)p1b);


		free_pid(0);

		int32_t p0 = alloc_pid();
		TEST_ASSERT_EQ(p0, (int32_t)1);
		free_pid((pid_t)p0);

		free_pid((pid_t)-1);
		TEST_ASSERT(pid_task((pid_t)-1) == NULL);

		int32_t after_negative = alloc_pid();
		TEST_ASSERT_EQ(after_negative, (int32_t)1);
		free_pid((pid_t)after_negative);
	}
	TEST_END("pid: basic alloc/free");
	return;
fail:
	TEST_FAIL("pid: basic alloc/free", "see above");
}

void test_pid_exhaust(void)
{
	TEST_BEGIN("pid: exhaust and recover");
	{
		int32_t pids[PID_COUNT];
		int count = 0;


		for (int i = 0; i < PID_COUNT - 1; i++) {
			int32_t pid = alloc_pid();
			if (pid < 0)
				break;
			pids[count++] = pid;
		}


		TEST_ASSERT_EQ(count, PID_COUNT - 1);


		int32_t oom = alloc_pid();
		TEST_ASSERT(oom < 0);


		free_pid((pid_t)pids[100]);
		int32_t recovered = alloc_pid();
		TEST_ASSERT_EQ(recovered, pids[100]);


		free_pid((pid_t)recovered);
		for (int i = 0; i < count; i++) {
			if (i != 100)
				free_pid((pid_t)pids[i]);
		}
	}
	TEST_END("pid: exhaust and recover");
	return;
fail:
	TEST_FAIL("pid: exhaust and recover", "see above");
}
