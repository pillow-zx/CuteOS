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
 * 信号状态封装：sighand_struct 持有可共享的 handler 表，
 * signal_struct 持有线程组共享 pending；task_struct 仅保留每线程
 * blocked / pending / in_handler。外部模块通过 send_signal /
 * send_group_signal / signals_clone 等 API 管理生命周期和语义。
 */

#include <kernel/types.h>
#include <kernel/refcount.h>
#include <kernel/sync.h>
#include <kernel/resource.h>
#include <asm/page.h>
#include <asm/pte.h>
#include <asm/trap.h>
#include <uapi/signal.h>

struct sighand_struct {
	refcount_t refcount;
	mutex_t lock;
	struct sigaction sigactions[NSIG];
};

struct signal_struct {
	refcount_t refcount;
	mutex_t lock;
	uint64_t shared_pending;
	struct rlimit64 rlimits[RLIM_NLIMITS];
};

#define SIGNAL_TRAMPOLINE_ADDR (USER_STACK_BASE - PAGE_SIZE)

struct signal_frame {
	struct trap_frame tf;
	uint64_t blocked;
	uint64_t sig; /* 本帧投递的信号号；sys_sigreturn 据此清 in_handler */
	uint64_t on_altstack;
};

struct task_struct;

bool signal_is_valid(int sig);
uint64_t signal_mask(int sig);
bool signal_is_catchable(int sig);
uint64_t unblockable_mask(void);
uint64_t signal_blocked_mask(struct task_struct *task);
void signal_block_mask(struct task_struct *task, uint64_t mask);
void signal_unblock_mask(struct task_struct *task, uint64_t mask);
void signal_set_blocked_mask(struct task_struct *task, uint64_t mask);
void signal_mark_pending(struct task_struct *task, uint64_t mask);
void signal_clear_pending(struct task_struct *task, uint64_t mask);
void signal_enter_handler(struct task_struct *task, int sig);
void signal_leave_handler(struct task_struct *task, int sig);
void signal_clear_handlers(struct task_struct *task);
int send_signal(int sig, struct task_struct *task);
int send_group_signal(int sig, struct task_struct *leader);
int send_current_signal(int sig);
int force_signal(int sig, struct task_struct *task);
bool signal_pending(struct task_struct *task);
int signals_init(struct task_struct *task);
int signals_clone(struct task_struct *child, bool share_sighand,
		  bool share_signal, bool disable_altstack);
void signals_release(struct task_struct *task);
void do_signal(struct trap_frame *tf);
void signal_user_map_init(void);

/* 内部 API（供 syscall/sys_signal.c ABI 边界调用） */
int do_kill(pid_t pid, int sig);
int do_tgkill(pid_t tgid, pid_t tid, int sig);
int do_sigaltstack(const struct stack_t *ss, struct stack_t *old_ss);
int do_sigaction(int sig, const struct sigaction *act,
		 struct sigaction *oldact);
int do_sigprocmask(int how, const uint64_t *set, uint64_t *oldset);
int do_sigreturn(struct trap_frame *tf, uintptr_t sp);

#endif
