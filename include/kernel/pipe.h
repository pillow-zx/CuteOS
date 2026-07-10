#ifndef _CUTEOS_KERNEL_PIPE_H
#define _CUTEOS_KERNEL_PIPE_H

/*
 * include/kernel/pipe.h - 管道内部 API
 */

#include <kernel/types.h>

struct file;

int do_pipe2(int fds[2], int flags);
bool pipe_file(struct file *file);
ssize_t pipe_splice_to_file(struct file *pipe_file, struct file *out_file,
			    loff_t *out_offset, size_t len);

#ifdef KERNEL_SELFTEST
void pipe_test_set_file_alloc_fail_at(int fail_at);
uint32_t pipe_test_live_buffers(void);
#endif

#endif
