/*
 * fs/pipe.c - 管道
 *
 * 功能：
 *   实现进程间通信的管道（pipe）机制。pipe_buffer 结构包含 data
 *   （4KB kmalloc 缓冲区）、head/tail 读写指针、readers_wq 和
 *   writers_wq 等待队列、write_open 和 read_open 标志。sys_pipe
 *   创建两个 file 对象共享同一个 pipe_buffer。pipe_read 在缓冲区
 *   为空时在 readers_wq 上睡眠等待。pipe_write 在缓冲区满时在
 *   writers_wq 上睡眠等待。当 read_open==0 时写入端收到 SIGPIPE。
 *
 * 主要函数：
 *   sys_pipe(fd)              - 创建两个 file 共享一个 pipe_buffer
 *   pipe_read(buf, count)     - 读数据，缓冲区空时在 readers_wq 睡眠
 *   pipe_write(buf, count)    - 写数据，缓冲区满时在 writers_wq 睡眠
 *
 * 关键数据结构：
 *   pipe_buffer               - {data(4KB kmalloc), head, tail,
 *                               readers_wq, writers_wq, write_open, read_open}
 */
