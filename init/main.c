/*
 * init/main.c - kernel_main() 内核初始化入口
 */

#include <kernel/printk.h>
#include <kernel/buddy.h>
#include <kernel/init.h>
#include <kernel/slab.h>
#include <kernel/page_cache.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <kernel/timer.h>
#include <kernel/tty.h>
#include <kernel/syscall.h>
#include <kernel/signal.h>
#include <kernel/user_map.h>
#include <kernel/vmalloc.h>
#include <kernel/vfs.h>
#include <drivers/console.h>
#include <drivers/virtio_blk.h>
#include <kernel/trap.h>
#include <kernel/processor.h>
#include <kernel/pgtable.h>
#include <kernel/user_map_arch.h>

#ifdef KERNEL_SELFTEST
#include <arch/sbi.h>
#include <kernel/test.h>
#endif

#ifdef KERNEL_SELFTEST
static void kernel_selftest_thread(void *arg)
{
	struct ktest_summary test_summary;
	int ret;

	(void)arg;

	ret = kernel_test_run(&test_summary);
	pr_info("[KTEST] done modules=%u failed_modules=%u cases=%u "
		"failed_cases=%u\n",
		test_summary.modules, test_summary.failed_modules,
		test_summary.cases, test_summary.failed_cases);
	if (ret < 0)
		pr_err("[KTEST] result failed\n");
	else
		pr_info("[KTEST] result passed\n");
	sbi_shutdown();
}
#endif

void kernel_main(void)
{
#ifdef KERNEL_SELFTEST
	struct task_struct *selftest;
#else
	struct task_struct *init;
	struct task_struct *writeback;
#endif
	int ret;

	console_init_sbi();

	pr_info("\n");
	pr_info("  /$$$$$$              /$$                /$$$$$$   /$$$$$$ \n");
	pr_info(" /$$__  $$            | $$               /$$__  $$ /$$__  $$\n");
	pr_info("| $$  \\__/ /$$   /$$ /$$$$$$    /$$$$$$ | $$  \\ $$| $$  \\__/\n");
	pr_info("| $$      | $$  | $$|_  $$_/   /$$__  $$| $$  | $$|  $$$$$$ \n");
	pr_info("| $$      | $$  | $$  | $$    | $$$$$$$$| $$  | $$ \\____  $$\n");
	pr_info("| $$    $$| $$  | $$  | $$ /$$| $$_____/| $$  | $$ /$$  \\ $$\n");
	pr_info("|  $$$$$$/|  $$$$$$/  |  $$$$/|  $$$$$$$|  $$$$$$/|  $$$$$$/\n");
	pr_info(" \\______/  \\______/    \\___/   \\_______/ \\______/  \\______/ \n");
	pr_info("\n");

	pagetable_init();
	console_init_mmio();
	console_chrdev_init();
	pr_info("uart: init successfully\n");

	buddy_init();
	pagetable_use_buddy();
	slab_init();
	vmalloc_init();
	user_map_init();
	BUG_ON(user_map_reserve("stack_guard", USER_STACK_GUARD_BASE,
				USER_STACK_BASE) < 0);
	signal_user_map_init();
	pr_info("mm: init successfully\n");

	trap_init();
	pr_info("trap: init successfully\n");

	task_init();
	pr_info("task: init successfully\n");

	arch_timer_init();
	pr_info("timer: init successfully\n");

	sched_init();
	pr_info("sched: init successfully\n");

	syscall_init();
	pr_info("syscall: init successfully\n");

	vfs_init();
	pr_info("vfs: init successfully\n");

	ret = filesystems_init();
	if (ret < 0)
		panic("filesystems: init failed (%d)", ret);
	pr_info("filesystems: init successfully\n");

	virtio_blk_init();
	ret = vfs_mount_root(ROOT_DEV);
	if (ret < 0)
		panic("VFS: root mount failed (%d)", ret);

#ifdef KERNEL_SELFTEST
	selftest = kernel_thread(kernel_selftest_thread, NULL);
	BUG_ON(!selftest);
#else
	init = kernel_thread(init_process, NULL);
	BUG_ON(!init);
	set_init_task(init);
	tty_console_init_session(init);

	writeback = kernel_thread(page_cache_wb_thread, NULL);
	BUG_ON(!writeback);
#endif

	while (true) {
		schedule();
		wait_for_interrupt();
	}
}
