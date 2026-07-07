/*
 * include/kernel/test.h - 内核自测框架
 */

#ifndef _CUTEOS_KERNEL_TEST_H
#define _CUTEOS_KERNEL_TEST_H

#include <kernel/printk.h>
#include <kernel/types.h>
#include <kernel/compiler.h>

extern uint32_t __test_total;
extern uint32_t __test_passed;
extern uint32_t __test_failed;

#define TEST_SECTION(name) pr_info("\n=== " name " ===\n")

#define TEST_BEGIN(name) pr_info("  [TEST] %s ... ", name)

#define TEST_END(name)                                                         \
	do {                                                                   \
		pr_info("PASS\n");                                             \
		__test_passed++;                                               \
		__test_total++;                                                \
	} while (0)

#define TEST_FAIL(name, msg)                                                   \
	do {                                                                   \
		pr_err("FAIL\n");                                              \
		pr_err("    [FAIL] %s: %s\n", name, msg);                      \
		__test_failed++;                                               \
		__test_total++;                                                \
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

void kernel_test(void);

#endif
