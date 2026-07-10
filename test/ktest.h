#ifndef _CUTEOS_TEST_KTEST_H
#define _CUTEOS_TEST_KTEST_H

int test_arch_interface_static_contracts(void);

int test_bitmap(void);
int test_bitmap_find_first_zero(void);
int test_bitmap_odd_bits(void);

int test_hash_insert_lookup(void);
int test_hash_collision_delete(void);

int test_pid_basic(void);
int test_pid_exhaust(void);

int test_buddy_single_page(void);
int test_buddy_multi_order(void);
int test_buddy_merge(void);
int test_buddy_stress(void);
int test_buddy_split(void);
int test_buddy_over_order_preserves_free_count(void);
int test_buddy_multi_order_preserves_free_count(void);

int test_slab_basic(void);
int test_slab_cross_cache(void);
int test_slab_stress(void);
int test_slab_returns_empty_page_to_buddy(void);
int test_kmalloc_large_alloc_free(void);
int test_kzalloc_large_zeroes_requested_size(void);
int test_kmalloc_oversize_preserves_free_count(void);

int test_trap_frame_layout(void);
int test_trap_from_user(void);
int test_trap_context_layout(void);
int test_trap_irq_codes(void);
int test_trap_user_return_task_setup(void);
int test_user_return_work_ecall_path(void);
int test_user_return_work_page_fault_path(void);
int test_user_return_work_timer_path(void);

int test_timer_mtime(void);
int test_timer_mtimecmp(void);
int test_timer_jiffies(void);
int test_timer_constants(void);
int test_mtime_deadline_helpers(void);
int test_waitqueue_timeout_expiry_wakes_task(void);
int test_waitqueue_timeout_cancel_prevents_wake(void);
int test_ktimer_arm_cancel_remaining(void);
int test_ktimer_timer_run_expired_callback(void);
int test_ktimer_interval_rearms_after_expiry(void);

int test_atomic_basic(void);
int test_spinlock_irqsave(void);
int test_wait_event_interruptible_ready(void);
int test_wait_event_interruptible_signal(void);
int test_waitqueue_prepare_finish(void);
int test_waitqueue_wake_one_fifo(void);
int test_waitqueue_wake_all(void);
int test_wait_schedule_until_timeout(void);
int test_wait_schedule_preserves_early_wakeup(void);
int test_mutex_blocking(void);

int test_cleanup_free_scope(void);
int test_cleanup_take_ptr(void);
int test_cleanup_forget_ptr(void);
int test_cleanup_guard_scope(void);
int test_cleanup_with_guard_block(void);
int test_cleanup_class_helpers(void);
int test_cleanup_kfree_scope(void);

int test_mm_vma_merge_adjacent(void);
int test_mm_vma_munmap_middle_split(void);
int test_mm_vma_munmap_head_tail_trim(void);
int test_mm_vma_split_enospc_preserves_layout(void);
int test_mm_vma_munmap_full_table_edge_trim(void);
int test_mm_dup_split_vmas(void);
int test_mm_vma_mprotect_split_merge(void);
int test_mm_vma_mprotect_enospc_preserves_layout(void);
int test_mm_madvise_supported_hints_are_noop(void);
int test_mm_move_user_pages_preserves_resident_page(void);
int test_mm_msync_shared_mapping_writes_back(void);
int test_mm_exec_file_segment_faults_lazily(void);
int test_mm_exec_file_segment_zero_fills_tail(void);
int test_mm_exec_file_segment_split_keeps_offset(void);
int test_mm_exec_file_segment_trim_keeps_offset(void);
int test_mm_exec_file_segment_merge_requires_contiguous_offset(void);
int test_map_page_first_table_oom_rolls_back(void);
int test_map_page_second_table_oom_rolls_back(void);
int test_vmalloc_alloc_writable_pages(void);
int test_vmalloc_vfree_reuses_range(void);
int test_vmalloc_free_merges_adjacent_ranges(void);
int test_vmalloc_mapping_failure_rolls_back(void);

int test_task_alloc_free(void);
int test_task_layout_contract(void);
int test_cpu_boot_topology(void);
int test_cpu_current_task_accessors(void);
int test_task_multiple(void);
int test_task_process_tree(void);
int test_task_idle(void);
int test_task_free_null(void);

int test_files_struct_copy_and_share(void);
int test_files_struct_copy_preserves_cloexec(void);
int test_fs_struct_copy_and_share(void);
int test_sighand_struct_copy_and_share(void);
int test_signal_struct_pending(void);
int test_signal_struct_rlimits_copy(void);

int test_rlimit_defaults(void);
int test_vfs_default_poll_masks(void);
int test_vfs_poll_table_registers_multiple_queues(void);
int test_vfs_default_ioctl_enotty(void);
int test_console_tty_line_discipline(void);
int test_tty_signal_delivery_policy(void);
int test_tty_console_job_control_policy(void);
int test_signal_rt_sigsetsize_validation(void);
int test_root_statfs_fields(void);
int test_pipe2_file_alloc_failure_cleanup(void);

int test_sched_init(void);
int test_sched_enqueue_dequeue(void);
int test_sched_need_resched(void);
int test_sched_preempt_count_is_cpu_local(void);
int test_sched_wakeup_refresh(void);
int test_sched_boost(void);

int test_kernel_thread_basic(void);
int test_kernel_thread_ctx_setup(void);

int test_virtio_blk(void);
int test_virtio_blk_errors(void);

int test_page_cache_metadata_basic(void);
int test_page_cache_metadata_errors(void);
int test_page_cache_block_zero_writeback(void);
int test_page_cache_metadata_eviction(void);

int test_page_cache_dirty_write_visibility(void);
int test_page_cache_fsync_inode_scope(void);
int test_vfs_datasync_metadata_policy(void);
int test_page_cache_datasync_skips_pure_inode_metadata(void);
int test_page_cache_raw_alias_fsync(void);
int test_page_cache_directory_alias_refresh(void);
int test_page_cache_raw_alias_drop(void);
int test_page_cache_pressure_eviction(void);
int test_page_cache_clustered_writeback(void);
int test_page_cache_indirect_reclaim_progress(void);
int test_page_cache_truncate_extend_zero_fill(void);
int test_page_cache_large_offset_rejected(void);

int test_fs_at_path_lookup_basics(void);
int test_fs_at_empty_path_error(void);
int test_fs_at_mkdir_rmdir_cycle(void);
int test_fs_at_readlink_not_symlink(void);
int test_fs_at_lookup_nofollow_on_dir(void);
int test_fs_at_non_directory_parent_error(void);
int test_fs_at_openat_regular_file(void);
int test_fs_mount_ext2_on_directory(void);
int test_ext2_bgdt_uses_vmalloc_for_large_tables(void);

int test_vfs_root_autodetect_missing_device(void);
int test_vfs_root_autodetect_no_match(void);
int test_vfs_root_autodetect_single_match(void);
int test_vfs_root_autodetect_ambiguous_match(void);
int test_vfs_root_autodetect_probe_error(void);
int test_vfs_root_autodetect_skips_no_probe(void);

#endif
