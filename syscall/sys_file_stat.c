/*
 * syscall/sys_file_stat.c - stat/statfs 元数据查询系统调用
 */

#include <kernel/fdtable.h>
#include <kernel/blkdev.h>
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
#include <kernel/task.h>
#include <kernel/vfs.h>
#include <kernel/page.h>
#include <kernel/trap.h>

#include "sys_file_internal.h"

ssize_t sys_fstat(struct trap_frame *tf)
{
	int fd = (int)syscall_arg(tf, 0);
	struct stat *ustat = (struct stat *)syscall_arg(tf, 1);
	struct file *file __cleanup_with(file) = fd_get(fd);
	struct stat st;
	int ret;

	if (!file)
		return -EBADF;
	if (!access_ok(ustat, sizeof(*ustat)))
		return -EFAULT;

	ret = vfs_stat_file(file, &st);
	if (ret == 0)
		ret = copy_to_user(ustat, &st, sizeof(st)) != 0 ? -EFAULT : 0;

	return ret;
}

ssize_t sys_newfstatat(struct trap_frame *tf)
{
	int dfd = (int)syscall_arg(tf, 0);
	const char *upath = (const char *)syscall_arg(tf, 1);
	struct stat *ustat = (struct stat *)syscall_arg(tf, 2);
	int flags = (int)syscall_arg(tf, 3);
	struct sys_at_lookup_result lookup __cleanup_with(sys_at_lookup) = {};
	struct stat st;
	int ret;

	if (!ustat || !access_ok(ustat, sizeof(*ustat)))
		return -EFAULT;
	if (flags & ~(AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW))
		return -EINVAL;

	ret = sys_at_lookup(&lookup, dfd, upath, flags,
			    (flags & AT_SYMLINK_NOFOLLOW) ? LOOKUP_NOFOLLOW : 0,
			    true);
	if (ret < 0)
		return ret;

	ret = vfs_stat_inode(lookup.inode, &st);
	if (ret < 0)
		return ret;
	if (copy_to_user(ustat, &st, sizeof(st)) != 0)
		return -EFAULT;

	return 0;
}

static void statx_timestamp_from_stat(struct statx_timestamp *dst,
				      int64_t sec, uint64_t nsec)
{
	dst->tv_sec = sec;
	dst->tv_nsec = (uint32_t)nsec;
	dst->__reserved = 0;
}

static void statx_from_stat(const struct stat *st, struct statx *stx)
{
	memset(stx, 0, sizeof(*stx));
	stx->stx_mask = STATX_BASIC_STATS;
	stx->stx_blksize = st->st_blksize;
	stx->stx_nlink = st->st_nlink;
	stx->stx_uid = st->st_uid;
	stx->stx_gid = st->st_gid;
	stx->stx_mode = (uint16_t)st->st_mode;
	stx->stx_ino = st->st_ino;
	stx->stx_size = (uint64_t)st->st_size;
	stx->stx_blocks = st->st_blocks;
	statx_timestamp_from_stat(&stx->stx_atime, st->st_atime_sec,
				  st->st_atime_nsec);
	statx_timestamp_from_stat(&stx->stx_mtime, st->st_mtime_sec,
				  st->st_mtime_nsec);
	statx_timestamp_from_stat(&stx->stx_ctime, st->st_ctime_sec,
				  st->st_ctime_nsec);
	stx->stx_rdev_major = MAJOR(st->st_rdev);
	stx->stx_rdev_minor = (uint32_t)(st->st_rdev & ((1u << MINORBITS) - 1));
	stx->stx_dev_major = MAJOR(st->st_dev);
	stx->stx_dev_minor = (uint32_t)(st->st_dev & ((1u << MINORBITS) - 1));
}

/*
 * SYSCALL_SUPPORT(B): statx
 * Current: reports STATX_BASIC_STATS converted from struct stat.
 * Unsupported errno: reserved mask bits and unsupported AT_STATX flag
 * combinations return -EINVAL.
 * Future: document the supported mask and only report fields with real data.
 */
ssize_t sys_statx(struct trap_frame *tf)
{
	int dfd = (int)syscall_arg(tf, 0);
	const char *upath = (const char *)syscall_arg(tf, 1);
	int flags = (int)syscall_arg(tf, 2);
	uint32_t mask = (uint32_t)syscall_arg(tf, 3);
	struct statx *ustatx = (struct statx *)syscall_arg(tf, 4);
	struct sys_at_lookup_result lookup __cleanup_with(sys_at_lookup) = {};
	struct stat st;
	struct statx stx;
	int ret;

	if (!ustatx || !access_ok(ustatx, sizeof(*ustatx)))
		return -EFAULT;
	if (mask & STATX__RESERVED)
		return -EINVAL;
	if (flags & ~(AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW |
		      AT_STATX_SYNC_TYPE))
		return -EINVAL;
	if ((flags & AT_STATX_SYNC_TYPE) != AT_STATX_FORCE_SYNC &&
	    (flags & AT_STATX_SYNC_TYPE) != AT_STATX_DONT_SYNC &&
	    (flags & AT_STATX_SYNC_TYPE) != 0)
		return -EINVAL;

	ret = sys_at_lookup(&lookup, dfd, upath, flags,
			    (flags & AT_SYMLINK_NOFOLLOW) ? LOOKUP_NOFOLLOW : 0,
			    true);
	if (ret < 0)
		return ret;

	ret = vfs_stat_inode(lookup.inode, &st);
	if (ret < 0)
		return ret;

	statx_from_stat(&st, &stx);
	if (copy_to_user(ustatx, &stx, sizeof(stx)) != 0)
		return -EFAULT;

	return 0;
}

/*
 * SYSCALL_SUPPORT(B): statfs64
 * Current: reports the mounted superblock statfs data for a resolved path.
 * Unsupported errno: missing mount/superblock returns -EINVAL; detailed field
 * completeness depends on the filesystem.
 * Future: complete and document ext2 statfs fields.
 */
ssize_t sys_statfs64(struct trap_frame *tf)
{
	const char *upath = (const char *)syscall_arg(tf, 0);
	struct statfs64 *ubuf = (struct statfs64 *)syscall_arg(tf, 1);
	char *path __cleanup_with(page0) = NULL;
	struct path found __cleanup_with(path) = {};
	struct statfs64 st;
	int ret;

	if (!ubuf || !access_ok(ubuf, sizeof(*ubuf)))
		return -EFAULT;

	ret = copy_user_path(&path, upath);
	if (ret < 0)
		return ret;

	ret = path_lookupat_path(NULL, path, 0, &found);
	if (ret < 0)
		return ret;
	if (!found.mnt || !found.mnt->mnt_sb)
		return -EINVAL;

	ret = vfs_statfs(found.mnt->mnt_sb, &st);
	if (ret < 0)
		return ret;
	if (copy_to_user(ubuf, &st, sizeof(st)) != 0)
		return -EFAULT;

	return 0;
}

/*
 * SYSCALL_SUPPORT(B): fstatfs64
 * Current: reports the mounted superblock statfs data for an open file.
 * Unsupported errno: bad fd returns -EBADF; missing mount/superblock returns
 * -EINVAL; field completeness depends on the filesystem.
 * Future: track statfs64 when ext2 field coverage is deepened.
 */
ssize_t sys_fstatfs64(struct trap_frame *tf)
{
	int fd = (int)syscall_arg(tf, 0);
	struct statfs64 *ubuf = (struct statfs64 *)syscall_arg(tf, 1);
	struct file *file __cleanup_with(file) = NULL;
	struct statfs64 st;
	int ret;

	if (!ubuf || !access_ok(ubuf, sizeof(*ubuf)))
		return -EFAULT;

	file = fd_get(fd);
	if (!file)
		return -EBADF;
	if (!file->f_path.mnt || !file->f_path.mnt->mnt_sb)
		return -EINVAL;

	ret = vfs_statfs(file->f_path.mnt->mnt_sb, &st);
	if (ret < 0)
		return ret;
	if (copy_to_user(ubuf, &st, sizeof(st)) != 0)
		return -EFAULT;

	return 0;
}
