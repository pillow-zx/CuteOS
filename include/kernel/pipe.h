#ifndef _CUTEOS_KERNEL_PIPE_H
#define _CUTEOS_KERNEL_PIPE_H

/*
 * include/kernel/pipe.h - 管道内部 API
 *
 * 声明 do_pipe2()，供 syscall ABI 边界调用。实际分配逻辑在 fs/pipe.c
 * 中实现，只返回内核态 fd 数组，不触碰用户指针。
 */

#include <kernel/types.h>

struct file;

int do_pipe2(int fds[2], int flags);
bool pipe_file(struct file *file);
ssize_t pipe_splice_to_file(struct file *pipe_file, struct file *out_file,
			    loff_t *out_offset, size_t len);

#ifdef CONFIG_KERNEL_TEST
void pipe_test_set_file_alloc_fail_at(int fail_at);
uint32_t pipe_test_live_buffers(void);
#endif

#endif /* _CUTEOS_KERNEL_PIPE_H */
