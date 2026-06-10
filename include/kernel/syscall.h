#ifndef _CUTEOS_KERNEL_SYSCALL_H
#define _CUTEOS_KERNEL_SYSCALL_H

/*
 * include/kernel/syscall.h - 系统调用号（Linux riscv64 ABI）
 *
 * 定义约 34 个系统调用的 SYS_* 常量。
 * 编号遵循 Linux riscv64 ABI，以确保与 busybox 等
 * 用户空间二进制程序兼容。
 *
 * 系统调用分发：
 *   syscall_table[] - 按 SYS_* 编号索引的函数指针数组
 *   do_syscall(tf)  - 从 trap_handler 在用户模式 ecall 时调用
 *
 * 支持的 SYS_* 编号：
 *   17  getcwd      23  dup         24  dup3
 *   34  mkdirat     35  unlinkat    49  chdir
 *   56  openat      57  close       59  pipe2
 *   61  getdents64  62  lseek       63  read         64  write
 *   80  fstat
 *   93  exit        94  exit_group  96  set_tid_addr
 *   101 nanosleep   124 sched_yield 129 kill
 *   134 sigaction   135 sigprocmask 139 sigreturn
 *   160 uname       169 gettimeofday
 *   172 getpid      173 getppid     174 getuid       175 getgid
 *   214 brk         215 munmap      222 mmap         226 mprotect
 *   260 wait4
 *   1000 fork       1001 execve     (cuteOS-private numbers)
 */

#include <kernel/types.h>
#include <asm/csr.h>

#define SYS_getcwd	 17
#define SYS_dup		 23
#define SYS_dup3	 24
#define SYS_mkdirat	 34
#define SYS_unlinkat	 35
#define SYS_chdir	 49
#define SYS_openat	 56
#define SYS_close	 57
#define SYS_pipe2	 59
#define SYS_getdents64	 61
#define SYS_lseek	 62
#define SYS_read	 63
#define SYS_write	 64
#define SYS_fstat	 80
#define SYS_exit	 93
#define SYS_exit_group	 94
#define SYS_set_tid_addr 96
#define SYS_nanosleep	 101
#define SYS_sched_yield	 124
#define SYS_kill	 129
#define SYS_sigaction	 134
#define SYS_sigprocmask	 135
#define SYS_sigreturn	 139
#define SYS_uname	 160
#define SYS_gettimeofday 169
#define SYS_getpid	 172
#define SYS_getppid	 173
#define SYS_getuid	 174
#define SYS_getgid	 175
#define SYS_brk		 214
#define SYS_munmap	 215
#define SYS_execve	 221
#define SYS_mmap	 222
#define SYS_mprotect	 226
#define SYS_wait4	 260
#define SYS_fork	 1000

static __always_inline bool user_access_begin(void)
{
	bool had_sum = (csr_read(sstatus) & SSTATUS_SUM) != 0;
	if (!had_sum)
		csr_set(sstatus, SSTATUS_SUM);
	return had_sum;
}

static __always_inline void user_access_end(bool had_sum)
{
	if (!had_sum)
		csr_clear(sstatus, SSTATUS_SUM);
}

struct trap_frame;

ssize_t sys_write(struct trap_frame *tf);
ssize_t sys_exit(struct trap_frame *tf);
ssize_t sys_brk(struct trap_frame *tf);

/* ---- 系统调用分发接口 ---- */

void do_syscall(struct trap_frame *tf);
void syscall_init(void);

#endif
