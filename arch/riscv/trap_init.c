/*
 * arch/riscv/trap_init.c - stvec 初始化
 *
 * 设置 stvec CSR 为 __alltraps（汇编 Trap 入口地址），
 * 使用 Direct 模式 (stvec.mode=0)，所有异常/中断统一跳转到 __alltraps。
 * 同时将 sscratch 清零为 0：
 *   sscratch == 0 表示当前在 S 态，__alltraps 中判断 Trap 来源时使用。
 */

#include <asm/csr.h>
#include <kernel/printk.h>

extern void __alltraps(void);

void trap_init(void)
{
	csr_write(stvec, __alltraps);

	csr_write(sscratch, 0);
	printk("stvec: 0x%lx, sscratch: 0x%lx\n", csr_read(stvec), csr_read(sscratch));
}
