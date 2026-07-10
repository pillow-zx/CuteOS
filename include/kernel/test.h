/*
 * include/kernel/test.h - 内核自测框架
 */

#ifndef _CUTEOS_KERNEL_TEST_H
#define _CUTEOS_KERNEL_TEST_H

#include <kernel/printk.h>
#include <kernel/types.h>
#include <kernel/compiler.h>

struct ktest_case {
	const char *name;
	int (*run)(void);
};

struct ktest_module {
	const char *name;
	const struct ktest_case *cases;
	uint32_t nr_cases;
};

struct ktest_subsystem {
	const char *name;
	const struct ktest_module *const *modules;
	uint32_t nr_modules;
};

struct ktest_summary {
	uint32_t subsystems;
	uint32_t modules;
	uint32_t failed_modules;
	uint32_t cases;
	uint32_t failed_cases;
};

#define KTEST_CASE(fn)                                                         \
	{                                                                      \
		.name = #fn, .run = fn                                         \
	}

#define KTEST_ARRAY_SIZE(array) ((uint32_t)(sizeof(array) / sizeof((array)[0])))

#define TEST_SECTION(name) pr_info("\n=== " name " ===\n")

#define TEST_BEGIN(name)                                                       \
	int __test_ret = 0;                                                    \
	pr_info("    [CASE] %s ... ", name)

#define TEST_END(name)                                                         \
	do {                                                                   \
		pr_info("PASS\n");                                             \
		__test_ret = 0;                                                \
	} while (0)

#define TEST_FAIL(name, msg)                                                   \
	do {                                                                   \
		pr_err("FAIL\n");                                              \
		pr_err("    [FAIL] %s: %s\n", name, msg);                      \
		__test_ret = -1;                                               \
	} while (0)

#define TEST_ASSERT(cond)                                                      \
	do {                                                                   \
		if (unlikely(!(cond))) {                                       \
			pr_err("    assert %s failed at %s:%d\n", #cond,       \
			       __FILE__, __LINE__);                            \
			goto fail;                                             \
		}                                                              \
	} while (0)

#define TEST_ASSERT_EQ(a, b)                                                   \
	do {                                                                   \
		if (unlikely((a) != (b))) {                                    \
			pr_err("    assert %s == %s failed"                    \
			       " (0x%lx != 0x%lx) at %s:%d\n",                 \
			       #a, #b, (uintptr_t)(a), (uintptr_t)(b),         \
			       __FILE__, __LINE__);                            \
			goto fail;                                             \
		}                                                              \
	} while (0)

#define TEST_ASSERT_NE(a, b)                                                   \
	do {                                                                   \
		if (unlikely((a) == (b))) {                                    \
			pr_err("    assert %s != %s failed"                    \
			       " at %s:%d\n",                                  \
			       #a, #b, __FILE__, __LINE__);                    \
			goto fail;                                             \
		}                                                              \
	} while (0)

#define TEST_ASSERT_NULL(p)                                                    \
	do {                                                                   \
		if (unlikely((p) != NULL)) {                                   \
			pr_err("    assert %s == NULL failed"                  \
			       " (got %p) at %s:%d\n",                         \
			       #p, (void *)(p), __FILE__, __LINE__);           \
			goto fail;                                             \
		}                                                              \
	} while (0)

#define TEST_ASSERT_NOT_NULL(p)                                                \
	do {                                                                   \
		if (unlikely((p) == NULL)) {                                   \
			pr_err("    assert %s != NULL failed"                  \
			       " at %s:%d\n",                                  \
			       #p, __FILE__, __LINE__);                        \
			goto fail;                                             \
		}                                                              \
	} while (0)

#define TEST_ASSERT_ALIGNED(p, align)                                          \
	do {                                                                   \
		if (unlikely((uintptr_t)(p) & ((align) - 1))) {                \
			pr_err("    assert %s aligned to %d failed"            \
			       " (addr=%p) at %s:%d\n",                        \
			       #p, (int)(align), (void *)(p), __FILE__,        \
			       __LINE__);                                      \
			goto fail;                                             \
		}                                                              \
	} while (0)

int kernel_test_run(struct ktest_summary *summary);

#endif
