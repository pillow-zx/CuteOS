/*
 * user/include/user.h - 用户态系统调用封装
 *
 * 功能：
 *   提供用户态程序的 ecall 系统调用封装。使用内联汇编将系统调用号
 *   放入 a7，参数放入 a0~a2，执行 ecall 触发 U→S trap。
 *   返回值通过 a0 传回。
 *
 *   当前仅提供 syscall0（用于 exit）和 syscall3（用于 write）。
 *   系统调用号遵循 Linux riscv64 ABI。
 */

#ifndef _USER_H
#define _USER_H

typedef unsigned long size_t;

/* Linux riscv64 系统调用号 */
#define SYS_write	64
#define SYS_exit	93

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

static inline long write(int fd, const void *buf, size_t len)
{
	return syscall3(SYS_write, fd, (long)buf, (long)len);
}

static inline void exit(int code)
{
	syscall1(SYS_exit, code);
	__builtin_unreachable();
}

#endif
