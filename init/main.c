/*
 * init/main.c - kernel_main() 内核初始化入口
 *
 * 功能：
 *   内核 C 代码的主入口点。由 arch/riscv/boot.S 在建立临时页表和
 *   开启 MMU 后跳转至此。负责按依赖顺序调用各子系统初始化函数，
 *   建立完整的内核运行环境，最后创建 init 进程并启动调度器。
 *
 * 初始化流程（严格按依赖顺序）：
 *   1. console_init_sbi()       — SBI ecall 控制台，printk 可用
 *   2. pr_info("cuteOS starting...")
 *   3. arch_pt_init()  — 建立正式内核页表（4KB 页 + MMIO mega page）
 *   4. console_init_mmio()      — 切换到 UART MMIO 轮询模式
 *   5. console_chrdev_init()    — 注册 /dev/console 字符设备操作
 *   6. buddy_init()             — 物理页分配器（从 _end 到 DRAM 结束）
 *   7. slab_init()              — kmalloc 可用（8 组 size class）
 *   8. arch_trap_init()              — stvec, sscratch, SIE.STIE
 *   9. task_init()              — 创建 idle (PID 0, BSS 静态), 设置 current
 *  10. arch_timer_init()             — Sstc stimecmp 设置首次时钟中断
 *  11. sched_init()             — 初始化全局就绪队列
 *  12. kernel_test()            — DEBUG 构建运行内核自测
 *  13. kernel_thread(init_process, NULL) — 创建 init (PID 1)
 *  14. while(1) { wfi(); schedule(); }   — idle 循环
 *
 * 依赖关系：
 *   console → pagetable → buddy → slab → trap → task → timer → sched → thread
 *
 * 注意事项：
 *   不解析 DTB；设备地址仍硬编码，DRAM 大小等参数来自 Kconfig。
 *   仅 hart0 运行，非 0 hart 在 boot.S 中已被 park。
 */

#include <kernel/printk.h>
#include <kernel/buddy.h>
#include <kernel/slab.h>
#include <kernel/page_cache.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <kernel/timer.h>
#include <kernel/syscall.h>
#include <kernel/signal.h>
#include <kernel/vfs.h>
#include <drivers/console.h>
#include <drivers/virtio_blk.h>
#include <asm/trap.h>
#include <asm/csr.h>
#include <asm/pte.h>
#include <asm/user_map.h>

#ifdef CONFIG_KERNEL_TEST
#include <kernel/test.h>
#endif

/* PID 1 init 内核线程入口 (kernel/init_process.c) */
extern void init_process(void *arg);

void kernel_main(void)
{
	struct task_struct *init;
	struct task_struct *writeback;

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

	arch_pt_init();
	console_init_mmio();
	console_chrdev_init();
	pr_info("uart: init successfully\n");

	buddy_init();
	arch_pt_use_buddy();
	slab_init();
	arch_user_map_init();
	signal_user_map_init();
	pr_info("mm: init successfully\n");

	arch_trap_init();
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

	virtio_blk_init();
	if (mount_root() < 0)
		pr_warn("VFS: root mount skipped\n");

#ifdef CONFIG_KERNEL_TEST
	kernel_test();
#endif

	init = kernel_thread(init_process, NULL);
	BUG_ON(!init);
	set_init_task(init);

	writeback = kernel_thread(page_cache_wb_thread, NULL);
	BUG_ON(!writeback);

	/* 进入 idle 循环 — idle 进程的执行体 */
	while (true) {
		schedule();
		wfi();
	}
}
