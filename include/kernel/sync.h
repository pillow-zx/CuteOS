#ifndef _CUTEOS_KERNEL_SYNC_H
#define _CUTEOS_KERNEL_SYNC_H

/*
 * include/kernel/sync.h - 同步原语
 *
 * 声明 spinlock_t 及相关宏。当前没有 acquire/release 原语，也没有真实锁
 * 语义；内核仍依赖单核、非抢占和关中断路径作为隐式保护。mutex、
 * semaphore 和 wait-queue 等原语的完整实现计划在开发路线图第 7 阶段完成。
 *
 * Current definitions:
 *   spinlock_t          - Basic spinlock (volatile int locked)
 *   SPINLOCK_INIT       - Static initializer { .locked = 0 }
 *   DEFINE_SPINLOCK(name) - Define a static spinlock
 */

typedef struct {
	volatile int locked;
} spinlock_t;

#define SPINLOCK_INIT	      {.locked = 0}
#define DEFINE_SPINLOCK(name) spinlock_t name = SPINLOCK_INIT

#endif
