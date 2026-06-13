/*
 * fs/vfs/namei.c - 路径解析
 *
 * 功能：
 *   实现 VFS 层的路径名解析（Pathname Lookup）。path_lookup 逐分量
 *   解析路径。绝对路径从 root_dentry 开始，相对路径从 current->cwd
 *   开始。每个分量处理 "."（跳过）和 ".."（回溯到 d_parent）。不
 *   支持符号链接跟随。每个分量最终调用 i_op->lookup 在磁盘上查找。
 *
 * 主要函数：
 *   path_lookup(path, flags) - 主路径解析函数。绝对路径从 root_dentry
 *                              开始，相对路径从 current->cwd 开始
 *   follow_dotdot(dentry)    - 处理 ".." 分量，通过 d_parent 回溯
 *   lookup_one(parent, name) - 单级分量解析，调用 i_op->lookup
 */

#include <kernel/fs.h>
#include <kernel/errno.h>
#include <kernel/string.h>
#include <kernel/task.h>
#include <kernel/vfs.h>

struct dentry *root_dentry;

static bool is_dot(const char *name, size_t len)
{
	return len == 1 && name[0] == '.';
}

static bool is_dotdot(const char *name, size_t len)
{
	return len == 2 && name[0] == '.' && name[1] == '.';
}

static const char *skip_slashes(const char *path)
{
	while (*path == '/')
		path++;
	return path;
}

static struct dentry *follow_dotdot(struct dentry *dentry)
{
	struct dentry *parent;

	if (!dentry)
		return NULL;

	parent = dentry->d_parent ? dentry->d_parent : dentry;
	dget(parent);
	return parent;
}

static struct dentry *lookup_one(struct dentry *parent, const char *name,
				 size_t len)
{
	struct dentry *dentry;
	struct dentry *found;

	dentry = dcache_lookup(parent, name, len);
	if (dentry) {
		if (dentry->d_inode)
			return dentry;

		dput(dentry);
		return NULL;
	}

	if (!parent || !parent->d_inode || !parent->d_inode->i_op ||
	    !parent->d_inode->i_op->lookup)
		return NULL;

	dentry = dentry_alloc(parent, name, len);
	if (!dentry)
		return NULL;

	found = parent->d_inode->i_op->lookup(parent->d_inode, dentry);
	if (!found || !found->d_inode) {
		dcache_insert(dentry);
		dput(dentry);
		return NULL;
	}

	if (found == dentry)
		dcache_insert(dentry);
	else
		dput(dentry);

	return found;
}

struct dentry *path_lookup(const char *path, uint32_t flags)
{
	struct dentry *dentry;

	(void)flags;

	if (!path || !*path)
		return NULL;

	if (*path == '/')
		dentry = root_dentry;
	else
		dentry = current ? current->cwd : NULL;

	if (!dentry)
		return NULL;

	dget(dentry);
	path = skip_slashes(path);

	while (*path) {
		const char *name = path;
		size_t len;

		while (*path && *path != '/')
			path++;

		len = (size_t)(path - name);
		path = skip_slashes(path);

		if (len == 0 || is_dot(name, len))
			continue;

		if (is_dotdot(name, len)) {
			struct dentry *parent = follow_dotdot(dentry);
			dput(dentry);
			dentry = parent;
			if (!dentry)
				return NULL;
			continue;
		}

		struct dentry *next = lookup_one(dentry, name, len);
		dput(dentry);
		if (!next)
			return NULL;

		dentry = next;
	}

	return dentry;
}

struct dentry *path_parent_lookup(const char *path, char *name,
				  size_t *namelen)
{
	struct dentry *parent;
	const char *last;
	size_t last_len;
	bool absolute;

	if (!path || !*path || !name || !namelen)
		return NULL;

	absolute = path[0] == '/';
	while (*path == '/')
		path++;
	if (!*path)
		return NULL;

	if (absolute)
		parent = root_dentry;
	else
		parent = current ? current->cwd : NULL;
	if (!parent)
		return NULL;

	dget(parent);
	last = NULL;
	last_len = 0;

	while (*path) {
		const char *component = path;
		size_t len;

		while (*path && *path != '/')
			path++;
		len = (size_t)(path - component);
		path = skip_slashes(path);

		if (len == 0 || is_dot(component, len))
			continue;

		if (!*path) {
			last = component;
			last_len = len;
			break;
		}

		if (is_dotdot(component, len)) {
			struct dentry *next = follow_dotdot(parent);
			dput(parent);
			parent = next;
			if (!parent)
				return NULL;
			continue;
		}

		struct dentry *next = lookup_one(parent, component, len);
		dput(parent);
		if (!next)
			return NULL;
		parent = next;
	}

	if (!last || last_len == 0 || last_len > VFS_NAME_MAX ||
	    is_dot(last, last_len) || is_dotdot(last, last_len)) {
		dput(parent);
		return NULL;
	}

	memcpy(name, last, last_len);
	name[last_len] = '\0';
	*namelen = last_len;
	return parent;
}

int vfs_create(const char *path, uint32_t mode, struct dentry **res)
{
	char name[VFS_NAME_MAX + 1];
	size_t namelen;
	struct dentry *parent;
	struct dentry *dentry;
	bool new_dentry = false;
	int ret;

	if (res)
		*res = NULL;

	parent = path_parent_lookup(path, name, &namelen);
	if (!parent)
		return -ENOENT;
	if (!parent->d_inode || !parent->d_inode->i_op ||
	    !parent->d_inode->i_op->create) {
		dput(parent);
		return -ENOTDIR;
	}

	dentry = dcache_lookup(parent, name, namelen);
	if (dentry && dentry->d_inode) {
		dput(parent);
		if (res)
			*res = dentry;
		else
			dput(dentry);
		return -EEXIST;
	}
	if (!dentry) {
		dentry = dentry_alloc(parent, name, namelen);
		if (!dentry) {
			dput(parent);
			return -ENOMEM;
		}
		new_dentry = true;
	}

	ret = parent->d_inode->i_op->create(parent->d_inode, dentry, mode);
	if (ret == 0) {
		if (new_dentry)
			dcache_insert(dentry);
		if (res)
			*res = dentry;
		else
			dput(dentry);
	} else {
		dput(dentry);
	}

	dput(parent);
	return ret;
}

int vfs_mkdir(const char *path, uint32_t mode)
{
	char name[VFS_NAME_MAX + 1];
	size_t namelen;
	struct dentry *parent;
	struct dentry *dentry;
	bool new_dentry = false;
	int ret;

	parent = path_parent_lookup(path, name, &namelen);
	if (!parent)
		return -ENOENT;
	if (!parent->d_inode || !parent->d_inode->i_op ||
	    !parent->d_inode->i_op->mkdir) {
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
		new_dentry = true;
	}

	ret = parent->d_inode->i_op->mkdir(parent->d_inode, dentry, mode);
	if (ret == 0 && new_dentry)
		dcache_insert(dentry);
	dput(dentry);
	dput(parent);
	return ret;
}

int vfs_unlink(const char *path, int flags)
{
	char name[VFS_NAME_MAX + 1];
	size_t namelen;
	struct dentry *parent;
	struct dentry *dentry;
	int ret;

	parent = path_parent_lookup(path, name, &namelen);
	if (!parent)
		return -ENOENT;
	if (!parent->d_inode || !parent->d_inode->i_op) {
		dput(parent);
		return -ENOTDIR;
	}

	dentry = lookup_one(parent, name, namelen);
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

int vfs_mknod(const char *path, uint32_t mode, dev_t dev)
{
	struct dentry *dentry;
	int ret;

	ret = vfs_create(path, mode, &dentry);
	if (ret < 0)
		return ret;

	if (dentry->d_inode) {
		dentry->d_inode->i_mode = mode;
		dentry->d_inode->i_rdev = dev;
		if (dentry->d_inode->i_sb && dentry->d_inode->i_sb->s_op &&
		    dentry->d_inode->i_sb->s_op->write_inode)
			dentry->d_inode->i_sb->s_op->write_inode(
				dentry->d_inode);
	}
	dput(dentry);
	return 0;
}

void vfs_set_root_dentry(struct dentry *dentry)
{
	if (root_dentry)
		dput(root_dentry);

	root_dentry = dentry;
	if (!root_dentry)
		return;

	root_dentry->d_parent = root_dentry;
	if (!root_dentry->d_sb && root_dentry->d_inode)
		root_dentry->d_sb = root_dentry->d_inode->i_sb;
	dget(root_dentry);

	if (current && !current->cwd) {
		current->cwd = root_dentry;
		dget(current->cwd);
	}
}
