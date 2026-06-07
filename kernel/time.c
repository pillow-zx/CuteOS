/*
 * kernel/time.c - 时间子系统
 *
 * 功能：
 *   提供时间相关系统调用和内核时间工具函数。管理全局 jiffies 计数器，
 *   实现用户态程序可调用的时间服务。
 *
 * 主要函数：
 *   sys_times(buf)           - times 系统调用。基于 jiffies 返回进程的
 *                              用户态时间和内核态时间（tms_utime / tms_stime），
 *                              以及 cutime / cstime（子进程累计时间）。
 *   sys_gettimeofday(tv, tz) - gettimeofday 系统调用。
 *                              读取 CLINT 的 mtime 寄存器，
 *                              将其转换为秒 + 微秒精度的时间值。
 */
