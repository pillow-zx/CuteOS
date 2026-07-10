/*
 * fs/pipe.c - 管道
 */

#include <kernel/buddy.h>
#include <kernel/errno.h>
#include <kernel/fdtable.h>
#include <kernel/fs.h>
#include <kernel/pipe.h>
#include <kernel/slab.h>
#include <kernel/signal.h>
#include <kernel/spinlock.h>
#include <kernel/wait.h>
#include <kernel/page.h>

#define PIPE_SIZE PAGE_SIZE

struct pipe_buffer {
	spinlock_t lock;
	uint8_t *data;
	size_t head;
	size_t tail;
	size_t used;

	int readers;
	int writers;
	struct wait_queue_head readers_wq;
	struct wait_queue_head writers_wq;
};

static ssize_t pipe_read(struct file *file, char *buf, size_t count);
static ssize_t pipe_write(struct file *file, const char *buf, size_t count);
static int pipe_poll(struct file *file, uint32_t events,
		     struct wait_registrar *registrar);
static int pipe_release(struct file *file);

static const struct file_operations pipe_read_fops = {
	.read = pipe_read,
	.poll = pipe_poll,
	.release = pipe_release,
};

static const struct file_operations pipe_write_fops = {
	.write = pipe_write,
	.poll = pipe_poll,
	.release = pipe_release,
};

bool pipe_file(struct file *file)
{
	return file && (file->f_op == &pipe_read_fops ||
			file->f_op == &pipe_write_fops);
}

#ifdef KERNEL_SELFTEST
static int pipe_test_file_alloc_fail_at;
static int pipe_test_file_alloc_calls;
static uint32_t pipe_test_live_buffer_count;

void pipe_test_set_file_alloc_fail_at(int fail_at)
{
	pipe_test_file_alloc_fail_at = fail_at;
	pipe_test_file_alloc_calls = 0;
}

uint32_t pipe_test_live_buffers(void)
{
	return pipe_test_live_buffer_count;
}
#endif

static struct file *pipe_file_alloc(const struct file_operations *f_op,
				    uint32_t mode, void *private_data)
{
#ifdef KERNEL_SELFTEST
	pipe_test_file_alloc_calls++;
	if (pipe_test_file_alloc_fail_at > 0 &&
	    pipe_test_file_alloc_calls == pipe_test_file_alloc_fail_at)
		return NULL;
#endif

	return file_alloc(f_op, mode, private_data);
}

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

static int pipe_read_probe(struct wait_registrar *registrar, void *arg)
{
	struct pipe_buffer *pipe = arg;
	irq_flags_t flags;
	int ret;

	spin_lock_irqsave(&pipe->lock, &flags);
	if (pipe->used > 0 || pipe->writers == 0) {
		spin_unlock_irqrestore(&pipe->lock, flags);
		return 1;
	}

	ret = wait_register(registrar, &pipe->readers_wq);
	if (ret < 0) {
		spin_unlock_irqrestore(&pipe->lock, flags);
		return ret;
	}

	ret = pipe->used > 0 || pipe->writers == 0;
	spin_unlock_irqrestore(&pipe->lock, flags);
	return ret;
}

static int pipe_write_probe(struct wait_registrar *registrar, void *arg)
{
	struct pipe_buffer *pipe = arg;
	irq_flags_t flags;
	int ret;

	spin_lock_irqsave(&pipe->lock, &flags);
	if (pipe->used < PIPE_SIZE || pipe->readers == 0) {
		spin_unlock_irqrestore(&pipe->lock, flags);
		return 1;
	}

	ret = wait_register(registrar, &pipe->writers_wq);
	if (ret < 0) {
		spin_unlock_irqrestore(&pipe->lock, flags);
		return ret;
	}

	ret = pipe->used < PIPE_SIZE || pipe->readers == 0;
	spin_unlock_irqrestore(&pipe->lock, flags);
	return ret;
}

static int pipe_wait(struct pipe_buffer *pipe, wait_probe_t probe)
{
	const struct wait_deadline deadline = {
		.active = false,
	};
	struct wait_source source = {
		.probe = probe,
		.arg = pipe,
		.registration_limit = 1,
	};
	wait_completion_t completion;
	int ret;

	ret = wait_complete(&source, WAIT_F_INTERRUPTIBLE, &deadline,
			    &completion);
	if (ret < 0)
		return ret;
	if (completion == WAIT_COMPLETION_SIGNAL)
		return -EINTR;
	BUG_ON(completion != WAIT_COMPLETION_EVENT);
	return 0;
}

static void pipe_commit_read_locked(struct pipe_buffer *pipe, size_t count)
{
	pipe->tail = (pipe->tail + count) % PIPE_SIZE;
	pipe->used -= count;
	wake_up(&pipe->writers_wq);
}

static struct pipe_buffer *pipe_buffer_alloc(void)
{
	struct pipe_buffer *pipe = kmalloc(sizeof(*pipe));
	if (!pipe)
		return NULL;

	memset(pipe, 0, sizeof(*pipe));
	pipe->lock = (spinlock_t)SPINLOCK_INIT;
	pipe->data = get_free_page(0);
	if (!pipe->data) {
		kfree(pipe);
		return NULL;
	}

	init_waitqueue_head(&pipe->readers_wq);
	init_waitqueue_head(&pipe->writers_wq);

#ifdef KERNEL_SELFTEST
	pipe_test_live_buffer_count++;
#endif

	return pipe;
}

static void pipe_buffer_free(struct pipe_buffer *pipe)
{
	if (!pipe)
		return;

	if (pipe->data)
		free_page(pipe->data, 0);
	kfree(pipe);
#ifdef KERNEL_SELFTEST
	pipe_test_live_buffer_count--;
#endif
}

static ssize_t pipe_read(struct file *file, char *buf, size_t count)
{
	struct pipe_buffer *pipe = file->private_data;
	size_t done = 0;
	irq_flags_t flags;

	if (!pipe)
		return -EINVAL;

	while (done < count) {
		size_t chunk;
		size_t linear;

		spin_lock_irqsave(&pipe->lock, &flags);
		if (pipe->used == 0) {
			if (pipe->writers == 0 || done > 0) {
				spin_unlock_irqrestore(&pipe->lock, flags);
				break;
			}
			if (file->f_flags & O_NONBLOCK) {
				spin_unlock_irqrestore(&pipe->lock, flags);
				return -EAGAIN;
			}
			spin_unlock_irqrestore(&pipe->lock, flags);

			int ret = pipe_wait(pipe, pipe_read_probe);
			if (ret < 0)
				return done ? (ssize_t)done : ret;
			continue;
		}

		chunk = count - done;
		linear = pipe_linear_tail(pipe);

		if (chunk > linear)
			chunk = linear;

		memcpy(buf + done, pipe->data + pipe->tail, chunk);
		pipe_commit_read_locked(pipe, chunk);
		spin_unlock_irqrestore(&pipe->lock, flags);
		done += chunk;
	}

	return (ssize_t)done;
}

static ssize_t pipe_write(struct file *file, const char *buf, size_t count)
{
	struct pipe_buffer *pipe = file->private_data;
	size_t done = 0;
	irq_flags_t flags;

	if (!pipe)
		return -EINVAL;

	while (done < count) {
		size_t chunk;
		size_t linear;

		spin_lock_irqsave(&pipe->lock, &flags);
		if (pipe->readers == 0) {
			spin_unlock_irqrestore(&pipe->lock, flags);
			if (done == 0)
				(void)send_current_signal(SIGPIPE);
			return done ? (ssize_t)done : -EPIPE;
		}

		if (pipe->used == PIPE_SIZE) {
			if (done > 0) {
				spin_unlock_irqrestore(&pipe->lock, flags);
				break;
			}
			if (file->f_flags & O_NONBLOCK) {
				spin_unlock_irqrestore(&pipe->lock, flags);
				return -EAGAIN;
			}
			spin_unlock_irqrestore(&pipe->lock, flags);

			int ret = pipe_wait(pipe, pipe_write_probe);
			if (ret < 0)
				return done ? (ssize_t)done : ret;
			continue;
		}

		chunk = count - done;
		linear = pipe_linear_head_space(pipe);

		if (chunk > linear)
			chunk = linear;

		memcpy(pipe->data + pipe->head, buf + done, chunk);
		pipe->head = (pipe->head + chunk) % PIPE_SIZE;
		pipe->used += chunk;
		done += chunk;

		wake_up(&pipe->readers_wq);
		spin_unlock_irqrestore(&pipe->lock, flags);
	}

	return (ssize_t)done;
}

static int pipe_poll(struct file *file, uint32_t events,
		     struct wait_registrar *registrar)
{
	struct pipe_buffer *pipe = file->private_data;
	uint32_t mask = 0;
	irq_flags_t flags;
	int ret;

	if (!pipe)
		return POLLERR;
	spin_lock_irqsave(&pipe->lock, &flags);
	if ((events & POLLIN) && (file->f_mode & FMODE_READ)) {
		if (registrar) {
			ret = wait_register(registrar, &pipe->readers_wq);
			if (ret < 0) {
				spin_unlock_irqrestore(&pipe->lock, flags);
				return ret;
			}
		}
		if (pipe->used > 0)
			mask |= POLLIN;
		if (pipe->writers == 0)
			mask |= POLLHUP;
	}
	if ((events & POLLOUT) && (file->f_mode & FMODE_WRITE)) {
		if (registrar) {
			ret = wait_register(registrar, &pipe->writers_wq);
			if (ret < 0) {
				spin_unlock_irqrestore(&pipe->lock, flags);
				return ret;
			}
		}
		if (pipe->readers == 0)
			mask |= POLLERR;
		else if (pipe->used < PIPE_SIZE)
			mask |= POLLOUT;
	}
	spin_unlock_irqrestore(&pipe->lock, flags);
	return mask;
}

static int pipe_release(struct file *file)
{
	struct pipe_buffer *pipe = file->private_data;
	irq_flags_t flags;
	bool free_pipe;

	if (!pipe)
		return 0;

	spin_lock_irqsave(&pipe->lock, &flags);
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

	free_pipe = pipe->readers == 0 && pipe->writers == 0;
	spin_unlock_irqrestore(&pipe->lock, flags);

	if (free_pipe)
		pipe_buffer_free(pipe);

	return 0;
}

ssize_t pipe_splice_to_file(struct file *pipe_file, struct file *out_file,
			    loff_t *out_offset, size_t len)
{
	struct pipe_buffer *pipe = pipe_file ? pipe_file->private_data : NULL;
	char *buffer __cleanup_with(page0) = NULL;
	size_t done = 0;
	irq_flags_t flags;

	if (!pipe || !out_file)
		return -EINVAL;

	while (done < len) {
		size_t chunk;
		ssize_t ret;

		spin_lock_irqsave(&pipe->lock, &flags);
		if (pipe->used == 0) {
			if (pipe->writers == 0 || done > 0) {
				spin_unlock_irqrestore(&pipe->lock, flags);
				break;
			}
			if (pipe_file->f_flags & O_NONBLOCK) {
				spin_unlock_irqrestore(&pipe->lock, flags);
				return -EAGAIN;
			}
			spin_unlock_irqrestore(&pipe->lock, flags);

			ret = pipe_wait(pipe, pipe_read_probe);
			if (ret < 0)
				return done ? (ssize_t)done : ret;
			continue;
		}

		chunk = len - done;
		if (chunk > pipe_linear_tail(pipe))
			chunk = pipe_linear_tail(pipe);

		if (!buffer) {
			spin_unlock_irqrestore(&pipe->lock, flags);
			buffer = get_free_page(0);
			if (!buffer)
				return done ? (ssize_t)done : -ENOMEM;
			continue;
		}

		memcpy(buffer, pipe->data + pipe->tail, chunk);
		spin_unlock_irqrestore(&pipe->lock, flags);

		ret = vfs_write_pos(out_file, buffer, chunk, out_offset);

		if (ret < 0)
			return done ? (ssize_t)done : ret;
		if (ret == 0)
			break;

		spin_lock_irqsave(&pipe->lock, &flags);
		BUG_ON((size_t)ret > pipe->used);
		pipe_commit_read_locked(pipe, (size_t)ret);
		spin_unlock_irqrestore(&pipe->lock, flags);
		done += (size_t)ret;
		if ((size_t)ret < chunk)
			break;
	}

	return (ssize_t)done;
}

int do_pipe2(int fds[2], int flags)
{
	uint32_t status_flags = (uint32_t)flags & O_NONBLOCK;
	int fd_flags = flags & O_CLOEXEC;

	if (flags & ~(O_CLOEXEC | O_NONBLOCK))
		return -EINVAL;
	if (!fds)
		return -EINVAL;

	struct pipe_buffer *pipe = pipe_buffer_alloc();
	if (!pipe)
		return -ENOMEM;

	struct file *read_file =
		pipe_file_alloc(&pipe_read_fops, FMODE_READ, pipe);
	if (!read_file) {
		pipe_buffer_free(pipe);
		return -ENOMEM;
	}
	read_file->f_flags = status_flags | O_RDONLY;

	struct file *write_file =
		pipe_file_alloc(&pipe_write_fops, FMODE_WRITE, pipe);
	if (!write_file) {
		file_put(read_file);
		return -ENOMEM;
	}
	write_file->f_flags = status_flags | O_WRONLY;

	pipe->readers = 1;
	pipe->writers = 1;

	fds[0] = fd_alloc_flags(read_file, fd_flags);
	if (fds[0] < 0) {
		file_put(read_file);
		file_put(write_file);
		return fds[0];
	}

	fds[1] = fd_alloc_flags(write_file, fd_flags);
	if (fds[1] < 0) {
		fd_close(fds[0]);
		file_put(write_file);
		return fds[1];
	}

	return 0;
}
