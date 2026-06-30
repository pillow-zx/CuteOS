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
	struct path parent;
	struct dentry *dentry;
	bool new_dentry;
};

static int prepare_create_target(const struct path *base, const char *path,
				 bool require_mkdir,
				 struct create_target *target)
{
	char name[VFS_NAME_MAX + 1];
	size_t namelen;
	struct dentry *parent;
	struct dentry *dentry;
	int ret;

	target->parent.mnt = NULL;
	target->parent.dentry = NULL;
	target->dentry = NULL;
	target->new_dentry = false;

	ret = path_parent_lookupat_path(base, path, name, &namelen,
					&target->parent);
	if (ret < 0)
		return ret;
	parent = target->parent.dentry;
	if (!parent->d_inode || !parent->d_inode->i_op ||
	    (require_mkdir && !parent->d_inode->i_op->mkdir) ||
	    (!require_mkdir && !parent->d_inode->i_op->create)) {
		path_put(&target->parent);
		return -ENOTDIR;
	}

	dentry = dcache_lookup(parent, name, namelen);
	if (dentry && dentry->d_inode) {
		dput(dentry);
		path_put(&target->parent);
		return -EEXIST;
	}
	if (!dentry) {
		dentry = dentry_alloc(parent, name, namelen);
		if (!dentry) {
			path_put(&target->parent);
			return -ENOMEM;
		}
		target->new_dentry = true;
	}

	target->dentry = dentry;
	return 0;
}

static void put_create_target(struct create_target *target)
{
	if (target->dentry)
		dput(target->dentry);
	path_put(&target->parent);
}

static void rollback_create_target(struct create_target *target, bool directory)
{
	struct dentry *parent = target->parent.dentry;
	int ret;

	if (!parent || !parent->d_inode || !parent->d_inode->i_op ||
	    !target->dentry ||
	    !target->dentry->d_inode)
		return;

	if (directory) {
		if (!parent->d_inode->i_op->rmdir)
			return;
		ret = parent->d_inode->i_op->rmdir(parent->d_inode,
						   target->dentry);
	} else {
		if (!parent->d_inode->i_op->unlink)
			return;
		ret = parent->d_inode->i_op->unlink(parent->d_inode,
						    target->dentry);
	}

	if (ret == 0)
		dcache_invalidate(target->dentry);
}

int vfs_create_at_path(const struct path *base, const char *path, uint32_t mode,
		       struct path *res)
{
	struct create_target target;
	struct dentry *parent;
	int ret;

	if (res) {
		res->mnt = NULL;
		res->dentry = NULL;
	}

	ret = prepare_create_target(base, path, false, &target);
	if (ret < 0)
		return ret;

	parent = target.parent.dentry;
	ret = parent->d_inode->i_op->create(parent->d_inode, target.dentry,
					    mode);
	if (ret == 0) {
		ret = vfs_init_inode_owner(target.dentry->d_inode);
		if (ret < 0) {
			rollback_create_target(&target, false);
		} else {
			if (target.new_dentry)
				dcache_insert(target.dentry);
			if (res) {
				res->mnt = target.parent.mnt;
				res->dentry = target.dentry;
				mntget(res->mnt);
				target.dentry = NULL;
			}
		}
	}

	put_create_target(&target);
	return ret;
}

int vfs_create_at(struct dentry *base, const char *path, uint32_t mode,
		  struct dentry **res)
{
	struct path base_path = {0};
	struct path created;
	int ret;

	if (res)
		*res = NULL;
	if (base) {
		ret = vfs_path_from_dentry(base, &base_path);
		if (ret < 0)
			return ret;
	}

	ret = vfs_create_at_path(base ? &base_path : NULL, path, mode, &created);
	if (base)
		path_put(&base_path);
	if (ret < 0)
		return ret;

	if (res) {
		dget(created.dentry);
		*res = created.dentry;
	}
	path_put(&created);
	return 0;
}

int vfs_create(const char *path, uint32_t mode, struct dentry **res)
{
	return vfs_create_at(NULL, path, mode, res);
}

int vfs_symlink_at_path(const struct path *base, const char *target,
			const char *linkpath)
{
	struct create_target create;
	struct dentry *parent;
	int ret;

	if (!target || !*target)
		return -ENOENT;

	ret = prepare_create_target(base, linkpath, false, &create);
	if (ret < 0)
		return ret;
	parent = create.parent.dentry;
	if (!parent->d_inode->i_op->symlink) {
		put_create_target(&create);
		return -EINVAL;
	}

	ret = parent->d_inode->i_op->symlink(parent->d_inode, create.dentry,
					     target);
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

int vfs_symlink_at(struct dentry *base, const char *target,
		   const char *linkpath)
{
	struct path base_path = {0};
	int ret;

	if (base) {
		ret = vfs_path_from_dentry(base, &base_path);
		if (ret < 0)
			return ret;
	}

	ret = vfs_symlink_at_path(base ? &base_path : NULL, target, linkpath);
	if (base)
		path_put(&base_path);
	return ret;
}

int vfs_link_at_path(struct dentry *old_dentry, const struct path *new_base,
		     const char *new_path)
{
	struct create_target target;
	struct dentry *parent;
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
	parent = target.parent.dentry;
	if (parent->d_inode->i_sb != inode->i_sb) {
		put_create_target(&target);
		return -EXDEV;
	}
	if (!parent->d_inode->i_op->link) {
		put_create_target(&target);
		return -EINVAL;
	}

	ret = parent->d_inode->i_op->link(old_dentry, parent->d_inode,
					  target.dentry);
	if (ret == 0 && target.new_dentry)
		dcache_insert(target.dentry);

	put_create_target(&target);
	return ret;
}

int vfs_link_at(struct dentry *old_dentry, struct dentry *new_base,
		const char *new_path)
{
	struct path new_base_path = {0};
	int ret;

	if (new_base) {
		ret = vfs_path_from_dentry(new_base, &new_base_path);
		if (ret < 0)
			return ret;
	}

	ret = vfs_link_at_path(old_dentry, new_base ? &new_base_path : NULL,
			       new_path);
	if (new_base)
		path_put(&new_base_path);
	return ret;
}

int vfs_mkdir_at_path(const struct path *base, const char *path, uint32_t mode)
{
	struct create_target target;
	struct dentry *parent;
	int ret;

	ret = prepare_create_target(base, path, true, &target);
	if (ret < 0)
		return ret;

	parent = target.parent.dentry;
	ret = parent->d_inode->i_op->mkdir(parent->d_inode, target.dentry,
					   mode);
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

int vfs_mkdir_at(struct dentry *base, const char *path, uint32_t mode)
{
	struct path base_path = {0};
	int ret;

	if (base) {
		ret = vfs_path_from_dentry(base, &base_path);
		if (ret < 0)
			return ret;
	}

	ret = vfs_mkdir_at_path(base ? &base_path : NULL, path, mode);
	if (base)
		path_put(&base_path);
	return ret;
}

int vfs_mkdir(const char *path, uint32_t mode)
{
	return vfs_mkdir_at_path(NULL, path, mode);
}

int vfs_unlink_at_path(const struct path *base, const char *path, int flags)
{
	char name[VFS_NAME_MAX + 1];
	size_t namelen;
	struct path parent_path;
	struct dentry *parent;
	struct dentry *dentry;
	int ret;

	ret = path_parent_lookupat_path(base, path, name, &namelen,
					&parent_path);
	if (ret < 0)
		return ret;
	parent = parent_path.dentry;
	if (!parent->d_inode || !parent->d_inode->i_op) {
		path_put(&parent_path);
		return -ENOTDIR;
	}

	dentry = vfs_lookup_one(parent, name, namelen);
	if (!dentry) {
		path_put(&parent_path);
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
	path_put(&parent_path);
	return ret;
}

int vfs_unlink_at(struct dentry *base, const char *path, int flags)
{
	struct path base_path = {0};
	int ret;

	if (base) {
		ret = vfs_path_from_dentry(base, &base_path);
		if (ret < 0)
			return ret;
	}

	ret = vfs_unlink_at_path(base ? &base_path : NULL, path, flags);
	if (base)
		path_put(&base_path);
	return ret;
}

int vfs_unlink(const char *path, int flags)
{
	return vfs_unlink_at_path(NULL, path, flags);
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

int vfs_rename_at_path(const struct path *old_base, const char *old_path,
		       const struct path *new_base, const char *new_path,
		       unsigned int flags)
{
	char old_name[VFS_NAME_MAX + 1];
	char new_name[VFS_NAME_MAX + 1];
	size_t old_namelen, new_namelen;
	struct path old_parent_path, new_parent_path;
	struct dentry *old_parent, *new_parent;
	struct dentry *old_dentry, *new_dentry;
	int ret;

	ret = path_parent_lookupat_path(old_base, old_path, old_name,
					&old_namelen, &old_parent_path);
	if (ret < 0)
		return ret;
	old_parent = old_parent_path.dentry;
	if (!old_parent->d_inode || !old_parent->d_inode->i_op) {
		path_put(&old_parent_path);
		return -ENOTDIR;
	}

	ret = path_parent_lookupat_path(new_base, new_path, new_name,
					&new_namelen, &new_parent_path);
	if (ret < 0) {
		path_put(&old_parent_path);
		return ret;
	}
	new_parent = new_parent_path.dentry;
	if (!new_parent->d_inode || !new_parent->d_inode->i_op) {
		path_put(&old_parent_path);
		path_put(&new_parent_path);
		return -ENOTDIR;
	}

	if (!old_parent->d_inode->i_op->rename) {
		path_put(&old_parent_path);
		path_put(&new_parent_path);
		return -EINVAL;
	}

	old_dentry = vfs_lookup_one(old_parent, old_name, old_namelen);
	if (!old_dentry) {
		path_put(&old_parent_path);
		path_put(&new_parent_path);
		return -ENOENT;
	}

	new_dentry = vfs_lookup_one_any(new_parent, new_name, new_namelen);
	if (!new_dentry) {
		dput(old_dentry);
		path_put(&old_parent_path);
		path_put(&new_parent_path);
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
	path_put(&old_parent_path);
	path_put(&new_parent_path);
	return ret;
}

int vfs_rename_at(struct dentry *old_base, const char *old_path,
		  struct dentry *new_base, const char *new_path,
		  unsigned int flags)
{
	struct path old_base_path = {0};
	struct path new_base_path = {0};
	int ret;

	if (old_base) {
		ret = vfs_path_from_dentry(old_base, &old_base_path);
		if (ret < 0)
			return ret;
	}
	if (new_base) {
		ret = vfs_path_from_dentry(new_base, &new_base_path);
		if (ret < 0) {
			if (old_base)
				path_put(&old_base_path);
			return ret;
		}
	}

	ret = vfs_rename_at_path(old_base ? &old_base_path : NULL, old_path,
				 new_base ? &new_base_path : NULL, new_path,
				 flags);
	if (old_base)
		path_put(&old_base_path);
	if (new_base)
		path_put(&new_base_path);
	return ret;
}

int vfs_mknod_at_path(const struct path *base, const char *path, uint32_t mode,
		      dev_t dev)
{
	struct path created;
	uint32_t type = mode & S_IFMT;
	int ret;

	if (type == 0)
		mode |= S_IFREG;
	else if (type != S_IFREG && type != S_IFCHR && type != S_IFBLK)
		return -EINVAL;

	ret = vfs_create_at_path(base, path, mode, &created);
	if (ret < 0)
		return ret;

	if (created.dentry->d_inode) {
		created.dentry->d_inode->i_mode = mode;
		created.dentry->d_inode->i_rdev = dev;
		ret = vfs_inode_writeback(created.dentry->d_inode);
	}
	path_put(&created);
	return ret;
}

int vfs_mknod_at(struct dentry *base, const char *path, uint32_t mode,
		 dev_t dev)
{
	struct path base_path = {0};
	int ret;

	if (base) {
		ret = vfs_path_from_dentry(base, &base_path);
		if (ret < 0)
			return ret;
	}

	ret = vfs_mknod_at_path(base ? &base_path : NULL, path, mode, dev);
	if (base)
		path_put(&base_path);
	return ret;
}

int vfs_mknod(const char *path, uint32_t mode, dev_t dev)
{
	return vfs_mknod_at_path(NULL, path, mode, dev);
}
