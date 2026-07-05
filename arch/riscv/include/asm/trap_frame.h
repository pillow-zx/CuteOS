#ifndef _CUTEOS_ASM_TRAP_FRAME_H
#define _CUTEOS_ASM_TRAP_FRAME_H

/*
 * arch/riscv/include/asm/trap_frame.h - trap entry register save layout
 *
 * This header is the assembly contract for entry.S and __trapret.  Code above
 * arch/riscv should use <arch/trap.h> accessors instead of field access.
 */

#include <kernel/compiler.h>
#include <kernel/types.h>
#include <asm/asm_offsets.h>

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

#define CHECK_TF_OFFSET(member, offset)                                        \
	static_assert(offsetof(struct trap_frame, member) == (offset),         \
		      "trap_frame." #member " offset mismatch")

CHECK_TF_OFFSET(sepc, TF_SEPC);
CHECK_TF_OFFSET(ra, TF_RA);
CHECK_TF_OFFSET(sp, TF_SP);
CHECK_TF_OFFSET(gp, TF_GP);
CHECK_TF_OFFSET(tp, TF_TP);
CHECK_TF_OFFSET(t0, TF_T0);
CHECK_TF_OFFSET(t1, TF_T1);
CHECK_TF_OFFSET(t2, TF_T2);
CHECK_TF_OFFSET(s0, TF_S0);
CHECK_TF_OFFSET(s1, TF_S1);
CHECK_TF_OFFSET(a0, TF_A0);
CHECK_TF_OFFSET(a1, TF_A1);
CHECK_TF_OFFSET(a2, TF_A2);
CHECK_TF_OFFSET(a3, TF_A3);
CHECK_TF_OFFSET(a4, TF_A4);
CHECK_TF_OFFSET(a5, TF_A5);
CHECK_TF_OFFSET(a6, TF_A6);
CHECK_TF_OFFSET(a7, TF_A7);
CHECK_TF_OFFSET(s2, TF_S2);
CHECK_TF_OFFSET(s3, TF_S3);
CHECK_TF_OFFSET(s4, TF_S4);
CHECK_TF_OFFSET(s5, TF_S5);
CHECK_TF_OFFSET(s6, TF_S6);
CHECK_TF_OFFSET(s7, TF_S7);
CHECK_TF_OFFSET(s8, TF_S8);
CHECK_TF_OFFSET(s9, TF_S9);
CHECK_TF_OFFSET(s10, TF_S10);
CHECK_TF_OFFSET(s11, TF_S11);
CHECK_TF_OFFSET(t3, TF_T3);
CHECK_TF_OFFSET(t4, TF_T4);
CHECK_TF_OFFSET(t5, TF_T5);
CHECK_TF_OFFSET(t6, TF_T6);
CHECK_TF_OFFSET(scause, TF_SCAUSE);
CHECK_TF_OFFSET(stval, TF_STVAL);
CHECK_TF_OFFSET(sstatus, TF_SSTATUS);
static_assert(sizeof(struct trap_frame) == TRAP_FRAME_SIZE,
	      "trap_frame size mismatch");

#undef CHECK_TF_OFFSET

#endif
