/*
 * arch/riscv/task.c - RISC-V task context helpers
 */

#include <kernel/mm.h>
#include <kernel/task.h>
#include <kernel/tools.h>
#include <uapi/sched.h>
#include <arch/pgtable.h>
#include <arch/trap.h>
#include <asm/csr.h>

static __always_inline __must_check __pure uintptr_t
arch_task_satp_or_kernel(const struct task_struct *task)
{
	uint64_t satp = task_address_space_satp(task);

	return satp ? satp : kernel_satp();
}

void arch_task_init(struct task_struct *task)
{
	task->arch.ctx.ra = 0;
	task->arch.ctx.sp = 0;
	task_set_trap_frame(task, NULL);
	task_set_satp(task, 0);
}

void arch_task_setup_kernel_thread(struct task_struct *task, void (*fn)(void *),
				   void *arg)
{
	struct trap_frame *tf = task_kernel_trap_frame(task);

	trap_set_kernel_thread_frame(tf, (uintptr_t)fn, (uintptr_t)arg);

	task_set_trap_frame(task, tf);
	task->arch.ctx.ra = (size_t)__trapret;
	task->arch.ctx.sp = (size_t)tf;
}

void arch_task_setup_clone_frame(struct task_struct *child,
				 const struct trap_frame *parent_tf,
				 unsigned long flags, uintptr_t child_stack,
				 uintptr_t tls)
{
	struct trap_frame *child_tf = task_kernel_trap_frame(child);

	trap_clone_frame(child_tf, parent_tf);
	trap_set_clone_return(child_tf);
	if (child_stack != 0)
		trap_set_user_sp(child_tf, child_stack);
	if (flags & CLONE_SETTLS)
		trap_set_tls(child_tf, tls);

	task_set_trap_frame(child, child_tf);
	child->arch.ctx.ra = (size_t)__trapret;
	child->arch.ctx.sp = (size_t)child_tf;
}

void arch_task_switch_address_space(const struct task_struct *prev,
				    const struct task_struct *next)
{
	const uintptr_t satp_val = arch_task_satp_or_kernel(next);

	if (arch_task_satp_or_kernel(prev) == satp_val)
		return;

	csr_write(satp, satp_val);
	tlb_flush_all();
}

void arch_task_switch(struct task_struct *prev, struct task_struct *next)
{
	switch_to(&prev->arch.ctx, &next->arch.ctx);
}

bool arch_task_trap_from_user(const struct task_struct *task)
{
	const struct trap_frame *tf = task_trap_frame_const(task);

	return tf && trap_frame_from_user(tf);
}

#ifdef CONFIG_KERNEL_TEST
bool arch_task_test_layout_contract(void)
{
	return offsetof(struct task_struct, arch.kstack) == TASK_KSTACK &&
	       offsetof(struct task_struct, arch.satp) == TASK_SATP &&
	       offsetof(struct cpu, current_task) == CPU_CURRENT_TASK &&
	       offsetof(struct cpu, preempt_count) == CPU_PREEMPT_COUNT;
}

bool arch_task_test_kernel_thread_setup(const struct task_struct *task,
					void (*fn)(void *), void *arg)
{
	if (!task || !fn)
		return false;

	const struct trap_frame *tf =
		task_trap_frame((struct task_struct *)task);
	const struct trap_frame *expected_tf =
		task_kernel_trap_frame((struct task_struct *)task);

	return tf == expected_tf && task->arch.ctx.ra == (size_t)__trapret &&
	       task->arch.ctx.sp == (size_t)expected_tf &&
	       trap_user_pc(tf) == (uintptr_t)fn &&
	       trap_return_value(tf) == (uintptr_t)arg &&
	       (trap_status(tf) & SSTATUS_SPP) &&
	       (trap_status(tf) & SSTATUS_SPIE);
}

void arch_task_test_setup_user_return(struct task_struct *task, size_t user_pc,
				      size_t user_sp, size_t user_sstatus)
{
	struct trap_frame *tf = task_kernel_trap_frame(task);

	memset(tf, 0, sizeof(*tf));

	trap_set_user_pc(tf, user_pc);
	trap_set_user_sp(tf, user_sp);
	trap_set_status(tf, user_sstatus);

	task_set_trap_frame(task, tf);
	task->arch.ctx.ra = (size_t)__trapret;
	task->arch.ctx.sp = (size_t)tf;
}
#endif
