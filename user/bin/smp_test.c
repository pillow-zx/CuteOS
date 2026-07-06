/*
 * user/bin/smp_test.c - SMP-related user ABI tests
 */

#include <ulib.h>

#define MEMBARRIER_SUPPORTED_MASK                                              \
	(MEMBARRIER_CMD_GLOBAL | MEMBARRIER_CMD_GLOBAL_EXPEDITED |             \
	 MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED |                            \
	 MEMBARRIER_CMD_PRIVATE_EXPEDITED |                                    \
	 MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED |                           \
	 MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE |                          \
	 MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE |                 \
	 MEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ |                               \
	 MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_RSEQ |                      \
	 MEMBARRIER_CMD_GET_REGISTRATIONS)

static long membarrier(int cmd, unsigned int flags, int cpu_id)
{
	return syscall(SYS_membarrier, cmd, flags, cpu_id);
}

static int smp_expect_ret(const char *name, long got, long want)
{
	if (got != want) {
		printf("FAIL: %s expected %ld got %ld\n", name, want, got);
		return 1;
	}

	return 0;
}

static int test_membarrier_query(void)
{
	long ret = membarrier(MEMBARRIER_CMD_QUERY, 0, 0);

	return smp_expect_ret("query", ret, MEMBARRIER_SUPPORTED_MASK);
}

static int test_membarrier_commands(void)
{
	int failed = 0;
	long registrations;

	failed += smp_expect_ret("global",
				 membarrier(MEMBARRIER_CMD_GLOBAL, 0, 0), 0);
	failed += smp_expect_ret(
		"global expedited",
		membarrier(MEMBARRIER_CMD_GLOBAL_EXPEDITED, 0, 0), 0);
	failed += smp_expect_ret(
		"private expedited before register",
		membarrier(MEMBARRIER_CMD_PRIVATE_EXPEDITED, 0, 0), -EPERM);
	failed += smp_expect_ret(
		"private sync core before register",
		membarrier(MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE, 0, 0),
		-EPERM);
	failed += smp_expect_ret(
		"register private expedited",
		membarrier(MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED, 0, 0), 0);
	failed += smp_expect_ret(
		"private expedited after register",
		membarrier(MEMBARRIER_CMD_PRIVATE_EXPEDITED, 0, 0), 0);
	failed += smp_expect_ret(
		"register private sync core",
		membarrier(MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE,
			   0, 0),
		0);
	failed += smp_expect_ret(
		"private sync core after register",
		membarrier(MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE, 0, 0),
		0);
	failed += smp_expect_ret(
		"private expedited rseq before register",
		membarrier(MEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ, 0, 0),
		-EPERM);
	failed += smp_expect_ret(
		"private expedited rseq before register cpu flag",
		membarrier(MEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ,
			   MEMBARRIER_CMD_FLAG_CPU, 0),
		-EPERM);
	failed += smp_expect_ret(
		"register private expedited rseq",
		membarrier(MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_RSEQ, 0,
			   0),
		0);
	failed += smp_expect_ret(
		"private expedited rseq after register",
		membarrier(MEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ, 0, 0), 0);
	failed += smp_expect_ret(
		"private expedited rseq after register cpu flag",
		membarrier(MEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ,
			   MEMBARRIER_CMD_FLAG_CPU, 0),
		0);

	registrations = membarrier(MEMBARRIER_CMD_GET_REGISTRATIONS, 0, 0);
	failed += smp_expect_ret(
		"registrations", registrations,
		MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED |
			MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE |
			MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_RSEQ);

	return failed;
}

static int test_membarrier_errors(void)
{
	int failed = 0;

	failed += smp_expect_ret(
		"query flags",
		membarrier(MEMBARRIER_CMD_QUERY, MEMBARRIER_CMD_FLAG_CPU, 0),
		-EINVAL);
	failed += smp_expect_ret("unknown command", membarrier(1 << 30, 0, 0),
				 -EINVAL);
	failed += smp_expect_ret(
		"rseq bad flag",
		membarrier(MEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ, 2, 0),
		-EINVAL);

	return failed;
}

static void report_group(const char *name, int ret, int *failed)
{
	printf("smp_test: %s ... ", name);
	if (ret)
		(*failed)++;
	else
		printf("PASS\n");
}

int main(void)
{
	int failed = 0;

	report_group("membarrier query", test_membarrier_query(), &failed);
	report_group("membarrier commands", test_membarrier_commands(),
		     &failed);
	report_group("membarrier error paths", test_membarrier_errors(),
		     &failed);

	if (failed)
		printf("smp_test: %d test group(s) FAILED\n", failed);
	else
		printf("smp_test: all tests PASSED\n");

	return failed ? 1 : 0;
}
