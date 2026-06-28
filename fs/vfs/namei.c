/*
 * fs/vfs/namei.c - 路径解析
 *
 * 功能：
 *   实现 VFS 层的路径名解析（Pathname Lookup）。path_lookup 逐分量
 *   解析路径。绝对路径从当前 fs_struct 的 root 开始，相对路径从
 *   fs_struct 的 cwd 开始。每个分量处理 "."（跳过）和 ".."（回溯到
 *   d_parent）。遇到
 *   符号链接时读取其目标并继续解析；中间分量总是跟随符号链接，末端
 *   分量在未设置 LOOKUP_NOFOLLOW 时跟随。跟随深度受 MAXSYMLINKS 限制
 *   以防环路。每个分量最终调用 i_op->lookup 在磁盘上查找。
 *
 * 主要函数：
 *   path_lookup(path, flags) - 主路径解析函数。绝对路径从 fs root 开始，
 *                              相对路径从 fs cwd 开始
 *   walk_path(base, path, …) - 从 base 起逐分量解析并跟随符号链接
 *   follow_symlink(dir, link, …) - 读取符号链接目标并解析出目标 dentry
 *   follow_dotdot(dentry)    - 处理 ".." 分量，通过 d_parent 回溯
 *   vfs_lookup_one(parent, name) - 单级分量解析，调用 i_op->lookup
 */

#include <kernel/fs.h>
#include <kernel/errno.h>
#include <kernel/buddy.h>
#include <kernel/fs_struct.h>
#include <kernel/stat.h>
#include <kernel/string.h>
#include <kernel/task.h>
#include <kernel/vfs.h>
#include <asm/page.h>

#include "namei_internal.h"

struct dentry *root_dentry;

/* 单次路径解析中允许跟随的符号链接最大深度，防止符号链接环路。 */
#define MAXSYMLINKS 8

struct namei_context {
	int symlink_depth;
};

static_assert(VFS_PATH_MAX <= PAGE_SIZE,
	      "VFS path buffers are allocated as one page");

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

int vfs_getcwd_path(struct dentry *cwd, char *buf, size_t size)
{
	struct dentry *stack[32];
	struct dentry *dentry = cwd;
	size_t depth = 0;
	size_t pos = 0;

	if (!dentry || !root_dentry)
		return -ENOENT;
	while (dentry && dentry != root_dentry) {
		if (depth >= 32)
			return -ENAMETOOLONG;
		stack[depth++] = dentry;
		dentry = vfs_dentry_parent(dentry);
	}
	if (size < 2)
		return -ERANGE;

	buf[pos++] = '/';
	for (size_t i = depth; i > 0; i--) {
		struct dentry *entry = stack[i - 1];
		size_t namelen = vfs_dentry_namelen(entry);

		if (pos != 1)
			buf[pos++] = '/';
		if (pos + namelen + 1 > size)
			return -ERANGE;
		memcpy(buf + pos, vfs_dentry_name(entry), namelen);
		pos += namelen;
	}

	buf[pos] = '\0';
	return (int)pos + 1;
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

struct dentry *vfs_lookup_one(struct dentry *parent, const char *name,
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

struct dentry *vfs_lookup_one_any(struct dentry *parent, const char *name,
				  size_t len)
{
	struct dentry *dentry;
	struct dentry *found;

	dentry = dcache_lookup(parent, name, len);
	if (dentry)
		return dentry;

	if (!parent || !parent->d_inode || !parent->d_inode->i_op ||
	    !parent->d_inode->i_op->lookup)
		return NULL;

	dentry = dentry_alloc(parent, name, len);
	if (!dentry)
		return NULL;

	found = parent->d_inode->i_op->lookup(parent->d_inode, dentry);
	if (!found || !found->d_inode) {
		dcache_insert(dentry);
		return dentry;
	}

	if (found == dentry)
		dcache_insert(dentry);
	else
		dput(dentry);

	return found;
}

static bool d_is_symlink(struct dentry *dentry)
{
	return dentry && dentry->d_inode && S_ISLNK(dentry->d_inode->i_mode);
}

int vfs_readlink(struct dentry *dentry, char *buf, size_t size)
{
	struct inode *inode;

	if (!dentry || !dentry->d_inode || !buf || size == 0)
		return -EINVAL;

	inode = dentry->d_inode;
	if (!S_ISLNK(inode->i_mode) || !inode->i_op || !inode->i_op->readlink)
		return -EINVAL;

	return inode->i_op->readlink(inode, buf, size);
}

static int walk_path(struct dentry *base, const char *path, uint32_t flags,
		     struct namei_context *ctx, struct dentry **res);

/*
 * 跟随符号链接：读取 link 指向的目标路径，并从合适的起点解析出目标
 * dentry。dir 是 link 所在的目录（用于解析相对目标），调用者保留其
 * 引用；link 的引用由本函数释放。成功时通过 res 返回目标 dentry（已
 * dget），失败返回负错误码。
 */
static int follow_symlink(struct dentry *dir, struct dentry *link,
			  uint32_t flags, struct namei_context *ctx,
			  struct dentry **res)
{
	char *target;
	struct dentry *base;
	int len;
	int ret;

	if (ctx->symlink_depth >= MAXSYMLINKS) {
		dput(link);
		return -ELOOP;
	}
	ctx->symlink_depth++;

	target = get_free_page(0);
	if (!target) {
		dput(link);
		return -ENOMEM;
	}

	len = vfs_readlink(link, target, VFS_PATH_MAX - 1);
	dput(link);
	if (len < 0) {
		free_page(target, 0);
		return len;
	}
	if (len == 0 || len >= VFS_PATH_MAX - 1) {
		free_page(target, 0);
		return len == 0 ? -ENOENT : -ENAMETOOLONG;
	}
	target[len] = '\0';

	if (target[0] == '/')
		base = fs_get_root_dentry(task_fs(current));
	else {
		base = dir;
		dget(base);
	}
	if (!base) {
		free_page(target, 0);
		return -ENOENT;
	}
	ret = walk_path(base, target, flags, ctx, res);
	free_page(target, 0);
	return ret;
}

/*
 * 从 base 起逐分量解析 path。消费 base 的引用：成功时返回最终 dentry
 * （持有引用），失败时释放 base 并返回负错误码。中间分量总是跟随
 * 符号链接；末端分量在未设置 LOOKUP_NOFOLLOW 时也跟随。
 */
static int walk_path(struct dentry *base, const char *path, uint32_t flags,
		     struct namei_context *ctx, struct dentry **res)
{
	struct dentry *dentry = base;

	*res = NULL;
	path = skip_slashes(path);

	while (*path) {
		const char *name = path;
		size_t len;
		bool is_last;
		struct dentry *next;

		while (*path && *path != '/')
			path++;

		len = (size_t)(path - name);
		path = skip_slashes(path);
		is_last = *path == '\0';

		if (len == 0 || is_dot(name, len))
			continue;
		if (len > VFS_NAME_MAX) {
			dput(dentry);
			return -ENAMETOOLONG;
		}

		if (is_dotdot(name, len)) {
			struct dentry *parent = follow_dotdot(dentry);
			dput(dentry);
			dentry = parent;
			if (!dentry)
				return -ENOENT;
			continue;
		}

		next = vfs_lookup_one(dentry, name, len);
		if (!next) {
			dput(dentry);
			return -ENOENT;
		}

		if (d_is_symlink(next) &&
		    !(is_last && (flags & LOOKUP_NOFOLLOW))) {
			struct dentry *target;
			int ret = follow_symlink(dentry, next, flags, ctx,
						 &target);

			dput(dentry);
			if (ret < 0)
				return ret;
			dentry = target;
			continue;
		}

		dput(dentry);
		dentry = next;
	}

	*res = dentry;
	return 0;
}

int path_lookupat_err(struct dentry *base, const char *path, uint32_t flags,
		      struct dentry **res)
{
	struct namei_context ctx = { 0 };
	struct dentry *start;

	if (res)
		*res = NULL;
	if (!res)
		return -EINVAL;
	if (!path || !*path)
		return -ENOENT;

	if (*path == '/')
		start = fs_get_root_dentry(task_fs(current));
	else if (base) {
		start = base;
		dget(start);
	} else
		start = fs_get_cwd_dentry(task_fs(current));

	if (!start)
		return -ENOENT;

	return walk_path(start, path, flags, &ctx, res);
}

int path_lookup_err(const char *path, uint32_t flags, struct dentry **res)
{
	return path_lookupat_err(NULL, path, flags, res);
}

struct dentry *path_lookup(const char *path, uint32_t flags)
{
	struct dentry *dentry;

	if (path_lookup_err(path, flags, &dentry) < 0)
		return NULL;
	return dentry;
}

int path_parent_lookupat_err(struct dentry *base, const char *path, char *name,
			     size_t *namelen, struct dentry **res)
{
	struct namei_context ctx = { 0 };
	struct dentry *parent;
	const char *last;
	size_t last_len;
	bool absolute;

	if (res)
		*res = NULL;
	if (!res || !name || !namelen)
		return -EINVAL;
	if (!path || !*path)
		return -ENOENT;

	absolute = path[0] == '/';
	while (*path == '/')
		path++;
	if (!*path)
		return -ENOENT;

	if (absolute)
		parent = fs_get_root_dentry(task_fs(current));
	else if (base) {
		parent = base;
		dget(parent);
	} else
		parent = fs_get_cwd_dentry(task_fs(current));
	if (!parent)
		return -ENOENT;
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

		if (!parent->d_inode || !S_ISDIR(parent->d_inode->i_mode)) {
			dput(parent);
			return -ENOTDIR;
		}

		if (is_dotdot(component, len)) {
			struct dentry *next = follow_dotdot(parent);
			dput(parent);
			parent = next;
			if (!parent)
				return -ENOENT;
			continue;
		}

		struct dentry *next = vfs_lookup_one(parent, component, len);
		if (!next) {
			dput(parent);
			return -ENOENT;
		}

		if (d_is_symlink(next)) {
			struct dentry *target;
			int ret = follow_symlink(parent, next, 0, &ctx,
						 &target);

			dput(parent);
			if (ret < 0)
				return ret;
			parent = target;
			continue;
		}

		dput(parent);
		parent = next;
	}

	if (!last || last_len == 0 || last_len > VFS_NAME_MAX ||
	    is_dot(last, last_len) || is_dotdot(last, last_len)) {
		dput(parent);
		return last_len > VFS_NAME_MAX ? -ENAMETOOLONG : -ENOENT;
	}

	if (!parent->d_inode || !S_ISDIR(parent->d_inode->i_mode)) {
		dput(parent);
		return -ENOTDIR;
	}

	memcpy(name, last, last_len);
	name[last_len] = '\0';
	*namelen = last_len;
	*res = parent;
	return 0;
}

struct dentry *path_parent_lookup(const char *path, char *name, size_t *namelen)
{
	struct dentry *parent;

	if (path_parent_lookupat_err(NULL, path, name, namelen, &parent) < 0)
		return NULL;
	return parent;
}

int vfs_chdir_dentry(struct dentry *dentry)
{
	struct inode *inode = vfs_dentry_inode(dentry);

	if (!dentry || !inode) {
		dput(dentry);
		return -ENOENT;
	}
	if (!S_ISDIR(vfs_inode_mode(inode))) {
		dput(dentry);
		return -ENOTDIR;
	}

	return fs_set_cwd(task_fs(current), dentry);
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

	if (current)
		fs_set_root_if_empty(task_fs(current), root_dentry);
}
