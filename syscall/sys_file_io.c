/*
 * syscall/sys_file_io.c - fd I/O 和 iovec 系统调用
 *
 * 覆盖范围：
 *   fd 可读/可写校验、分块用户内存拷贝（rw_user_buffer）、
 *   偏移读写（rw_at_offset）、scatter-gather I/O（rw_iovec），
 *   以及所有基于 fd 的读写、定位、控制和管理系统调用。
 */

#include <kernel/fdtable.h>
#include <kernel/fs.h>
#include <kernel/fs_struct.h>
#include <kernel/signal.h>
#include <kernel/stat.h>
#include <kernel/statfs.h>
#include <kernel/types.h>
#include <kernel/errno.h>
#include <kernel/syscall.h>
#include <kernel/mm.h>
#include <kernel/buddy.h>
#include <kernel/pipe.h>
#include <kernel/string.h>
#include <kernel/task.h>
#include <kernel/timer.h>
#include <kernel/vfs.h>
#include <asm/page.h>
#include <asm/trap.h>
#include <kernel/time.h>

#include "sys_file_internal.h"

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define F_DUPFD		    0
#define F_GETFD		    1
#define F_SETFD		    2
#define F_GETFL		    3
#define F_SETFL		    4
#define F_GETLK		    5
#define F_SETLK		    6
#define F_SETLKW	    7
#define F_SETOWN	    8
#define F_GETOWN	    9
#define F_SETSIG	    10
#define F_GETSIG	    11
#define F_GETLK64	    12
#define F_SETLK64	    13
#define F_SETLKW64	    14
#define F_SETOWN_EX	    15
#define F_GETOWN_EX	    16
#define F_GETOWNER_UIDS	    17
#define F_OFD_GETLK	    36
#define F_OFD_SETLK	    37
#define F_OFD_SETLKW	    38
#define F_LINUX_SPECIFIC_BASE 1024
#define F_SETLEASE	    (F_LINUX_SPECIFIC_BASE + 0)
#define F_GETLEASE	    (F_LINUX_SPECIFIC_BASE + 1)
#define F_NOTIFY	    (F_LINUX_SPECIFIC_BASE + 2)
#define F_DUPFD_QUERY	    (F_LINUX_SPECIFIC_BASE + 3)
#define F_CREATED_QUERY	    (F_LINUX_SPECIFIC_BASE + 4)
#define F_CANCELLK	    (F_LINUX_SPECIFIC_BASE + 5)
#define F_DUPFD_CLOEXEC    (F_LINUX_SPECIFIC_BASE + 6)
#define F_SETPIPE_SZ	    (F_LINUX_SPECIFIC_BASE + 7)
#define F_GETPIPE_SZ	    (F_LINUX_SPECIFIC_BASE + 8)
#define F_ADD_SEALS	    (F_LINUX_SPECIFIC_BASE + 9)
#define F_GET_SEALS	    (F_LINUX_SPECIFIC_BASE + 10)
#define F_GET_RW_HINT	    (F_LINUX_SPECIFIC_BASE + 11)
#define F_SET_RW_HINT	    (F_LINUX_SPECIFIC_BASE + 12)
#define F_GET_FILE_RW_HINT  (F_LINUX_SPECIFIC_BASE + 13)
#define F_SET_FILE_RW_HINT  (F_LINUX_SPECIFIC_BASE + 14)
#define F_GETDELEG	    (F_LINUX_SPECIFIC_BASE + 15)
#define F_SETDELEG	    (F_LINUX_SPECIFIC_BASE + 16)
#define FD_CLOEXEC	    1

#define FALLOC_FL_KEEP_SIZE 0x01

/*
 * ftruncate/fallocate 允许设置的最大 i_size。真实上界取决于具体文件系统；
 * 此处取保守值，主要防止无界膨胀导致 fill_kstat 的 st_blocks 计算溢出，
 * 以及未来 extent/块分配逻辑对越界 i_size 误判。
 */
#define MAX_FILE_SIZE (1ULL << 40) /* 1 TiB */

static struct file *fd_get_readable(int fd)
{
	struct file *file = fd_get(fd);

	if (!file)
		return NULL;
	if (!(file->f_mode & FMODE_READ) || !file->f_op || !file->f_op->read) {
		file_put(file);
		return NULL;
	}

	return file;
}

static struct file *fd_get_writable(int fd)
{
	struct file *file = fd_get(fd);

	if (!file)
		return NULL;
	if (!(file->f_mode & FMODE_WRITE) || !file->f_op ||
	    !file->f_op->write) {
		file_put(file);
		return NULL;
	}

	return file;
}

static ssize_t rw_user_buffer(struct file *file, void *buf, size_t len,
			      bool write)
{
	char kbuf[SYS_FILE_BUF_SIZE];
	size_t done = 0;

	if (!access_ok(buf, len))
		return -EFAULT;

	while (done < len) {
		size_t chunk = len - done;
		ssize_t ret;

		if (chunk > SYS_FILE_BUF_SIZE)
			chunk = SYS_FILE_BUF_SIZE;

		if (write) {
			if (copy_from_user(kbuf, (char *)buf + done, chunk) !=
			    0)
				return done ? (ssize_t)done : -EFAULT;
			ret = vfs_write(file, kbuf, chunk);
		} else {
			ret = vfs_read(file, kbuf, chunk);
			if (ret > 0) {
				size_t left = copy_to_user((char *)buf + done,
							   kbuf, (size_t)ret);

				if (left != 0) {
					/*
					 * vfs_read 已推进 f_pos 整个 ret，但只
					 * 有 (ret - left)
					 * 字节真正送达用户。回退
					 * 未送达的尾部，避免下次读静默跳过这些
					 * 字节。
					 */
					file->f_pos -= (loff_t)left;
					done += (size_t)ret - left;
					return done ? (ssize_t)done : -EFAULT;
				}
			}
		}

		if (ret < 0)
			return ret;
		if (ret == 0)
			break;

		done += (size_t)ret;
		if ((size_t)ret < chunk)
			break;
	}

	return (ssize_t)done;
}

static ssize_t rw_at_offset(struct file *file, void *buf, size_t len,
			    loff_t offset, bool write)
{
	loff_t old_pos;
	ssize_t ret;

	if (offset < 0)
		return -EINVAL;

	old_pos = file->f_pos;
	file->f_pos = offset;
	ret = rw_user_buffer(file, buf, len, write);
	file->f_pos = old_pos;

	return ret;
}

static ssize_t rw_iovec(struct file *file, const struct sys_iovec *uiov,
			size_t iovcnt, bool write)
{
	struct sys_iovec iov;
	ssize_t total = 0;

	if (iovcnt > SYS_IOV_MAX)
		return -EINVAL;

	for (size_t i = 0; i < iovcnt; i++) {
		size_t done = 0;

		if (copy_from_user(&iov, uiov + i, sizeof(iov)) != 0)
			return total ? total : -EFAULT;
		if (!access_ok((void *)(uintptr_t)iov.iov_base, iov.iov_len))
			return total ? total : -EFAULT;

		while (done < iov.iov_len) {
			ssize_t ret = rw_user_buffer(
				file, (void *)(uintptr_t)(iov.iov_base + done),
				(size_t)(iov.iov_len - done), write);

			if (ret < 0)
				return total ? total : ret;
			if (ret == 0)
				return total;

			done += (size_t)ret;
			total += ret;
		}
	}

	return total;
}

ssize_t sys_write(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	const char *buf = (const char *)tf->a1;
	size_t len = tf->a2;
	struct file *file = fd_get_writable(fd);
	ssize_t ret;

	if (!file)
		return -EBADF;

	ret = rw_user_buffer(file, (void *)buf, len, true);
	file_put(file);
	return ret;
}

ssize_t sys_read(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	char *buf = (char *)tf->a1;
	size_t len = tf->a2;
	struct file *file = fd_get_readable(fd);
	ssize_t ret;

	if (!file)
		return -EBADF;

	ret = rw_user_buffer(file, buf, len, false);
	file_put(file);
	return ret;
}

ssize_t sys_readv(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	const struct sys_iovec *uiov = (const struct sys_iovec *)tf->a1;
	size_t iovcnt = tf->a2;
	struct file *file = fd_get_readable(fd);
	ssize_t ret;

	if (!file)
		return -EBADF;
	if (!access_ok(uiov, iovcnt * sizeof(*uiov))) {
		file_put(file);
		return -EFAULT;
	}

	ret = rw_iovec(file, uiov, iovcnt, false);
	file_put(file);
	return ret;
}

ssize_t sys_writev(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	const struct sys_iovec *uiov = (const struct sys_iovec *)tf->a1;
	size_t iovcnt = tf->a2;
	struct file *file = fd_get_writable(fd);
	ssize_t ret;

	if (!file)
		return -EBADF;
	if (!access_ok(uiov, iovcnt * sizeof(*uiov))) {
		file_put(file);
		return -EFAULT;
	}

	ret = rw_iovec(file, uiov, iovcnt, true);
	file_put(file);
	return ret;
}

ssize_t sys_pread64(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	char *buf = (char *)tf->a1;
	size_t len = tf->a2;
	loff_t offset = (loff_t)tf->a3;
	struct file *file = fd_get_readable(fd);
	ssize_t ret;

	if (!file)
		return -EBADF;

	ret = rw_at_offset(file, buf, len, offset, false);
	file_put(file);
	return ret;
}

ssize_t sys_pwrite64(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	const char *buf = (const char *)tf->a1;
	size_t len = tf->a2;
	loff_t offset = (loff_t)tf->a3;
	struct file *file = fd_get_writable(fd);
	ssize_t ret;

	if (!file)
		return -EBADF;

	ret = rw_at_offset(file, (void *)buf, len, offset, true);
	file_put(file);
	return ret;
}

ssize_t sys_close(struct trap_frame *tf)
{
	return fd_close((int)tf->a0);
}

ssize_t sys_lseek(struct trap_frame *tf)
{
	struct file *file = fd_get((int)tf->a0);
	loff_t offset = (loff_t)tf->a1;
	int whence = (int)tf->a2;
	ssize_t ret;

	if (!file)
		return -EBADF;

	ret = vfs_llseek(file, offset, whence);
	file_put(file);
	return ret;
}

ssize_t sys_ioctl(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	uint64_t cmd = tf->a1;
	uint64_t arg = tf->a2;
	struct file *file = fd_get(fd);
	ssize_t ret;

	if (!file)
		return -EBADF;

	ret = vfs_ioctl(file, cmd, arg);
	file_put(file);
	return ret;
}

ssize_t sys_fcntl(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	int cmd = (int)tf->a1;
	unsigned long arg = tf->a2;
	int ret;

	switch (cmd) {
	case F_GETFD:
		ret = fd_get_close_on_exec(fd);
		if (ret < 0)
			return ret;
		return ret ? FD_CLOEXEC : 0;
	case F_SETFD:
		return fd_set_close_on_exec(fd, arg & FD_CLOEXEC);
	default:
		return -EINVAL;
	}
}

ssize_t sys_dup(struct trap_frame *tf)
{
	return fd_dup((int)tf->a0);
}

ssize_t sys_dup3(struct trap_frame *tf)
{
	int oldfd = (int)tf->a0;
	int newfd = (int)tf->a1;
	int flags = (int)tf->a2;

	if (flags & ~O_CLOEXEC)
		return -EINVAL;

	return fd_dup2(oldfd, newfd, flags & O_CLOEXEC);
}

ssize_t sys_fsync(struct trap_frame *tf)
{
	struct file *file = fd_get((int)tf->a0);
	ssize_t ret;

	if (!file)
		return -EBADF;

	ret = vfs_sync_file(file);
	file_put(file);
	return ret;
}

ssize_t sys_fdatasync(struct trap_frame *tf)
{
	return sys_fsync(tf);
}

ssize_t sys_ftruncate64(struct trap_frame *tf)
{
	struct file *file = fd_get((int)tf->a0);
	int64_t length = (int64_t)tf->a1;
	ssize_t ret;

	if (!file)
		return -EBADF;
	if (!(file->f_mode & FMODE_WRITE)) {
		file_put(file);
		return -EBADF;
	}
	if (length < 0 || length > MAX_FILE_SIZE) {
		file_put(file);
		return -EINVAL;
	}
	if (!file->f_inode) {
		file_put(file);
		return -EINVAL;
	}

	ret = vfs_truncate_file(file, (uint64_t)length);
	file_put(file);
	return ret;
}

ssize_t sys_fallocate(struct trap_frame *tf)
{
	(void)tf;
	/* TODO(ext2): 需要真正分配/预留数据块后才能实现 fallocate 语义。 */
	return -ENOSYS;
}

ssize_t sys_pipe2(struct trap_frame *tf)
{
	int *user_fds = (int *)tf->a0;
	int flags = (int)tf->a1;
	int fds[2];
	int ret;

	if (!access_ok(user_fds, sizeof(int[2])))
		return -EFAULT;
	if (flags & ~O_CLOEXEC)
		return -EINVAL;

	ret = do_pipe2(fds, flags);
	if (ret < 0)
		return ret;

	if (copy_to_user(user_fds, fds, sizeof(fds)) != 0) {
		fd_close(fds[0]);
		fd_close(fds[1]);
		return -EFAULT;
	}

	return 0;
}
