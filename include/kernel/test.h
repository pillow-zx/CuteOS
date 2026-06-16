/*
 * include/kernel/test.h - 内核自测框架
 *
 * 提供一套轻量级测试宏，用于在内核初始化完成后验证各子系统行为。
 * 每项测试独立运行，失败时打印 FAIL 信息但不中止内核运行。
 *
 * 调试宏：
 *   TEST_BEGIN(name)        - 开始一项测试，打印测试名
 *   TEST_END(name)          - 结束一项测试，打印通过信息
 *   TEST_PASS(name)         - 打印某测试通过
 *   TEST_FAIL(name, msg)    - 打印某测试失败信息
 *   TEST_ASSERT(cond)       - 断言，失败则 goto 标签并打印失败信息
 *   TEST_ASSERT_EQ(a, b)    - 断言 a == b
 *   TEST_ASSERT_NE(a, b)    - 断言 a != b
 *   TEST_ASSERT_NULL(p)     - 断言指针为 NULL
 *   TEST_ASSERT_NOT_NULL(p) - 断言指针不为 NULL
 *   TEST_ASSERT_ALIGNED(p, align) - 断言指针按 align 对齐
 *   TEST_SECTION(name)      - 打印子系统测试分节标题
 *
 * 添加新测试：
 *   1. 在 init/test.c 中编写 test_xxx() 函数
 *   2. 在 kernel_test() 中调用
 */

#ifndef _CUTEOS_KERNEL_TEST_H
#define _CUTEOS_KERNEL_TEST_H

#include <kernel/printk.h>
#include <kernel/types.h>
#include <kernel/compiler.h>

/* ---- 测试计数器（在 test.c 中定义） ---- */

extern uint32_t __test_total;
extern uint32_t __test_passed;
extern uint32_t __test_failed;

/* ---- 分节与标题 ---- */

/**
 * TEST_SECTION - 打印子系统分节标题
 * @name: 子系统名称
 *
 * 用于 kernel_test() 中各子系统之间的视觉分隔。
 */
#define TEST_SECTION(name) printk("\n=== " name " ===\n")

/**
 * TEST_BEGIN - 开始一项测试用例
 * @name: 测试用例名称（字符串字面量）
 *
 * 打印测试开始标记。
 */
#define TEST_BEGIN(name) printk("  [TEST] %s ... ", name)

/**
 * TEST_END - 结束一项测试用例（通过）
 * @name: 测试用例名称（字符串字面量）
 *
 * 打印通过标记并递增计数器。
 */
#define TEST_END(name)                                                         \
	do {                                                                   \
		printk("PASS\n");                                              \
		__test_passed++;                                               \
		__test_total++;                                                \
	} while (0)

/**
 * TEST_PASS - 快速打印通过信息
 * @name: 测试名称
 */
#define TEST_PASS(name) printk("  [PASS] %s\n", name)

/**
 * TEST_FAIL - 快速打印失败信息
 * @name: 测试名称
 * @msg:  失败原因
 */
#define TEST_FAIL(name, msg)                                                   \
	do {                                                                   \
		printk("FAIL\n");                                              \
		printk("    [FAIL] %s: %s\n", name, msg);                      \
		__test_failed++;                                               \
		__test_total++;                                                \
	} while (0)

/* ---- 断言宏 ----
 *
 * 这些宏使用 goto 跳转到测试函数末尾的 fail 标签。
 * 使用前需在函数内定义 `fail:` 标签：
 *
 *   static void test_xxx(void) {
 *       ...
 *       TEST_ASSERT(cond);
 *       ...
 *       TEST_END("xxx");
 *       return;
 *   fail:
 *       TEST_FAIL("xxx", "unexpected");
 *   }
 *
 * 注意：TEST_ASSERT 不能在同一个函数中跨越 TEST_END 使用，
 *       因为 TEST_END 会递增 __test_passed。
 */

/**
 * TEST_ASSERT - 断言条件为真
 * @cond: 条件表达式
 *
 * 若条件为假，打印文件名/行号并跳转到 fail 标签。
 */
#define TEST_ASSERT(cond)                                                      \
	do {                                                                   \
		if (unlikely(!(cond))) {                                       \
			printk("FAIL\n");                                      \
			printk("    assert %s failed at %s:%d\n", #cond,       \
			       __FILE__, __LINE__);                            \
			__test_failed++;                                       \
			__test_total++;                                        \
			goto fail;                                             \
		}                                                              \
	} while (0)

/**
 * TEST_ASSERT_EQ - 断言两个值相等
 * @a: 左操作数
 * @b: 右操作数
 */
#define TEST_ASSERT_EQ(a, b)                                                   \
	do {                                                                   \
		if (unlikely((a) != (b))) {                                    \
			printk("FAIL\n");                                      \
			printk("    assert %s == %s failed"                    \
			       " (0x%lx != 0x%lx) at %s:%d\n",                 \
			       #a, #b, (uintptr_t)(a), (uintptr_t)(b),         \
			       __FILE__, __LINE__);                            \
			__test_failed++;                                       \
			__test_total++;                                        \
			goto fail;                                             \
		}                                                              \
	} while (0)

/**
 * TEST_ASSERT_NE - 断言两个值不相等
 * @a: 左操作数
 * @b: 右操作数
 */
#define TEST_ASSERT_NE(a, b)                                                   \
	do {                                                                   \
		if (unlikely((a) == (b))) {                                    \
			printk("FAIL\n");                                      \
			printk("    assert %s != %s failed"                    \
			       " at %s:%d\n",                                  \
			       #a, #b, __FILE__, __LINE__);                    \
			__test_failed++;                                       \
			__test_total++;                                        \
			goto fail;                                             \
		}                                                              \
	} while (0)

/**
 * TEST_ASSERT_NULL - 断言指针为 NULL
 * @p: 指针表达式
 */
#define TEST_ASSERT_NULL(p)                                                    \
	do {                                                                   \
		if (unlikely((p) != NULL)) {                                   \
			printk("FAIL\n");                                      \
			printk("    assert %s == NULL failed"                  \
			       " (got %p) at %s:%d\n",                         \
			       #p, (void *)(p), __FILE__, __LINE__);           \
			__test_failed++;                                       \
			__test_total++;                                        \
			goto fail;                                             \
		}                                                              \
	} while (0)

/**
 * TEST_ASSERT_NOT_NULL - 断言指针不为 NULL
 * @p: 指针表达式
 */
#define TEST_ASSERT_NOT_NULL(p)                                                \
	do {                                                                   \
		if (unlikely((p) == NULL)) {                                   \
			printk("FAIL\n");                                      \
			printk("    assert %s != NULL failed"                  \
			       " at %s:%d\n",                                  \
			       #p, __FILE__, __LINE__);                        \
			__test_failed++;                                       \
			__test_total++;                                        \
			goto fail;                                             \
		}                                                              \
	} while (0)

/**
 * TEST_ASSERT_ALIGNED - 断言指针按指定对齐
 * @p:     指针表达式
 * @align: 对齐值（必须是 2 的幂）
 */
#define TEST_ASSERT_ALIGNED(p, align)                                          \
	do {                                                                   \
		if (unlikely((uintptr_t)(p) & ((align) - 1))) {                \
			printk("FAIL\n");                                      \
			printk("    assert %s aligned to %d failed"            \
			       " (addr=%p) at %s:%d\n",                        \
			       #p, (int)(align), (void *)(p), __FILE__,        \
			       __LINE__);                                      \
			__test_failed++;                                       \
			__test_total++;                                        \
			goto fail;                                             \
		}                                                              \
	} while (0)

/* ---- 公共接口 ---- */

/**
 * kernel_test - 运行所有内核自测
 *
 * 在 kernel_main 中、各子系统初始化完成后调用。
 * 最后打印汇总：总测试数、通过数、失败数。
 */
void kernel_test(void);

#endif
