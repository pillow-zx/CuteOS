#ifndef _CUTEOS_ARCH_RISCV_TASK_H
#define _CUTEOS_ARCH_RISCV_TASK_H

/*
 * arch/riscv/include/arch/task.h - RISC-V task architecture state
 *
 * This header owns the architecture-specific part embedded in task_struct.
 * Generic task code may keep the storage and use narrow accessors, while
 * RISC-V code owns the meaning of context, trap frame, and satp fields.
 */

#include <kernel/types.h>
#include <kernel/compiler.h>
#include <arch/page.h>
#include <asm/asm_offsets.h>
#include <asm/context.h>
#include <asm/trap_frame.h>

#define ARCH_KSTACK_ORDER 1
#define ARCH_KSTACK_SIZE  (PAGE_SIZE << ARCH_KSTACK_ORDER)

struct task_arch_state {
	struct context ctx;
	struct trap_frame *tf;
	void *kstack;
	uint64_t satp;
};

static_assert(ARCH_KSTACK_SIZE == TASK_KSTACK_SIZE,
	      "entry.S __trapret kstack arithmetic is out of sync");
static_assert((ARCH_KSTACK_SIZE - sizeof(struct trap_frame)) %
			      __alignof__(struct trap_frame) ==
		      0,
	      "kernel trap frame must be aligned at the top of each kstack");

#endif
