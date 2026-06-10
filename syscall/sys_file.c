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
#include <kernel/syscall.h>
#include <drivers/uart.h>

ssize_t sys_write(size_t fd, size_t buf, size_t len, size_t _, size_t __, size_t ___)
{
	(void)_;
	(void)__;
	(void)___;

	/* 当前只支持 fd=1 (stdout) 和 fd=2 (stderr) */
	if (fd != KERN_STDOUT && fd != KERN_STDERR)
		return -1;

	const char *s = (const char *)buf;
	bool had_sum = user_access_begin();

	for (uint64_t i = 0; i < len; i++)
		uart_putc(s[i]);

	user_access_end(had_sum);
	return (long)len;
}


