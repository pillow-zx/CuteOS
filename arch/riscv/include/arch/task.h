#ifndef _CUTEOS_ARCH_RISCV_TASK_H
#define _CUTEOS_ARCH_RISCV_TASK_H

/**
 * @file
 * @brief RISC-V task architecture state.
 */

#include <kernel/types.h>
#include <kernel/compiler.h>
#include <arch/page.h>
#include <asm/asm_offsets.h>
#include <asm/context.h>
#include <asm/trap_frame.h>

/** @def ARCH_KSTACK_ORDER
 * @brief Buddy allocation order for one kernel stack.
 */
constexpr uint32_t ARCH_KSTACK_ORDER = 1;
/** @def ARCH_KSTACK_SIZE
 * @brief Kernel stack size in bytes for each task.
 */
constexpr size_t ARCH_KSTACK_SIZE = PAGE_SIZE << ARCH_KSTACK_ORDER;

/**
 * @struct task_arch_state
 * @brief RISC-V-owned task state embedded in struct task_struct.
 *
 * @par Fields
 * - @c ctx: Callee-saved kernel context for switch.S.
 * - @c tf: Current trap frame while running in kernel.
 * - @c kstack: Base address of the task kernel stack allocation.
 * - @c satp: User page-table root installed before U-mode return.
 */
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
