#ifndef _CUTEOS_ASM_SBI_H
#define _CUTEOS_ASM_SBI_H

#include <kernel/types.h>
#include <kernel/compiler.h>

/*
 * sbi.h - RISC-V SBI (Supervisor Binary Interface) 声明
 *
 * SBI 是 RISC-V 特权级规范中定义的 M 态固件为 S 态操作系统
 * 提供服务的接口。本头文件声明了 cuteOS 使用的基础 SBI 调用。
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
 * sbi_shutdown - 关闭系统
 *
 * 通过 SBI 请求固件关闭机器，调用后不会返回。
 */
void __noreturn sbi_shutdown(void);

#endif
