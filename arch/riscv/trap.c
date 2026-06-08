/*
 * arch/riscv/trap.c - Trap 分发（C 层）
 *
 * 功能：
 *   在汇编层保存完 trap_frame 后，由 trap_handler() 统一接住异常和中断。
 *   当前系统尚未实现细分分发器，因此任何 trap 都会带着诊断信息 panic，
 *   避免静默返回导致更难排查的错误。
 */

#include <asm/csr.h>
#include <asm/trap.h>
#include <kernel/printk.h>
#include <kernel/types.h>

static const __const char *trap_origin(const struct trap_frame *tf)
{
	return (tf->sstatus & SSTATUS_SPP) ? "kernel" : "user";
}

void trap_handler(struct trap_frame *tf)
{
	uint64_t scause = tf->scause;
	bool is_interrupt = (scause & (1UL << 63)) != 0;
	uint64_t code = scause & ~(1UL << 63);

	if (is_interrupt) {
		panic("unhandled interrupt: origin=%s scause=%lu code=%lu "
		      "sepc=%p stval=%p",
		      trap_origin(tf), (size_t)scause, (size_t)code,
		      (void *)tf->sepc, (void *)tf->stval);
	} else {
		panic("unhandled exception: origin=%s scause=%lu code=%lu "
		      "sepc=%p stval=%p",
		      trap_origin(tf), (size_t)scause, (size_t)code,
		      (void *)tf->sepc, (void *)tf->stval);
	}
}
