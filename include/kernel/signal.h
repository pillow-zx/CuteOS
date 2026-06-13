#ifndef _CUTEOS_KERNEL_SIGNAL_H
#define _CUTEOS_KERNEL_SIGNAL_H

/*
 * include/kernel/signal.h - 信号编号、处理函数与投递
 *
 * 声明 POSIX 信号基础设施：信号编号常量、sigaction 结构体
 * 用于注册处理函数，以及用于向用户进程投递信号的信号帧。
 *
 * Signal numbers (1-31):
 *   SIGHUP=1, SIGINT=2, SIGQUIT=3, SIGILL=4, SIGTRAP=5,
 *   SIGABRT=6, SIGBUS=7, SIGFPE=8, SIGKILL=9, SIGUSR1=10,
 *   SIGSEGV=11, SIGUSR2=12, SIGPIPE=13, SIGALRM=14, SIGTERM=15,
 *   ... up to SIGSYS=31
 *
 * Structs:
 *   struct sigaction   - Handler description (sa_handler, sa_mask, sa_flags)
 *   struct signal_frame - Saved trap frame for signal delivery/return
 *
 * Constants:
 *   SIG_DFL - Default signal handler sentinel
 *   SIG_IGN - Ignore signal sentinel
 *
 * TODO(signal): Stage 6 完整信号机制实现时，把 task_struct 中的
 * sighand/blocked/pending 访问收敛到 signal.c 提供的 API。
 */

#define SIGINT		2
#define SIGKILL		9
#define SIGSEGV		13
#define SIGTERM		15
#define SIGUSR1		10
#define SIGUSR2		12
#define SIGSTOP		19
#define SIGCONT		18
#define SIGCHLD		17

#endif
