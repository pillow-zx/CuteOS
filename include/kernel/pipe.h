#ifndef _CUTEOS_KERNEL_PIPE_H
#define _CUTEOS_KERNEL_PIPE_H

/*
 * include/kernel/pipe.h - 管道内部 API
 *
 * 声明 do_pipe2()，供 syscall/sys_file.c（ABI 边界）调用。
 * 实际分配逻辑在 fs/pipe.c 中实现。
 */

int do_pipe2(int *user_fds, int flags);

#endif /* _CUTEOS_KERNEL_PIPE_H */
