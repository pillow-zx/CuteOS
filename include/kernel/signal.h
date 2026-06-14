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
 * 信号状态封装：task_struct 中 sigactions / blocked / pending / in_handler
 * 的访问全部收敛到本文件提供的 API（send_signal / force_signal /
 * signals_clone 等）；外部模块（如 fork）不直接读写这些字段，从而把信号
 * 状态的表示作为 signal.c 的私有实现细节。
 */

#include <kernel/types.h>
#include <asm/page.h>
#include <asm/pte.h>
#include <asm/trap.h>

/*
 * 信号 ABI 常量。本块与 user/include/user.h 中的同名定义刻意各自独立
 * 维护：内核态与用户态是两个独立的编译单元（后续将彻底拆分），二者之间
 * 唯一的契约是此处描述的二进制 ABI（信号编号、sigaction 布局、
 * SIG_DFL/IGN 哨兵值等）。修改任一编号或字段都必须同步另一处——这是有
 * 意的边界重复，而非可消除的重复。详见 user/include/user.h 的对应注释。
 */
#define SIGHUP		1
#define SIGINT		2
#define SIGQUIT		3
#define SIGILL		4
#define SIGTRAP		5
#define SIGABRT		6
#define SIGBUS		7
#define SIGFPE		8
#define SIGKILL		9
#define SIGUSR1		10
#define SIGSEGV		11
#define SIGUSR2		12
#define SIGPIPE		13
#define SIGALRM		14
#define SIGTERM		15
#define SIGCHLD		17
#define SIGCONT		18
#define SIGSTOP		19
#define SIGSYS		31

#define NSIG		32

typedef void (*__sighandler_t)(int);
typedef void (*__sigrestorer_t)(void);

#define SIG_DFL		((__sighandler_t)0)
#define SIG_IGN		((__sighandler_t)1)
#define SIG_ERR		((__sighandler_t)-1)

#define SIG_BLOCK	0
#define SIG_UNBLOCK	1
#define SIG_SETMASK	2

struct sigaction {
	__sighandler_t sa_handler;
	unsigned long sa_flags;
	__sigrestorer_t sa_restorer;
	unsigned long sa_mask;
};

/*
 * 进程因信号被默认终止时写入 wait 状态的编码：低字节 = 128 + 信号号。
 * 必须与 user/include/user.h 的 SIGNAL_EXIT_CODE 保持一致。
 */
#define SIGNAL_EXIT_CODE(sig) (128 + (sig))

#define SIGNAL_TRAMPOLINE_ADDR (USER_STACK_BASE - PAGE_SIZE)

struct signal_frame {
	struct trap_frame tf;
	uint64_t blocked;
	uint64_t sig; /* 本帧投递的信号号；sys_sigreturn 据此清 in_handler */
};

struct task_struct;

bool signal_is_valid(int sig);
uint64_t signal_mask(int sig);
int send_signal(int sig, struct task_struct *task);
int force_signal(int sig, struct task_struct *task);
void signals_clone(struct task_struct *child);
vaddr_t signal_trampoline_start(void);
vaddr_t signal_trampoline_end(void);
bool signal_trampoline_contains(vaddr_t addr);
bool signal_trampoline_overlaps(vaddr_t start, vaddr_t end);
void do_signal(struct trap_frame *tf);
int signal_map_trampoline(pte_t *pgd);

#endif
