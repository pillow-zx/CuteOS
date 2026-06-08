/*
 * init/test.c - 内核子系统自测
 *
 * 功能：
 *   汇总所有子系统测试函数，由 kernel_test() 统一调用。
 *   每项测试独立运行，失败时打印 FAIL 但不中止内核。
 *
 * 添加新测试：
 *   1. 编写 static void test_xxx(void) { ... }
 *   2. 在 kernel_test() 中调用
 */

#include <kernel/test.h>
#include <kernel/printk.h>
#include <kernel/string.h>
#include <kernel/slab.h>
#include <kernel/buddy.h>
#include <asm/page.h>

/* ---- SLAB 测试 ---- */

/**
 * test_slab - 测试 kmalloc/kfree 各种大小
 *
 * 流程：分配 8 种大小 → memset → 释放 → 再分配 → 再释放。
 * 验证 free_list 回收与再分配路径正常。
 */
static void test_slab(void)
{
#define NR_CACHES 8
        static const size_t sizes[NR_CACHES] = {16,  32,  64,   128,
                                                256, 512, 1024, 2048};
        void *ptrs[NR_CACHES];

        /* Phase 1: 分配各大小并写入模式 */
        for (int i = 0; i < NR_CACHES; i++) {
                ptrs[i] = kmalloc(sizes[i]);
                if (!ptrs[i]) {
                        printk("  FAIL: kmalloc(%d) returned NULL\n",
                               (int)sizes[i]);
                        return;
                }
                memset(ptrs[i], 0xAA, sizes[i]);
        }

        /* Phase 2: 全部释放 */
        for (int i = 0; i < NR_CACHES; i++)
                kfree(ptrs[i]);

        /* Phase 3: 再次分配（应从 free_list 取回） */
        for (int i = 0; i < NR_CACHES; i++) {
                ptrs[i] = kmalloc(sizes[i]);
                if (!ptrs[i]) {
                        printk("  FAIL: re-kmalloc(%d) returned NULL\n",
                               (int)sizes[i]);
                        return;
                }
        }

        /* Phase 4: 再次释放 */
        for (int i = 0; i < NR_CACHES; i++)
                kfree(ptrs[i]);

        printk("  slab: passed\n");
#undef NR_CACHES
}

/* ---- 公共接口 ---- */

/**
 * kernel_test - 运行所有内核自测
 *
 * 在 kernel_main 中、各子系统初始化完成后调用。
 * 每个子测试前后打印分隔线，方便阅读。
 */
void kernel_test(void)
{
        test_slab();
}
