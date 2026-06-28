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
 *  Hash Table 测试
 * ================================================================
 */

void test_hash_insert_lookup(void);
void test_hash_collision_delete(void);

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
void test_timer_wait_expiry_wakes_task(void);
void test_timer_wait_cancel_prevents_wake(void);

/* ================================================================
 *  Sync 原语测试
 * ================================================================
 */

void test_atomic_basic(void);
void test_spinlock_irqsave(void);
void test_wait_event_interruptible_ready(void);
void test_wait_event_interruptible_signal(void);
void test_mutex_blocking(void);

/* ================================================================
 *  MM/VMA 测试
 * ================================================================
 */

void test_mm_vma_merge_adjacent(void);
void test_mm_vma_munmap_middle_split(void);
void test_mm_vma_munmap_head_tail_trim(void);
void test_mm_vma_split_enospc_preserves_layout(void);
void test_mm_vma_munmap_full_table_edge_trim(void);
void test_mm_dup_split_vmas(void);
void test_mm_vma_mprotect_split_merge(void);
void test_mm_vma_mprotect_enospc_preserves_layout(void);

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
 *  Task 共享资源测试
 * ================================================================
 */

void test_files_struct_copy_and_share(void);
void test_fs_struct_copy_and_share(void);
void test_sighand_struct_copy_and_share(void);
void test_signal_struct_pending(void);
void test_signal_struct_rlimits_copy(void);

/* ================================================================
 *  Syscall compatibility helper tests
 * ================================================================
 */

void test_rlimit_defaults(void);
void test_vfs_default_poll_masks(void);
void test_vfs_default_ioctl_enotty(void);
void test_console_tty_line_discipline(void);
void test_tty_signal_delivery_policy(void);
void test_root_statfs_fields(void);

/* ================================================================
 *  Sched 调度器测试
 * ================================================================
 */

void test_sched_init(void);
void test_sched_enqueue_dequeue(void);
void test_sched_need_resched(void);
void test_sched_wakeup_refresh(void);
void test_sched_boost(void);

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
void test_virtio_blk_errors(void);

/* ================================================================
 *  Page Cache Metadata 测试
 * ================================================================ */

void test_page_cache_metadata_basic(void);
void test_page_cache_metadata_errors(void);
void test_page_cache_block_zero_writeback(void);
void test_page_cache_metadata_eviction(void);

/* ================================================================
 *  Page Cache 测试
 * ================================================================ */

void test_page_cache_dirty_write_visibility(void);
void test_page_cache_fsync_inode_scope(void);
void test_page_cache_raw_alias_fsync(void);
void test_page_cache_directory_alias_refresh(void);
void test_page_cache_raw_alias_drop(void);
void test_page_cache_pressure_eviction(void);
void test_page_cache_clustered_writeback(void);
void test_page_cache_indirect_reclaim_progress(void);
void test_page_cache_truncate_extend_zero_fill(void);
void test_page_cache_large_offset_rejected(void);

/* ================================================================
 *  fs-at 路径/fd 语义测试
 * ================================================================ */

void test_fs_at_path_lookup_basics(void);
void test_fs_at_empty_path_error(void);
void test_fs_at_mkdir_rmdir_cycle(void);
void test_fs_at_readlink_not_symlink(void);
void test_fs_at_lookup_nofollow_on_dir(void);
void test_fs_at_openat_regular_file(void);

#endif
