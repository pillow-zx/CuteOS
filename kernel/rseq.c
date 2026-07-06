/*
 * kernel/rseq.c - restartable sequence legacy single-core support
 */

#include <kernel/errno.h>
#include <kernel/mm.h>
#include <kernel/rseq.h>
#include <kernel/task.h>
#include <kernel/trap.h>
#include <kernel/tools.h>
#include <kernel/bitops.h>
#include <uapi/rseq.h>
#include <uapi/sched.h>

#define RSEQ_ORIG_SIZE	    32U
#define RSEQ_SINGLE_CPU_ID  0U
#define RSEQ_SINGLE_NODE_ID 0U
#define RSEQ_SINGLE_MM_CID  0U

static __always_inline __must_check __pure bool
rseq_area_aligned(const struct rseq *area)
{
	return IS_ALIGNED((uintptr_t)area, RSEQ_ORIG_SIZE);
}

static int __must_check __nonnull(1) rseq_write_initial_area(struct rseq *area)
{
	struct rseq init = {
		.cpu_id_start = RSEQ_SINGLE_CPU_ID,
		.cpu_id = RSEQ_SINGLE_CPU_ID,
		.rseq_cs = 0,
		.flags = 0,
		.node_id = RSEQ_SINGLE_NODE_ID,
		.mm_cid = RSEQ_SINGLE_MM_CID,
	};

	if (copy_to_user(area, &init, sizeof(init)) != 0)
		return -EFAULT;

	return 0;
}

static int __must_check __nonnull(1) rseq_write_current_ids(struct rseq *area)
{
	unsigned int value;

	value = RSEQ_SINGLE_CPU_ID;
	if (copy_to_user(&area->cpu_id_start, &value, sizeof(value)) != 0)
		return -EFAULT;
	if (copy_to_user(&area->cpu_id, &value, sizeof(value)) != 0)
		return -EFAULT;

	value = RSEQ_SINGLE_NODE_ID;
	if (copy_to_user(&area->node_id, &value, sizeof(value)) != 0)
		return -EFAULT;

	value = RSEQ_SINGLE_MM_CID;
	if (copy_to_user(&area->mm_cid, &value, sizeof(value)) != 0)
		return -EFAULT;

	return 0;
}

static int __must_check __nonnull(1)
	rseq_write_unregistered_area(struct rseq *area)
{
	unsigned int cpu_id = RSEQ_CPU_ID_UNINITIALIZED;

	if (copy_to_user(&area->cpu_id, &cpu_id, sizeof(cpu_id)) != 0)
		return -EFAULT;

	return 0;
}

static int __must_check __nonnull(1, 2)
	rseq_copy_cs(struct rseq_cs *dst, const struct rseq_cs *src)
{
	if ((uintptr_t)src >= TASK_SIZE)
		return -EFAULT;
	if (copy_from_user(dst, src, sizeof(*dst)) != 0)
		return -EFAULT;

	return 0;
}

static int __must_check rseq_read_signature(uintptr_t abort_ip, uint32_t *sig)
{
	uint32_t *usig;

	if (!sig)
		return -EINVAL;
	if (abort_ip >= TASK_SIZE || abort_ip < sizeof(*sig))
		return -EFAULT;

	usig = (uint32_t *)(abort_ip - sizeof(*sig));
	if (copy_from_user(sig, usig, sizeof(*sig)) != 0)
		return -EFAULT;

	return 0;
}

static int __must_check __nonnull(1) rseq_clear_user_cs(struct rseq *area)
{
	unsigned long zero = 0;

	if (copy_to_user(&area->rseq_cs, &zero, sizeof(zero)) != 0)
		return -EFAULT;

	return 0;
}

static int __must_check __nonnull(1, 2)
	rseq_handle_cs(struct task_struct *task, struct trap_frame *tf,
		       unsigned long csaddr)
{
	struct rseq_cs cs;
	uintptr_t ip;
	uintptr_t start_ip;
	uintptr_t end;
	uintptr_t abort_ip;
	uint32_t sig;
	int ret;

	ret = rseq_copy_cs(&cs, (const struct rseq_cs *)csaddr);
	if (ret < 0)
		return ret;

	ip = trap_user_pc(tf);
	start_ip = cs.start_ip;
	abort_ip = cs.abort_ip;
	end = start_ip + cs.post_commit_offset;
	if (end < start_ip || end > TASK_SIZE)
		return -EFAULT;
	if (ip < start_ip || ip >= end)
		return rseq_clear_user_cs(task_rseq_area(task));

	ret = rseq_read_signature(abort_ip, &sig);
	if (ret < 0)
		return ret;
	if (sig != task_rseq_sig(task))
		return -EFAULT;

	ret = rseq_clear_user_cs(task_rseq_area(task));
	if (ret < 0)
		return ret;

	trap_set_user_pc(tf, abort_ip);
	return 0;
}

static int __must_check __nonnull(1, 2)
	rseq_update_user(struct task_struct *task, struct trap_frame *tf,
			 bool force)
{
	struct rseq *area = task_rseq_area(task);
	unsigned long csaddr;
	int ret;

	if (!area)
		return 0;
	if (!force && !task_rseq_need_update(task))
		return 0;

	task_set_rseq_need_update(task, 0);

	ret = rseq_write_current_ids(area);
	if (ret < 0)
		return ret;

	if (copy_from_user(&csaddr, &area->rseq_cs, sizeof(csaddr)) != 0)
		return -EFAULT;
	if (!csaddr)
		return 0;

	return rseq_handle_cs(task, tf, csaddr);
}

static int __must_check rseq_reregister(struct rseq *area, uint32_t len,
					uint32_t sig)
{
	struct task_struct *task = current_task();

	if (task_rseq_area(task) != area || task_rseq_len(task) != len)
		return -EINVAL;
	if (task_rseq_sig(task) != sig)
		return -EPERM;

	return -EBUSY;
}

static int __must_check rseq_register(struct rseq *area, uint32_t len,
				      uint32_t sig)
{
	struct task_struct *task = current_task();
	int ret;

	if (!area)
		return -EFAULT;
	if (len != RSEQ_ORIG_SIZE || !rseq_area_aligned(area))
		return -EINVAL;
	if (!access_ok(area, len))
		return -EFAULT;
	if (task_rseq_area(task))
		return rseq_reregister(area, len, sig);

	ret = rseq_write_initial_area(area);
	if (ret < 0)
		return ret;

	task_set_rseq(task, area, len, sig);
	return 0;
}

static int __must_check rseq_unregister(struct rseq *area, uint32_t len,
					int flags, uint32_t sig)
{
	struct task_struct *task = current_task();
	int ret;

	if (flags & ~RSEQ_FLAG_UNREGISTER)
		return -EINVAL;
	if (task_rseq_area(task) != area || !task_rseq_area(task))
		return -EINVAL;
	if (task_rseq_len(task) != len)
		return -EINVAL;
	if (task_rseq_sig(task) != sig)
		return -EPERM;

	ret = rseq_write_unregistered_area(area);
	if (ret < 0)
		return ret;

	task_clear_rseq(task);
	return 0;
}

ssize_t kernel_rseq(struct rseq *area, uint32_t len, int flags, uint32_t sig)
{
	if (flags & RSEQ_FLAG_UNREGISTER)
		return rseq_unregister(area, len, flags, sig);
	if (flags)
		return -EINVAL;

	return rseq_register(area, len, sig);
}

void rseq_execve(struct task_struct *task)
{
	task_clear_rseq(task);
}

void rseq_clone(struct task_struct *child, const struct task_struct *parent,
		unsigned long flags)
{
	if (flags & CLONE_VM) {
		task_clear_rseq(child);
		return;
	}

	task_set_rseq(child, task_rseq_area(parent), task_rseq_len(parent),
		      task_rseq_sig(parent));
	task_set_rseq_need_update(child, task_rseq_need_update(parent));
}

void rseq_sched_switch(struct task_struct *prev)
{
	if (!prev || !task_rseq_area(prev) || !arch_task_trap_from_user(prev))
		return;

	task_set_rseq_need_update(prev, 1);
}

int rseq_resume_user(struct trap_frame *tf)
{
	return rseq_update_user(current_task(), tf, false);
}

int rseq_signal_deliver(struct trap_frame *tf)
{
	return rseq_update_user(current_task(), tf, true);
}
