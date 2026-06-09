/*
 * include/kernel/timer.h - 时钟子系统公共接口
 *
 * 提供 100Hz 定时常量、jiffies 全局计数器声明、以及 Sstc 定时器
 * CSR 操作函数声明。所有需要访问时钟常量或 jiffies 的模块应包含此头文件，
 * 而不是在各自 .c 文件中 extern 声明或重复定义常量。
 *
 * 常量：
 *   HZ              - 时钟中断频率 (100 Hz)
 *   MTIME_FREQ      - mtime 计数器频率 (10 MHz, QEMU virt 平台)
 *   CLOCKS_PER_TICK - 每次时钟中断的 mtime 周期数 (MTIME_FREQ / HZ)
 *
 * 全局变量（在 arch/riscv/timer.c 中定义）：
 *   jiffies - 自启动以来的时钟中断总次数
 *
 * 函数：
 *   get_mtime()    - 读取 time CSR 获取当前时间计数器
 *   set_mtimecmp() - 写入 stimecmp CSR 设置下一次时钟中断
 *   timer_init()   - 设置首次时钟中断超时值
 */

#ifndef _CUTEOS_KERNEL_TIMER_H
#define _CUTEOS_KERNEL_TIMER_H

#include <kernel/types.h>

/* ---- 时钟常量 ---- */

#define HZ		100UL		  /* 100 Hz tick = 10ms 间隔 */
#define MTIME_FREQ	10000000UL	  /* QEMU virt: mtime 频率 10 MHz */
#define CLOCKS_PER_TICK (MTIME_FREQ / HZ) /* 100000 ticks per interrupt */

/* ---- 全局变量 ---- */

extern volatile uint64_t jiffies;

/* ---- 函数声明 ---- */

/**
 * get_mtime - 读取 time CSR 获取当前 mtime 计数值
 */
uint64_t get_mtime(void);

/**
 * set_mtimecmp - 写入 stimecmp CSR 设置下一次时钟中断
 * @value: mtime 目标值，当 mtime >= value 时触发中断
 */
void set_mtimecmp(uint64_t value);

/**
 * timer_init - 设置首次时钟中断超时值
 *
 * 仅配置 stimecmp，中断使能（SIE.STIE、sstatus.SIE）
 * 由 trap_init() 统一管理。
 */
void timer_init(void);

#endif
