#ifndef _CUTEOS_KERNEL_PIPE_H
#define _CUTEOS_KERNEL_PIPE_H

/*
 * include/kernel/pipe.h - 管道数据结构与操作
 *
 * 声明内核管道实现。管道通过内核管理的环形缓冲区提供
 * 单向进程间通信。
 *
 * Structs:
 *   struct pipe_buffer - Circular buffer with head/tail indices,
 *                        reader/writer counts, and wait queues
 *
 * Functions:
 *   pipe_read_fops  - file_operations for the read end
 *   pipe_write_fops - file_operations for the write end
 */

#endif
