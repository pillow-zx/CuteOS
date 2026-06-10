#ifndef _CUTEOS_TEST_KTEST_H
#define _CUTEOS_TEST_KTEST_H

/* ================================================================
 *  Bitmap 测试
 * ================================================================
 */

void test_bitmap(void);
void test_bitmap_find_first_zero(void);
void test_bitmap_odd_bits(void);

/* ================================================================
 *  PID 分配器测试
 * ================================================================
 */

void test_pid_basic(void);
void test_pid_exhaust(void);

/* ================================================================
 *  Buddy 分配器测试
 * ================================================================
 */

void test_buddy_single_page(void);
void test_buddy_multi_order(void);
void test_buddy_merge(void);
void test_buddy_stress(void);
void test_buddy_split(void);

/* ================================================================
 *  SLAB 分配器测试
 * ================================================================
 */

void test_slab_basic(void);
void test_slab_cross_cache(void);
void test_slab_stress(void);

/* ================================================================
 *  Trap 测试
 * ================================================================
 */

void test_trap_frame_layout(void);
void test_trap_from_user(void);
void test_trap_context_layout(void);
void test_trap_irq_codes(void);
void test_trap_user_return_task_setup(void);

/* ================================================================
 *  Timer 测试
 * ================================================================
 */

void test_timer_mtime(void);
void test_timer_mtimecmp(void);
void test_timer_jiffies(void);
void test_timer_constants(void);

/* ================================================================
 *  Task 管理测试
 * ================================================================
 */

void test_task_alloc_free(void);
void test_task_canary(void);
void test_task_multiple(void);
void test_task_process_tree(void);
void test_task_idle(void);
void test_task_free_null(void);

/* ================================================================
 *  Sched 调度器测试
 * ================================================================
 */

void test_sched_init(void);
void test_sched_enqueue_dequeue(void);
void test_sched_need_resched(void);

/* ================================================================
 *  Kernel Thread 测试
 * ================================================================
 */

void test_kernel_thread_basic(void);
void test_kernel_thread_ctx_setup(void);

/* ================================================================
 *  virtio-blk 驱动测试
 * ================================================================ */

void test_virtio_blk(void);

#endif
