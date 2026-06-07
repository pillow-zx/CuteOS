/*
 * arch/riscv/sbi.c - OpenSBI ecall 封装
 *
 * 功能：
 *   封装 RISC-V SBI（Supervisor Binary Interface）调用，通过 ecall 指令
 *   陷入 M 模式下的 OpenSBI 固件执行特权操作。SBI 是内核与底层固件之间的
 *   标准接口层，提供平台无关的服务抽象。
 *
 * 主要函数：
 *   sbi_ecall(eid, fid, arg0~arg4)  - 通用 SBI 调用入口。
 *                  将 eid 放入 a7，fid 放入 a6，参数通过 a0~a4 传递，
 *                  执行 ecall 陷入 M 模式，从 a0/a1 获取返回值。
 *
 *   sbi_console_putchar(ch) - 早期控制台输出。通过 SBI putchar (EID=0x01)
 *                  输出单个字符，用于内核早期 printk 在 UART 驱动初始化前
 *                  输出调试信息。
 *
 *   sbi_set_timer(stime_value) - 定时器设置 (EID=0x00)。通过 SBI 设置
 *                  mtimecmp 寄存器，调度下一个 Supervisor Timer 中断。
 *                  参数为绝对时刻值（mtime 单位），由 timer.c 调用。
 *
 *   sbi_shutdown() - 系统关机 (EID=0x08)。通过 SBI 请求 OpenSBI 执行
 *                  系统关闭，用于 kernel panic 或正常关机路径。
 */

#include <asm/sbi.h>


#define SBI_EID_SET_TIMER 0x00
#define SBI_EID_CONSOLE_PUTCHAR 0x01
#define SBI_EID_SHUTDOWN 0x08


static inline struct sbi_ret sbi_ecall(uint64_t eid, uint64_t fid,
                                       uint64_t arg0, uint64_t arg1,
                                       uint64_t arg2, uint64_t arg3,
                                       uint64_t arg4)
{
        register long a0 __asm__("a0") = (long)arg0;
        register long a1 __asm__("a1") = (long)arg1;
        register long a2 __asm__("a2") = (long)arg2;
        register long a3 __asm__("a3") = (long)arg3;
        register long a4 __asm__("a4") = (long)arg4;
        register long a6 __asm__("a6") = (long)fid;
        register long a7 __asm__("a7") = (long)eid;

        __asm__ __volatile__("ecall"
                             : "+r"(a0), "+r"(a1)
                             : "r"(a2), "r"(a3), "r"(a4), "r"(a6), "r"(a7)
                             : "memory");

        return (struct sbi_ret){.error = a0, .value = a1};
}


void sbi_console_putchar(int ch)
{
        sbi_ecall(SBI_EID_CONSOLE_PUTCHAR, 0, (uint64_t)(unsigned char)ch, 0, 0,
                  0, 0);
}


void sbi_set_timer(uint64_t stime_value)
{
        sbi_ecall(SBI_EID_SET_TIMER, 0, stime_value, 0, 0, 0, 0);
}


void __noreturn sbi_shutdown(void)
{
        sbi_ecall(SBI_EID_SHUTDOWN, 0, 0, 0, 0, 0, 0);
        unreachable();
}
