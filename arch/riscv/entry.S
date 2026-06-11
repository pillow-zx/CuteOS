/*
 * arch/riscv/entry.S - Trap 汇编入口/出口 + 上下文切换
 *
 * __alltraps : 统一 trap 向量入口, 区分 U→S / S→S 后保存 trap_frame
 * __trapret  : 从 trap_frame 恢复寄存器并 sret
 * switch_to  : 进程上下文切换, 保存/恢复 callee-saved 寄存器
 *
 * Trap 帧布局 (struct trap_frame, 35 × 8 = 280 字节):
 *   sepc, ra, sp, gp, tp, t0-t6, s0-s11, a0-a7, scause, stval, sstatus
 *
 * 上下文切换布局 (struct context, 14 × 8 = 112 字节):
 *   ra, sp, s0-s11
 */

/* ---- trap_frame offsets ---- */
#define TF_SEPC    ( 0*8)
#define TF_RA      ( 1*8)
#define TF_SP      ( 2*8)
#define TF_GP      ( 3*8)
#define TF_TP      ( 4*8)
#define TF_T0      ( 5*8)
#define TF_T1      ( 6*8)
#define TF_T2      ( 7*8)
#define TF_S0      ( 8*8)
#define TF_S1      ( 9*8)
#define TF_A0      (10*8)
#define TF_A1      (11*8)
#define TF_A2      (12*8)
#define TF_A3      (13*8)
#define TF_A4      (14*8)
#define TF_A5      (15*8)
#define TF_A6      (16*8)
#define TF_A7      (17*8)
#define TF_S2      (18*8)
#define TF_S3      (19*8)
#define TF_S4      (20*8)
#define TF_S5      (21*8)
#define TF_S6      (22*8)
#define TF_S7      (23*8)
#define TF_S8      (24*8)
#define TF_S9      (25*8)
#define TF_S10     (26*8)
#define TF_S11     (27*8)
#define TF_T3      (28*8)
#define TF_T4      (29*8)
#define TF_T5      (30*8)
#define TF_T6      (31*8)
#define TF_SCAUSE  (32*8)
#define TF_STVAL   (33*8)
#define TF_SSTATUS (34*8)

/* ---- context offsets ---- */
#define CTX_RA   ( 0*8)
#define CTX_SP   ( 1*8)
#define CTX_S0   ( 2*8)
#define CTX_S1   ( 3*8)
#define CTX_S2   ( 4*8)
#define CTX_S3   ( 5*8)
#define CTX_S4   ( 6*8)
#define CTX_S5   ( 7*8)
#define CTX_S6   ( 8*8)
#define CTX_S7   ( 9*8)
#define CTX_S8   (10*8)
#define CTX_S9   (11*8)
#define CTX_S10  (12*8)
#define CTX_S11  (13*8)

/* ---- task_struct 字段偏移（供 __trapret 读取 current->satp）----
 * 必须与 include/kernel/task.h 中的 struct task_struct 保持同步；
 * 偏移正确性由 task.h 内的 _Static_assert 在编译期校验，避免布局
 * 变动后汇编静默读到错误字段。
 *
 *   pid(4) + state(4) + ctx(112) + tf(8) + kstack(8) + mm(8) = 144
 */
#define TASK_SATP 144

	.section .text.entry
	.globl __alltraps
	.globl __trapret
	.align 4

/* ================================================================
 * __alltraps - Trap 向量入口 (Direct mode)
 *
 * 判断 trap 来源:
 *   读 sscratch → 如果 t0 != 0 则来自用户态 (U→S)
 *   否则来自内核态 (S→S), 需恢复 t0 后直接用当前 sp。
 *
 * 关键: csrrw t0, sscratch, t0 将用户态 t0 交换到 sscratch 中,
 *       后续可从 sscratch 恢复。
 * ================================================================ */
__alltraps:
	csrrw   t0, sscratch, t0       /* t0 = old sscratch; sscratch = old t0 */
	bnez    t0, .Lfrom_user

	/* ============================================================
	 * S → S path
	 * sscratch 里存的是被换出的旧 t0, 换回来即可。
	 * sp 不变, 直接在当前内核栈上分配 trap_frame。
	 * ============================================================ */
	csrrw   zero, sscratch, t0     /* sscratch = 0; 恢复 t0 */
	addi    sp, sp, -(35*8)

	/* 保存原始 sp (分配前的值) */
	addi    t1, sp, (35*8)
	sd      t1, TF_SP(sp)

	/* 保存所有 GPR */
	sd      ra,  TF_RA(sp)
	sd      gp,  TF_GP(sp)
	sd      tp,  TF_TP(sp)
	sd      t0,  TF_T0(sp)
	sd      t1,  TF_T1(sp)
	sd      t2,  TF_T2(sp)
	sd      s0,  TF_S0(sp)
	sd      s1,  TF_S1(sp)
	sd      a0,  TF_A0(sp)
	sd      a1,  TF_A1(sp)
	sd      a2,  TF_A2(sp)
	sd      a3,  TF_A3(sp)
	sd      a4,  TF_A4(sp)
	sd      a5,  TF_A5(sp)
	sd      a6,  TF_A6(sp)
	sd      a7,  TF_A7(sp)
	sd      s2,  TF_S2(sp)
	sd      s3,  TF_S3(sp)
	sd      s4,  TF_S4(sp)
	sd      s5,  TF_S5(sp)
	sd      s6,  TF_S6(sp)
	sd      s7,  TF_S7(sp)
	sd      s8,  TF_S8(sp)
	sd      s9,  TF_S9(sp)
	sd      s10, TF_S10(sp)
	sd      s11, TF_S11(sp)
	sd      t3,  TF_T3(sp)
	sd      t4,  TF_T4(sp)
	sd      t5,  TF_T5(sp)
	sd      t6,  TF_T6(sp)

	/* 保存 CSR */
	csrr    t1, sepc
	sd      t1, TF_SEPC(sp)
	csrr    t1, scause
	sd      t1, TF_SCAUSE(sp)
	csrr    t1, stval
	sd      t1, TF_STVAL(sp)
	csrr    t1, sstatus
	sd      t1, TF_SSTATUS(sp)

	j       .Lcall_handler

	/* ============================================================
	 * U → S path
	 * t0 = 内核栈指针 (来自 sscratch)
	 * sp 仍为用户态 sp (未被修改)
	 * 用户态 t0 在 sscratch 里 (被 csrrw 交换进去)
	 * ============================================================ */
.Lfrom_user:
	/* 将用户 sp 暂存到内核栈顶 (不与 trap_frame 冲突) */
	sd      sp, (0)(t0)
	/* 切换到内核栈 */
	mv      sp, t0
	addi    sp, sp, -(35*8)

	/* 取回用户 sp, 存入 TF_SP */
	ld      t0, (35*8)(sp)
	sd      t0, TF_SP(sp)

	/* 从 sscratch 取回用户态原始 t0 */
	csrr    t0, sscratch
	sd      t0, TF_T0(sp)

	/* sscratch 清零 */
	csrw    sscratch, zero

	/* 保存其余 GPR (都是原始用户态值) */
	sd      ra,  TF_RA(sp)
	sd      gp,  TF_GP(sp)
	sd      tp,  TF_TP(sp)
	sd      t1,  TF_T1(sp)
	sd      t2,  TF_T2(sp)
	sd      s0,  TF_S0(sp)
	sd      s1,  TF_S1(sp)
	sd      a0,  TF_A0(sp)
	sd      a1,  TF_A1(sp)
	sd      a2,  TF_A2(sp)
	sd      a3,  TF_A3(sp)
	sd      a4,  TF_A4(sp)
	sd      a5,  TF_A5(sp)
	sd      a6,  TF_A6(sp)
	sd      a7,  TF_A7(sp)
	sd      s2,  TF_S2(sp)
	sd      s3,  TF_S3(sp)
	sd      s4,  TF_S4(sp)
	sd      s5,  TF_S5(sp)
	sd      s6,  TF_S6(sp)
	sd      s7,  TF_S7(sp)
	sd      s8,  TF_S8(sp)
	sd      s9,  TF_S9(sp)
	sd      s10, TF_S10(sp)
	sd      s11, TF_S11(sp)
	sd      t3,  TF_T3(sp)
	sd      t4,  TF_T4(sp)
	sd      t5,  TF_T5(sp)
	sd      t6,  TF_T6(sp)

	/* 保存 CSR */
	csrr    t0, sepc
	sd      t0, TF_SEPC(sp)
	csrr    t0, scause
	sd      t0, TF_SCAUSE(sp)
	csrr    t0, stval
	sd      t0, TF_STVAL(sp)
	csrr    t0, sstatus
	sd      t0, TF_SSTATUS(sp)

	/* fall through */

.Lcall_handler:
	mv      a0, sp
	jal     ra, trap_handler

/* ================================================================
 * __trapret - 从 trap 返回
 * ================================================================ */
	.align 4
__trapret:
	/* 恢复 CSR */
	ld      t0, TF_SEPC(sp)
	csrw    sepc, t0
	ld      t0, TF_SCAUSE(sp)
	csrw    scause, t0
	ld      t0, TF_STVAL(sp)
	csrw    stval, t0
	ld      t0, TF_SSTATUS(sp)
	csrw    sstatus, t0

	/* 恢复 GPR (sp, t0 留到最后) */
	ld      ra,  TF_RA(sp)
	ld      gp,  TF_GP(sp)
	ld      tp,  TF_TP(sp)
	ld      t1,  TF_T1(sp)
	ld      t2,  TF_T2(sp)
	ld      s0,  TF_S0(sp)
	ld      s1,  TF_S1(sp)
	ld      a0,  TF_A0(sp)
	ld      a1,  TF_A1(sp)
	ld      a2,  TF_A2(sp)
	ld      a3,  TF_A3(sp)
	ld      a4,  TF_A4(sp)
	ld      a5,  TF_A5(sp)
	ld      a6,  TF_A6(sp)
	ld      a7,  TF_A7(sp)
	ld      s2,  TF_S2(sp)
	ld      s3,  TF_S3(sp)
	ld      s4,  TF_S4(sp)
	ld      s5,  TF_S5(sp)
	ld      s6,  TF_S6(sp)
	ld      s7,  TF_S7(sp)
	ld      s8,  TF_S8(sp)
	ld      s9,  TF_S9(sp)
	ld      s10, TF_S10(sp)
	ld      s11, TF_S11(sp)
	ld      t3,  TF_T3(sp)
	ld      t4,  TF_T4(sp)
	ld      t5,  TF_T5(sp)
	ld      t6,  TF_T6(sp)

	/* 判断返回目标: SPP=0 → 用户态, SPP=1 → 内核态 */
	csrr    t0, sstatus
	andi    t0, t0, (1 << 8)
	bnez    t0, .Lret_supervisor

.Lret_user:
	/* sscratch = 内核栈顶, 供下次 U→S 使用 */
	addi    t0, sp, (35*8)
	csrw    sscratch, t0

	/* 切换到当前进程的用户页表 */
	la      t0, current
	ld      t0, 0(t0)               /* t0 = current (task_struct *) */
	ld      t0, TASK_SATP(t0)       /* t0 = current->satp */
	beqz    t0, .Lret_user_noswitch
	csrw    satp, t0
	sfence.vma zero, zero
.Lret_user_noswitch:
	ld      t0, TF_T0(sp)
	ld      sp, TF_SP(sp)
	sret

.Lret_supervisor:
	csrw    sscratch, zero
	ld      t0, TF_T0(sp)
	addi    sp, sp, (35*8)
	sret

/* ================================================================
 * switch_to(prev, next) - 进程上下文切换
 *   a0 = &prev->context, a1 = &next->context
 * ================================================================ */
	.globl switch_to
	.align 4
switch_to:
	sd      ra,  CTX_RA(a0)
	sd      sp,  CTX_SP(a0)
	sd      s0,  CTX_S0(a0)
	sd      s1,  CTX_S1(a0)
	sd      s2,  CTX_S2(a0)
	sd      s3,  CTX_S3(a0)
	sd      s4,  CTX_S4(a0)
	sd      s5,  CTX_S5(a0)
	sd      s6,  CTX_S6(a0)
	sd      s7,  CTX_S7(a0)
	sd      s8,  CTX_S8(a0)
	sd      s9,  CTX_S9(a0)
	sd      s10, CTX_S10(a0)
	sd      s11, CTX_S11(a0)

	ld      ra,  CTX_RA(a1)
	ld      sp,  CTX_SP(a1)
	ld      s0,  CTX_S0(a1)
	ld      s1,  CTX_S1(a1)
	ld      s2,  CTX_S2(a1)
	ld      s3,  CTX_S3(a1)
	ld      s4,  CTX_S4(a1)
	ld      s5,  CTX_S5(a1)
	ld      s6,  CTX_S6(a1)
	ld      s7,  CTX_S7(a1)
	ld      s8,  CTX_S8(a1)
	ld      s9,  CTX_S9(a1)
	ld      s10, CTX_S10(a1)
	ld      s11, CTX_S11(a1)
	ret
