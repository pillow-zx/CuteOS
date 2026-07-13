/*
 * test/test.c - kernel self-test runner
 */

#include <kernel/irq.h>
#include <kernel/buddy.h>
#include <kernel/printk.h>
#include <kernel/test.h>
#include <kernel/types.h>

#include "ktest.h"

static const struct ktest_case arch_interface_cases[] = {
	KTEST_CASE(test_arch_interface_static_contracts),
};

static const struct ktest_case bitmap_cases[] = {
	KTEST_CASE(test_bitmap),
	KTEST_CASE(test_bitmap_find_first_zero),
	KTEST_CASE(test_bitmap_odd_bits),
};

static const struct ktest_case hash_cases[] = {
	KTEST_CASE(test_hash_insert_lookup),
	KTEST_CASE(test_hash_collision_delete),
};

static const struct ktest_case cleanup_cases[] = {
	KTEST_CASE(test_cleanup_free_scope),
	KTEST_CASE(test_cleanup_take_ptr),
	KTEST_CASE(test_cleanup_forget_ptr),
	KTEST_CASE(test_cleanup_guard_scope),
	KTEST_CASE(test_cleanup_with_guard_block),
	KTEST_CASE(test_cleanup_class_helpers),
	KTEST_CASE(test_cleanup_kfree_scope),
};

static const struct ktest_case buddy_cases[] = {
	KTEST_CASE(test_buddy_single_page),
	KTEST_CASE(test_buddy_multi_order),
	KTEST_CASE(test_buddy_merge),
	KTEST_CASE(test_buddy_split),
	KTEST_CASE(test_buddy_over_order_preserves_free_count),
	KTEST_CASE(test_buddy_multi_order_preserves_free_count),
	KTEST_CASE(test_buddy_stress),
};

static const struct ktest_case slab_cases[] = {
	KTEST_CASE(test_slab_basic),
	KTEST_CASE(test_slab_cross_cache),
	KTEST_CASE(test_slab_stress),
	KTEST_CASE(test_slab_returns_empty_page_to_buddy),
	KTEST_CASE(test_kmalloc_large_alloc_free),
	KTEST_CASE(test_kzalloc_large_zeroes_requested_size),
	KTEST_CASE(test_kmalloc_oversize_preserves_free_count),
};

static const struct ktest_case vmalloc_cases[] = {
	KTEST_CASE(test_vmalloc_alloc_writable_pages),
	KTEST_CASE(test_vmalloc_vfree_reuses_range),
	KTEST_CASE(test_vmalloc_free_merges_adjacent_ranges),
	KTEST_CASE(test_vmalloc_mapping_failure_rolls_back),
};

static const struct ktest_case pagetable_cases[] = {
	KTEST_CASE(test_map_page_first_table_oom_rolls_back),
	KTEST_CASE(test_map_page_second_table_oom_rolls_back),
};

static const struct ktest_case vma_cases[] = {
	KTEST_CASE(test_mm_vma_merge_adjacent),
	KTEST_CASE(test_mm_vma_munmap_middle_split),
	KTEST_CASE(test_mm_vma_munmap_head_tail_trim),
	KTEST_CASE(test_mm_vma_split_enospc_preserves_layout),
	KTEST_CASE(test_mm_vma_munmap_full_table_edge_trim),
	KTEST_CASE(test_mm_dup_split_vmas),
	KTEST_CASE(test_mm_vma_mprotect_split_merge),
	KTEST_CASE(test_mm_vma_mprotect_enospc_preserves_layout),
	KTEST_CASE(test_mm_madvise_supported_hints_are_noop),
	KTEST_CASE(test_mm_move_user_pages_preserves_resident_page),
	KTEST_CASE(test_mm_msync_shared_mapping_writes_back),
	KTEST_CASE(test_mm_sparse_shared_mapping_writes_back),
};

static const struct ktest_case exec_mapping_cases[] = {
	KTEST_CASE(test_mm_exec_file_segment_faults_lazily),
	KTEST_CASE(test_mm_exec_file_segment_zero_fills_tail),
	KTEST_CASE(test_mm_exec_file_segment_split_keeps_offset),
	KTEST_CASE(test_mm_exec_file_segment_trim_keeps_offset),
	KTEST_CASE(test_mm_exec_file_segment_merge_requires_contiguous_offset),
};

static const struct ktest_case pid_cases[] = {
	KTEST_CASE(test_pid_basic),
	KTEST_CASE(test_pid_exhaust),
};

static const struct ktest_case task_cases[] = {
	KTEST_CASE(test_task_idle),
	KTEST_CASE(test_task_layout_contract),
	KTEST_CASE(test_cpu_boot_topology),
	KTEST_CASE(test_cpu_current_task_accessors),
	KTEST_CASE(test_task_alloc_free),
	KTEST_CASE(test_task_multiple),
	KTEST_CASE(test_task_process_tree),
	KTEST_CASE(test_task_free_null),
};

static const struct ktest_case resource_cases[] = {
	KTEST_CASE(test_files_struct_copy_and_share),
	KTEST_CASE(test_files_struct_copy_preserves_cloexec),
	KTEST_CASE(test_fs_struct_copy_and_share),
	KTEST_CASE(test_sighand_struct_copy_and_share),
	KTEST_CASE(test_signal_struct_pending),
	KTEST_CASE(test_signal_struct_rlimits_copy),
};

static const struct ktest_case sched_cases[] = {
	KTEST_CASE(test_sched_init),
	KTEST_CASE(test_sched_enqueue_dequeue),
	KTEST_CASE(test_sched_need_resched),
	KTEST_CASE(test_sched_preempt_count_is_cpu_local),
	KTEST_CASE(test_sched_wakeup_refresh),
	KTEST_CASE(test_sched_boost),
};

static const struct ktest_case kthread_cases[] = {
	KTEST_CASE(test_kernel_thread_basic),
	KTEST_CASE(test_kernel_thread_ctx_setup),
};

static const struct ktest_case timer_cases[] = {
	KTEST_CASE(test_timer_mtime),
	KTEST_CASE(test_timer_mtimecmp),
	KTEST_CASE(test_timer_jiffies),
	KTEST_CASE(test_timer_constants),
	KTEST_CASE(test_mtime_deadline_helpers),
};

static const struct ktest_case ktimer_cases[] = {
	KTEST_CASE(test_ktimer_arm_cancel_remaining),
	KTEST_CASE(test_ktimer_timer_run_expired_callback),
	KTEST_CASE(test_ktimer_interval_rearms_after_expiry),
};

static const struct ktest_case waitqueue_cases[] = {
	KTEST_CASE(test_wait_for_timeout),
	KTEST_CASE(test_wait_for_event),
	KTEST_CASE(test_wait_for_spurious_retry),
	KTEST_CASE(test_wait_for_priority),
	KTEST_CASE(test_wait_for_wake_before_block),
	KTEST_CASE(test_wait_for_registration),
	KTEST_CASE(test_wait_for_partial_error_cleanup),
	KTEST_CASE(test_wait_for_signal_only),
	KTEST_CASE(test_wait_for_validation),
};

static const struct ktest_case sync_cases[] = {
	KTEST_CASE(test_atomic_basic),
	KTEST_CASE(test_spinlock_irqsave),
};

static const struct ktest_case mutex_cases[] = {
	KTEST_CASE(test_mutex_blocking),
	KTEST_CASE(test_mutex_uncontended_preserves_sleep_state),
};

static const struct ktest_case trap_cases[] = {
	KTEST_CASE(test_trap_frame_layout),
	KTEST_CASE(test_trap_from_user),
	KTEST_CASE(test_trap_context_layout),
	KTEST_CASE(test_trap_irq_codes),
	KTEST_CASE(test_trap_user_exception_classification),
	KTEST_CASE(test_signal_riscv_frame_abi),
};

static const struct ktest_case user_return_cases[] = {
	KTEST_CASE(test_user_return_work_ecall_path),
	KTEST_CASE(test_user_return_work_page_fault_path),
	KTEST_CASE(test_user_return_work_timer_path),
};

static const struct ktest_case user_trap_cases[] = {
	KTEST_CASE(test_trap_user_return_task_setup),
};

static const struct ktest_case fs_at_cases[] = {
	KTEST_CASE(test_fs_at_path_lookup_basics),
	KTEST_CASE(test_fs_at_empty_path_error),
	KTEST_CASE(test_fs_at_mkdir_rmdir_cycle),
	KTEST_CASE(test_fs_at_readlink_not_symlink),
	KTEST_CASE(test_fs_at_lookup_nofollow_on_dir),
	KTEST_CASE(test_fs_at_non_directory_parent_error),
	KTEST_CASE(test_fs_at_openat_regular_file),
	KTEST_CASE(test_fs_mount_ext2_on_directory),
	KTEST_CASE(test_ext2_bgdt_uses_vmalloc_for_large_tables),
};

static const struct ktest_case vfs_root_cases[] = {
	KTEST_CASE(test_vfs_root_autodetect_missing_device),
	KTEST_CASE(test_vfs_root_autodetect_no_match),
	KTEST_CASE(test_vfs_root_autodetect_single_match),
	KTEST_CASE(test_vfs_root_autodetect_ambiguous_match),
	KTEST_CASE(test_vfs_root_autodetect_probe_error),
	KTEST_CASE(test_vfs_root_autodetect_skips_no_probe),
};

static const struct ktest_case page_cache_metadata_cases[] = {
	KTEST_CASE(test_page_cache_metadata_basic),
	KTEST_CASE(test_page_cache_metadata_errors),
	KTEST_CASE(test_page_cache_block_zero_writeback),
	KTEST_CASE(test_page_cache_metadata_eviction),
};

static const struct ktest_case page_cache_cases[] = {
	KTEST_CASE(test_page_cache_dirty_write_visibility),
	KTEST_CASE(test_page_cache_physical_key_identity),
	KTEST_CASE(test_page_cache_writeback_retry),
	KTEST_CASE(test_page_cache_fsync_inode_scope),
	KTEST_CASE(test_vfs_datasync_metadata_policy),
	KTEST_CASE(test_page_cache_datasync_skips_pure_inode_metadata),
	KTEST_CASE(test_page_cache_raw_alias_fsync),
	KTEST_CASE(test_page_cache_directory_alias_refresh),
	KTEST_CASE(test_page_cache_raw_alias_drop),
	KTEST_CASE(test_page_cache_pressure_eviction),
	KTEST_CASE(test_page_cache_clustered_writeback),
	KTEST_CASE(test_page_cache_indirect_reclaim_progress),
	KTEST_CASE(test_page_cache_truncate_extend_zero_fill),
	KTEST_CASE(test_page_cache_large_offset_rejected),
};

static const struct ktest_case virtio_blk_cases[] = {
	KTEST_CASE(test_virtio_blk),
	KTEST_CASE(test_virtio_blk_errors),
};

static const struct ktest_case syscall_compat_cases[] = {
	KTEST_CASE(test_rlimit_defaults),
	KTEST_CASE(test_vfs_default_poll_masks),
	KTEST_CASE(test_vfs_poll_propagates_session_errors),
	KTEST_CASE(test_vfs_default_ioctl_enotty),
	KTEST_CASE(test_console_tty_line_discipline),
	KTEST_CASE(test_tty_signal_delivery_policy),
	KTEST_CASE(test_tty_console_job_control_policy),
	KTEST_CASE(test_signal_rt_sigsetsize_validation),
	KTEST_CASE(test_root_statfs_fields),
	KTEST_CASE(test_pipe2_file_alloc_failure_cleanup),
};

#define KTEST_MODULE(module_name, module_cases)                                \
	{                                                                      \
		.name = module_name, .cases = module_cases,                    \
		.nr_cases = KTEST_ARRAY_SIZE(module_cases),                    \
	}

static const struct ktest_module core_bitmap_module =
	KTEST_MODULE("bitmap", bitmap_cases);
static const struct ktest_module core_hash_module =
	KTEST_MODULE("hash", hash_cases);
static const struct ktest_module core_cleanup_module =
	KTEST_MODULE("cleanup", cleanup_cases);

static const struct ktest_module memory_buddy_module =
	KTEST_MODULE("buddy", buddy_cases);
static const struct ktest_module memory_slab_module =
	KTEST_MODULE("slab", slab_cases);
static const struct ktest_module memory_vmalloc_module =
	KTEST_MODULE("vmalloc", vmalloc_cases);
static const struct ktest_module memory_pagetable_module =
	KTEST_MODULE("pagetable", pagetable_cases);
static const struct ktest_module memory_vma_module =
	KTEST_MODULE("vma", vma_cases);
static const struct ktest_module memory_exec_mapping_module =
	KTEST_MODULE("exec_mapping", exec_mapping_cases);

static const struct ktest_module process_pid_module =
	KTEST_MODULE("pid", pid_cases);
static const struct ktest_module process_task_module =
	KTEST_MODULE("task", task_cases);
static const struct ktest_module process_resources_module =
	KTEST_MODULE("resources", resource_cases);
static const struct ktest_module process_sched_module =
	KTEST_MODULE("sched", sched_cases);
static const struct ktest_module process_kthread_module =
	KTEST_MODULE("kthread", kthread_cases);

static const struct ktest_module time_timer_module =
	KTEST_MODULE("timer", timer_cases);
static const struct ktest_module time_ktimer_module =
	KTEST_MODULE("ktimer", ktimer_cases);
static const struct ktest_module time_waitqueue_module =
	KTEST_MODULE("waitqueue", waitqueue_cases);
static const struct ktest_module time_sync_module =
	KTEST_MODULE("sync", sync_cases);
static const struct ktest_module time_mutex_module =
	KTEST_MODULE("mutex", mutex_cases);

static const struct ktest_module trap_arch_interface_module =
	KTEST_MODULE("arch_interface", arch_interface_cases);
static const struct ktest_module trap_trap_module =
	KTEST_MODULE("trap", trap_cases);
static const struct ktest_module trap_user_return_module =
	KTEST_MODULE("user_return", user_return_cases);
static const struct ktest_module trap_user_trap_module =
	KTEST_MODULE("user_trap", user_trap_cases);

static const struct ktest_module io_fs_at_module =
	KTEST_MODULE("fs_at", fs_at_cases);
static const struct ktest_module io_vfs_root_module =
	KTEST_MODULE("vfs_root", vfs_root_cases);
static const struct ktest_module io_page_cache_metadata_module =
	KTEST_MODULE("page_cache_metadata", page_cache_metadata_cases);
static const struct ktest_module io_page_cache_module =
	KTEST_MODULE("page_cache", page_cache_cases);
static const struct ktest_module io_virtio_blk_module =
	KTEST_MODULE("virtio_blk", virtio_blk_cases);

static const struct ktest_module abi_syscall_compat_module =
	KTEST_MODULE("syscall_compat", syscall_compat_cases);

static const struct ktest_module *const core_modules[] = {
	&core_bitmap_module,
	&core_hash_module,
	&core_cleanup_module,
};

static const struct ktest_module *const memory_modules[] = {
	&memory_buddy_module,
	&memory_slab_module,
	&memory_vmalloc_module,
	&memory_pagetable_module,
	&memory_vma_module,
	&memory_exec_mapping_module,
};

static const struct ktest_module *const process_modules[] = {
	&process_pid_module,
	&process_task_module,
	&process_resources_module,
	&process_sched_module,
	&process_kthread_module,
};

static const struct ktest_module *const time_sync_modules[] = {
	&time_timer_module,
	&time_ktimer_module,
	&time_waitqueue_module,
	&time_sync_module,
	&time_mutex_module,
};

static const struct ktest_module *const trap_modules[] = {
	&trap_arch_interface_module,
	&trap_trap_module,
	&trap_user_return_module,
	&trap_user_trap_module,
};

static const struct ktest_module *const io_modules[] = {
	&io_fs_at_module,
	&io_vfs_root_module,
	&io_page_cache_metadata_module,
	&io_page_cache_module,
	&io_virtio_blk_module,
};

static const struct ktest_module *const abi_modules[] = {
	&abi_syscall_compat_module,
};

#define KTEST_SUBSYSTEM(subsystem_name, subsystem_modules)                     \
	{                                                                      \
		.name = subsystem_name, .modules = subsystem_modules,          \
		.nr_modules = KTEST_ARRAY_SIZE(subsystem_modules),             \
	}

static const struct ktest_subsystem ktest_subsystems[] = {
	KTEST_SUBSYSTEM("core", core_modules),
	KTEST_SUBSYSTEM("memory", memory_modules),
	KTEST_SUBSYSTEM("process", process_modules),
	KTEST_SUBSYSTEM("time-sync", time_sync_modules),
	KTEST_SUBSYSTEM("trap", trap_modules),
	KTEST_SUBSYSTEM("io", io_modules),
	KTEST_SUBSYSTEM("abi", abi_modules),
};

static int ktest_run_module(const struct ktest_subsystem *subsystem,
			    const struct ktest_module *module,
			    struct ktest_summary *summary)
{
	uint32_t failed_cases = 0;

	pr_info("[KTEST] module %s/%s\n", subsystem->name, module->name);

	for (uint32_t i = 0; i < module->nr_cases; i++) {
		const struct ktest_case *test = &module->cases[i];
		int ret;

		summary->cases++;
		local_irq_disable();
		ret = test->run();
		local_irq_disable();
		buddy_test_validate();
		if (ret < 0) {
			failed_cases++;
			summary->failed_cases++;
			pr_err("    [FAIL] %s/%s/%s returned %d\n",
			       subsystem->name, module->name, test->name, ret);
		}
	}

	summary->modules++;
	if (failed_cases > 0) {
		summary->failed_modules++;
		pr_err("[FAIL] %s\n", module->name);
		return -1;
	}

	pr_info("[PASS] %s\n", module->name);
	return 0;
}

int kernel_test_run(struct ktest_summary *summary)
{
	struct ktest_summary result = { 0 };
	irq_flags_t flags;

	pr_info("\n");
	pr_info("========================================\n");
	pr_info("        CuteOS Kernel Self-Test         \n");
	pr_info("========================================\n");

	flags = local_irq_save();
	for (uint32_t i = 0; i < KTEST_ARRAY_SIZE(ktest_subsystems); i++) {
		const struct ktest_subsystem *subsystem = &ktest_subsystems[i];

		result.subsystems++;
		pr_info("[KTEST] subsystem %s\n", subsystem->name);
		for (uint32_t j = 0; j < subsystem->nr_modules; j++)
			ktest_run_module(subsystem, subsystem->modules[j],
					 &result);
	}

	if (summary)
		*summary = result;

	local_irq_restore(flags);
	return result.failed_cases ? -1 : 0;
}
