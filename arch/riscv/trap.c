/*
 * arch/riscv/trap.c - Trap 分发（C 层）
 *
 * 功能：
 *   在汇编层保存完 trap_frame 后，由 trap_handler() 统一分发各类异常和中断。
 *   根据 scause 寄存器（Exception Code）判断来源，调用对应的处理函数。
 *
 * 主要函数：
 *   trap_handler(tf) - 主分发入口，由 __alltraps 在保存现场后调用。
 *                      分发逻辑如下：
 *
 *                      - scause=5  (Supervisor Timer Interrupt)
 *                        → handle_timer_irq()：更新 jiffies，处理调度 ticks
 *
 *                      - scause=9  (Supervisor External Interrupt)
 *                        → handle_external_irq()：PLIC 外设中断（UART 等）
 *
 *                      - scause=8  (Environment Call from U-mode)
 *                        → do_syscall()：处理用户态系统调用请求
 *
 *                      - scause=12 (Instruction Page Fault)
 *                      - scause=13 (Load Page Fault)
 *                      - scause=15 (Store/AMO Page Fault)
 *                        → do_page_fault()：按缺页类型处理（COW/ Demand paging/
 *                          栈扩展/非法访问），sig=sigsegv 兜底
 *
 *                      - 其他未识别的异常
 *                        → panic 或向当前进程发送 SIGSEGV 信号
 *
 *   返回用户态前处理：
 *     trap_handler 返回前检查当前进程是否有 pending 信号，
 *     若有则调用 do_signal() 在返回用户态之前将信号处理函数注入用户栈。
 *
 * 调用关系：
 *   entry.S:__alltraps → trap_handler() → 各次级处理函数
 *   trap_handler 返回后 → entry.S:__trapret 恢复现场并 sret
 */
