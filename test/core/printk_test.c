#include <kernel/printk.h>
#include <kernel/test.h>

#include "../ktest.h"

static char printk_test_pattern(size_t index)
{
	return (char)('a' + index % 26);
}

int test_printk_ring_read_clear_and_overwrite(void)
{
	char buffer[16];

	TEST_BEGIN("printk: ring read, clear, and overwrite");
	printk_test_reset();
	TEST_ASSERT_EQ(printk_log_buffer_size(), (size_t)4096);
	printk_test_append("abc", 3);
	TEST_ASSERT_EQ(printk_test_read_all(buffer, sizeof(buffer), false),
		       (size_t)3);
	TEST_ASSERT(memcmp(buffer, "abc", 3) == 0);
	TEST_ASSERT_EQ(printk_test_read(buffer, 1), (size_t)1);
	TEST_ASSERT_EQ(buffer[0], 'a');
	TEST_ASSERT_EQ(printk_test_read_all(buffer, sizeof(buffer), false),
		       (size_t)3);
	TEST_ASSERT(memcmp(buffer, "abc", 3) == 0);
	TEST_ASSERT_EQ(printk_test_read_all(buffer, sizeof(buffer), true),
		       (size_t)3);
	TEST_ASSERT(memcmp(buffer, "abc", 3) == 0);
	TEST_ASSERT_EQ(printk_test_read_all(buffer, sizeof(buffer), false),
		       (size_t)0);
	TEST_ASSERT_EQ(printk_test_read(buffer, sizeof(buffer)), (size_t)2);
	TEST_ASSERT(memcmp(buffer, "bc", 2) == 0);

	printk_test_reset();
	for (size_t index = 0; index < printk_log_buffer_size() + 8; index++) {
		char character = printk_test_pattern(index);

		printk_test_append(&character, 1);
	}
	TEST_ASSERT_EQ(printk_log_unread_size(), printk_log_buffer_size());
	TEST_ASSERT_EQ(printk_test_read_all(buffer, sizeof(buffer), false),
		       sizeof(buffer));
	for (size_t index = 0; index < sizeof(buffer); index++)
		TEST_ASSERT_EQ(buffer[index],
			       printk_test_pattern(printk_log_buffer_size() +
						   8 - sizeof(buffer) + index));
	TEST_ASSERT_EQ(printk_test_read(buffer, sizeof(buffer)),
		       sizeof(buffer));
	for (size_t index = 0; index < sizeof(buffer); index++)
		TEST_ASSERT_EQ(buffer[index], printk_test_pattern(8 + index));
	TEST_END("printk: ring read, clear, and overwrite");
	return __test_ret;
fail:
	TEST_FAIL("printk: ring read, clear, and overwrite", "see above");

	return __test_ret;
}
