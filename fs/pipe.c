/*
 * fs/pipe.c - 管道
 *
 * 功能：
 *   实现进程间通信的匿名管道。pipe_buffer 使用一页 4KB 循环缓冲区，
 *   并维护读者/写者计数以及两端等待队列。sys_pipe2 创建两个匿名
 *   file 对象共享同一个 pipe_buffer。pipe_read 在缓冲区为空且仍有
 *   写端时睡眠；pipe_write 在缓冲区满且仍有读端时睡眠。最后一个
 *   读端关闭后写返回 -EPIPE；最后一个写端关闭后读完剩余数据返回 0。
 *
 * 主要函数：
 *   sys_pipe2(fd, flags)      - 创建两个 file 共享一个 pipe_buffer
 *   pipe_read(buf, count)     - 读数据，缓冲区空时在 readers_wq 睡眠
 *   pipe_write(buf, count)    - 写数据，缓冲区满时在 writers_wq 睡眠
 *
 * 关键数据结构：
 *   pipe_buffer               - {data, head, tail, used, readers, writers,
 *                               readers_wq, writers_wq}
 */

#include <kernel/buddy.h>
#include <kernel/errno.h>
#include <kernel/fdtable.h>
#include <kernel/mm.h>
#include <kernel/slab.h>
#include <kernel/signal.h>
#include <kernel/string.h>
#include <kernel/task.h>
#include <kernel/wait.h>
#include <asm/page.h>
#include <asm/trap.h>

#define PIPE_SIZE PAGE_SIZE

struct pipe_buffer {
	uint8_t *data;
	size_t head;
	size_t tail;
	size_t used;
	/*
	 * Current Stage 4 semantics are single-core and non-preemptive while
	 * executing syscalls. When kernel preemption or SMP is introduced,
	 * protect the buffer state and wait-queue condition checks with a lock
	 * and convert the sleep loops to a wait_event-style primitive.
	 */
	int readers;
	int writers;
	struct wait_queue_head readers_wq;
	struct wait_queue_head writers_wq;
};

static ssize_t pipe_read(struct file *file, char *buf, size_t count);
static ssize_t pipe_write(struct file *file, const char *buf, size_t count);
static int pipe_release(struct file *file);

static const struct file_operations pipe_read_fops = {
	.read = pipe_read,
	.release = pipe_release,
};

static const struct file_operations pipe_write_fops = {
	.write = pipe_write,
	.release = pipe_release,
};

static size_t pipe_linear_tail(struct pipe_buffer *pipe)
{
	size_t until_end = PIPE_SIZE - pipe->tail;

	if (pipe->used < until_end)
		return pipe->used;
	return until_end;
}

static size_t pipe_linear_head_space(struct pipe_buffer *pipe)
{
	size_t space = PIPE_SIZE - pipe->used;
	size_t until_end = PIPE_SIZE - pipe->head;

	if (space < until_end)
		return space;
	return until_end;
}

static struct pipe_buffer *pipe_buffer_alloc(void)
{
	struct pipe_buffer *pipe = kmalloc(sizeof(*pipe));
	if (!pipe)
		return NULL;

	memset(pipe, 0, sizeof(*pipe));
	pipe->data = get_free_page(0);
	if (!pipe->data) {
		kfree(pipe);
		return NULL;
	}

	pipe->readers = 1;
	pipe->writers = 1;
	init_waitqueue_head(&pipe->readers_wq);
	init_waitqueue_head(&pipe->writers_wq);

	return pipe;
}

static void pipe_buffer_free(struct pipe_buffer *pipe)
{
	if (!pipe)
		return;

	if (pipe->data)
		free_page(pipe->data, 0);
	kfree(pipe);
}

static ssize_t pipe_read(struct file *file, char *buf, size_t count)
{
	struct pipe_buffer *pipe = file->private_data;
	size_t done = 0;

	if (!pipe)
		return -EINVAL;

	while (done < count) {
		if (pipe->used == 0) {
			if (pipe->writers == 0 || done > 0)
				break;

			sleep_on(&pipe->readers_wq);
			continue;
		}

		size_t chunk = count - done;
		size_t linear = pipe_linear_tail(pipe);

		if (chunk > linear)
			chunk = linear;

		memcpy(buf + done, pipe->data + pipe->tail, chunk);
		pipe->tail = (pipe->tail + chunk) % PIPE_SIZE;
		pipe->used -= chunk;
		done += chunk;

		wake_up(&pipe->writers_wq);
	}

	return (ssize_t)done;
}

static ssize_t pipe_write(struct file *file, const char *buf, size_t count)
{
	struct pipe_buffer *pipe = file->private_data;
	size_t done = 0;

	if (!pipe)
		return -EINVAL;

	while (done < count) {
		if (pipe->readers == 0) {
			if (done == 0)
				send_signal(SIGPIPE, current);
			return done ? (ssize_t)done : -EPIPE;
		}

		if (pipe->used == PIPE_SIZE) {
			if (done > 0)
				break;

			sleep_on(&pipe->writers_wq);
			continue;
		}

		size_t chunk = count - done;
		size_t linear = pipe_linear_head_space(pipe);

		if (chunk > linear)
			chunk = linear;

		memcpy(pipe->data + pipe->head, buf + done, chunk);
		pipe->head = (pipe->head + chunk) % PIPE_SIZE;
		pipe->used += chunk;
		done += chunk;

		wake_up(&pipe->readers_wq);
	}

	return (ssize_t)done;
}

static int pipe_release(struct file *file)
{
	struct pipe_buffer *pipe = file->private_data;

	if (!pipe)
		return 0;

	if (file->f_mode & FMODE_READ) {
		if (pipe->readers > 0)
			pipe->readers--;
		wake_up_all(&pipe->writers_wq);
	}

	if (file->f_mode & FMODE_WRITE) {
		if (pipe->writers > 0)
			pipe->writers--;
		wake_up_all(&pipe->readers_wq);
	}

	if (pipe->readers == 0 && pipe->writers == 0)
		pipe_buffer_free(pipe);

	return 0;
}

ssize_t sys_pipe2(struct trap_frame *tf)
{
	int *user_fds = (int *)tf->a0;
	int flags = (int)tf->a1;
	int fds[2];

	if (flags != 0)
		return -EINVAL;
	if (!access_ok(user_fds, sizeof(fds)))
		return -EFAULT;

	struct pipe_buffer *pipe = pipe_buffer_alloc();
	if (!pipe)
		return -ENOMEM;

	struct file *read_file = file_alloc(&pipe_read_fops, FMODE_READ, pipe);
	if (!read_file) {
		pipe_buffer_free(pipe);
		return -ENOMEM;
	}

	struct file *write_file =
		file_alloc(&pipe_write_fops, FMODE_WRITE, pipe);
	if (!write_file) {
		file_put(read_file);
		return -ENOMEM;
	}

	fds[0] = fd_alloc(read_file);
	if (fds[0] < 0) {
		file_put(read_file);
		file_put(write_file);
		return fds[0];
	}

	fds[1] = fd_alloc(write_file);
	if (fds[1] < 0) {
		fd_close(fds[0]);
		file_put(write_file);
		return fds[1];
	}

	if (copy_to_user(user_fds, fds, sizeof(fds)) != 0) {
		fd_close(fds[0]);
		fd_close(fds[1]);
		return -EFAULT;
	}

	return 0;
}
