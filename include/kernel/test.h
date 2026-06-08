/*
 * include/kernel/test.h - 内核自测框架
 *
 * 声明 kernel_test()，在内核各子系统初始化完成后调用，
 * 依次运行所有已注册的子系统测试。每项测试独立，
 * 失败时打印 FAIL 信息但不中止内核运行。
 *
 * 添加新测试：
 *   1. 在 init/test.c 中编写 test_xxx() 函数
 *   2. 在 kernel_test() 中调用
 */

#ifndef _CUTEOS_KERNEL_TEST_H
#define _CUTEOS_KERNEL_TEST_H

void kernel_test(void);

#endif
