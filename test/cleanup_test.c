#include <kernel/cleanup.h>
#include <kernel/test.h>
#include <kernel/types.h>

struct cleanup_test_object {
	int id;
};

static int cleanup_test_free_count;
static int cleanup_test_last_id;
static int cleanup_test_guard_log[4];
static int cleanup_test_guard_log_count;

static void cleanup_test_reset(void)
{
	cleanup_test_free_count = 0;
	cleanup_test_last_id = 0;
	cleanup_test_guard_log_count = 0;
}

static void cleanup_test_put(struct cleanup_test_object *obj)
{
	cleanup_test_free_count++;
	cleanup_test_last_id = obj->id;
}

CLEANUP_DEFINE(cleanup_test_put, struct cleanup_test_object *,
	       if (_T) cleanup_test_put(_T))

static void cleanup_test_lock(struct cleanup_test_object *obj)
{
	cleanup_test_guard_log[cleanup_test_guard_log_count++] = obj->id;
}

static void cleanup_test_unlock(struct cleanup_test_object *obj)
{
	cleanup_test_guard_log[cleanup_test_guard_log_count++] = -obj->id;
}

SCOPE_GUARD_DEFINE(cleanup_test, struct cleanup_test_object *,
		   cleanup_test_lock(_T), cleanup_test_unlock(_T))

static struct cleanup_test_object *
cleanup_test_class_init(struct cleanup_test_object *obj, int id)
{
	obj->id = id;
	return obj;
}

SCOPE_DEFINE(cleanup_test_class, struct cleanup_test_object *,
	     if (_T) cleanup_test_put(_T), cleanup_test_class_init(obj, id),
	     struct cleanup_test_object *obj, int id)

SCOPE_EXTEND(cleanup_test_class, _zero, cleanup_test_class_init(obj, 0),
	     struct cleanup_test_object *obj)

void test_cleanup_free_scope(void)
{
	TEST_BEGIN("cleanup: __cleanup_with releases at scope exit");

	cleanup_test_reset();

	{
		struct cleanup_test_object obj = { .id = 42 };
		struct cleanup_test_object *ptr __cleanup_with(cleanup_test_put) = &obj;

		TEST_ASSERT_EQ(cleanup_test_free_count, 0);
		TEST_ASSERT_EQ(ptr->id, 42);
	}

	TEST_ASSERT_EQ(cleanup_test_free_count, 1);
	TEST_ASSERT_EQ(cleanup_test_last_id, 42);

	TEST_END("cleanup: __cleanup_with releases at scope exit");
	return;

fail:
	TEST_FAIL("cleanup: __cleanup_with releases at scope exit", "see above");
}

static struct cleanup_test_object *
cleanup_test_return_owned_ptr(struct cleanup_test_object *obj)
{
	struct cleanup_test_object *ptr __cleanup_with(cleanup_test_put) = obj;

	cleanup_return_ptr(ptr);
}

void test_cleanup_take_ptr(void)
{
	struct cleanup_test_object obj = { .id = 7 };
	struct cleanup_test_object *ptr;

	TEST_BEGIN("cleanup: cleanup_take_ptr transfers ownership");

	cleanup_test_reset();

	{
		struct cleanup_test_object *owned __cleanup_with(cleanup_test_put) = &obj;

		ptr = cleanup_take_ptr(owned);
		TEST_ASSERT_EQ(ptr, &obj);
		TEST_ASSERT_NULL(owned);
	}

	TEST_ASSERT_EQ(cleanup_test_free_count, 0);
	TEST_ASSERT_EQ(cleanup_test_return_owned_ptr(&obj), &obj);
	TEST_ASSERT_EQ(cleanup_test_free_count, 0);

	TEST_END("cleanup: cleanup_take_ptr transfers ownership");
	return;

fail:
	TEST_FAIL("cleanup: cleanup_take_ptr transfers ownership", "see above");
}

void test_cleanup_forget_ptr(void)
{
	struct cleanup_test_object obj = { .id = 9 };

	TEST_BEGIN("cleanup: cleanup_forget_ptr cancels cleanup");

	cleanup_test_reset();

	{
		struct cleanup_test_object *owned __cleanup_with(cleanup_test_put) = &obj;

		cleanup_forget_ptr(owned);
		TEST_ASSERT_NULL(owned);
	}

	TEST_ASSERT_EQ(cleanup_test_free_count, 0);

	TEST_END("cleanup: cleanup_forget_ptr cancels cleanup");
	return;

fail:
	TEST_FAIL("cleanup: cleanup_forget_ptr cancels cleanup", "see above");
}

void test_cleanup_guard_scope(void)
{
	struct cleanup_test_object obj = { .id = 3 };

	TEST_BEGIN("cleanup: scope_guard unlocks at scope exit");

	cleanup_test_reset();

	{
		scope_guard(cleanup_test)(&obj);

		TEST_ASSERT_EQ(cleanup_test_guard_log_count, 1);
		TEST_ASSERT_EQ(cleanup_test_guard_log[0], 3);
	}

	TEST_ASSERT_EQ(cleanup_test_guard_log_count, 2);
	TEST_ASSERT_EQ(cleanup_test_guard_log[1], -3);

	TEST_END("cleanup: scope_guard unlocks at scope exit");
	return;

fail:
	TEST_FAIL("cleanup: scope_guard unlocks at scope exit", "see above");
}

void test_cleanup_with_guard_block(void)
{
	struct cleanup_test_object obj = { .id = 5 };

	TEST_BEGIN("cleanup: with_guard lifetime is block");

	cleanup_test_reset();

	with_guard(cleanup_test, &obj) {
		TEST_ASSERT_EQ(cleanup_test_guard_log_count, 1);
		TEST_ASSERT_EQ(cleanup_test_guard_log[0], 5);
	}

	TEST_ASSERT_EQ(cleanup_test_guard_log_count, 2);
	TEST_ASSERT_EQ(cleanup_test_guard_log[1], -5);

	TEST_END("cleanup: with_guard lifetime is block");
	return;

fail:
	TEST_FAIL("cleanup: with_guard lifetime is block", "see above");
}

void test_cleanup_class_helpers(void)
{
	struct cleanup_test_object obj = { .id = 0 };

	TEST_BEGIN("cleanup: scope helpers construct and destroy");

	cleanup_test_reset();

	{
		SCOPE_VAR(cleanup_test_class, owned)(&obj, 13);

		TEST_ASSERT_EQ(owned, &obj);
		TEST_ASSERT_EQ(obj.id, 13);
		TEST_ASSERT_EQ(cleanup_test_free_count, 0);
	}

	TEST_ASSERT_EQ(cleanup_test_free_count, 1);
	TEST_ASSERT_EQ(cleanup_test_last_id, 13);

	cleanup_test_reset();
	obj.id = 21;

	{
		SCOPE_VAR_INIT(cleanup_test_class, owned, &obj);

		TEST_ASSERT_EQ(owned, &obj);
		TEST_ASSERT_EQ(obj.id, 21);
	}

	TEST_ASSERT_EQ(cleanup_test_free_count, 1);
	TEST_ASSERT_EQ(cleanup_test_last_id, 21);

	cleanup_test_reset();

	{
		SCOPE_VAR(cleanup_test_class_zero, owned)(&obj);

		TEST_ASSERT_EQ(owned, &obj);
		TEST_ASSERT_EQ(obj.id, 0);
	}

	TEST_ASSERT_EQ(cleanup_test_free_count, 1);
	TEST_ASSERT_EQ(cleanup_test_last_id, 0);

	TEST_END("cleanup: scope helpers construct and destroy");
	return;

fail:
	TEST_FAIL("cleanup: scope helpers construct and destroy", "see above");
}
