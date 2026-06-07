/*
 * syscall/sys_signal.c - 信号相关系统调用
 *
 * 功能：
 *   实现信号相关的系统调用，为应用层提供进程间信号通信和信号处理能力。
 *   kill 在目标进程中设置 pending 位。sigaction 对 SIGKILL/SIGSTOP
 *   返回 -EINVAL（不可捕获）。sigprocmask 设置信号阻塞掩码。
 *   sigreturn 从信号栈恢复被中断的上下文和 blocked 掩码。
 *
 * 主要函数：
 *   sys_kill(pid, sig)               - 设置目标进程的 pending 位
 *   sys_sigaction(sig, act, oldact)  - 注册信号处理器（SIGKILL/SIGSTOP
 *                                      返回 -EINVAL）
 *   sys_sigprocmask(how, set, oldset)- 设置/查询信号阻塞掩码
 *   sys_sigreturn()                  - 恢复信号帧和 blocked 掩码
 */
