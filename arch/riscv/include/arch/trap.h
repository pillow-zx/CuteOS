#ifndef _CUTEOS_ARCH_RISCV_TRAP_H
#define _CUTEOS_ARCH_RISCV_TRAP_H

#include <kernel/compiler.h>
#include <kernel/types.h>
#include <kernel/trap_types.h>
#include <asm/csr.h>
#include <asm/context.h>
#include <asm/trap.h>
#include <asm/trap_frame.h>

typedef bool (*trap_test_hook_t)(struct trap_frame *tf);

struct signal_frame_state {
	struct trap_frame tf;
};

#define ARCH_TRAP_REG_SEPC 0
#define ARCH_TRAP_REG_RA 1
#define ARCH_TRAP_REG_SP 2
#define ARCH_TRAP_REG_GP 3
#define ARCH_TRAP_REG_TP 4
#define ARCH_TRAP_REG_T0 5
#define ARCH_TRAP_REG_T1 6
#define ARCH_TRAP_REG_T2 7
#define ARCH_TRAP_REG_S0 8
#define ARCH_TRAP_REG_S1 9
#define ARCH_TRAP_REG_A0 10
#define ARCH_TRAP_REG_A1 11
#define ARCH_TRAP_REG_A2 12
#define ARCH_TRAP_REG_A3 13
#define ARCH_TRAP_REG_A4 14
#define ARCH_TRAP_REG_A5 15
#define ARCH_TRAP_REG_A6 16
#define ARCH_TRAP_REG_A7 17
#define ARCH_TRAP_REG_S2 18
#define ARCH_TRAP_REG_S3 19
#define ARCH_TRAP_REG_S4 20
#define ARCH_TRAP_REG_S5 21
#define ARCH_TRAP_REG_S6 22
#define ARCH_TRAP_REG_S7 23
#define ARCH_TRAP_REG_S8 24
#define ARCH_TRAP_REG_S9 25
#define ARCH_TRAP_REG_S10 26
#define ARCH_TRAP_REG_S11 27
#define ARCH_TRAP_REG_T3 28
#define ARCH_TRAP_REG_T4 29
#define ARCH_TRAP_REG_T5 30
#define ARCH_TRAP_REG_T6 31
#define ARCH_TRAP_REG_SCAUSE 32
#define ARCH_TRAP_REG_STVAL 33
#define ARCH_TRAP_REG_SSTATUS 34

void trap_init(void);
void trap_handler(struct trap_frame *tf);
void trap_set_hook(trap_test_hook_t hook);
void __trapret(void);
void trapret_to_user(struct trap_frame *tf) __noreturn;
void switch_to(struct context *prev, struct context *next);

static __always_inline __must_check __pure __nonnull(1) size_t
	syscall_nr(const struct trap_frame *tf)
{
	return tf->a7;
}

static __always_inline __must_check __pure __nonnull(1) size_t
	syscall_arg(const struct trap_frame *tf, uint32_t nr)
{
	switch (nr) {
	case 0:
		return tf->a0;
	case 1:
		return tf->a1;
	case 2:
		return tf->a2;
	case 3:
		return tf->a3;
	case 4:
		return tf->a4;
	case 5:
		return tf->a5;
	default:
		unreachable();
	}
}

static __always_inline __nonnull(1) void
syscall_set_return(struct trap_frame *tf, ssize_t ret)
{
	tf->a0 = (size_t)ret;
}

static __always_inline __must_check __pure __nonnull(1) uintptr_t
	trap_user_sp(const struct trap_frame *tf)
{
	return tf->sp;
}

static __always_inline __must_check __pure __nonnull(1) uintptr_t
	trap_user_pc(const struct trap_frame *tf)
{
	return tf->sepc;
}

static __always_inline __must_check __pure __nonnull(1) uintptr_t
	trap_fault_addr(const struct trap_frame *tf)
{
	return tf->stval;
}

static __always_inline __must_check __pure
	__nonnull(1) bool trap_frame_from_user(const struct trap_frame *tf)
{
	return (tf->sstatus & SSTATUS_SPP) == 0;
}

static __always_inline __must_check __pure __nonnull(1) uintptr_t
	trap_frame_cause(const struct trap_frame *tf)
{
	return tf->scause;
}

static __always_inline __must_check __pure __nonnull(1) uintptr_t
	trap_status(const struct trap_frame *tf)
{
	return tf->sstatus;
}

static __always_inline __nonnull(1) void
trap_set_status(struct trap_frame *tf, uintptr_t status)
{
	tf->sstatus = status;
}

static __always_inline __nonnull(1) void
trap_advance_pc(struct trap_frame *tf, uintptr_t bytes)
{
	tf->sepc += bytes;
}

static __always_inline __must_check __pure __nonnull(1) enum trap_access_type
	trap_fault_access(const struct trap_frame *tf)
{
	switch (tf->scause & ~SCAUSE_IRQ_FLAG) {
	case EXC_INST_PAGE_FAULT:
		return TRAP_ACCESS_EXEC;
	case EXC_LOAD_PAGE_FAULT:
	case EXC_INST_ACCESS:
		return TRAP_ACCESS_READ;
	case EXC_STORE_PAGE_FAULT:
		return TRAP_ACCESS_WRITE;
	default:
		return TRAP_ACCESS_READ;
	}
}

static __always_inline __must_check __pure __nonnull(1) const
	char *trap_fault_name(const struct trap_frame *tf)
{
	switch (tf->scause & ~SCAUSE_IRQ_FLAG) {
	case EXC_INST_PAGE_FAULT:
		return "instruction";
	case EXC_LOAD_PAGE_FAULT:
		return "load";
	case EXC_STORE_PAGE_FAULT:
		return "store";
	case EXC_INST_ACCESS:
		return "inst-access";
	default:
		return "unknown";
	}
}

static __always_inline __nonnull(1) void
trap_set_user_sp(struct trap_frame *tf, uintptr_t sp)
{
	tf->sp = sp;
}

static __always_inline __nonnull(1) void
trap_set_user_pc(struct trap_frame *tf, uintptr_t pc)
{
	tf->sepc = pc;
}

static __always_inline __nonnull(1) void
trap_set_arg0(struct trap_frame *tf, uintptr_t value)
{
	tf->a0 = value;
}

static __always_inline
	__nonnull(1) void trap_set_kernel_return(struct trap_frame *tf,
						      uintptr_t pc)
{
	tf->sepc = pc;
	tf->sstatus |= SSTATUS_SPP | SSTATUS_SPIE;
}

static __always_inline __nonnull(1) void
trap_set_kernel_thread_frame(struct trap_frame *tf, uintptr_t pc, uintptr_t arg0)
{
	memset(tf, 0, sizeof(*tf));
	tf->sepc = pc;
	tf->a0 = arg0;
	tf->sstatus = SSTATUS_SPP | SSTATUS_SPIE;
}

static __always_inline __nonnull(1, 2) void
trap_clone_frame(struct trap_frame *dst, const struct trap_frame *src)
{
	memcpy(dst, src, sizeof(*dst));
}

static __always_inline __nonnull(1) void
trap_set_clone_return(struct trap_frame *tf)
{
	tf->a0 = 0;
}

static __always_inline
	__nonnull(1) void trap_set_tls(struct trap_frame *tf,
					    uintptr_t tls)
{
	tf->tp = tls;
}

static __always_inline __nonnull(1) void trap_setup_signal_handler(
	struct trap_frame *tf, uintptr_t handler, uintptr_t restorer,
	uintptr_t sp, uintptr_t arg0)
{
	tf->sepc = handler;
	tf->ra = restorer;
	tf->sp = sp;
	tf->a0 = arg0;
}

static __always_inline __must_check __pure __nonnull(1) uintptr_t
	trap_return_value(const struct trap_frame *tf)
{
	return tf->a0;
}

static __always_inline __nonnull(1) void trap_setup_user_return(
	struct trap_frame *tf, uintptr_t pc, uintptr_t sp)
{
	tf->sepc = pc;
	tf->sp = sp;
	tf->sstatus = SSTATUS_SPIE;
}

static __always_inline __nonnull(1, 2) void trap_save_signal_state(
	struct signal_frame_state *state, const struct trap_frame *tf)
{
	state->tf = *tf;
}

static __always_inline __nonnull(1, 2) void trap_restore_signal_state(
	struct trap_frame *tf, const struct signal_frame_state *state)
{
	*tf = state->tf;
}

#ifdef CONFIG_KERNEL_TEST
static __always_inline __must_check __const size_t trap_frame_size(void)
{
	return sizeof(struct trap_frame);
}

static __always_inline __must_check __const size_t trap_context_size(void)
{
	return sizeof(struct context);
}

static __always_inline __must_check __pure __nonnull(1) uintptr_t
	trap_test_reg(const struct trap_frame *tf, uint32_t reg)
{
	const size_t *regs = (const size_t *)tf;

	return regs[reg];
}
#endif

#endif
