#ifndef _CUTEOS_ARCH_RISCV_TASK_ACCESS_H
#define _CUTEOS_ARCH_RISCV_TASK_ACCESS_H

/*
 * arch/riscv/include/arch/task_access.h - RISC-V task accessors
 *
 * Included by <kernel/task.h> after struct task_struct is complete.
 */

#include <kernel/compiler.h>
#include <kernel/types.h>

static_assert(offsetof(struct task_struct, arch.kstack) == TASK_KSTACK,
	      "TASK_KSTACK offset in entry.S out of sync with task_struct");
static_assert(offsetof(struct task_struct, arch.satp) == TASK_SATP,
	      "TASK_SATP offset in entry.S out of sync with task_struct");

static __always_inline __must_check __pure uint64_t
task_address_space_satp(const struct task_struct *task)
{
	return task ? task->arch.satp : 0;
}

static __always_inline void task_set_satp(struct task_struct *task,
					  uint64_t satp)
{
	if (task)
		task->arch.satp = satp;
}

static __always_inline __must_check __pure struct context *
task_context(struct task_struct *task)
{
	return task ? &task->arch.ctx : NULL;
}

static __always_inline __must_check __pure struct trap_frame *
task_trap_frame(struct task_struct *task)
{
	return task ? task->arch.tf : NULL;
}

static __always_inline __must_check __pure const struct trap_frame *
task_trap_frame_const(const struct task_struct *task)
{
	return task ? task->arch.tf : NULL;
}

static __always_inline void task_set_trap_frame(struct task_struct *task,
						struct trap_frame *tf)
{
	if (task)
		task->arch.tf = tf;
}

static __always_inline __must_check __pure __nonnull(1) __returns_nonnull
	struct trap_frame *task_kernel_trap_frame(struct task_struct *task)
{
	uintptr_t frame = (uintptr_t)task->arch.kstack + KSTACK_SIZE -
			  sizeof(struct trap_frame);

	return (struct trap_frame *)frame;
}

static __always_inline __must_check __pure
	__nonnull(1) void *task_kernel_stack(const struct task_struct *task)
{
	return task->arch.kstack;
}

static __always_inline __must_check __pure void *
task_kernel_stack_safe(const struct task_struct *task)
{
	return task ? task_kernel_stack(task) : NULL;
}

static __always_inline void task_set_kernel_stack(struct task_struct *task,
						  void *kstack)
{
	if (task)
		task->arch.kstack = kstack;
}

#endif
