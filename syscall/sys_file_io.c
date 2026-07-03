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
#include <kernel/task.h>
#include <kernel/timer.h>
#include <kernel/vfs.h>
#include <uapi/uio.h>
#include <asm/page.h>
#include <asm/trap.h>
#include <kernel/time.h>

#include "sys_file_internal.h"

#define F_GETLK		      5
#define F_SETLK		      6
#define F_SETLKW	      7
#define F_SETOWN	      8
#define F_GETOWN	      9
#define F_SETSIG	      10
#define F_GETSIG	      11
#define F_GETLK64	      12
#define F_SETLK64	      13
#define F_SETLKW64	      14
#define F_SETOWN_EX	      15
#define F_GETOWN_EX	      16
#define F_GETOWNER_UIDS	      17
#define F_OFD_GETLK	      36
#define F_OFD_SETLK	      37
#define F_OFD_SETLKW	      38
#define F_LINUX_SPECIFIC_BASE 1024
#define F_SETLEASE	      (F_LINUX_SPECIFIC_BASE + 0)
#define F_GETLEASE	      (F_LINUX_SPECIFIC_BASE + 1)
#define F_NOTIFY	      (F_LINUX_SPECIFIC_BASE + 2)
#define F_DUPFD_QUERY	      (F_LINUX_SPECIFIC_BASE + 3)
#define F_CREATED_QUERY	      (F_LINUX_SPECIFIC_BASE + 4)
#define F_CANCELLK	      (F_LINUX_SPECIFIC_BASE + 5)
#define F_SETPIPE_SZ	      (F_LINUX_SPECIFIC_BASE + 7)
#define F_GETPIPE_SZ	      (F_LINUX_SPECIFIC_BASE + 8)
#define F_ADD_SEALS	      (F_LINUX_SPECIFIC_BASE + 9)
#define F_GET_SEALS	      (F_LINUX_SPECIFIC_BASE + 10)
#define F_GET_RW_HINT	      (F_LINUX_SPECIFIC_BASE + 11)
#define F_SET_RW_HINT	      (F_LINUX_SPECIFIC_BASE + 12)
#define F_GET_FILE_RW_HINT    (F_LINUX_SPECIFIC_BASE + 13)
#define F_SET_FILE_RW_HINT    (F_LINUX_SPECIFIC_BASE + 14)
#define F_GETDELEG	      (F_LINUX_SPECIFIC_BASE + 15)
#define F_SETDELEG	      (F_LINUX_SPECIFIC_BASE + 16)
#define SPLICE_F_SUPPORTED_HINTS                                             \
	(SPLICE_F_MOVE | SPLICE_F_MORE | SPLICE_F_GIFT)
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

static ssize_t read_user_buffer_pos(struct file *file, void *buf, size_t len,
				    loff_t *pos)
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

		ret = vfs_read_pos(file, kbuf, chunk, pos);
		if (ret > 0) {
			size_t left =
				copy_to_user((char *)buf + done, kbuf, (size_t)ret);

			if (left != 0) {
				/*
				 * vfs_read_pos 已推进位置整个 ret，但只有
				 * (ret - left) 字节真正送达用户。回退未送达的
				 * 尾部，避免下次读静默跳过这些字节。
				 */
				if (pos)
					*pos -= (loff_t)left;
				else
					vfs_rewind_pos(file, (loff_t)left);
				done += (size_t)ret - left;
				return done ? (ssize_t)done : -EFAULT;
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

static ssize_t write_user_buffer_pos(struct file *file, const void *buf,
				     size_t len, loff_t *pos)
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

		if (copy_from_user(kbuf, (const char *)buf + done, chunk) != 0)
			return done ? (ssize_t)done : -EFAULT;

		ret = vfs_write_pos(file, kbuf, chunk, pos);
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

static bool splice_regular_file(struct file *file)
{
	return file && file->f_inode && S_ISREG(file->f_inode->i_mode);
}

static int copy_user_offset(loff_t *uoffset, loff_t *offset)
{
	if (!access_ok(uoffset, sizeof(*uoffset)))
		return -EFAULT;
	if (copy_from_user(offset, uoffset, sizeof(*offset)) != 0)
		return -EFAULT;
	if (*offset < 0)
		return -EINVAL;

	return 0;
}

static ssize_t rw_iovec(struct file *file, const struct iovec *uiov,
			size_t iovcnt, bool write)
{
	struct iovec iov;
	ssize_t total = 0;

	if (iovcnt > SYS_IOV_MAX)
		return -EINVAL;

	for (size_t i = 0; i < iovcnt; i++) {
		uintptr_t base;
		size_t done = 0;

		if (copy_from_user(&iov, uiov + i, sizeof(iov)) != 0)
			return total ? total : -EFAULT;
		base = (uintptr_t)iov.iov_base;
		if (!access_ok((void *)base, iov.iov_len))
			return total ? total : -EFAULT;

		while (done < iov.iov_len) {
			uintptr_t chunk_base = base + done;
			size_t chunk_len = (size_t)(iov.iov_len - done);
			ssize_t ret;

			if (write)
				ret = write_user_buffer_pos(file,
							(const void *)chunk_base,
							chunk_len, NULL);
			else
				ret = read_user_buffer_pos(file, (void *)chunk_base,
						chunk_len, NULL);

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
	struct file *file __cleanup_with(file) = fd_get_writable(fd);
	ssize_t ret;

	if (!file)
		return -EBADF;

	ret = write_user_buffer_pos(file, buf, len, NULL);
	return ret;
}

ssize_t sys_read(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	char *buf = (char *)tf->a1;
	size_t len = tf->a2;
	struct file *file __cleanup_with(file) = fd_get_readable(fd);
	ssize_t ret;

	if (!file)
		return -EBADF;

	ret = read_user_buffer_pos(file, buf, len, NULL);
	return ret;
}

ssize_t sys_readv(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	const struct iovec *uiov = (const struct iovec *)tf->a1;
	size_t iovcnt = tf->a2;
	struct file *file __cleanup_with(file) = fd_get_readable(fd);
	ssize_t ret;

	if (!file)
		return -EBADF;
	if (!access_ok(uiov, iovcnt * sizeof(*uiov)))
		return -EFAULT;

	ret = rw_iovec(file, uiov, iovcnt, false);
	return ret;
}

ssize_t sys_writev(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	const struct iovec *uiov = (const struct iovec *)tf->a1;
	size_t iovcnt = tf->a2;
	struct file *file __cleanup_with(file) = fd_get_writable(fd);
	ssize_t ret;

	if (!file)
		return -EBADF;
	if (!access_ok(uiov, iovcnt * sizeof(*uiov)))
		return -EFAULT;

	ret = rw_iovec(file, uiov, iovcnt, true);
	return ret;
}

ssize_t sys_pread64(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	char *buf = (char *)tf->a1;
	size_t len = tf->a2;
	loff_t offset = (loff_t)tf->a3;
	struct file *file __cleanup_with(file) = fd_get_readable(fd);
	ssize_t ret;

	if (!file)
		return -EBADF;
	if (offset < 0)
		return -EINVAL;

	ret = read_user_buffer_pos(file, buf, len, &offset);
	return ret;
}

ssize_t sys_pwrite64(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	const char *buf = (const char *)tf->a1;
	size_t len = tf->a2;
	loff_t offset = (loff_t)tf->a3;
	struct file *file __cleanup_with(file) = fd_get_writable(fd);
	ssize_t ret;

	if (!file)
		return -EBADF;
	if (offset < 0)
		return -EINVAL;

	ret = write_user_buffer_pos(file, buf, len, &offset);
	return ret;
}

ssize_t sys_sendfile(struct trap_frame *tf)
{
	int out_fd = (int)tf->a0;
	int in_fd = (int)tf->a1;
	loff_t *uoffset = (loff_t *)tf->a2;
	size_t count = tf->a3;
	struct file *out_file __cleanup_with(file) = fd_get_writable(out_fd);
	struct file *in_file __cleanup_with(file) = fd_get_readable(in_fd);
	loff_t offset;
	ssize_t ret;

	if (!out_file || !in_file)
		return -EBADF;
	if (!in_file->f_inode ||
	    (in_file->f_inode->i_mode & S_IFMT) != S_IFREG)
		return -EINVAL;
	if (out_file->f_flags & O_APPEND)
		return -EINVAL;
	if (count == 0)
		return 0;

	if (uoffset) {
		ret = copy_user_offset(uoffset, &offset);
		if (ret < 0)
			return ret;
		ret = vfs_copy_file_buffered(out_file, in_file, &offset, NULL,
					     count);
		if (ret > 0 &&
		    copy_to_user(uoffset, &offset, sizeof(offset)) != 0)
			return -EFAULT;
		return ret;
	}

	ret = vfs_copy_file_buffered(out_file, in_file, NULL, NULL, count);
	return ret;
}

ssize_t sys_splice(struct trap_frame *tf)
{
	int fd_in = (int)tf->a0;
	loff_t *uoff_in = (loff_t *)tf->a1;
	int fd_out = (int)tf->a2;
	loff_t *uoff_out = (loff_t *)tf->a3;
	size_t len = tf->a4;
	unsigned int flags = (unsigned int)tf->a5;
	struct file *in_file __cleanup_with(file) = fd_get_readable(fd_in);
	struct file *out_file __cleanup_with(file) = fd_get_writable(fd_out);
	bool in_pipe;
	bool out_pipe;
	loff_t in_offset;
	loff_t out_offset;
	loff_t *in_offsetp = NULL;
	loff_t *out_offsetp = NULL;
	ssize_t ret;

	if (!in_file || !out_file)
		return -EBADF;
	if (flags & ~SPLICE_F_SUPPORTED_HINTS)
		return -EINVAL;
	if (len == 0)
		return 0;

	in_pipe = pipe_file(in_file);
	out_pipe = pipe_file(out_file);
	if (in_pipe == out_pipe)
		return -EINVAL;
	if (in_pipe && uoff_in)
		return -ESPIPE;
	if (out_pipe && uoff_out)
		return -ESPIPE;
	if (!in_pipe && !splice_regular_file(in_file))
		return -EINVAL;
	if (!out_pipe && !splice_regular_file(out_file))
		return -EINVAL;
	if (!out_pipe && (out_file->f_flags & O_APPEND))
		return -EINVAL;

	if (uoff_in) {
		ret = copy_user_offset(uoff_in, &in_offset);
		if (ret < 0)
			return ret;
		in_offsetp = &in_offset;
	}
	if (uoff_out) {
		ret = copy_user_offset(uoff_out, &out_offset);
		if (ret < 0)
			return ret;
		out_offsetp = &out_offset;
	}

	if (in_pipe)
		ret = pipe_splice_to_file(in_file, out_file, out_offsetp, len);
	else
		ret = vfs_copy_file_buffered(out_file, in_file, in_offsetp,
					     out_offsetp, len);
	if (ret > 0 && uoff_in &&
	    copy_to_user(uoff_in, &in_offset, sizeof(in_offset)) != 0)
		return -EFAULT;
	if (ret > 0 && uoff_out &&
	    copy_to_user(uoff_out, &out_offset, sizeof(out_offset)) != 0)
		return -EFAULT;
	return ret;
}

ssize_t sys_close(struct trap_frame *tf)
{
	return fd_close((int)tf->a0);
}

ssize_t sys_lseek(struct trap_frame *tf)
{
	struct file *file __cleanup_with(file) = fd_get((int)tf->a0);
	loff_t offset = (loff_t)tf->a1;
	int whence = (int)tf->a2;
	ssize_t ret;

	if (!file)
		return -EBADF;

	ret = vfs_llseek(file, offset, whence);
	return ret;
}

ssize_t sys_ioctl(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	uint64_t cmd = tf->a1;
	uint64_t arg = tf->a2;
	struct file *file __cleanup_with(file) = fd_get(fd);
	ssize_t ret;

	if (!file)
		return -EBADF;

	ret = vfs_ioctl(file, cmd, arg);
	return ret;
}

ssize_t sys_fcntl(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	int cmd = (int)tf->a1;
	unsigned long arg = tf->a2;
	int ret;

	switch (cmd) {
	case F_DUPFD:
		return fd_dup_from(fd, arg, 0);
	case F_GETFD:
		ret = fd_get_close_on_exec(fd);
		if (ret < 0)
			return ret;
		return ret ? FD_CLOEXEC : 0;
	case F_SETFD:
		return fd_set_close_on_exec(fd, arg & FD_CLOEXEC);
	case F_GETFL: {
		struct file *file __cleanup_with(file) = fd_get(fd);

		if (!file)
			return -EBADF;
		return file_get_status_flags(file);
	}
	case F_SETFL: {
		struct file *file __cleanup_with(file) = fd_get(fd);

		if (!file)
			return -EBADF;
		ret = file_set_status_flags(file, (uint32_t)arg);
		return ret;
	}
	case F_DUPFD_CLOEXEC:
		return fd_dup_from(fd, arg, 1);
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
	struct file *file __cleanup_with(file) = fd_get((int)tf->a0);
	ssize_t ret;

	if (!file)
		return -EBADF;

	ret = vfs_sync_file(file);
	return ret;
}

ssize_t sys_fdatasync(struct trap_frame *tf)
{
	return sys_fsync(tf);
}

ssize_t sys_ftruncate64(struct trap_frame *tf)
{
	struct file *file __cleanup_with(file) = fd_get((int)tf->a0);
	int64_t length = (int64_t)tf->a1;
	ssize_t ret;

	if (!file)
		return -EBADF;
	if (!(file->f_mode & FMODE_WRITE))
		return -EBADF;
	if (length < 0 || length > (int64_t)MAX_FILE_SIZE)
		return -EINVAL;
	if (!file->f_inode)
		return -EINVAL;

	ret = vfs_truncate_file(file, (uint64_t)length);
	return ret;
}

ssize_t sys_fallocate(struct trap_frame *tf)
{
	struct file *file __cleanup_with(file) = fd_get((int)tf->a0);
	int mode = (int)tf->a1;
	int64_t offset = (int64_t)tf->a2;
	int64_t len = (int64_t)tf->a3;
	uint64_t uoffset;
	uint64_t ulen;
	ssize_t ret;

	if (!file)
		return -EBADF;
	if (!(file->f_mode & FMODE_WRITE))
		return -EBADF;
	if (mode != 0)
		return -EINVAL;
	if (offset < 0 || len <= 0)
		return -EINVAL;

	uoffset = (uint64_t)offset;
	ulen = (uint64_t)len;
	if (uoffset > MAX_FILE_SIZE || ulen > MAX_FILE_SIZE - uoffset)
		return -EFBIG;

	ret = vfs_fallocate_file(file, mode, uoffset, ulen);
	return ret;
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
