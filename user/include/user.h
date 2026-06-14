/*
 * user/include/user.h - 用户态系统调用封装
 *
 * 功能：
 *   提供用户态程序的 ecall 系统调用封装。使用内联汇编将系统调用号
 *   放入 a7，参数放入 a0~a5，执行 ecall 触发 U→S trap。
 *   返回值通过 a0 传回。
 *
 *   使用宏重载技术，syscall() 宏根据参数个数自动选择 syscall0~syscall6，
 *   无需手写 syscallN 前缀。系统调用号遵循 Linux riscv64 ABI。
 */

#ifndef _USER_H
#define _USER_H

typedef unsigned long size_t;

/* Linux riscv64 系统调用号 */
#define SYS_dup	    23
#define SYS_dup3    24
#define SYS_openat  56
#define SYS_close   57
#define SYS_pipe2   59
#define SYS_read    63
#define SYS_write   64
#define SYS_exit    93
#define SYS_clock_gettime 113
#define SYS_clock_getres  114
#define SYS_yield   124
#define SYS_kill    129
#define SYS_tgkill  131
#define SYS_rt_sigaction   134
#define SYS_rt_sigprocmask 135
#define SYS_sigreturn	    139
#define SYS_times   153
#define SYS_gettimeofday 169
#define SYS_getpid  172
#define SYS_getppid 173
#define SYS_getuid  174
#define SYS_geteuid 175
#define SYS_getgid  176
#define SYS_getegid 177
#define SYS_gettid  178
#define SYS_brk	    214
#define SYS_munmap  215
#define SYS_fork    220
#define SYS_execve  221
#define SYS_mmap    222
#define SYS_wait4   260

#define AT_FDCWD    -100

#define O_RDONLY    00000000
#define O_WRONLY    00000001
#define O_RDWR	    00000002
#define O_CREAT	    00000100
#define O_EXCL	    00000200
#define O_TRUNC	    00001000
#define O_APPEND    00002000

#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4

#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1
#define CLOCK_BOOTTIME  7

struct tms {
	long tms_utime;
	long tms_stime;
	long tms_cutime;
	long tms_cstime;
};

struct timeval {
	long tv_sec;
	long tv_usec;
};

struct timespec {
	long tv_sec;
	long tv_nsec;
};

/*
 * 信号 ABI 常量。本块与 include/kernel/signal.h 中的同名定义刻意各自
 * 独立维护：用户态不依赖任何内核源码头文件，二者之间唯一的契约是这里
 * 描述的二进制 ABI（信号编号、sigaction 布局、SIG_DFL/IGN 哨兵值等）。
 * 只要遵守同一份 ABI，用户态程序就能在内核上运行——这正是"内核与用户态
 * 是两个独立部分"这一设计的体现。修改任一编号或字段都必须同步另一处；
 * 这是有意的边界重复，而非可消除的重复。详见 include/kernel/signal.h。
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

/* 因信号默认终止时的 wait 状态编码：低字节 = 128 + 信号号。 */
#define SIGNAL_EXIT_CODE(sig) (128 + (sig))

/* ---- syscallN: 底层内联汇编封装 (a0~a5, 最多 6 个参数) ---- */

static inline long syscall0(long n)
{
	register long a7 __asm__("a7") = n;
	register long a0 __asm__("a0");
	__asm__ volatile("ecall" : "=r"(a0) : "r"(a7) : "memory");
	return a0;
}

static inline long syscall1(long n, long a)
{
	register long a7 __asm__("a7") = n;
	register long _a0 __asm__("a0") = a;
	__asm__ volatile("ecall" : "+r"(_a0) : "r"(a7) : "memory");
	return _a0;
}

static inline long syscall2(long n, long a, long b)
{
	register long a7 __asm__("a7") = n;
	register long _a0 __asm__("a0") = a;
	register long _a1 __asm__("a1") = b;
	__asm__ volatile("ecall" : "+r"(_a0) : "r"(_a1), "r"(a7) : "memory");
	return _a0;
}

static inline long syscall3(long n, long a, long b, long c)
{
	register long a7 __asm__("a7") = n;
	register long _a0 __asm__("a0") = a;
	register long _a1 __asm__("a1") = b;
	register long _a2 __asm__("a2") = c;
	__asm__ volatile("ecall"
			 : "+r"(_a0)
			 : "r"(_a1), "r"(_a2), "r"(a7)
			 : "memory");
	return _a0;
}

static inline long syscall4(long n, long a, long b, long c, long d)
{
	register long a7 __asm__("a7") = n;
	register long _a0 __asm__("a0") = a;
	register long _a1 __asm__("a1") = b;
	register long _a2 __asm__("a2") = c;
	register long _a3 __asm__("a3") = d;
	__asm__ volatile("ecall"
			 : "+r"(_a0)
			 : "r"(_a1), "r"(_a2), "r"(_a3), "r"(a7)
			 : "memory");
	return _a0;
}

static inline long syscall5(long n, long a, long b, long c, long d, long e)
{
	register long a7 __asm__("a7") = n;
	register long _a0 __asm__("a0") = a;
	register long _a1 __asm__("a1") = b;
	register long _a2 __asm__("a2") = c;
	register long _a3 __asm__("a3") = d;
	register long _a4 __asm__("a4") = e;
	__asm__ volatile("ecall"
			 : "+r"(_a0)
			 : "r"(_a1), "r"(_a2), "r"(_a3), "r"(_a4), "r"(a7)
			 : "memory");
	return _a0;
}

static inline long syscall6(long n, long a, long b, long c, long d, long e,
			    long f)
{
	register long a7 __asm__("a7") = n;
	register long _a0 __asm__("a0") = a;
	register long _a1 __asm__("a1") = b;
	register long _a2 __asm__("a2") = c;
	register long _a3 __asm__("a3") = d;
	register long _a4 __asm__("a4") = e;
	register long _a5 __asm__("a5") = f;
	__asm__ volatile("ecall"
			 : "+r"(_a0)
			 : "r"(_a1), "r"(_a2), "r"(_a3), "r"(_a4), "r"(_a5),
			   "r"(a7)
			 : "memory");
	return _a0;
}

/*
 * 宏重载：syscall(n, ...) 根据参数个数自动展开为 syscallN(n, ...)
 *
 * 展开示例：
 *   syscall(SYS_exit, code)        → syscall1(SYS_exit, code)
 *   syscall(SYS_write, fd, buf, n) → syscall3(SYS_write, fd, buf, n)
 *
 * 原理：_SYSCALL_NARGS_X 对参数计数（syscall 号始终存在，无空 VA_ARGS 问题），
 *        _SYSCALL_CONCAT 将计数拼接到 "syscall" 前缀形成函数名。
 */
#define _SYSCALL_NARGS_X(_1, _2, _3, _4, _5, _6, _7, n, ...) n
#define _SYSCALL_NARGS(...) _SYSCALL_NARGS_X(__VA_ARGS__, 6, 5, 4, 3, 2, 1, 0)
#define _SYSCALL_CONCAT_X(x, y) x##y
#define _SYSCALL_CONCAT(x, y)	_SYSCALL_CONCAT_X(x, y)
#define syscall(...)                                                           \
	_SYSCALL_CONCAT(syscall, _SYSCALL_NARGS(__VA_ARGS__))(__VA_ARGS__)

/* ---- 便捷封装 ---- */

static inline long write(int fd, const void *buf, size_t len)
{
	return syscall(SYS_write, fd, (long)buf, (long)len);
}

static inline long read(int fd, void *buf, size_t len)
{
	return syscall(SYS_read, fd, (long)buf, (long)len);
}

static inline long openat(int dfd, const char *path, int flags, int mode)
{
	return syscall(SYS_openat, dfd, (long)path, flags, mode);
}

static inline long open(const char *path, int flags)
{
	return openat(AT_FDCWD, path, flags, 0);
}

static inline long close(int fd)
{
	return syscall(SYS_close, fd);
}

static inline long dup(int oldfd)
{
	return syscall(SYS_dup, oldfd);
}

static inline long dup2(int oldfd, int newfd)
{
	return syscall(SYS_dup3, oldfd, newfd, 0);
}

static inline long pipe(int pipefd[2])
{
	return syscall(SYS_pipe2, (long)pipefd, 0);
}

static inline void exit(int code)
{
	syscall(SYS_exit, code);
	__builtin_unreachable();
}

static inline long getpid(void)
{
	return syscall0(SYS_getpid);
}

static inline long getppid(void)
{
	return syscall0(SYS_getppid);
}

static inline long getuid(void)
{
	return syscall0(SYS_getuid);
}

static inline long geteuid(void)
{
	return syscall0(SYS_geteuid);
}

static inline long getgid(void)
{
	return syscall0(SYS_getgid);
}

static inline long getegid(void)
{
	return syscall0(SYS_getegid);
}

static inline long gettid(void)
{
	return syscall0(SYS_gettid);
}

static inline long yield(void)
{
	return syscall0(SYS_yield);
}

static inline long kill(long pid, int sig)
{
	return syscall(SYS_kill, pid, sig);
}

static inline long tgkill(long tgid, long tid, int sig)
{
	return syscall(SYS_tgkill, tgid, tid, sig);
}

static inline long sigaction(int sig, const struct sigaction *act,
			     struct sigaction *oldact)
{
	return syscall(SYS_rt_sigaction, sig, (long)act, (long)oldact,
		       sizeof(unsigned long));
}

static inline long sigprocmask(int how, const unsigned long *set,
			       unsigned long *oldset)
{
	return syscall(SYS_rt_sigprocmask, how, (long)set, (long)oldset,
		       sizeof(unsigned long));
}

static inline long fork(void)
{
	return syscall0(SYS_fork);
}

static inline long execve(const char *path, char *const argv[],
			  char *const envp[])
{
	return syscall(SYS_execve, (long)path, (long)argv, (long)envp);
}

static inline long wait4(long pid, int *status, int options, void *rusage)
{
	return syscall(SYS_wait4, pid, (long)status, options, (long)rusage);
}

static inline long wait(int *status)
{
	return wait4(-1, status, 0, 0);
}

static inline long times(struct tms *buf)
{
	return syscall(SYS_times, (long)buf);
}

static inline long gettimeofday(struct timeval *tv, void *tz)
{
	return syscall(SYS_gettimeofday, (long)tv, (long)tz);
}

static inline long clock_gettime(int clock_id, struct timespec *ts)
{
	return syscall(SYS_clock_gettime, clock_id, (long)ts);
}

static inline long clock_getres(int clock_id, struct timespec *ts)
{
	return syscall(SYS_clock_getres, clock_id, (long)ts);
}

/*
 * brk - 设置/查询堆顶地址
 * @addr: 新的 brk 地址，0 表示查询当前值
 *
 * 返回新的 brk 地址（失败时返回原值）。
 */
static inline long brk(long addr)
{
	return syscall(SYS_brk, addr);
}

static inline void *mmap(void *addr, size_t length, int prot, int flags,
			 int fd, long offset)
{
	return (void *)syscall(SYS_mmap, (long)addr, (long)length, prot,
			       flags, fd, offset);
}

static inline long munmap(void *addr, size_t length)
{
	return syscall(SYS_munmap, (long)addr, (long)length);
}

/*
 * sbrk - 增量式堆扩展（用户态实现）
 * @incr: 要增加的字节数
 *
 * 返回增加前的旧 brk 地址，失败返回 -1。
 */
static inline long sbrk(long incr)
{
	long old = brk(0);
	long new_addr = old + incr;
	if (brk(new_addr) != new_addr)
		return -1;
	return old;
}

#endif
