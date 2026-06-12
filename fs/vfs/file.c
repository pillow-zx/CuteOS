/*
 * fs/vfs/file.c - Stage 4 最小 file/fd 层
 */

#include <kernel/fs.h>
#include <kernel/task.h>
#include <kernel/slab.h>
#include <kernel/string.h>
#include <kernel/errno.h>
#include <drivers/uart.h>

static ssize_t console_write(struct file *file, const char *buf, size_t count);

static const struct file_operations console_fops = {
	.write = console_write,
};

static struct file console_stdout = {
	.f_op = &console_fops,
	.f_mode = FMODE_WRITE,
	.refcount = 1,
	.static_file = true,
};

static struct file console_stderr = {
	.f_op = &console_fops,
	.f_mode = FMODE_WRITE,
	.refcount = 1,
	.static_file = true,
};

struct file *file_alloc(const struct file_operations *f_op, uint32_t mode,
			void *private_data)
{
	struct file *file = kmalloc(sizeof(*file));
	if (!file)
		return NULL;

	memset(file, 0, sizeof(*file));
	file->f_op = f_op;
	file->f_mode = mode;
	file->private_data = private_data;
	file->refcount = 1;

	return file;
}

void file_get(struct file *file)
{
	if (file && !file->static_file)
		file->refcount++;
}

void file_put(struct file *file)
{
	if (!file || file->static_file)
		return;

	file->refcount--;
	if (file->refcount > 0)
		return;

	if (file->f_op && file->f_op->release)
		file->f_op->release(file);

	kfree(file);
}

int fd_alloc(struct file *file)
{
	if (!file)
		return -EINVAL;

	for (int fd = 0; fd < NR_OPEN; fd++) {
		if (!current->fd_array[fd]) {
			current->fd_array[fd] = file;
			return fd;
		}
	}

	return -EMFILE;
}

struct file *fd_get(int fd)
{
	if (fd < 0 || fd >= NR_OPEN)
		return NULL;

	return current->fd_array[fd];
}

int fd_close(int fd)
{
	struct file *file = fd_get(fd);
	if (!file)
		return -EBADF;

	current->fd_array[fd] = NULL;
	file_put(file);

	return 0;
}

int fd_dup(int oldfd)
{
	struct file *file = fd_get(oldfd);
	if (!file)
		return -EBADF;

	int newfd = fd_alloc(file);
	if (newfd < 0)
		return newfd;

	file_get(file);
	return newfd;
}

int fd_dup2(int oldfd, int newfd)
{
	struct file *file = fd_get(oldfd);
	if (!file)
		return -EBADF;
	if (newfd < 0 || newfd >= NR_OPEN)
		return -EBADF;
	if (oldfd == newfd)
		return newfd;

	if (current->fd_array[newfd])
		fd_close(newfd);

	current->fd_array[newfd] = file;
	file_get(file);

	return newfd;
}

int copy_files(struct task_struct *child)
{
	if (!child)
		return -EINVAL;

	for (int fd = 0; fd < NR_OPEN; fd++) {
		struct file *file = current->fd_array[fd];
		child->fd_array[fd] = file;
		file_get(file);
	}

	return 0;
}

void close_files(struct task_struct *task)
{
	if (!task)
		return;

	for (int fd = 0; fd < NR_OPEN; fd++) {
		struct file *file = task->fd_array[fd];
		if (!file)
			continue;

		task->fd_array[fd] = NULL;
		file_put(file);
	}
}

void file_install_standard_fds(struct task_struct *task)
{
	if (!task)
		return;

	task->fd_array[KERN_STDOUT] = &console_stdout;
	task->fd_array[KERN_STDERR] = &console_stderr;
}

static ssize_t console_write(struct file *file, const char *buf, size_t count)
{
	(void)file;

	for (size_t i = 0; i < count; i++)
		uart_putc(buf[i]);

	return (ssize_t)count;
}
