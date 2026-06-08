/*
 * arch/riscv/timer.c - Sstc 时钟 (100Hz)
 *
 * 基于 RISC-V Sstc 扩展的定时器管理。
 * 使用 100Hz 固定频率（HZ=100，即每 10ms 一次）驱动时钟中断，
 * 为内核调度器和时间管理提供心跳。
 *
 * Sstc 扩展允许 S 模式直接通过 CSR 操作定时器，无需 ecall 陷入 M 模式：
 *   - time CSR:     读取当前 mtime 计数值
 *   - stimecmp CSR: 设置下一次时钟中断的比较值
 *
 * timer_init() 仅负责设置首次 stimecmp 比较值。
 * 中断使能（SIE.STIE、sstatus.SIE）由 trap_init() 统一管理。
 *
 * 主要函数：
 *   get_mtime()    - 读取 time CSR 获取当前时间计数器
 *   set_mtimecmp() - 写入 stimecmp CSR 设置下一次时钟中断
 *   timer_init()   - 设置首次超时值
 *
 * 全局变量：
 *   jiffies - 自启动以来的时钟中断总次数
 */

#include <kernel/types.h>
#include <asm/csr.h>

#define HZ              100
#define MTIME_FREQ      10000000UL
#define CLOCKS_PER_TICK (MTIME_FREQ / HZ) /* 100000 */

volatile uint64_t jiffies = 0;

/*
 * get_mtime - 通过 time CSR 读取当前时间计数器
 */
uint64_t get_mtime(void)
{
	return csr_read(time);
}

/*
 * set_mtimecmp - 通过 stimecmp CSR 设置下一次时钟中断
 */
void set_mtimecmp(uint64_t value)
{
	csr_write(stimecmp, value);
}

/*
 * timer_init - 设置首次时钟中断超时值
 *
 * 仅配置 stimecmp，中断使能由 trap_init() 负责。
 */
void timer_init(void)
{
	set_mtimecmp(get_mtime() + CLOCKS_PER_TICK);
}
