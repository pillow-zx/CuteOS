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
 *   2. printk("cuteOS starting...")
 *   3. kernel_pagetable_init()  — 建立正式内核页表（4KB 页 + MMIO mega page）
 *   4. console_init_mmio()      — 切换到 UART MMIO 轮询模式
 *   5. buddy_init()             — 物理页分配器（从 _end 到 DRAM 结束）
 *   6. slab_init()              — kmalloc 可用（8 组 size class）
 *   7. trap_init()              — stvec = __alltraps, sscratch = 0, SIE.STIE
 *   8. task_init()              — 创建 idle (PID 0, BSS 静态), 设置 current
 *   9. timer_init()             — Sstc stimecmp 设置首次时钟中断
 *  10. sched_init()             — 初始化全局就绪队列
 *  11. kernel_test()            — 运行内核自测
 *  12. kernel_thread(init_process, NULL) — 创建 init (PID 1)
 *  13. while(1) { wfi(); schedule(); }   — idle 循环
 *
 * 依赖关系：
 *   console → pagetable → buddy → slab → trap → task → timer → sched → thread
 *
 * 注意事项：
 *   不解析 DTB，所有参数（DRAM_BASE/DRAM_SIZE/设备地址）编译时硬编码。
 *   仅 hart0 运行，非 0 hart 在 boot.S 中已被 park。
 */

// #define DEBUG_ENABLE

#include <kernel/printk.h>
#include <kernel/buddy.h>
#include <kernel/slab.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <kernel/timer.h>
#include <kernel/syscall.h>
#include <kernel/vfs.h>
#include <drivers/virtio_blk.h>
#include <asm/trap.h>
#include <asm/csr.h>
#include <asm/pte.h>

#ifdef DEBUG_ENABLE
	#include <kernel/test.h>
#endif

/* PID 1 init 内核线程入口 (kernel/init_process.c) */
extern void init_process(void *arg);

void kernel_main(void)
{
	console_init_sbi();

	printk("\n");
	printk("  /$$$$$$              /$$                /$$$$$$   /$$$$$$ \n");
	printk(" /$$__  $$            | $$               /$$__  $$ /$$__  $$\n");
	printk("| $$  \\__/ /$$   /$$ /$$$$$$    /$$$$$$ | $$  \\ $$| $$  \\__/\n");
	printk("| $$      | $$  | $$|_  $$_/   /$$__  $$| $$  | $$|  $$$$$$ \n");
	printk("| $$      | $$  | $$  | $$    | $$$$$$$$| $$  | $$ \\____  $$\n");
	printk("| $$    $$| $$  | $$  | $$ /$$| $$_____/| $$  | $$ /$$  \\ $$\n");
	printk("|  $$$$$$/|  $$$$$$/  |  $$$$/|  $$$$$$$|  $$$$$$/|  $$$$$$/\n");
	printk(" \\______/  \\______/    \\___/   \\_______/ \\______/  \\______/ \n");
	printk("\n");

	kernel_pagetable_init();
	console_init_mmio();
	printk("uart: init successfully\n");

	buddy_init();
	page_table_use_buddy();
	slab_init();
	printk("mm: init successfully\n");

	trap_init();
	printk("trap: init successfully\n");

	task_init();
	printk("task: init successfully\n");

	timer_init();
	printk("timer: init successfully\n");

	sched_init();
	printk("sched: init successfully\n");

	syscall_init();
	printk("syscall: init successfully\n");

	vfs_init();
	printk("vfs: init successfully\n");

	virtio_blk_init();
	if (mount_root() < 0)
		printk("VFS: root mount skipped\n");

#ifdef DEBUG_ENABLE
	kernel_test();
#endif

	set_init_task(kernel_thread(init_process, NULL));

	/* 进入 idle 循环 — idle 进程的执行体 */
	while (true) {
		wfi();
		schedule();
	}
}
