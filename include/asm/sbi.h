#ifndef _CUTEOS_ASM_SBI_H
#define _CUTEOS_ASM_SBI_H

#include <kernel/types.h>
#include <kernel/compiler.h>

/*
 * sbi.h - RISC-V SBI (Supervisor Binary Interface) 声明
 *
 * SBI 是 RISC-V 特权级规范中定义的 M 态固件为 S 态操作系统
 * 提供服务的接口。本头文件声明了 cuteOS 使用的基础 SBI 调用，
 * 以及定时器相关辅助函数。
 */

/*
 * sbi_ret - SBI 调用返回值结构
 * @error: 错误码，0 表示成功，非零为 SBI 定义的错误码
 * @value: 调用成功时返回的实际值
 */
struct sbi_ret {
        int64_t error;
        int64_t value;
};

/*
 * sbi_console_putchar - 通过 SBI 向控制台输出一个字符
 * @ch: 待输出的字符 (仅低 8 位有效)
 *
 * 调用 SBI ecall 将字符写入调试控制台，主要用于早期内核输出。
 */
void sbi_console_putchar(int ch);

/*
 * sbi_set_timer - 设置下一次时钟中断的时间
 * @stime_value: 目标时间，以 mtime 的计数值表示
 *
 * 当 mtime 达到 stime_value 时触发 M 态时钟中断，
 * SBI 固件会将其转发为 S 态的 supervisor timer interrupt。
 */
void sbi_set_timer(uint64_t stime_value);

/*
 * sbi_shutdown - 关闭系统
 *
 * 通过 SBI 请求固件关闭机器，调用后不会返回。
 */
void __noreturn sbi_shutdown(void);

/*
 * get_mtime - 获取当前 mtime 计数值
 *
 * 读取 CLINT 中 mtime 寄存器的当前值。
 */
uint64_t get_mtime(void);

/*
 * timer_init - 初始化定时器
 *
 * 首次设置时钟中断，使调度器能够获得周期性的时钟滴答。
 */
void timer_init(void);

#endif
