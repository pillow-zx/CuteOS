#ifndef _CUTEOS_TEST_KTEST_H
#define _CUTEOS_TEST_KTEST_H

void test_bitmap(void);
void test_bitmap_find_first_zero(void);
void test_bitmap_odd_bits(void);

void test_hash_insert_lookup(void);
void test_hash_collision_delete(void);

void test_pid_basic(void);
void test_pid_exhaust(void);

void test_buddy_single_page(void);
void test_buddy_multi_order(void);
void test_buddy_merge(void);
void test_buddy_stress(void);
void test_buddy_split(void);
void test_buddy_over_order_preserves_free_count(void);
void test_buddy_multi_order_preserves_free_count(void);

void test_slab_basic(void);
void test_slab_cross_cache(void);
void test_slab_stress(void);
void test_slab_returns_empty_page_to_buddy(void);
void test_kmalloc_large_alloc_free(void);
void test_kzalloc_large_zeroes_requested_size(void);
void test_kmalloc_oversize_preserves_free_count(void);

void test_trap_frame_layout(void);
void test_trap_from_user(void);
void test_trap_context_layout(void);
void test_trap_irq_codes(void);
void test_trap_user_return_task_setup(void);
void test_user_return_work_ecall_path(void);
void test_user_return_work_page_fault_path(void);
void test_user_return_work_timer_path(void);

void test_timer_mtime(void);
void test_timer_mtimecmp(void);
void test_timer_jiffies(void);
void test_timer_constants(void);
void test_mtime_deadline_helpers(void);
void test_waitqueue_timeout_expiry_wakes_task(void);
void test_waitqueue_timeout_cancel_prevents_wake(void);
void test_ktimer_arm_cancel_remaining(void);
void test_ktimer_timer_run_expired_callback(void);
void test_ktimer_interval_rearms_after_expiry(void);

void test_atomic_basic(void);
void test_spinlock_irqsave(void);
void test_wait_event_interruptible_ready(void);
void test_wait_event_interruptible_signal(void);
void test_waitqueue_prepare_finish(void);
void test_waitqueue_wake_one_fifo(void);
void test_waitqueue_wake_all(void);
void test_wait_schedule_until_timeout(void);
void test_wait_schedule_preserves_early_wakeup(void);
void test_mutex_blocking(void);

void test_cleanup_free_scope(void);
void test_cleanup_take_ptr(void);
void test_cleanup_forget_ptr(void);
void test_cleanup_guard_scope(void);
void test_cleanup_with_guard_block(void);
void test_cleanup_class_helpers(void);
void test_cleanup_kfree_scope(void);

void test_mm_vma_merge_adjacent(void);
void test_mm_vma_munmap_middle_split(void);
void test_mm_vma_munmap_head_tail_trim(void);
void test_mm_vma_split_enospc_preserves_layout(void);
void test_mm_vma_munmap_full_table_edge_trim(void);
void test_mm_dup_split_vmas(void);
void test_mm_vma_mprotect_split_merge(void);
void test_mm_vma_mprotect_enospc_preserves_layout(void);
void test_mm_madvise_supported_hints_are_noop(void);
void test_mm_move_user_pages_preserves_resident_page(void);
void test_mm_msync_shared_mapping_writes_back(void);
void test_mm_exec_file_segment_faults_lazily(void);
void test_mm_exec_file_segment_zero_fills_tail(void);
void test_mm_exec_file_segment_split_keeps_offset(void);
void test_mm_exec_file_segment_trim_keeps_offset(void);
void test_mm_exec_file_segment_merge_requires_contiguous_offset(void);
void test_map_page_first_table_oom_rolls_back(void);
void test_map_page_second_table_oom_rolls_back(void);
void test_vmalloc_alloc_writable_pages(void);
void test_vmalloc_vfree_reuses_range(void);
void test_vmalloc_free_merges_adjacent_ranges(void);
void test_vmalloc_mapping_failure_rolls_back(void);

void test_task_alloc_free(void);
void test_task_layout_contract(void);
void test_cpu_boot_topology(void);
void test_cpu_current_task_accessors(void);
void test_task_canary(void);
void test_task_multiple(void);
void test_task_process_tree(void);
void test_task_idle(void);
void test_task_free_null(void);

void test_files_struct_copy_and_share(void);
void test_files_struct_copy_preserves_cloexec(void);
void test_fs_struct_copy_and_share(void);
void test_sighand_struct_copy_and_share(void);
void test_signal_struct_pending(void);
void test_signal_struct_rlimits_copy(void);

void test_rlimit_defaults(void);
void test_vfs_default_poll_masks(void);
void test_vfs_poll_table_registers_multiple_queues(void);
void test_vfs_default_ioctl_enotty(void);
void test_console_tty_line_discipline(void);
void test_tty_signal_delivery_policy(void);
void test_signal_rt_sigsetsize_validation(void);
void test_root_statfs_fields(void);
void test_pipe2_file_alloc_failure_cleanup(void);

void test_sched_init(void);
void test_sched_enqueue_dequeue(void);
void test_sched_need_resched(void);
void test_sched_preempt_count_is_cpu_local(void);
void test_sched_wakeup_refresh(void);
void test_sched_boost(void);

void test_kernel_thread_basic(void);
void test_kernel_thread_ctx_setup(void);

void test_virtio_blk(void);
void test_virtio_blk_errors(void);

void test_page_cache_metadata_basic(void);
void test_page_cache_metadata_errors(void);
void test_page_cache_block_zero_writeback(void);
void test_page_cache_metadata_eviction(void);

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

void test_fs_at_path_lookup_basics(void);
void test_fs_at_empty_path_error(void);
void test_fs_at_mkdir_rmdir_cycle(void);
void test_fs_at_readlink_not_symlink(void);
void test_fs_at_lookup_nofollow_on_dir(void);
void test_fs_at_non_directory_parent_error(void);
void test_fs_at_openat_regular_file(void);
void test_fs_mount_ext2_on_directory(void);
void test_ext2_bgdt_uses_vmalloc_for_large_tables(void);

#endif
