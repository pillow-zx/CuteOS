/*
 * arch/riscv/trap_init.c - Trap 基础设施初始化
 *
 * 建立 trap 处理的完整运行环境：
 *   1. 设置 stvec = __alltraps（Direct 模式，所有异常/中断统一入口）
 *   2. 清零 sscratch（0 表示当前在 S 态，供 __alltraps 判断 trap 来源）
 *   3. 启用 SIE.STIE（Supervisor Timer Interrupt Enable）
 *   4. 启用 sstatus.SIE（全局中断开关）
 */

#include <asm/csr.h>
#include <asm/trap.h>
#include <kernel/printk.h>

extern void __alltraps(void);

void trap_init(void)
{
        csr_write(stvec, __alltraps);
        csr_write(sscratch, 0);
        csr_set(sie, SIE_STIE);
        csr_set(sstatus, SSTATUS_SIE);
        printk("stvec: 0x%lx, sscratch: 0x%lx, sie: 0x%lx, sstatus: 0x%lx\n",
               csr_read(stvec), csr_read(sscratch), csr_read(sie),
               csr_read(sstatus));
}
