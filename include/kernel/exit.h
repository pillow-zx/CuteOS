#ifndef _CUTEOS_KERNEL_EXIT_H
#define _CUTEOS_KERNEL_EXIT_H

/*
 * include/kernel/exit.h - 进程退出接口
 *
 * 声明 do_exit()，供 page_fault、syscall 等模块调用。
 * 完整实现（资源释放、wait4 等）在 Stage 4 补充。
 */

struct task_struct;

/*
 * do_exit - 终止当前进程
 * @code: 退出码
 *
 * 将当前进程标记为 TASK_ZOMBIE，从就绪队列移除，
 * 调用 schedule() 永不返回。
 *
 * 注意：当前为极简实现，不释放资源（fd、mm 等）。
 * 完整版本在 Stage 4（kernel/exit.c）中补充。
 */
void do_exit(int code);

#endif
