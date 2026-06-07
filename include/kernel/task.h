#ifndef _CUTEOS_KERNEL_TASK_H
#define _CUTEOS_KERNEL_TASK_H

/*
 * include/kernel/task.h - 进程控制块与任务管理
 *
 * 声明 struct task_struct，表示进程或内核线程的核心数据结构。
 * 同时定义任务状态常量，并声明 idle/init 任务实例。
 *
 * struct task_struct fields:
 *   pid        - Process ID
 *   state      - Current task state (RUNNING/SLEEPING/ZOMBIE/DEAD)
 *   mm         - Pointer to mm_struct (NULL for kernel threads)
 *   fd_array   - Open file descriptors (fixed array of 32 entries)
 *   sighand    - Signal handler table (32 entries)
 *   blocked    - Blocked signal mask
 *   pending    - Pending signal mask
 *   context    - Saved callee-saved registers for context switch
 *   stack      - Kernel stack pointer
 *   children   - List of child tasks
 *   sibling    - Linkage in parent's children list
 *   cwd        - Current working directory dentry
 *
 * Task states:
 *   TASK_RUNNING  - Runnable or currently executing
 *   TASK_SLEEPING - Waiting for an event
 *   TASK_ZOMBIE   - Exited, waiting for parent to reap
 *   TASK_DEAD     - Fully reaped
 *
 * Globals:
 *   idle_task  - PID 0, the idle loop task
 *   init_task  - PID 1, the first user process
 */

#endif
