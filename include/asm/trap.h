#ifndef _CUTEOS_ASM_TRAP_H
#define _CUTEOS_ASM_TRAP_H

/*
 * include/asm/trap.h - 异常/中断帧结构，用于寄存器保存与恢复
 *
 * 定义 struct trap_frame，保存进入 trap handler 时的完整 CPU 状态，
 * 在返回时恢复。字段顺序必须与 entry.S 中的保存/恢复序列完全一致。
 *
 * struct trap_frame layout (35 x uint64_t fields, in order):
 *   [0]  sepc        - Supervisor Exception Program Counter
 *   [1]  ra   (x1)   - Return address
 *   [2]  sp   (x2)   - Stack pointer
 *   [3]  gp   (x3)   - Global pointer
 *   [4]  tp   (x4)   - Thread pointer
 *   [5]  t0   (x5)   - Temporary register 0
 *   [6]  t1   (x6)   - Temporary register 1
 *   [7]  t2   (x7)   - Temporary register 2
 *   [8]  s0   (x8)   - Saved register 0 / frame pointer
 *   [9]  s1   (x9)   - Saved register 1
 *   [10] a0   (x10)  - Function argument 0 / syscall return value
 *   [11] a1   (x11)  - Function argument 1
 *   [12] a2   (x12)  - Function argument 2
 *   [13] a3   (x13)  - Function argument 3
 *   [14] a4   (x14)  - Function argument 4
 *   [15] a5   (x15)  - Function argument 5
 *   [16] a6   (x16)  - Function argument 6
 *   [17] a7   (x17)  - Function argument 7 / syscall number
 *   [18] s2   (x18)  - Saved register 2
 *   [19] s3   (x19)  - Saved register 3
 *   [20] s4   (x20)  - Saved register 4
 *   [21] s5   (x21)  - Saved register 5
 *   [22] s6   (x22)  - Saved register 6
 *   [23] s7   (x23)  - Saved register 7
 *   [24] s8   (x24)  - Saved register 8
 *   [25] s9   (x25)  - Saved register 9
 *   [26] s10  (x26)  - Saved register 10
 *   [27] s11  (x27)  - Saved register 11
 *   [28] t3   (x28)  - Temporary register 3
 *   [29] t4   (x29)  - Temporary register 4
 *   [30] t5   (x30)  - Temporary register 5
 *   [31] t6   (x31)  - Temporary register 6
 *   [32] scause       - Supervisor Cause register
 *   [33] stval        - Supervisor Trap Value register
 *   [34] sstatus      - Supervisor Status register
 *
 * Helper:
 *   from_user(tf) - Returns true if trap originated from user mode
 *                   (checks SSTATUS_SPP bit in tf->sstatus)
 */

#include <kernel/types.h>
#include <kernel/bitops.h>
#include <kernel/compiler.h>
#include <asm/csr.h>

/* ---- scause 中断/异常码 ----
 *
 * scause[63] = 1 表示中断, = 0 表示异常。
 * scause[62:0] 为具体编码。
 * 以下常量仅包含编码值，不含中断位。
 */

/* 中断码 (scause[63] = 1) */
#define IRQ_S_SOFT	1UL  /* Supervisor Software Interrupt */
#define IRQ_VS_SOFT	2UL  /* Virtual Supervisor Software Interrupt */
#define IRQ_M_SOFT	3UL  /* Machine Software Interrupt */
#define IRQ_S_TIMER	5UL  /* Supervisor Timer Interrupt */
#define IRQ_VS_TIMER	6UL  /* Virtual Supervisor Timer Interrupt */
#define IRQ_M_TIMER	7UL  /* Machine Timer Interrupt */
#define IRQ_S_EXT	9UL  /* Supervisor External Interrupt */
#define IRQ_VS_EXT	10UL /* Virtual Supervisor External Interrupt */
#define IRQ_M_EXT	11UL /* Machine External Interrupt */

/* 异常码 (scause[63] = 0) */
#define EXC_INST_MISALIGNED  0UL
#define EXC_INST_ACCESS	     1UL
#define EXC_INST_ILLEGAL     2UL
#define EXC_BREAKPOINT	     3UL
#define EXC_LOAD_MISALIGNED  4UL
#define EXC_LOAD_ACCESS	     5UL
#define EXC_STORE_MISALIGNED 6UL
#define EXC_STORE_ACCESS     7UL
#define EXC_ECALL_U	     8UL /* ecall from U-mode */
#define EXC_ECALL_S	     9UL /* ecall from S-mode */
#define EXC_INST_PAGE_FAULT  12UL
#define EXC_LOAD_PAGE_FAULT  13UL
#define EXC_STORE_PAGE_FAULT 15UL

/* scause 中断标志位 */
#define SCAUSE_IRQ_FLAG (1UL << 63)

struct trap_frame {
	size_t sepc;
	size_t ra, sp, gp, tp;
	size_t t0, t1, t2;
	size_t s0, s1;
	size_t a0, a1, a2, a3, a4, a5, a6, a7;
	size_t s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
	size_t t3, t4, t5, t6;
	size_t scause;
	size_t stval;
	size_t sstatus;
};

struct context {
	size_t ra;
	size_t sp;
	size_t s0, s1, s2, s3, s4, s5;
	size_t s6, s7, s8, s9, s10, s11;
};

typedef bool (*trap_test_hook_t)(struct trap_frame *tf);

static __always_inline bool from_user(const struct trap_frame *tf)
{
	return (tf->sstatus & SSTATUS_SPP) == 0;
}

void trap_init(void);

void trap_handler(struct trap_frame *tf);

void trap_set_test_hook(trap_test_hook_t hook);

void __trapret(void);
void trapret_to_user(struct trap_frame *tf) __noreturn;
void switch_to(struct context *prev, struct context *next);

#endif
