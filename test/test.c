/*
 * init/test.c - 内核子系统自测
 *
 * 功能：
 *   汇总所有子系统测试函数，由 kernel_test() 统一调用。
 *   每项测试独立运行，失败时打印 FAIL 但不中止内核。
 *
 * 测试覆盖：
 *   - bitmap  : 位图 set/clear/test/find_first_zero 完整操作
 *   - pid     : PID 分配、释放、耗尽、PID 0 保护
 *   - buddy   : 多阶分配/释放、伙伴合并、对齐、OOM、压力循环
 *   - slab    : 多大小分配/释放、零大小处理、压力分配、跨缓存复用
 *   - trap    : trap_frame 结构布局、from_user 辅助函数
 *              （真实 U->S trap 往返测试见 user_trap_test.c）
 *   - task    : task_alloc/free、PID 绑定、canary 完整性、进程树链接
 *   - timer   : jiffies 递增、mtime 单调性、mtimecmp 设置
 *
 * 添加新测试：
 *   1. 编写 static void test_xxx(void) { ... }
 *   2. 在 kernel_test() 中调用
 */

#include <kernel/types.h>
#include <kernel/printk.h>
#include <kernel/test.h>

#include "ktest.h"

/* ---- 测试计数器 ---- */

uint32_t __test_total;
uint32_t __test_passed;
uint32_t __test_failed;

/* ================================================================
 *  公共接口
 * ================================================================ */

/**
 * kernel_test - 运行所有内核自测
 *
 * 在 kernel_main 中、各子系统初始化完成后调用。
 * 每个子系统前后打印分隔线，方便阅读。最后输出汇总。
 */
void kernel_test(void)
{
	__test_total = 0;
	__test_passed = 0;
	__test_failed = 0;

	printk("\n");
	printk("========================================\n");
	printk("        CuteOS Kernel Self-Test         \n");
	printk("========================================\n");

	/* ---- Bitmap ---- */
	TEST_SECTION("Bitmap");
	test_bitmap();
	test_bitmap_find_first_zero();
	test_bitmap_odd_bits();

	/* ---- PID ---- */
	TEST_SECTION("PID");
	test_pid_basic();
	test_pid_exhaust();

	/* ---- Buddy ---- */
	TEST_SECTION("Buddy");
	test_buddy_single_page();
	test_buddy_multi_order();
	test_buddy_merge();
	test_buddy_split();
	test_buddy_stress();

	/* ---- SLAB ---- */
	TEST_SECTION("SLAB");
	test_slab_basic();
	test_slab_cross_cache();
	test_slab_stress();

	/* ---- Trap ---- */
	TEST_SECTION("Trap");
	test_trap_frame_layout();
	test_trap_from_user();
	test_trap_context_layout();
	test_trap_irq_codes();
	test_trap_user_return_task_setup();

	/* ---- Timer ---- */
	TEST_SECTION("Timer");
	test_timer_mtime();
	test_timer_mtimecmp();
	test_timer_jiffies();
	test_timer_constants();

	/* ---- Sync ---- */
	TEST_SECTION("Sync");
	test_atomic_basic();
	test_spinlock_irqsave();
	test_mutex_blocking();

	/* ---- MM/VMA ---- */
	TEST_SECTION("MM/VMA");
	test_mm_vma_merge_adjacent();
	test_mm_vma_munmap_middle_split();
	test_mm_vma_munmap_head_tail_trim();
	test_mm_vma_split_enospc_preserves_layout();
	test_mm_vma_munmap_full_table_edge_trim();
	test_mm_dup_split_vmas();

	/* ---- Task ---- */
	TEST_SECTION("Task");
	test_task_idle();
	test_task_alloc_free();
	test_task_canary();
	test_task_multiple();
	test_task_process_tree();
	test_task_free_null();

	/* ---- Task Resources ---- */
	TEST_SECTION("Task Resources");
	test_files_struct_copy_and_share();
	test_fs_struct_copy_and_share();
	test_sighand_struct_copy_and_share();
	test_signal_struct_pending();

	/* ---- Sched ---- */
	TEST_SECTION("Sched");
	test_sched_init();
	test_sched_enqueue_dequeue();
	test_sched_need_resched();
	test_sched_wakeup_refresh();
	test_sched_boost();

	/* ---- Kernel Thread ---- */
	TEST_SECTION("Kernel Thread");
	test_kernel_thread_basic();
	test_kernel_thread_ctx_setup();

	/* ---- virtio-blk ---- */
	TEST_SECTION("VirtIO-Blk");
	test_virtio_blk();
	test_virtio_blk_errors();

	/* ---- Buffer Cache ---- */
	TEST_SECTION("Buffer Cache");
	test_buffer_cache_basic();
	test_buffer_cache_errors();
	test_buffer_cache_eviction();

	/* ---- 汇总 ---- */
	printk("\n========================================\n");
	printk("  Total: %d  |  Passed: %d  |  Failed: %d\n", (int)__test_total,
	       (int)__test_passed, (int)__test_failed);
	printk("========================================\n");

	if (__test_failed > 0)
		printk("  *** SOME TESTS FAILED ***\n");
	else
		printk("  ALL TESTS PASSED\n");
	printk("\n");
}
