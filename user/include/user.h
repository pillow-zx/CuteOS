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
#define SYS_write	64
#define SYS_exit	93
#define SYS_sched_yield	124
#define SYS_getpid	172
#define SYS_brk		214
#define SYS_fork	1000

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
	__asm__ volatile("ecall"
			 : "+r"(_a0)
			 : "r"(_a1), "r"(a7)
			 : "memory");
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

static inline long syscall6(long n, long a, long b, long c, long d, long e, long f)
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
			 : "r"(_a1), "r"(_a2), "r"(_a3), "r"(_a4), "r"(_a5), "r"(a7)
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
#define _SYSCALL_CONCAT(x, y) _SYSCALL_CONCAT_X(x, y)
#define syscall(...) \
	_SYSCALL_CONCAT(syscall, _SYSCALL_NARGS(__VA_ARGS__))(__VA_ARGS__)

/* ---- 便捷封装 ---- */

static inline long write(int fd, const void *buf, size_t len)
{
	return syscall(SYS_write, fd, (long)buf, (long)len);
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

static inline long yield(void)
{
	return syscall0(SYS_sched_yield);
}

static inline long fork(void)
{
	return syscall0(SYS_fork);
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
