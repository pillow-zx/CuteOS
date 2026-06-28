#ifndef _CUTEOS_KERNEL_PIPE_H
#define _CUTEOS_KERNEL_PIPE_H

/*
 * include/kernel/pipe.h - 管道内部 API
 *
 * 声明 do_pipe2()，供 syscall ABI 边界调用。实际分配逻辑在 fs/pipe.c
 * 中实现，只返回内核态 fd 数组，不触碰用户指针。
 */

int do_pipe2(int fds[2], int flags);

#endif /* _CUTEOS_KERNEL_PIPE_H */
