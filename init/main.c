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
 *   2. printk("cuteOS starting...") / printk("DRAM: 256MB at 0x80000000")
 *   3. kernel_pagetable_init()  — 建立正式内核页表（4KB 页 + MMIO mega page），写 satp
 *   4. console_init_mmio()      — 切换到 UART MMIO 轮询模式
 *   5. buddy_init()             — 物理页分配器（从 _end 到 DRAM 结束）
 *   6. slab_init()              — kmalloc 可用（8 组 size class）
 *   7. trap_init()              — stvec = __alltraps, sscratch = 0
 *   8. task_init()              — 创建 idle (PID 0, BSS 静态), 设置 current
 *      └─ kernel_thread(init_process, NULL)  — 创建 init (PID 1)
 *   9. timer_init()             — 首次 sbi_set_timer, 启用 SIE.STIE
 *  10. schedule()               — 切到 init, idle 在后台 wfi
 *
 * 依赖关系：
 *   console → pagetable → buddy → slab → trap → task → timer → schedule
 *
 * 注意事项：
 *   不解析 DTB，所有参数（DRAM_BASE/DRAM_SIZE/设备地址）编译时硬编码。
 *   仅 hart0 运行，非 0 hart 在 boot.S 中已被 park。
 */

#include <kernel/printk.h>
#include <asm/page.h>
#include <asm/sbi.h>

void kernel_main(void)
{
	console_init_sbi();
	printk("cuteOS starting...\n");

	printk("DRAM: %dMB at 0x80000000\n", (int)(DRAM_SIZE >> 20));
	kernel_pagetable_init();

	sbi_shutdown();
}
