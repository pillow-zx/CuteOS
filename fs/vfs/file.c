/*
 * fs/vfs/file.c - Stage 4 最小 file/fd 层
 */

#include <kernel/fdtable.h>
#include <kernel/task.h>
#include <kernel/slab.h>
#include <kernel/stat.h>
#include <kernel/blkdev.h>
#include <kernel/string.h>
#include <kernel/errno.h>
#include <kernel/vfs.h>
#include <drivers/uart.h>

static ssize_t console_read(struct file *file, char *buf, size_t count);
static ssize_t console_write(struct file *file, const char *buf, size_t count);
static ssize_t null_read(struct file *file, char *buf, size_t count);
static ssize_t null_write(struct file *file, const char *buf, size_t count);

static const struct file_operations console_fops = {
	.read = console_read,
	.write = console_write,
};

static const struct file_operations null_fops = {
	.read = null_read,
	.write = null_write,
};

int vfs_inode_permission(struct inode *inode, uint32_t mask)
{
	uint32_t perm;
	uint32_t want = 0;

	if (!inode)
		return -ENOENT;
	if (!mask)
		return 0;

	/*
	 * 当前没有 capability 模型。root 简单放行，非 root 按
	 * owner/group/other 三组选一组权限位检查。
	 */
	if (current->uid == 0)
		return 0;

	if (current->uid == inode->i_uid)
		perm = (inode->i_mode >> 6) & 7;
	else if (current->gid == inode->i_gid)
		perm = (inode->i_mode >> 3) & 7;
	else
		perm = inode->i_mode & 7;

	if (mask & VFS_MAY_READ)
		want |= 4;
	if (mask & VFS_MAY_WRITE)
		want |= 2;
	if (mask & VFS_MAY_EXEC)
		want |= 1;

	return (perm & want) == want ? 0 : -EACCES;
}

struct file *fd_get_checked(int fd)
{
	if (fd < 0 || fd >= NR_OPEN)
		return NULL;

	return current->fd_array[fd];
}

static struct file console_stdin = {
	.f_op = &console_fops,
	.f_mode = FMODE_READ,
	.refcount = 1,
	.static_file = true,
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

struct file *file_alloc_dentry(struct dentry *dentry, uint32_t flags,
			       uint32_t mode)
{
	if (!dentry || !dentry->d_inode)
		return NULL;

	const struct file_operations *f_op = dentry->d_inode->i_fop;

	if ((dentry->d_inode->i_mode & S_IFMT) == S_IFCHR) {
		switch (dentry->d_inode->i_rdev) {
		case MKDEV(5, 1):
			f_op = &console_fops;
			break;
		case MKDEV(1, 3):
			f_op = &null_fops;
			break;
		default:
			return NULL;
		}
	}

	struct file *file = file_alloc(f_op, mode, NULL);
	if (!file)
		return NULL;

	file->f_dentry = dentry;
	file->f_inode = dentry->d_inode;
	file->f_flags = flags;
	dget(dentry);
	igrab(file->f_inode);

	if (file->f_op && file->f_op->open) {
		int ret = file->f_op->open(file->f_inode, file);
		if (ret < 0) {
			file->f_dentry = NULL;
			file->f_inode = NULL;
			dput(dentry);
			iput(dentry->d_inode);
			kfree(file);
			return NULL;
		}
	}

	return file;
}

int vfs_open(const char *path, uint32_t flags, uint32_t mode)
{
	struct dentry *dentry;
	struct file *file;
	int fd;
	int ret;
	uint32_t fmode;

	ret = path_lookup_err(path, 0, &dentry);
	if (ret < 0) {
		if (!(flags & O_CREAT) || ret != -ENOENT)
			return ret;

		ret = vfs_create(path, mode, &dentry);
		if (ret < 0)
			return ret;
	} else if ((flags & O_CREAT) && (flags & O_EXCL)) {
		dput(dentry);
		return -EEXIST;
	}

	if ((flags & O_DIRECTORY) && dentry->d_inode &&
	    (dentry->d_inode->i_mode & S_IFMT) != S_IFDIR) {
		dput(dentry);
		return -ENOTDIR;
	}

	switch (flags & O_ACCMODE) {
	case O_RDONLY:
		fmode = FMODE_READ;
		break;
	case O_WRONLY:
		fmode = FMODE_WRITE;
		break;
	case O_RDWR:
		fmode = FMODE_READ | FMODE_WRITE;
		break;
	default:
		dput(dentry);
		return -EINVAL;
	}

	ret = 0;
	if (fmode & FMODE_READ)
		ret = vfs_inode_permission(dentry->d_inode, VFS_MAY_READ);
	if (ret == 0 && (fmode & FMODE_WRITE))
		ret = vfs_inode_permission(dentry->d_inode, VFS_MAY_WRITE);
	if (ret < 0) {
		dput(dentry);
		return ret;
	}

	file = file_alloc_dentry(dentry, flags, fmode);
	dput(dentry);
	if (!file)
		return -ENOMEM;

	if ((flags & O_TRUNC) && (fmode & FMODE_WRITE) && file->f_inode) {
		ret = vfs_truncate_file(file, 0);
		if (ret < 0) {
			file_put(file);
			return ret;
		}
	}

	fd = fd_alloc(file);
	if (fd < 0) {
		file_put(file);
		return fd;
	}

	return fd;
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

	dput(file->f_dentry);
	iput(file->f_inode);
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
	return fd_get_checked(fd);
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

	task->fd_array[KERN_STDIN] = &console_stdin;
	task->fd_array[KERN_STDOUT] = &console_stdout;
	task->fd_array[KERN_STDERR] = &console_stderr;
}

static ssize_t console_read(struct file *file, char *buf, size_t count)
{
	(void)file;

	for (size_t i = 0; i < count; i++)
		buf[i] = (char)uart_getc();

	return (ssize_t)count;
}

static ssize_t console_write(struct file *file, const char *buf, size_t count)
{
	(void)file;

	for (size_t i = 0; i < count; i++)
		uart_putc(buf[i]);

	return (ssize_t)count;
}

static ssize_t null_read(struct file *file, char *buf, size_t count)
{
	(void)file;
	(void)buf;
	(void)count;

	return 0;
}

static ssize_t null_write(struct file *file, const char *buf, size_t count)
{
	(void)file;
	(void)buf;

	return (ssize_t)count;
}
