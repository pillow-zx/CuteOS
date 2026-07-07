/*
 * test/test.c - 内核子系统自测入口
 */

#include <kernel/types.h>
#include <kernel/printk.h>
#include <kernel/test.h>

#include "ktest.h"

uint32_t __test_total;
uint32_t __test_passed;
uint32_t __test_failed;

void kernel_test(void)
{
	__test_total = 0;
	__test_passed = 0;
	__test_failed = 0;

	pr_info("\n");
	pr_info("========================================\n");
	pr_info("        CuteOS Kernel Self-Test         \n");
	pr_info("========================================\n");


	TEST_SECTION("Bitmap");
	test_bitmap();
	test_bitmap_find_first_zero();
	test_bitmap_odd_bits();


	TEST_SECTION("Hash Table");
	test_hash_insert_lookup();
	test_hash_collision_delete();


	TEST_SECTION("PID");
	test_pid_basic();
	test_pid_exhaust();


	TEST_SECTION("Buddy");
	test_buddy_single_page();
	test_buddy_multi_order();
	test_buddy_merge();
	test_buddy_split();
	test_buddy_over_order_preserves_free_count();
	test_buddy_multi_order_preserves_free_count();
	test_buddy_stress();


	TEST_SECTION("SLAB");
	test_slab_basic();
	test_slab_cross_cache();
	test_slab_stress();
	test_slab_returns_empty_page_to_buddy();
	test_kmalloc_large_alloc_free();
	test_kzalloc_large_zeroes_requested_size();
	test_kmalloc_oversize_preserves_free_count();


	TEST_SECTION("Trap");
	test_trap_frame_layout();
	test_trap_from_user();
	test_trap_context_layout();
	test_trap_irq_codes();
	test_trap_user_return_task_setup();


	TEST_SECTION("Timer");
	test_timer_mtime();
	test_timer_mtimecmp();
	test_timer_jiffies();
	test_timer_constants();
	test_mtime_deadline_helpers();
	test_waitqueue_timeout_expiry_wakes_task();
	test_waitqueue_timeout_cancel_prevents_wake();
	test_ktimer_arm_cancel_remaining();
	test_ktimer_timer_run_expired_callback();
	test_ktimer_interval_rearms_after_expiry();


	TEST_SECTION("Sync");
	test_atomic_basic();
	test_spinlock_irqsave();
	test_wait_event_interruptible_ready();
	test_wait_event_interruptible_signal();
	test_waitqueue_prepare_finish();
	test_waitqueue_wake_one_fifo();
	test_waitqueue_wake_all();
	test_wait_schedule_until_timeout();
	test_wait_schedule_preserves_early_wakeup();
	test_mutex_blocking();


	TEST_SECTION("Cleanup");
	test_cleanup_free_scope();
	test_cleanup_take_ptr();
	test_cleanup_forget_ptr();
	test_cleanup_guard_scope();
	test_cleanup_with_guard_block();
	test_cleanup_class_helpers();


	TEST_SECTION("MM/VMA");
	test_mm_vma_merge_adjacent();
	test_mm_vma_munmap_middle_split();
	test_mm_vma_munmap_head_tail_trim();
	test_mm_vma_split_enospc_preserves_layout();
	test_mm_vma_munmap_full_table_edge_trim();
	test_mm_dup_split_vmas();
	test_mm_vma_mprotect_split_merge();
	test_mm_vma_mprotect_enospc_preserves_layout();
	test_mm_madvise_supported_hints_are_noop();
	test_mm_move_user_pages_preserves_resident_page();
	test_mm_msync_shared_mapping_writes_back();
	test_mm_exec_file_segment_faults_lazily();
	test_mm_exec_file_segment_zero_fills_tail();
	test_mm_exec_file_segment_split_keeps_offset();
	test_mm_exec_file_segment_trim_keeps_offset();
	test_mm_exec_file_segment_merge_requires_contiguous_offset();
	test_map_page_first_table_oom_rolls_back();
	test_map_page_second_table_oom_rolls_back();
	test_vmalloc_alloc_writable_pages();
	test_vmalloc_vfree_reuses_range();
	test_vmalloc_free_merges_adjacent_ranges();
	test_vmalloc_mapping_failure_rolls_back();


	TEST_SECTION("Task");
	test_task_idle();
	test_task_layout_contract();
	test_cpu_boot_topology();
	test_cpu_current_task_accessors();
	test_task_alloc_free();
	test_task_canary();
	test_task_multiple();
	test_task_process_tree();
	test_task_free_null();


	TEST_SECTION("Task Resources");
	test_files_struct_copy_and_share();
	test_files_struct_copy_preserves_cloexec();
	test_fs_struct_copy_and_share();
	test_sighand_struct_copy_and_share();
	test_signal_struct_pending();
	test_signal_struct_rlimits_copy();


	TEST_SECTION("Syscall Compat");
	test_rlimit_defaults();
	test_vfs_default_poll_masks();
	test_vfs_poll_table_registers_multiple_queues();
	test_vfs_default_ioctl_enotty();
	test_console_tty_line_discipline();
	test_tty_signal_delivery_policy();
	test_signal_rt_sigsetsize_validation();
	test_root_statfs_fields();
	test_pipe2_file_alloc_failure_cleanup();


	TEST_SECTION("fs-at");
	test_fs_at_path_lookup_basics();
	test_fs_at_empty_path_error();
	test_fs_at_mkdir_rmdir_cycle();
	test_fs_at_readlink_not_symlink();
	test_fs_at_lookup_nofollow_on_dir();
	test_fs_at_non_directory_parent_error();
	test_fs_at_openat_regular_file();
	test_fs_mount_ext2_on_directory();
	test_ext2_bgdt_uses_vmalloc_for_large_tables();


	TEST_SECTION("Sched");
	test_sched_init();
	test_sched_enqueue_dequeue();
	test_sched_need_resched();
	test_sched_preempt_count_is_cpu_local();
	test_sched_wakeup_refresh();
	test_sched_boost();


	TEST_SECTION("Kernel Thread");
	test_kernel_thread_basic();
	test_kernel_thread_ctx_setup();


	TEST_SECTION("VirtIO-Blk");
	test_virtio_blk();
	test_virtio_blk_errors();


	TEST_SECTION("Page Cache Metadata");
	test_page_cache_metadata_basic();
	test_page_cache_metadata_errors();
	test_page_cache_block_zero_writeback();
	test_page_cache_metadata_eviction();


	TEST_SECTION("Page Cache");
	test_page_cache_dirty_write_visibility();
	test_page_cache_fsync_inode_scope();
	test_page_cache_raw_alias_fsync();
	test_page_cache_directory_alias_refresh();
	test_page_cache_raw_alias_drop();
	test_page_cache_pressure_eviction();
	test_page_cache_clustered_writeback();
	test_page_cache_indirect_reclaim_progress();
	test_page_cache_truncate_extend_zero_fill();
	test_page_cache_large_offset_rejected();


	pr_info("\n========================================\n");
	pr_info("  Total: %d  |  Passed: %d  |  Failed: %d\n",
		(int)__test_total, (int)__test_passed, (int)__test_failed);
	pr_info("========================================\n");

	if (__test_failed > 0)
		pr_err("  *** SOME TESTS FAILED ***\n");
	else
		pr_info("  ALL TESTS PASSED\n");
	pr_info("\n");
}
