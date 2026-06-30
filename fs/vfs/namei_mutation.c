/*
 * fs/vfs/namei_mutation.c - VFS 命名修改操作
 *
 * 本文件承载 create/mkdir/unlink/rename/mknod 的 VFS 层公共模式：
 * 定位父目录、准备目标 dentry、调用具体文件系统 inode_operations，并维护
 * dcache 状态。路径逐分量解析仍位于 namei.c。
 */

#include <kernel/errno.h>
#include <kernel/fs.h>
#include <kernel/stat.h>
#include <kernel/vfs.h>

#include "namei_internal.h"

struct create_target {
	struct dentry *parent;
	struct dentry *dentry;
	bool new_dentry;
};

static int prepare_create_target(struct dentry *base, const char *path,
				 bool require_mkdir,
				 struct create_target *target)
{
	char name[VFS_NAME_MAX + 1];
	size_t namelen;
	struct dentry *parent;
	struct dentry *dentry;
	int ret;

	target->parent = NULL;
	target->dentry = NULL;
	target->new_dentry = false;

	ret = path_parent_lookupat_err(base, path, name, &namelen, &parent);
	if (ret < 0)
		return ret;
	if (!parent->d_inode || !parent->d_inode->i_op ||
	    (require_mkdir && !parent->d_inode->i_op->mkdir) ||
	    (!require_mkdir && !parent->d_inode->i_op->create)) {
		dput(parent);
		return -ENOTDIR;
	}

	dentry = dcache_lookup(parent, name, namelen);
	if (dentry && dentry->d_inode) {
		dput(dentry);
		dput(parent);
		return -EEXIST;
	}
	if (!dentry) {
		dentry = dentry_alloc(parent, name, namelen);
		if (!dentry) {
			dput(parent);
			return -ENOMEM;
		}
		target->new_dentry = true;
	}

	target->parent = parent;
	target->dentry = dentry;
	return 0;
}

static void put_create_target(struct create_target *target)
{
	if (target->dentry)
		dput(target->dentry);
	if (target->parent)
		dput(target->parent);
}

static void rollback_create_target(struct create_target *target, bool directory)
{
	int ret;

	if (!target->parent || !target->parent->d_inode ||
	    !target->parent->d_inode->i_op || !target->dentry ||
	    !target->dentry->d_inode)
		return;

	if (directory) {
		if (!target->parent->d_inode->i_op->rmdir)
			return;
		ret = target->parent->d_inode->i_op->rmdir(
			target->parent->d_inode, target->dentry);
	} else {
		if (!target->parent->d_inode->i_op->unlink)
			return;
		ret = target->parent->d_inode->i_op->unlink(
			target->parent->d_inode, target->dentry);
	}

	if (ret == 0)
		dcache_invalidate(target->dentry);
}

int vfs_create_at(struct dentry *base, const char *path, uint32_t mode,
		  struct dentry **res)
{
	struct create_target target;
	int ret;

	if (res)
		*res = NULL;

	ret = prepare_create_target(base, path, false, &target);
	if (ret < 0)
		return ret;

	ret = target.parent->d_inode->i_op->create(target.parent->d_inode,
						   target.dentry, mode);
	if (ret == 0) {
		ret = vfs_init_inode_owner(target.dentry->d_inode);
		if (ret < 0) {
			rollback_create_target(&target, false);
		} else {
			if (target.new_dentry)
				dcache_insert(target.dentry);
			if (res) {
				*res = target.dentry;
				target.dentry = NULL;
			}
		}
	}

	put_create_target(&target);
	return ret;
}

int vfs_create(const char *path, uint32_t mode, struct dentry **res)
{
	return vfs_create_at(NULL, path, mode, res);
}

int vfs_symlink_at(struct dentry *base, const char *target,
		   const char *linkpath)
{
	struct create_target create;
	int ret;

	if (!target || !*target)
		return -ENOENT;

	ret = prepare_create_target(base, linkpath, false, &create);
	if (ret < 0)
		return ret;
	if (!create.parent->d_inode->i_op->symlink) {
		put_create_target(&create);
		return -EINVAL;
	}

	ret = create.parent->d_inode->i_op->symlink(create.parent->d_inode,
						    create.dentry, target);
	if (ret == 0) {
		ret = vfs_init_inode_owner(create.dentry->d_inode);
		if (ret < 0) {
			rollback_create_target(&create, false);
		} else if (create.new_dentry) {
			dcache_insert(create.dentry);
		}
	}

	put_create_target(&create);
	return ret;
}

int vfs_link_at(struct dentry *old_dentry, struct dentry *new_base,
		const char *new_path)
{
	struct create_target target;
	struct inode *inode;
	int ret;

	if (!old_dentry || !old_dentry->d_inode)
		return -ENOENT;

	inode = old_dentry->d_inode;
	if (S_ISDIR(inode->i_mode))
		return -EPERM;

	ret = prepare_create_target(new_base, new_path, false, &target);
	if (ret < 0)
		return ret;
	if (target.parent->d_inode->i_sb != inode->i_sb) {
		put_create_target(&target);
		return -EXDEV;
	}
	if (!target.parent->d_inode->i_op->link) {
		put_create_target(&target);
		return -EINVAL;
	}

	ret = target.parent->d_inode->i_op->link(old_dentry,
						 target.parent->d_inode,
						 target.dentry);
	if (ret == 0 && target.new_dentry)
		dcache_insert(target.dentry);

	put_create_target(&target);
	return ret;
}

int vfs_mkdir_at(struct dentry *base, const char *path, uint32_t mode)
{
	struct create_target target;
	int ret;

	ret = prepare_create_target(base, path, true, &target);
	if (ret < 0)
		return ret;

	ret = target.parent->d_inode->i_op->mkdir(target.parent->d_inode,
						  target.dentry, mode);
	if (ret == 0) {
		ret = vfs_init_inode_owner(target.dentry->d_inode);
		if (ret < 0) {
			rollback_create_target(&target, true);
		} else if (target.new_dentry) {
			dcache_insert(target.dentry);
		}
	}

	put_create_target(&target);
	return ret;
}

int vfs_mkdir(const char *path, uint32_t mode)
{
	return vfs_mkdir_at(NULL, path, mode);
}

int vfs_unlink_at(struct dentry *base, const char *path, int flags)
{
	char name[VFS_NAME_MAX + 1];
	size_t namelen;
	struct dentry *parent;
	struct dentry *dentry;
	int ret;

	ret = path_parent_lookupat_err(base, path, name, &namelen, &parent);
	if (ret < 0)
		return ret;
	if (!parent->d_inode || !parent->d_inode->i_op) {
		dput(parent);
		return -ENOTDIR;
	}

	dentry = vfs_lookup_one(parent, name, namelen);
	if (!dentry) {
		dput(parent);
		return -ENOENT;
	}

	if (flags & AT_REMOVEDIR) {
		if (!parent->d_inode->i_op->rmdir)
			ret = -ENOTDIR;
		else
			ret = parent->d_inode->i_op->rmdir(parent->d_inode,
							   dentry);
	} else {
		if (!parent->d_inode->i_op->unlink)
			ret = -EINVAL;
		else
			ret = parent->d_inode->i_op->unlink(parent->d_inode,
							    dentry);
	}

	dput(dentry);
	dput(parent);
	return ret;
}

int vfs_unlink(const char *path, int flags)
{
	return vfs_unlink_at(NULL, path, flags);
}

static bool dentry_is_ancestor(struct dentry *ancestor, struct dentry *dentry)
{
	while (dentry) {
		if (dentry == ancestor)
			return true;
		if (dentry->d_parent == dentry)
			break;
		dentry = dentry->d_parent;
	}

	return false;
}

int vfs_rename_at(struct dentry *old_base, const char *old_path,
		  struct dentry *new_base, const char *new_path,
		  unsigned int flags)
{
	char old_name[VFS_NAME_MAX + 1];
	char new_name[VFS_NAME_MAX + 1];
	size_t old_namelen, new_namelen;
	struct dentry *old_parent, *new_parent;
	struct dentry *old_dentry, *new_dentry;
	int ret;

	ret = path_parent_lookupat_err(old_base, old_path, old_name,
				       &old_namelen, &old_parent);
	if (ret < 0)
		return ret;
	if (!old_parent->d_inode || !old_parent->d_inode->i_op) {
		dput(old_parent);
		return -ENOTDIR;
	}

	ret = path_parent_lookupat_err(new_base, new_path, new_name,
				       &new_namelen, &new_parent);
	if (ret < 0) {
		dput(old_parent);
		return ret;
	}
	if (!new_parent->d_inode || !new_parent->d_inode->i_op) {
		dput(old_parent);
		dput(new_parent);
		return -ENOTDIR;
	}

	if (!old_parent->d_inode->i_op->rename) {
		dput(old_parent);
		dput(new_parent);
		return -EINVAL;
	}

	old_dentry = vfs_lookup_one(old_parent, old_name, old_namelen);
	if (!old_dentry) {
		dput(old_parent);
		dput(new_parent);
		return -ENOENT;
	}

	new_dentry = vfs_lookup_one_any(new_parent, new_name, new_namelen);
	if (!new_dentry) {
		dput(old_dentry);
		dput(old_parent);
		dput(new_parent);
		return -ENOMEM;
	}

	if (old_dentry == new_dentry) {
		ret = (flags & RENAME_NOREPLACE) ? -EEXIST : 0;
		goto out_dput;
	}

	if (old_dentry->d_inode && S_ISDIR(old_dentry->d_inode->i_mode) &&
	    dentry_is_ancestor(old_dentry, new_parent)) {
		ret = -EINVAL;
		goto out_dput;
	}

	ret = old_parent->d_inode->i_op->rename(old_parent->d_inode, old_dentry,
						new_parent->d_inode, new_dentry,
						flags);
	if (ret == 0) {
		dcache_invalidate(new_dentry);
		dcache_move(old_dentry, new_parent, new_name, new_namelen);
	}

out_dput:
	dput(new_dentry);
	dput(old_dentry);
	dput(old_parent);
	dput(new_parent);
	return ret;
}

int vfs_mknod_at(struct dentry *base, const char *path, uint32_t mode,
		 dev_t dev)
{
	struct dentry *dentry;
	uint32_t type = mode & S_IFMT;
	int ret;

	if (type == 0)
		mode |= S_IFREG;
	else if (type != S_IFREG && type != S_IFCHR && type != S_IFBLK)
		return -EINVAL;

	ret = vfs_create_at(base, path, mode, &dentry);
	if (ret < 0)
		return ret;

	if (dentry->d_inode) {
		dentry->d_inode->i_mode = mode;
		dentry->d_inode->i_rdev = dev;
		ret = vfs_inode_writeback(dentry->d_inode);
	}
	dput(dentry);
	return ret;
}

int vfs_mknod(const char *path, uint32_t mode, dev_t dev)
{
	return vfs_mknod_at(NULL, path, mode, dev);
}
