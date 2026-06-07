/*
 * kernel/signal.c - 信号机制
 *
 * 功能：
 *   实现符合 POSIX 语义的信号机制。每个 task_struct 中包含：
 *     - sigaction[32]：32 个信号的处理器描述。
 *     - blocked       ：被阻塞的信号掩码。
 *     - pending       ：待处理信号位图。
 *
 *   信号投递流程（do_signal）：
 *     - 在每次 trap 返回用户态（U-mode）前调用 do_signal。
 *     - 遍历 pending 位图，找到第一个未被 blocked 的待处理信号。
 *     - SIGKILL（9）和 SIGSTOP（19）不可捕获、不可阻塞，
 *       始终执行默认行为。
 *     - 投递动作：在用户栈上构建 signal_frame（保存当前 trap_frame
 *       和 blocked 掩码），修改 trap_frame 使返回后执行信号处理器
 *      （sepc = handler 地址，a0 = signo，ra = trampoline 地址）。
 *     - sigreturn 系统调用：从 signal_frame 恢复原始 trap_frame
 *       和 blocked 掩码，使信号处理器返回后继续执行被中断的程序。
 *
 * 主要函数：
 *   do_signal(tf)                  - 在所有 trap 返回 U-mode 前调用，
 *                                     检查并投递 pending 信号。
 *   send_signal(sig, target)       - 向目标进程发送信号（置 pending 位）。
 *   setup_signal_frame(tf, ka)     - 在用户栈上构建 signal_frame，
 *                                     保存 trap_frame 和 blocked，
 *                                     修改 trap_frame（sepc=handler, a0=signo,
 * ra=trampoline）。 sys_sigreturn()                - 从信号处理器返回，恢复
 * trap_frame + blocked。 sys_kill(pid, sig)             - kill 系统调用实现。
 *   sys_sigaction(sig, act, oldact)- sigaction 系统调用（注册/查询处理器）。
 */
