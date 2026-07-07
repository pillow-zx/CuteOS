#ifndef _CUTEOS_KERNEL_CPU_H
#define _CUTEOS_KERNEL_CPU_H

/*
 * include/kernel/cpu.h - CPU-local kernel state
 */

#include <kernel/types.h>
#include <kernel/compiler.h>
#include <arch/cpu.h>

#define NR_CPUS CONFIG_QEMU_CPUS

#define CPU_OFFLINE 0u
#define CPU_BOOTING 1u
#define CPU_ONLINE  2u
#define CPU_PARKED  3u

struct task_struct;

struct cpu {
	uint32_t id;
	uint32_t hartid;
	uint32_t state;
	uint32_t flags;
	struct task_struct *idle_task;
	struct task_struct *current_task;
	volatile int preempt_count;
};

static_assert(offsetof(struct cpu, current_task) == CPU_CURRENT_TASK,
	      "CPU_CURRENT_TASK offset in entry.S out of sync with struct cpu");
static_assert(
	offsetof(struct cpu, preempt_count) == CPU_PREEMPT_COUNT,
	"CPU_PREEMPT_COUNT offset in entry.S out of sync with struct cpu");

extern struct cpu cpu_table[NR_CPUS];
extern uint32_t nr_cpu_ids;

void cpu_boot_init(struct task_struct *idle);

static __always_inline __must_check __pure __returns_nonnull struct cpu *
current_cpu(void)
{
	return &cpu_table[0];
}

static __always_inline __must_check __pure struct cpu *cpu_by_id(uint32_t id)
{
	return id < NR_CPUS ? &cpu_table[id] : NULL;
}

static __always_inline __must_check __pure bool cpu_is_online(uint32_t id)
{
	struct cpu *cpu = cpu_by_id(id);

	return cpu && cpu->state == CPU_ONLINE;
}

static __always_inline __must_check __pure __nonnull(1)
struct task_struct *cpu_current_task(const struct cpu *cpu)
{
	return cpu->current_task;
}

static __always_inline __nonnull(1)
void cpu_set_task(struct cpu *cpu, struct task_struct *task)
{
	cpu->current_task = task;
}

static __always_inline __must_check __pure
struct task_struct *current_task(void)
{
	return cpu_current_task(current_cpu());
}

static __always_inline void set_current_task(struct task_struct *task)
{
	cpu_set_task(current_cpu(), task);
}

static __always_inline __must_check __pure __nonnull(1)
struct task_struct *cpu_idle_task(const struct cpu *cpu)
{
	return cpu->idle_task;
}

static __always_inline __must_check __pure __nonnull(1)
int cpu_preempt_count(const struct cpu *cpu)
{
	return cpu->preempt_count;
}

static __always_inline __nonnull(1) void cpu_set_preempt_count(struct cpu *cpu,
							       int count)
{
	cpu->preempt_count = count;
}

static __always_inline __nonnull(1) void cpu_inc_preempt_count(struct cpu *cpu)
{
	cpu->preempt_count++;
}

static __always_inline __nonnull(1) void cpu_dec_preempt_count(struct cpu *cpu)
{
	cpu->preempt_count--;
}

#endif
