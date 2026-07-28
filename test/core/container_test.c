#include <kernel/errno.h>
#include <kernel/kfifo.h>
#include <kernel/klifo.h>
#include <kernel/test.h>

#include "../ktest.h"

struct container_test_entry {
	uint32_t kind;
	uint64_t value;
};

int test_kfifo_order_and_wrap(void)
{
	TEST_BEGIN("kfifo: order, bounds, and wraparound");
	{
		KFIFO_DECLARE(fifo, int, 3);
		int value;

		TEST_ASSERT(kfifo_valid(&fifo));
		TEST_ASSERT(kfifo_empty(&fifo));
		TEST_ASSERT_EQ(kfifo_capacity(&fifo), (size_t)3);
		TEST_ASSERT_EQ(kfifo_put(&fifo, &(int){1}), 0);
		TEST_ASSERT_EQ(kfifo_put(&fifo, &(int){2}), 0);
		TEST_ASSERT_EQ(kfifo_put(&fifo, &(int){3}), 0);
		TEST_ASSERT(kfifo_full(&fifo));
		TEST_ASSERT_EQ(kfifo_put(&fifo, &(int){4}), -ENOSPC);

		TEST_ASSERT_EQ(kfifo_get(&fifo, &value), 0);
		TEST_ASSERT_EQ(value, 1);
		TEST_ASSERT_EQ(kfifo_put(&fifo, &(int){4}), 0);

		TEST_ASSERT_EQ(kfifo_get(&fifo, &value), 0);
		TEST_ASSERT_EQ(value, 2);
		TEST_ASSERT_EQ(kfifo_peek(&fifo, &value), 0);
		TEST_ASSERT_EQ(value, 3);
		TEST_ASSERT_EQ(kfifo_get(&fifo, &value), 0);
		TEST_ASSERT_EQ(value, 3);
		TEST_ASSERT_EQ(kfifo_get(&fifo, &value), 0);
		TEST_ASSERT_EQ(value, 4);
		TEST_ASSERT(kfifo_empty(&fifo));
		TEST_ASSERT_EQ(kfifo_get(&fifo, &value), -ENODATA);
	}
	TEST_END("kfifo: order, bounds, and wraparound");
	return __test_ret;
fail:
	TEST_FAIL("kfifo: order, bounds, and wraparound", "see above");

	return __test_ret;
}

int test_kfifo_bulk_and_init(void)
{
	TEST_BEGIN("kfifo: bulk objects and initialization validation");
	{
		KFIFO_DECLARE(fifo, struct container_test_entry, 3);
		struct container_test_entry input[] = {
			{.kind = 1, .value = 11},
			{.kind = 2, .value = 22},
			{.kind = 3, .value = 33},
			{.kind = 4, .value = 44},
		};
		struct container_test_entry output[3];

		TEST_ASSERT_EQ(kfifo_in(&fifo, input, 4), (size_t)3);
		TEST_ASSERT_EQ(kfifo_out(&fifo, output, 2), (size_t)2);
		TEST_ASSERT_EQ(output[0].kind, (uint32_t)1);
		TEST_ASSERT_EQ(output[0].value, (uint64_t)11);
		TEST_ASSERT_EQ(output[1].kind, (uint32_t)2);
		TEST_ASSERT_EQ(output[1].value, (uint64_t)22);
		TEST_ASSERT_EQ(kfifo_in(&fifo, &input[3], 1), (size_t)1);
		TEST_ASSERT_EQ(kfifo_out(&fifo, output, 3), (size_t)2);
		TEST_ASSERT_EQ(output[0].kind, (uint32_t)3);
		TEST_ASSERT_EQ(output[1].kind, (uint32_t)4);
		TEST_ASSERT_EQ(kfifo_reset(&fifo), 0);
		TEST_ASSERT(kfifo_empty(&fifo));
	}
	{
		struct kfifo fifo;
		int storage[2];

		TEST_ASSERT_EQ(kfifo_init(NULL, storage, sizeof(storage[0]), 2),
			       -EINVAL);
		TEST_ASSERT_EQ(kfifo_init(&fifo, NULL, sizeof(storage[0]), 2),
			       -EINVAL);
		TEST_ASSERT_EQ(kfifo_init(&fifo, storage, 0, 2), -EINVAL);
		TEST_ASSERT_EQ(
			kfifo_init(&fifo, storage, sizeof(storage[0]), 0),
			-EINVAL);
		TEST_ASSERT_EQ(
			kfifo_init(&fifo, storage, (size_t)UINT64_MAX, 2),
			-EINVAL);
		TEST_ASSERT(!kfifo_valid(&fifo));
		TEST_ASSERT_EQ(kfifo_put(&fifo, &(int){1}), -EINVAL);
		TEST_ASSERT_EQ(
			kfifo_init(&fifo, storage, sizeof(storage[0]), 2), 0);
		TEST_ASSERT(kfifo_valid(&fifo));
	}
	TEST_END("kfifo: bulk objects and initialization validation");
	return __test_ret;
fail:
	TEST_FAIL("kfifo: bulk objects and initialization validation",
		  "see above");

	return __test_ret;
}

int test_klifo_order_and_bounds(void)
{
	TEST_BEGIN("klifo: LIFO order and bounds");
	{
		KLIFO_DECLARE(lifo, int, 3);
		int value;

		TEST_ASSERT(klifo_valid(&lifo));
		TEST_ASSERT(klifo_empty(&lifo));
		TEST_ASSERT_EQ(klifo_push(&lifo, &(int){1}), 0);
		TEST_ASSERT_EQ(klifo_push(&lifo, &(int){2}), 0);
		TEST_ASSERT_EQ(klifo_push(&lifo, &(int){3}), 0);
		TEST_ASSERT(klifo_full(&lifo));
		TEST_ASSERT_EQ(klifo_push(&lifo, &(int){4}), -ENOSPC);

		TEST_ASSERT_EQ(klifo_peek(&lifo, &value), 0);
		TEST_ASSERT_EQ(value, 3);
		TEST_ASSERT_EQ(klifo_pop(&lifo, &value), 0);
		TEST_ASSERT_EQ(value, 3);
		TEST_ASSERT_EQ(klifo_pop(&lifo, &value), 0);
		TEST_ASSERT_EQ(value, 2);
		TEST_ASSERT_EQ(klifo_pop(&lifo, &value), 0);
		TEST_ASSERT_EQ(value, 1);
		TEST_ASSERT(klifo_empty(&lifo));
		TEST_ASSERT_EQ(klifo_pop(&lifo, &value), -ENODATA);
	}
	TEST_END("klifo: LIFO order and bounds");
	return __test_ret;
fail:
	TEST_FAIL("klifo: LIFO order and bounds", "see above");

	return __test_ret;
}

int test_klifo_objects_and_init(void)
{
	TEST_BEGIN("klifo: fixed-size objects and initialization validation");
	{
		KLIFO_DECLARE(lifo, struct container_test_entry, 2);
		struct container_test_entry first = {.kind = 1, .value = 11};
		struct container_test_entry second = {.kind = 2, .value = 22};
		struct container_test_entry output;

		TEST_ASSERT_EQ(klifo_push(&lifo, &first), 0);
		TEST_ASSERT_EQ(klifo_push(&lifo, &second), 0);
		TEST_ASSERT_EQ(klifo_pop(&lifo, &output), 0);
		TEST_ASSERT_EQ(output.kind, second.kind);
		TEST_ASSERT_EQ(output.value, second.value);
		TEST_ASSERT_EQ(klifo_reset(&lifo), 0);
		TEST_ASSERT(klifo_empty(&lifo));
	}
	{
		struct klifo lifo;
		int storage[2];

		TEST_ASSERT_EQ(klifo_init(NULL, storage, sizeof(storage[0]), 2),
			       -EINVAL);
		TEST_ASSERT_EQ(klifo_init(&lifo, NULL, sizeof(storage[0]), 2),
			       -EINVAL);
		TEST_ASSERT_EQ(klifo_init(&lifo, storage, 0, 2), -EINVAL);
		TEST_ASSERT_EQ(
			klifo_init(&lifo, storage, sizeof(storage[0]), 0),
			-EINVAL);
		TEST_ASSERT_EQ(
			klifo_init(&lifo, storage, (size_t)UINT64_MAX, 2),
			-EINVAL);
		TEST_ASSERT(!klifo_valid(&lifo));
		TEST_ASSERT_EQ(klifo_push(&lifo, &(int){1}), -EINVAL);
		TEST_ASSERT_EQ(
			klifo_init(&lifo, storage, sizeof(storage[0]), 2), 0);
		TEST_ASSERT(klifo_valid(&lifo));
	}
	TEST_END("klifo: fixed-size objects and initialization validation");
	return __test_ret;
fail:
	TEST_FAIL("klifo: fixed-size objects and initialization validation",
		  "see above");

	return __test_ret;
}
