/*
 * arch/riscv/entry.S - Trap 汇编入口/出口 + 上下文切换
 *
 * 功能：
 *   提供从 CPU 异常/中断到 C 层 trap_handler() 的汇编级桥接代码，
 *   以及进程上下文切换原语 switch_to。
 *
 * 主要符号：
 *   __alltraps    - Trap 向量入口。根据 sstatus.SPP 位判断 Trap 来源：
 *                   SPP=0 (用户态 → S 态)：从 sscratch 读取内核栈指针，
 *                   将用户 sp 保存到 trap_frame，切换到内核栈。
 *                   SPP=1 (S 态 → S 态)：直接使用当前 sp，无需切换栈。
 *                   保存完整的 trap_frame 后调用 trap_handler()。
 *
 *   __trapret     - 从 trap_frame 恢复所有寄存器，执行 sret 返回被中断处。
 *                   返回用户态前将 sscratch 设回 trap_frame 地址。
 *
 *   switch_to     - 进程上下文切换原语。保存/恢复 15 个 callee-saved 寄存器
 *                   (ra, sp, s0~s11, 共 120 字节) 到/从 task_struct.context。
 *                   调用后当前 CPU 已运行在 next 进程的上下文中。
 *
 * Trap 帧布局 (trap_frame)：
 *   大小：35 × 8 = 280 字节，按寄存器编号顺序连续存放：
 *     [0]  ra      [1]  sp      [2]  gp      [3]  tp
 *     [4]  t0      [5]  t1      [6]  t2      [7]  s0
 *     [8]  s1      [9]  a0      [10] a1      [11] a2
 *     [12] a3      [13] a4      [14] a5      [15] a6
 *     [16] a7      [17] s2      [18] s3      [19] s4
 *     [20] s5      [21] s6      [22] s7      [23] s8
 *     [24] s9      [25] s10     [26] s11     [27] t3
 *     [28] t4      [29] t5      [30] t6
 *     [31] sepc    [32] sstatus [33] scause  [34] stval
 *   保存/恢复顺序必须与 struct trap_frame (C 层) 字段布局严格一致。
 *
 * 上下文切换布局 (context)：
 *   15 个 callee-saved 寄存器，共 120 字节：
 *     ra, sp, s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, sepc
 */
