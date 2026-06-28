/*
 * fs/vfs/file.c - Stage 4 最小 file/fd 层
 */

#include <drivers/console.h>
#include <kernel/fdtable.h>
#include <kernel/slab.h>
#include <kernel/page_cache.h>
#include <kernel/stat.h>
#include <kernel/statfs.h>
#include <kernel/blkdev.h>
#include <kernel/string.h>
#include <kernel/errno.h>
#include <kernel/task.h>
#include <kernel/vfs.h>

static ssize_t null_read(struct file *file, char *buf, size_t count);
static ssize_t null_write(struct file *file, const char *buf, size_t count);
static uint32_t null_poll(struct file *file, uint32_t events);

static const struct file_operations null_fops = {
	.read = null_read,
	.write = null_write,
	.poll = null_poll,
};

int vfs_sync_file(struct file *file)
{
	int ret;

	if (!file || !file->f_inode)
		return -EINVAL;

	ret = page_cache_sync_inode(file->f_inode);
	if (ret < 0)
		return ret;

	return vfs_inode_writeback(file->f_inode);
}

int vfs_truncate_file(struct file *file, uint64_t size)
{
	if (!file || !file->f_inode)
		return -EINVAL;

	return vfs_inode_truncate(file->f_inode, size);
}

int vfs_stat_file(struct file *file, struct kstat *st)
{
	if (!file)
		return -EINVAL;

	return vfs_stat_inode(file->f_inode, st);
}

int vfs_statfs(struct super_block *sb, struct kstatfs *buf)
{
	if (!sb || !buf)
		return -EINVAL;
	if (!sb->s_op || !sb->s_op->statfs)
		return -ENOSYS;

	return sb->s_op->statfs(sb, buf);
}

uint32_t vfs_poll(struct file *file, uint32_t events)
{
	uint32_t mask = 0;

	if (!file)
		return POLLNVAL;
	if (file->f_op && file->f_op->poll)
		return file->f_op->poll(file, events);

	if ((events & POLLIN) && (file->f_mode & FMODE_READ))
		mask |= POLLIN;
	if ((events & POLLOUT) && (file->f_mode & FMODE_WRITE))
		mask |= POLLOUT;
	return mask;
}

int vfs_ioctl(struct file *file, uint64_t cmd, uint64_t arg)
{
	if (!file)
		return -EINVAL;
	if (!file->f_op || !file->f_op->ioctl)
		return -ENOTTY;

	return file->f_op->ioctl(file, cmd, arg);
}

static struct file console_stdin = {
	.f_op = &console_fops,
	.f_mode = FMODE_READ,
	.refcount = REFCOUNT_INIT(1),
	.static_file = true,
};

static struct file console_stdout = {
	.f_op = &console_fops,
	.f_mode = FMODE_WRITE,
	.refcount = REFCOUNT_INIT(1),
	.static_file = true,
};

static struct file console_stderr = {
	.f_op = &console_fops,
	.f_mode = FMODE_WRITE,
	.refcount = REFCOUNT_INIT(1),
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
	refcount_set(&file->refcount, 1);

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

int vfs_openat(struct dentry *base, const char *path, uint32_t flags,
	       uint32_t mode)
{
	struct dentry *dentry;
	struct file *file;
	int fd;
	int ret;
	uint32_t fmode;

	ret = path_lookupat_err(base, path, 0, &dentry);
	if (ret < 0) {
		if (!(flags & O_CREAT) || ret != -ENOENT)
			return ret;

		ret = vfs_create_at(base, path, mode, &dentry);
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

int vfs_open(const char *path, uint32_t flags, uint32_t mode)
{
	return vfs_openat(NULL, path, flags, mode);
}

void file_get(struct file *file)
{
	if (file && !file->static_file)
		refcount_inc(&file->refcount);
}

void file_put(struct file *file)
{
	if (!file || file->static_file)
		return;

	if (!refcount_dec_and_test(&file->refcount))
		return;

	if (file->f_op && file->f_op->release)
		file->f_op->release(file);

	dput(file->f_dentry);
	iput(file->f_inode);
	kfree(file);
}

struct files_struct *files_alloc(void)
{
	struct files_struct *files = kmalloc(sizeof(*files));

	if (!files)
		return NULL;

	memset(files, 0, sizeof(*files));
	refcount_set(&files->refcount, 1);
	mutex_init(&files->lock);
	return files;
}

struct files_struct *files_dup(struct files_struct *old)
{
	struct files_struct *files = files_alloc();

	if (!files)
		return NULL;
	if (!old)
		return files;

	mutex_lock(&old->lock);
	for (int fd = 0; fd < NR_OPEN; fd++) {
		files->fd[fd] = old->fd[fd];
		file_get(files->fd[fd]);
	}
	mutex_unlock(&old->lock);

	return files;
}

void files_get(struct files_struct *files)
{
	if (files)
		refcount_inc(&files->refcount);
}

void files_put(struct files_struct *files)
{
	if (!files)
		return;

	if (!refcount_dec_and_test(&files->refcount))
		return;

	for (int fd = 0; fd < NR_OPEN; fd++) {
		struct file *file = files->fd[fd];

		files->fd[fd] = NULL;
		file_put(file);
	}
	kfree(files);
}

void files_install_standard_fds(struct files_struct *files)
{
	if (!files)
		return;

	files->fd[KERN_STDIN] = &console_stdin;
	files->fd[KERN_STDOUT] = &console_stdout;
	files->fd[KERN_STDERR] = &console_stderr;
}

static struct files_struct *current_files(void)
{
	return task_files(current);
}

int fd_alloc_flags(struct file *file, int flags)
{
	struct files_struct *files = current_files();

	if (!file)
		return -EINVAL;
	if (!files)
		return -EBADF;

	mutex_lock(&files->lock);
	for (int fd = 0; fd < NR_OPEN; fd++) {
		if (!files->fd[fd]) {
			files->fd[fd] = file;
			if (flags & O_CLOEXEC)
				files->close_on_exec |= (1UL << (unsigned int)fd);
			mutex_unlock(&files->lock);
			return fd;
		}
	}
	mutex_unlock(&files->lock);

	return -EMFILE;
}

int fd_alloc(struct file *file)
{
	return fd_alloc_flags(file, 0);
}

struct file *fd_get_checked(int fd)
{
	struct files_struct *files = current_files();
	struct file *file;

	if (!files || fd < 0 || fd >= NR_OPEN)
		return NULL;

	mutex_lock(&files->lock);
	file = files->fd[fd];
	file_get(file);
	mutex_unlock(&files->lock);
	return file;
}

struct file *fd_get(int fd)
{
	return fd_get_checked(fd);
}

int fd_close(int fd)
{
	struct files_struct *files = current_files();
	struct file *file;

	if (!files || fd < 0 || fd >= NR_OPEN)
		return -EBADF;

	mutex_lock(&files->lock);
	file = files->fd[fd];
	if (!file) {
		mutex_unlock(&files->lock);
		return -EBADF;
	}

	files->fd[fd] = NULL;
	mutex_unlock(&files->lock);
	file_put(file);

	return 0;
}

int fd_dup(int oldfd)
{
	struct files_struct *files = current_files();
	struct file *file = fd_get(oldfd);
	int newfd = -EMFILE;

	if (!file)
		return -EBADF;
	if (!files) {
		file_put(file);
		return -EBADF;
	}

	mutex_lock(&files->lock);
	for (int fd = 0; fd < NR_OPEN; fd++) {
		if (!files->fd[fd]) {
			file_get(file);
			files->fd[fd] = file;
			newfd = fd;
			break;
		}
	}
	mutex_unlock(&files->lock);

	file_put(file);
	return newfd;
}

int fd_dup2(int oldfd, int newfd, int cloexec)
{
	struct files_struct *files = current_files();
	struct file *file = fd_get(oldfd);
	struct file *old = NULL;

	if (!file)
		return -EBADF;
	if (!files || newfd < 0 || newfd >= NR_OPEN) {
		file_put(file);
		return -EBADF;
	}
	if (oldfd == newfd) {
		file_put(file);
		return newfd;
	}

	mutex_lock(&files->lock);
	old = files->fd[newfd];
	file_get(file);
	files->fd[newfd] = file;
	if (cloexec)
		files->close_on_exec |= (1UL << (unsigned int)newfd);
	else
		files->close_on_exec &= ~(1UL << (unsigned int)newfd);
	mutex_unlock(&files->lock);

	file_put(old);
	file_put(file);

	return newfd;
}

int init_files(struct task_struct *task)
{
	if (!task)
		return -EINVAL;

	task->files = files_alloc();
	if (!task->files)
		return -ENOMEM;

	files_install_standard_fds(task->files);
	return 0;
}

int copy_files(struct task_struct *child, bool share)
{
	struct files_struct *files;

	if (!child)
		return -EINVAL;

	if (share) {
		files = task_files(current);
		if (!files)
			return init_files(child);
		files_get(files);
	} else {
		files = files_dup(task_files(current));
		if (!files)
			return -ENOMEM;
	}

	close_files(child);
	child->files = files;
	return 0;
}

void close_files(struct task_struct *task)
{
	if (!task || !task->files)
		return;

	files_put(task->files);
	task->files = NULL;
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

static uint32_t null_poll(struct file *file, uint32_t events)
{
	uint32_t mask = 0;

	if ((events & POLLIN) && (file->f_mode & FMODE_READ))
		mask |= POLLIN;
	if ((events & POLLOUT) && (file->f_mode & FMODE_WRITE))
		mask |= POLLOUT;
	return mask;
}
