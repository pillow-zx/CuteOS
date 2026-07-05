/*
 * syscall/sys_membarrier.c - membarrier Linux ABI compatibility wrapper
 *
 * 当前 cuteOS 保持单核执行。本文件实现 Linux riscv64 membarrier(2)
 * 的单核兼容子集，使 libc 探测和已注册 private expedited 命令获得稳定
 * 结果；完整 SMP IPI/runqueue 语义留给后续多核调度支持。
 */

#include <kernel/errno.h>
#include <kernel/mm.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <uapi/membarrier.h>
#include <asm/csr.h>
#include <asm/trap.h>

#define MEMBARRIER_SUPPORTED_MASK                                              \
	(MEMBARRIER_CMD_GLOBAL | MEMBARRIER_CMD_GLOBAL_EXPEDITED |             \
	 MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED |                            \
	 MEMBARRIER_CMD_PRIVATE_EXPEDITED |                                    \
	 MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED |                           \
	 MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE |                          \
	 MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE |                 \
	 MEMBARRIER_CMD_GET_REGISTRATIONS)

static inline void membarrier_full_mb(void)
{
	asm volatile("fence rw,rw" ::: "memory");
}

ssize_t sys_membarrier(struct trap_frame *tf)
{
	int cmd = (int)tf->a0;
	unsigned int flags = (unsigned int)tf->a1;
	struct mm_struct *mm = task_mm(current_task());
	uint32_t registrations;

	switch (cmd) {
	case MEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ:
		if (flags && flags != MEMBARRIER_CMD_FLAG_CPU)
			return -EINVAL;
		return -EINVAL;
	default:
		if (flags)
			return -EINVAL;
	}

	switch (cmd) {
	case MEMBARRIER_CMD_QUERY:
		return MEMBARRIER_SUPPORTED_MASK;
	case MEMBARRIER_CMD_GLOBAL:
	case MEMBARRIER_CMD_GLOBAL_EXPEDITED:
		membarrier_full_mb();
		return 0;
	case MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED:
	case MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED:
	case MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE:
		if (!mm)
			return -EINVAL;
		mm_membarrier_register(mm, (uint32_t)cmd);
		membarrier_full_mb();
		return 0;
	case MEMBARRIER_CMD_PRIVATE_EXPEDITED:
		if (!mm)
			return -EINVAL;
		registrations = mm_membarrier_registrations(mm);
		if (!(registrations &
		      MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED))
			return -EPERM;
		membarrier_full_mb();
		return 0;
	case MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE:
		if (!mm)
			return -EINVAL;
		registrations = mm_membarrier_registrations(mm);
		if (!(registrations &
		      MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE))
			return -EPERM;
		arch_icache_flush();
		return 0;
	case MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_RSEQ:
		return -EINVAL;
	case MEMBARRIER_CMD_GET_REGISTRATIONS:
		if (!mm)
			return 0;
		return mm_membarrier_registrations(mm);
	default:
		return -EINVAL;
	}
}
