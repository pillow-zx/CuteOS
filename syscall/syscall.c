/*
 * syscall/syscall.c - 系统调用分发
 *
 * 功能：
 *   管理系统调用分发表（syscall_table）。每个系统调用处理器接收完整的
 *   trap_frame 指针，直接从寄存器字段提取参数，无需占位符。
 *   从 tf->a7 读取系统调用号，调用 syscall_table[nr](tf)，
 *   返回值写入 tf->a0。未知的系统调用号返回 -ENOSYS。
 *
 * 主要函数：
 *   do_syscall(tf)
 *
 * 关键类型：
 *   syscall_fn_t           - 系统调用处理函数指针
 */

#include <kernel/syscall.h>
#include <kernel/errno.h>
#include <kernel/printk.h>
#include <kernel/task.h>
#include <kernel/mm.h>
#include <kernel/exit.h>
#include <asm/trap.h>
#include <asm/csr.h>
#include <drivers/uart.h>

typedef ssize_t (*syscall_fn_t)(struct trap_frame *);

static syscall_fn_t syscall_table[NR_SYSCALL];

void do_syscall(struct trap_frame *tf)
{
	size_t nr = tf->a7;

	if (nr >= NR_SYSCALL || !syscall_table[nr]) {
		tf->a0 = (uint64_t)(-ENOSYS);
		return;
	}

	/*
	 * Some successful syscalls may rewrite the trap frame in place. execve
	 * installs the new user context this way; after the handler returns,
	 * keep dispatcher post-processing limited to storing the return value.
	 */
	tf->a0 = (size_t)syscall_table[nr](tf);
}

void syscall_init(void)
{
	syscall_table[SYS_getcwd] = sys_getcwd;
	syscall_table[SYS_ioctl] = sys_ioctl;
	syscall_table[SYS_mknodat] = sys_mknod;
	syscall_table[SYS_mkdirat] = sys_mkdirat;
	syscall_table[SYS_unlinkat] = sys_unlinkat;
	syscall_table[SYS_chdir] = sys_chdir;
	syscall_table[SYS_openat] = sys_openat;
	syscall_table[SYS_write] = sys_write;
	syscall_table[SYS_read] = sys_read;
	syscall_table[SYS_close] = sys_close;
	syscall_table[SYS_pipe2] = sys_pipe2;
	syscall_table[SYS_getdents64] = sys_getdents64;
	syscall_table[SYS_lseek] = sys_lseek;
	syscall_table[SYS_fstat] = sys_fstat;
	syscall_table[SYS_dup] = sys_dup;
	syscall_table[SYS_dup3] = sys_dup3;
	syscall_table[SYS_exit] = sys_exit;
	syscall_table[SYS_exit_group] = sys_exit;
	syscall_table[SYS_nanosleep] = sys_nanosleep;
	syscall_table[SYS_getitimer] = sys_getitimer;
	syscall_table[SYS_setitimer] = sys_setitimer;
	syscall_table[SYS_timer_create] = sys_timer_create;
	syscall_table[SYS_timer_gettime] = sys_timer_gettime;
	syscall_table[SYS_timer_getoverrun] = sys_timer_getoverrun;
	syscall_table[SYS_timer_settime] = sys_timer_settime;
	syscall_table[SYS_timer_delete] = sys_timer_delete;
	syscall_table[SYS_clock_settime] = sys_clock_settime;
	syscall_table[SYS_clock_gettime] = sys_clock_gettime;
	syscall_table[SYS_clock_getres] = sys_clock_getres;
	syscall_table[SYS_clock_nanosleep] = sys_clock_nanosleep;
	syscall_table[SYS_yield] = sys_yield;
	syscall_table[SYS_kill] = sys_kill;
	syscall_table[SYS_tgkill] = sys_tgkill;
	syscall_table[SYS_sigaltstack] = sys_sigaltstack;
	syscall_table[SYS_rt_sigaction] = sys_sigaction;
	syscall_table[SYS_rt_sigprocmask] = sys_sigprocmask;
	syscall_table[SYS_sigreturn] = sys_sigreturn;
	syscall_table[SYS_times] = sys_times;
	syscall_table[SYS_gettimeofday] = sys_gettimeofday;
	syscall_table[SYS_getpid] = sys_getpid;
	syscall_table[SYS_getppid] = sys_getppid;
	syscall_table[SYS_getuid] = sys_getuid;
	syscall_table[SYS_geteuid] = sys_geteuid;
	syscall_table[SYS_getgid] = sys_getgid;
	syscall_table[SYS_getegid] = sys_getegid;
	syscall_table[SYS_gettid] = sys_gettid;
	syscall_table[SYS_brk] = sys_brk;
	syscall_table[SYS_munmap] = sys_munmap;
	syscall_table[SYS_clone] = sys_fork;
	syscall_table[SYS_execve] = sys_execve;
	syscall_table[SYS_mmap] = sys_mmap;
	syscall_table[SYS_wait4] = sys_wait4;

	printk("syscall: initialized (%d entries)\n", NR_SYSCALL);
}
