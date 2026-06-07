/*
 * arch/riscv/timer.c - CLINT 时钟 (100Hz)
 *
 * 功能：
 *   基于 RISC-V CLINT（Core Local Interruptor）的定时器管理。
 *   使用 100Hz 固定频率（HZ=100，即每 10ms 一次）驱动时钟中断，
 *   为内核调度器和时间管理提供心跳。
 *
 * 主要函数：
 *   timer_init()       - 初始化定时器。计算首次超时的 mtime 值，
 *                        通过 sbi_set_timer() 设置 mtimecmp，
 *                        开启 Supervisor Timer 中断 (sie.STIE)。
 *
 *   get_mtime()        - 读取 CLINT mtime 寄存器当前值。
 *                        通过 __va(0x0200BFF8) 将物理地址转换为虚拟地址
 *                        后读取（该地址在内核页表的 MMIO 映射区域中）。
 *
 * 时钟中断流程：
 *   1. mtime >= mtimecmp 时 CLINT 触发 Supervisor Timer Interrupt
 *   2. __alltraps → trap_handler() 检测到 scause=5
 *   3. 调用 handle_timer_irq()：更新全局 jiffies 计数器，
 *      检查当前进程时间片，必要时设置 need_resched
 *   4. 通过 sbi_set_timer() 设置下一次 mtimecmp = 当前 mtime + interval
 *   5. 返回 __trapret 恢复执行
 *
 * 相关：
 *   全局变量 jiffies 记录自启动以来的时钟中断总次数。
 *   kernel/sched.c 中的 handle_timer_irq() 由本模块调用。
 */

#include <kernel/types.h>
#include <asm/sbi.h>
#include <asm/csr.h>
#include <asm/page.h>


#define HZ              100
#define MTIME_FREQ      10000000UL   
#define CLOCKS_PER_TICK (MTIME_FREQ / HZ)  


volatile uint64_t jiffies = 0;


uint64_t get_mtime(void)
{
        volatile uint64_t *mtime =
                (volatile uint64_t *)__va(0x0200BFF8UL);
        return *mtime;
}


void timer_init(void)
{
        sbi_set_timer(get_mtime() + CLOCKS_PER_TICK);
        csr_set(sie, SIE_STIE);
}
