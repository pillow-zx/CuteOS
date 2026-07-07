/*
 * arch/riscv/sbi.c - OpenSBI ecall 封装
 */

#include <arch/sbi.h>
#include <kernel/types.h>

struct sbi_ret {
	int64_t error;
	int64_t value;
};

#define SBI_EID_CONSOLE_PUTCHAR 0x01
#define SBI_EID_SHUTDOWN	0x08

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

void __noreturn sbi_shutdown(void)
{
	sbi_ecall(SBI_EID_SHUTDOWN, 0, 0, 0, 0, 0, 0);
	unreachable();
}
