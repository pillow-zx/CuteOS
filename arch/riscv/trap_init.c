/*
 * arch/riscv/trap_init.c - stvec 初始化
 *
 * 功能：
 *   在 C 层完成 Trap 向量寄存器的初始化配置。
 *   必须在 kernel_main 早期调用，确保 CPU 能够正确响应中断和异常。
 *
 * 主要函数：
 *   trap_init() - 设置 stvec CSR 为 __alltraps（汇编 Trap 入口地址），
 *                 使用 Direct 模式 (stvec.mode=0)，所有异常/中断统一
 *                 跳转到 __alltraps。
 *                 同时将 sscratch 清零为 0，用于 __alltraps 中判断
 *                 当前 Trap 来源：sscratch==0 表示从 S 态进入，
 *                 sscratch!=0 表示从 U 态进入（此时 sscratch 存放内核栈指针）。
 *
 * 注意事项：
 *   - stvec 设置必须在任何可能触发异常的操作之前完成
 *   - sscratch 的切换由 entry.S 在用户态 ↔ 内核态转换时自动维护
 */
