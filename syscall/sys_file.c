/*
 * syscall/sys_file.c - 文件相关系统调用
 *
 * 功能：
 *   实现文件 I/O 和文件系统操作的系统调用。这些是应用层使用最频繁的
 *   syscall 类别之一，涵盖文件打开/关闭/读写、目录操作、文件描述符
 *   管理等功能。
 *
 * 主要函数：
 *   sys_openat(dfd, path, flags, mode) - 打开文件（仅支持 AT_FDCWD）
 *   sys_close(fd)                      - 关闭文件描述符
 *   sys_read(fd, buf, count)           - 从文件描述符读取数据
 *   sys_write(fd, buf, count)          - 向文件描述符写入数据
 *   sys_lseek(fd, offset, whence)      - 定位读写位置（SET/CUR/END）
 *   sys_ioctl(fd, cmd, arg)            - 设备控制（shell 特殊：直接返回 0）
 *   sys_mkdirat(dfd, path, mode)       - 创建目录
 *   sys_unlinkat(dfd, path, flags)     - 删除文件/目录
 *   sys_chdir(path)                    - 切换当前工作目录
 *   sys_getcwd(buf, size)              - 获取当前工作目录（沿 d_parent 回溯）
 *   sys_getdents64(fd, dirp, count)    - 读取目录条目（使用 filldir 回调）
 *   sys_fstat(fd, statbuf)             - 获取文件状态
 *   sys_dup(oldfd)                     - 复制文件描述符
 *   sys_dup2(oldfd, newfd)             - 复制到指定文件描述符
 *   sys_mknod(path, mode, dev)         - 创建设备节点
 */

#include <kernel/fs.h>
#include <kernel/types.h>
#include <kernel/errno.h>
#include <kernel/syscall.h>
#include <kernel/mm.h>
#include <asm/trap.h>

/* 内核临时缓冲区大小，sys_write 分块拷贝使用 */
#define WRITE_BUF_SIZE 256

ssize_t sys_write(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	const char *buf = (const char *)tf->a1;
	size_t len = tf->a2;

	struct file *file = fd_get(fd);
	if (!file || !(file->f_mode & FMODE_WRITE) || !file->f_op ||
	    !file->f_op->write)
		return -EBADF;

	/* 校验用户地址范围 */
	if (!access_ok(buf, len))
		return -EFAULT;

	char kbuf[WRITE_BUF_SIZE];
	size_t written = 0;

	while (written < len) {
		size_t chunk = len - written;
		if (chunk > WRITE_BUF_SIZE)
			chunk = WRITE_BUF_SIZE;

		if (copy_from_user(kbuf, buf + written, chunk) != 0)
			return -EFAULT;

		ssize_t ret = file->f_op->write(file, kbuf, chunk);
		if (ret < 0)
			return ret;
		if ((size_t)ret != chunk)
			return (ssize_t)(written + (size_t)ret);

		written += chunk;
	}

	return (ssize_t)written;
}

ssize_t sys_read(struct trap_frame *tf)
{
	int fd = (int)tf->a0;
	char *buf = (char *)tf->a1;
	size_t len = tf->a2;

	struct file *file = fd_get(fd);
	if (!file || !(file->f_mode & FMODE_READ) || !file->f_op ||
	    !file->f_op->read)
		return -EBADF;

	if (!access_ok(buf, len))
		return -EFAULT;

	char kbuf[WRITE_BUF_SIZE];
	size_t done = 0;

	while (done < len) {
		size_t chunk = len - done;
		if (chunk > WRITE_BUF_SIZE)
			chunk = WRITE_BUF_SIZE;

		ssize_t ret = file->f_op->read(file, kbuf, chunk);
		if (ret < 0)
			return ret;
		if (ret == 0)
			break;

		if (copy_to_user(buf + done, kbuf, (size_t)ret) != 0)
			return -EFAULT;

		done += (size_t)ret;
		if ((size_t)ret < chunk)
			break;
	}

	return (ssize_t)done;
}

ssize_t sys_close(struct trap_frame *tf)
{
	return fd_close((int)tf->a0);
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

	if (flags != 0)
		return -EINVAL;

	return fd_dup2(oldfd, newfd);
}
