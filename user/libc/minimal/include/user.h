/*
 * user/libc/minimal/include/user.h - 用户态系统调用封装
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

#include <uapi/tty.h>

typedef unsigned long size_t;

/* Linux riscv64 系统调用号 */
#define SYS_getcwd	   17
#define SYS_dup		   23
#define SYS_dup3	   24
#define SYS_ioctl	   29
#define SYS_mkdirat	   34
#define SYS_unlinkat	   35
#define SYS_chdir	   49
#define SYS_faccessat	   48
#define SYS_openat	   56
#define SYS_close	   57
#define SYS_pipe2	   59
#define SYS_getdents64	   61
#define SYS_read	   63
#define SYS_write	   64
#define SYS_readv	   65
#define SYS_writev	   66
#define SYS_pread64	   67
#define SYS_pwrite64	   68
#define SYS_readlinkat	   78
#define SYS_newfstatat	   79
#define SYS_fstat	   80
#define SYS_lseek	   62
#define SYS_exit	   93
#define SYS_exit_group	   94
#define SYS_fsync	   82
#define SYS_fdatasync	   83
#define SYS_ftruncate64	   46
#define SYS_fallocate	   47
#define SYS_statfs64	   43
#define SYS_fstatfs64	   44
#define SYS_ppoll	   73
#define SYS_set_tid_addr   96
#define SYS_futex	   98
#define SYS_set_robust_list 99
#define SYS_get_robust_list 100
#define SYS_nanosleep	   101
#define SYS_clock_gettime  113
#define SYS_clock_getres   114
#define SYS_yield	   124
#define SYS_kill	   129
#define SYS_tgkill	   131
#define SYS_rt_sigaction   134
#define SYS_rt_sigprocmask 135
#define SYS_sigreturn	   139
#define SYS_setgid	   144
#define SYS_setuid	   146
#define SYS_times	   153
#define SYS_getgroups	   158
#define SYS_setgroups	   159
#define SYS_uname	   160
#define SYS_umask	   166
#define SYS_gettimeofday   169
#define SYS_getpid	   172
#define SYS_getppid	   173
#define SYS_getuid	   174
#define SYS_geteuid	   175
#define SYS_getgid	   176
#define SYS_getegid	   177
#define SYS_gettid	   178
#define SYS_sysinfo	   179
#define SYS_brk		   214
#define SYS_munmap	   215
#define SYS_clone	   220
#define SYS_fork	   SYS_clone
#define SYS_execve	   221
#define SYS_mmap	   222
#define SYS_wait4	   260
#define SYS_prlimit64	   261
#define SYS_getrandom	   278
#define SYS_rseq	   293

#define AT_FDCWD	    -100
#define AT_REMOVEDIR	    0x200
#define AT_EMPTY_PATH	    0x1000
#define AT_SYMLINK_NOFOLLOW 0x100

#define O_RDONLY    00000000
#define O_WRONLY    00000001
#define O_RDWR	    00000002
#define O_CREAT	    00000100
#define O_EXCL	    00000200
#define O_TRUNC	    00001000
#define O_APPEND    00002000
#define O_DIRECTORY 00200000

#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define FALLOC_FL_KEEP_SIZE 0x01

#define S_IFMT	00170000
#define S_IFLNK 0120000
#define S_IFREG 0100000
#define S_IFBLK 0060000
#define S_IFDIR 0040000
#define S_IFCHR 0020000
#define S_IFIFO 0010000
#define S_IRUSR 00400
#define S_IWUSR 00200
#define S_IXUSR 00100
#define S_IRGRP 00040
#define S_IWGRP 00020
#define S_IXGRP 00010
#define S_IROTH 00004
#define S_IWOTH 00002
#define S_IXOTH 00001

#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)

#define DT_UNKNOWN 0
#define DT_FIFO	   1
#define DT_CHR	   2
#define DT_DIR	   4
#define DT_BLK	   6
#define DT_REG	   8
#define DT_LNK	   10
#define DT_SOCK	   12

#define ENOENT	2
#define EINTR	4
#define EACCES	13
#define EEXIST	17
#define ENOTDIR 20
#define EISDIR	21
#define EINVAL	22
#define ENOTTY	25
#define EAGAIN	11
#define EFAULT	14
#define EBADF	9
#define ENOSYS	38
#define ELOOP	40
#define ETIMEDOUT 110

#define POLLIN	 0x0001
#define POLLOUT	 0x0004
#define POLLERR	 0x0008
#define POLLHUP	 0x0010
#define POLLNVAL 0x0020

#define GRND_NONBLOCK 0x0001
#define GRND_RANDOM   0x0002
#define GRND_INSECURE 0x0004

#define RLIM_INFINITY (~0UL)
#define RLIMIT_CPU    0
#define RLIMIT_FSIZE  1
#define RLIMIT_DATA   2
#define RLIMIT_STACK  3
#define RLIMIT_CORE   4
#define RLIMIT_RSS    5
#define RLIMIT_NPROC  6
#define RLIMIT_NOFILE 7
#define RLIMIT_AS     9

#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20

#define CLONE_VM	     0x00000100
#define CLONE_FS	     0x00000200
#define CLONE_FILES	     0x00000400
#define CLONE_SIGHAND	     0x00000800
#define CLONE_THREAD	     0x00010000
#define CLONE_SETTLS	     0x00080000
#define CLONE_PARENT_SETTID  0x00100000
#define CLONE_CHILD_CLEARTID 0x00200000
#define CLONE_CHILD_SETTID   0x01000000

#define FUTEX_WAIT	    0
#define FUTEX_WAKE	    1
#define FUTEX_PRIVATE_FLAG 128
#define FUTEX_WAITERS	    0x80000000
#define FUTEX_OWNER_DIED    0x40000000
#define FUTEX_TID_MASK	    0x3fffffff

#define CLOCK_REALTIME	0
#define CLOCK_MONOTONIC 1
#define CLOCK_BOOTTIME	7

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

struct pollfd {
	int fd;
	short events;
	short revents;
};

struct statfs64 {
	long f_type;
	long f_bsize;
	unsigned long f_blocks;
	unsigned long f_bfree;
	unsigned long f_bavail;
	unsigned long f_files;
	unsigned long f_ffree;
	int f_fsid[2];
	long f_namelen;
	long f_frsize;
	long f_flags;
	long f_spare[4];
};

struct rlimit64 {
	unsigned long rlim_cur;
	unsigned long rlim_max;
};

struct robust_list {
	struct robust_list *next;
};

struct robust_list_head {
	struct robust_list list;
	long futex_offset;
	struct robust_list *list_op_pending;
};

struct iovec {
	void *iov_base;
	size_t iov_len;
};

struct linux_dirent64 {
	unsigned long d_ino;
	long d_off;
	unsigned short d_reclen;
	unsigned char d_type;
	char d_name[];
};

/*
 * 必须与 include/kernel/stat.h 的 struct kstat 逐字节一致：内核 fstat/
 * newfstatat 直接 copy_to_user 一个 struct kstat 出来。修改任一侧都要同步。
 */
struct stat {
	unsigned long st_dev;
	unsigned long st_ino;
	unsigned int st_mode;
	unsigned int st_nlink;
	unsigned int st_uid;
	unsigned int st_gid;
	unsigned long st_rdev;
	unsigned long __pad1;
	long st_size;
	unsigned int st_blksize;
	unsigned int __pad2;
	unsigned long st_blocks;
	long st_atime_sec;
	unsigned long st_atime_nsec;
	long st_mtime_sec;
	unsigned long st_mtime_nsec;
	long st_ctime_sec;
	unsigned long st_ctime_nsec;
	unsigned int st_unused[2];
};

struct utsname {
	char sysname[65];
	char nodename[65];
	char release[65];
	char version[65];
	char machine[65];
	char domainname[65];
};

struct sysinfo {
	long uptime;
	unsigned long loads[3];
	unsigned long totalram;
	unsigned long freeram;
	unsigned long sharedram;
	unsigned long bufferram;
	unsigned long totalswap;
	unsigned long freeswap;
	unsigned short procs;
	unsigned short pad;
	unsigned long totalhigh;
	unsigned long freehigh;
	unsigned int mem_unit;
};

/*
 * 信号 ABI 常量。本块与 include/kernel/signal.h 中的同名定义刻意各自
 * 独立维护：用户态不依赖任何内核源码头文件，二者之间唯一的契约是这里
 * 描述的二进制 ABI（信号编号、sigaction 布局、SIG_DFL/IGN 哨兵值等）。
 * 只要遵守同一份 ABI，用户态程序就能在内核上运行——这正是"内核与用户态
 * 是两个独立部分"这一设计的体现。修改任一编号或字段都必须同步另一处；
 * 这是有意的边界重复，而非可消除的重复。详见 include/kernel/signal.h。
 */
#define SIGHUP	1
#define SIGINT	2
#define SIGQUIT 3
#define SIGILL	4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGBUS	7
#define SIGFPE	8
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
#define SIGSYS	31

#define NSIG 32

typedef void (*__sighandler_t)(int);
typedef void (*__sigrestorer_t)(void);

#define SIG_DFL ((__sighandler_t)0)
#define SIG_IGN ((__sighandler_t)1)
#define SIG_ERR ((__sighandler_t) - 1)

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

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

static inline long readv(int fd, const struct iovec *iov, int iovcnt)
{
	return syscall(SYS_readv, fd, (long)iov, iovcnt);
}

static inline long writev(int fd, const struct iovec *iov, int iovcnt)
{
	return syscall(SYS_writev, fd, (long)iov, iovcnt);
}

static inline long pread64(int fd, void *buf, size_t len, long offset)
{
	return syscall(SYS_pread64, fd, (long)buf, (long)len, offset);
}

static inline long pwrite64(int fd, const void *buf, size_t len, long offset)
{
	return syscall(SYS_pwrite64, fd, (long)buf, (long)len, offset);
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

static inline long ioctl(int fd, unsigned long cmd, unsigned long arg)
{
	return syscall(SYS_ioctl, fd, cmd, arg);
}

static inline long faccessat(int dfd, const char *path, int mode, int flags)
{
	return syscall(SYS_faccessat, dfd, (long)path, mode, flags);
}

static inline long fstatat(int dfd, const char *path, struct stat *st,
			   int flags)
{
	return syscall(SYS_newfstatat, dfd, (long)path, (long)st, flags);
}

static inline long fstat(int fd, struct stat *st)
{
	return syscall(SYS_fstat, fd, (long)st);
}

static inline long readlinkat(int dfd, const char *path, char *buf,
			      size_t bufsiz)
{
	return syscall(SYS_readlinkat, dfd, (long)path, (long)buf,
		       (long)bufsiz);
}

static inline long getdents64(int fd, void *dirp, size_t count)
{
	return syscall(SYS_getdents64, fd, (long)dirp, (long)count);
}

static inline long mkdirat(int dfd, const char *path, int mode)
{
	return syscall(SYS_mkdirat, dfd, (long)path, mode);
}

static inline long unlinkat(int dfd, const char *path, int flags)
{
	return syscall(SYS_unlinkat, dfd, (long)path, flags);
}

static inline long chdir(const char *path)
{
	return syscall(SYS_chdir, (long)path);
}

static inline long getcwd(char *buf, size_t size)
{
	return syscall(SYS_getcwd, (long)buf, (long)size);
}

static inline long fsync(int fd)
{
	return syscall(SYS_fsync, fd);
}

static inline long fdatasync(int fd)
{
	return syscall(SYS_fdatasync, fd);
}

static inline long ftruncate(int fd, long length)
{
	return syscall(SYS_ftruncate64, fd, length);
}

static inline long fallocate(int fd, int mode, long offset, long len)
{
	return syscall(SYS_fallocate, fd, mode, offset, len);
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

static inline long ppoll(struct pollfd *fds, size_t nfds,
			 const struct timespec *timeout,
			 const unsigned long *sigmask)
{
	return syscall(SYS_ppoll, (long)fds, nfds, (long)timeout,
		       (long)sigmask, sizeof(unsigned long));
}

static inline void exit(int code)
{
	syscall(SYS_exit, code);
	__builtin_unreachable();
}

static inline void exit_group(int code)
{
	syscall(SYS_exit_group, code);
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

static inline long set_tid_addr(int *tidptr)
{
	return syscall(SYS_set_tid_addr, (long)tidptr);
}

static inline long futex(int *uaddr, int op, int val, const void *timeout,
			 int *uaddr2, int val3)
{
	return syscall(SYS_futex, (long)uaddr, op, val, (long)timeout,
		       (long)uaddr2, val3);
}

static inline long set_robust_list(struct robust_list_head *head, size_t len)
{
	return syscall(SYS_set_robust_list, (long)head, len);
}

static inline long get_robust_list(long pid, struct robust_list_head **head,
				   long *len)
{
	return syscall(SYS_get_robust_list, pid, (long)head, (long)len);
}

static inline long nanosleep(const struct timespec *req, struct timespec *rem)
{
	return syscall(SYS_nanosleep, (long)req, (long)rem);
}

static inline long setuid(unsigned int uid)
{
	return syscall(SYS_setuid, uid);
}

static inline long setgid(unsigned int gid)
{
	return syscall(SYS_setgid, gid);
}

static inline long getgroups(int size, unsigned int *groups)
{
	return syscall(SYS_getgroups, size, (long)groups);
}

static inline long setgroups(int size, const unsigned int *groups)
{
	return syscall(SYS_setgroups, size, (long)groups);
}

static inline long uname(struct utsname *buf)
{
	return syscall(SYS_uname, (long)buf);
}

static inline long umask(unsigned int mask)
{
	return syscall(SYS_umask, mask);
}

static inline long sysinfo(struct sysinfo *info)
{
	return syscall(SYS_sysinfo, (long)info);
}

static inline long getrandom(void *buf, size_t count, unsigned int flags)
{
	return syscall(SYS_getrandom, (long)buf, count, flags);
}

static inline long prlimit64(long pid, int resource,
			     const struct rlimit64 *new_limit,
			     struct rlimit64 *old_limit)
{
	return syscall(SYS_prlimit64, pid, resource, (long)new_limit,
		       (long)old_limit);
}

static inline long statfs64(const char *path, struct statfs64 *buf)
{
	return syscall(SYS_statfs64, (long)path, (long)buf);
}

static inline long fstatfs64(int fd, struct statfs64 *buf)
{
	return syscall(SYS_fstatfs64, fd, (long)buf);
}

static inline long rseq(void *rseq_area, unsigned int rseq_len,
			int flags, unsigned int sig)
{
	return syscall(SYS_rseq, (long)rseq_area, rseq_len, flags, sig);
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
	return syscall(SYS_clone, SIGCHLD, 0, 0, 0, 0);
}

static inline long clone(unsigned long flags, void *child_stack,
			 int *parent_tid, unsigned long tls, int *child_tid)
{
	return syscall(SYS_clone, flags, (long)child_stack, (long)parent_tid,
		       tls, (long)child_tid);
}

static inline long clone_thread(unsigned long flags, void *child_stack,
				int *parent_tid, unsigned long tls,
				int *child_tid, int (*fn)(void *), void *arg)
{
	register long a0 __asm__("a0") = flags;
	register long a1 __asm__("a1") = (long)child_stack;
	register long a2 __asm__("a2") = (long)parent_tid;
	register long a3 __asm__("a3") = (long)tls;
	register long a4 __asm__("a4") = (long)child_tid;
	register long a5 __asm__("a5") = (long)fn;
	register long a6 __asm__("a6") = (long)arg;
	register long a7 __asm__("a7") = SYS_clone;

	__asm__ volatile(
		"ecall\n"
		"bnez a0, 1f\n"
		"mv a0, a6\n"
		"jalr a5\n"
		"li a7, %[sys_exit]\n"
		"ecall\n"
		"1:\n"
		: "+r"(a0)
		: "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6),
		  "r"(a7), [sys_exit] "i"(SYS_exit)
		: "memory", "ra");

	return a0;
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

static inline void *mmap(void *addr, size_t length, int prot, int flags, int fd,
			 long offset)
{
	return (void *)syscall(SYS_mmap, (long)addr, (long)length, prot, flags,
			       fd, offset);
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
