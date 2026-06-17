#ifndef _CUTEOS_KERNEL_SYSCALL_H
#define _CUTEOS_KERNEL_SYSCALL_H

/*
 * include/kernel/syscall.h - 系统调用号（Linux riscv64 ABI）
 *
 * 定义 SYS_* 常量，编号遵循 Linux riscv64 ABI，以便与 busybox 等
 * 用户空间二进制程序兼容。
 *
 * 系统调用分发：
 *   syscall_table[] - 按 SYS_* 编号索引的函数指针数组
 *   do_syscall(tf)  - 从 trap_handler 在用户模式 ecall 时调用
 *
 * 已实现的入口在 syscall/syscall.c::syscall_init() 中登记；尚未实现的
 * 兼容入口集中在 syscall/sys_stub.c 返回 -ENOSYS。具体编号请直接查阅
 * 下面的 #define，避免在此维护易过期的编号清单。
 */

#include <kernel/types.h>
#include <asm/csr.h>

#define SYS_getcwd	      17
#define SYS_epoll_create1     20
#define SYS_epoll_ctl	      21
#define SYS_epoll_pwait	      22
#define SYS_dup		      23
#define SYS_dup3	      24
#define SYS_ioctl	      29
#define SYS_mknodat	      33
#define SYS_mkdirat	      34
#define SYS_unlinkat	      35
#define SYS_umount	      39
#define SYS_mount	      40
#define SYS_statfs64	      43
#define SYS_fstatfs64	      44
#define SYS_ftruncate64	      46
#define SYS_fallocate	      47
#define SYS_faccessat	      48
#define SYS_chdir	      49
#define SYS_openat	      56
#define SYS_close	      57
#define SYS_pipe2	      59
#define SYS_getdents64	      61
#define SYS_lseek	      62
#define SYS_read	      63
#define SYS_write	      64
#define SYS_readv	      65
#define SYS_writev	      66
#define SYS_pread64	      67
#define SYS_pwrite64	      68
#define SYS_ppoll	      73
#define SYS_readlinkat	      78
#define SYS_newfstatat	      79
#define SYS_fstat	      80
#define SYS_fsync	      82
#define SYS_fdatasync	      83
#define SYS_exit	      93
#define SYS_exit_group	      94
#define SYS_set_tid_addr      96
#define SYS_futex	      98
#define SYS_set_robust_list   99
#define SYS_get_robust_list   100
#define SYS_nanosleep	      101
#define SYS_getitimer	      102
#define SYS_setitimer	      103
#define SYS_timer_gettime     108
#define SYS_timer_getoverrun  109
#define SYS_timer_settime     110
#define SYS_timer_delete      111
#define SYS_clock_settime     112
#define SYS_clock_gettime     113
#define SYS_clock_getres      114
#define SYS_clock_nanosleep   115
#define SYS_timer_create      107
#define SYS_sched_setaffinity 122
#define SYS_sched_getaffinity 123
#define SYS_yield	      124
#define SYS_kill	      129
#define SYS_tgkill	      131
#define SYS_sigaltstack	      132
#define SYS_rt_sigaction      134
#define SYS_rt_sigprocmask    135
#define SYS_sigreturn	      139
#define SYS_setgid	      144
#define SYS_setuid	      146
#define SYS_times	      153
#define SYS_getgroups	      158
#define SYS_setgroups	      159
#define SYS_uname	      160
#define SYS_umask	      166
#define SYS_gettimeofday      169
#define SYS_getpid	      172
#define SYS_getppid	      173
#define SYS_getuid	      174
#define SYS_geteuid	      175
#define SYS_getgid	      176
#define SYS_getegid	      177
#define SYS_gettid	      178
#define SYS_sysinfo	      179
#define SYS_brk		      214
#define SYS_munmap	      215
#define SYS_mremap	      216
#define SYS_clone	      220
#define SYS_execve	      221
#define SYS_mmap	      222
#define SYS_mprotect	      226
#define SYS_mlock	      228
#define SYS_munlock	      229
#define SYS_mincore	      232
#define SYS_madvise	      233
#define SYS_wait4	      260
#define SYS_prlimit64	      261
#define SYS_renameat2	      276
#define SYS_getrandom	      278
#define SYS_rseq	      293

#define NR_SYSCALL (SYS_rseq + 1)

#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20

#define CLONE_NEWTIME	      0x00000080
#define CLONE_VM	      0x00000100
#define CLONE_FS	      0x00000200
#define CLONE_FILES	      0x00000400
#define CLONE_SIGHAND	      0x00000800
#define CLONE_PTRACE	      0x00002000
#define CLONE_VFORK	      0x00004000
#define CLONE_PARENT	      0x00008000
#define CLONE_THREAD	      0x00010000
#define CLONE_NEWNS	      0x00020000
#define CLONE_SYSVSEM	      0x00040000
#define CLONE_SETTLS	      0x00080000
#define CLONE_PARENT_SETTID   0x00100000
#define CLONE_CHILD_CLEARTID  0x00200000
#define CLONE_CHILD_SETTID    0x01000000
#define CLONE_NEWCGROUP	      0x02000000
#define CLONE_NEWUTS	      0x04000000
#define CLONE_NEWIPC	      0x08000000
#define CLONE_NEWUSER	      0x10000000
#define CLONE_NEWPID	      0x20000000
#define CLONE_NEWNET	      0x40000000
#define CLONE_IO	      0x80000000

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
ssize_t sys_read(struct trap_frame *tf);
ssize_t sys_readv(struct trap_frame *tf);
ssize_t sys_writev(struct trap_frame *tf);
ssize_t sys_pread64(struct trap_frame *tf);
ssize_t sys_pwrite64(struct trap_frame *tf);
ssize_t sys_openat(struct trap_frame *tf);
ssize_t sys_close(struct trap_frame *tf);
ssize_t sys_lseek(struct trap_frame *tf);
ssize_t sys_ioctl(struct trap_frame *tf);
ssize_t sys_mkdirat(struct trap_frame *tf);
ssize_t sys_unlinkat(struct trap_frame *tf);
ssize_t sys_chdir(struct trap_frame *tf);
ssize_t sys_getcwd(struct trap_frame *tf);
ssize_t sys_faccessat(struct trap_frame *tf);
ssize_t sys_getdents64(struct trap_frame *tf);
ssize_t sys_newfstatat(struct trap_frame *tf);
ssize_t sys_fstat(struct trap_frame *tf);
ssize_t sys_fsync(struct trap_frame *tf);
ssize_t sys_fdatasync(struct trap_frame *tf);
ssize_t sys_ftruncate64(struct trap_frame *tf);
ssize_t sys_fallocate(struct trap_frame *tf);
ssize_t sys_mknod(struct trap_frame *tf);
ssize_t sys_dup(struct trap_frame *tf);
ssize_t sys_dup3(struct trap_frame *tf);
ssize_t sys_pipe2(struct trap_frame *tf);
ssize_t sys_exit(struct trap_frame *tf);
ssize_t sys_exit_group(struct trap_frame *tf);
ssize_t sys_yield(struct trap_frame *tf);
ssize_t sys_getpid(struct trap_frame *tf);
ssize_t sys_getppid(struct trap_frame *tf);
ssize_t sys_getuid(struct trap_frame *tf);
ssize_t sys_geteuid(struct trap_frame *tf);
ssize_t sys_getgid(struct trap_frame *tf);
ssize_t sys_getegid(struct trap_frame *tf);
ssize_t sys_gettid(struct trap_frame *tf);
ssize_t sys_set_tid_addr(struct trap_frame *tf);
ssize_t sys_setuid(struct trap_frame *tf);
ssize_t sys_setgid(struct trap_frame *tf);
ssize_t sys_getgroups(struct trap_frame *tf);
ssize_t sys_setgroups(struct trap_frame *tf);
ssize_t sys_uname(struct trap_frame *tf);
ssize_t sys_umask(struct trap_frame *tf);
ssize_t sys_sysinfo(struct trap_frame *tf);
ssize_t sys_brk(struct trap_frame *tf);
ssize_t sys_mmap(struct trap_frame *tf);
ssize_t sys_munmap(struct trap_frame *tf);
ssize_t sys_mremap(struct trap_frame *tf);
ssize_t sys_mprotect(struct trap_frame *tf);
ssize_t sys_mlock(struct trap_frame *tf);
ssize_t sys_munlock(struct trap_frame *tf);
ssize_t sys_mincore(struct trap_frame *tf);
ssize_t sys_madvise(struct trap_frame *tf);
ssize_t sys_fork(struct trap_frame *tf);
ssize_t sys_clone(struct trap_frame *tf);
ssize_t sys_execve(struct trap_frame *tf);
void exec_user_path(const char *path) __noreturn;
ssize_t sys_wait4(struct trap_frame *tf);
ssize_t sys_epoll_create1(struct trap_frame *tf);
ssize_t sys_epoll_ctl(struct trap_frame *tf);
ssize_t sys_epoll_pwait(struct trap_frame *tf);
ssize_t sys_umount(struct trap_frame *tf);
ssize_t sys_mount(struct trap_frame *tf);
ssize_t sys_statfs64(struct trap_frame *tf);
ssize_t sys_fstatfs64(struct trap_frame *tf);
ssize_t sys_ppoll(struct trap_frame *tf);
ssize_t sys_readlinkat(struct trap_frame *tf);
ssize_t sys_futex(struct trap_frame *tf);
ssize_t sys_set_robust_list(struct trap_frame *tf);
ssize_t sys_get_robust_list(struct trap_frame *tf);
ssize_t sys_sched_setaffinity(struct trap_frame *tf);
ssize_t sys_sched_getaffinity(struct trap_frame *tf);
ssize_t sys_kill(struct trap_frame *tf);
ssize_t sys_tgkill(struct trap_frame *tf);
ssize_t sys_sigaltstack(struct trap_frame *tf);
ssize_t sys_sigaction(struct trap_frame *tf);
ssize_t sys_sigprocmask(struct trap_frame *tf);
ssize_t sys_sigreturn(struct trap_frame *tf);
ssize_t sys_nanosleep(struct trap_frame *tf);
ssize_t sys_getitimer(struct trap_frame *tf);
ssize_t sys_setitimer(struct trap_frame *tf);
ssize_t sys_timer_create(struct trap_frame *tf);
ssize_t sys_timer_gettime(struct trap_frame *tf);
ssize_t sys_timer_getoverrun(struct trap_frame *tf);
ssize_t sys_timer_settime(struct trap_frame *tf);
ssize_t sys_timer_delete(struct trap_frame *tf);
ssize_t sys_clock_settime(struct trap_frame *tf);
ssize_t sys_clock_gettime(struct trap_frame *tf);
ssize_t sys_clock_getres(struct trap_frame *tf);
ssize_t sys_clock_nanosleep(struct trap_frame *tf);
ssize_t sys_times(struct trap_frame *tf);
ssize_t sys_gettimeofday(struct trap_frame *tf);
ssize_t sys_prlimit64(struct trap_frame *tf);
ssize_t sys_renameat2(struct trap_frame *tf);
ssize_t sys_getrandom(struct trap_frame *tf);
ssize_t sys_rseq(struct trap_frame *tf);

/* ---- 系统调用分发接口 ---- */

void do_syscall(struct trap_frame *tf);
void syscall_init(void);

#endif
