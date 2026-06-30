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
 *   path_lookupat_path(base, path, flags, res) - 主路径解析函数。
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

static bool path_is_root(const struct path *path)
{
	return path && path->mnt && path->mnt->mnt_is_root &&
	       path->dentry == path->mnt->mnt_root;
}

int vfs_getcwd_path(const struct path *cwd, char *buf, size_t size)
{
	struct dentry *stack[32];
	struct path cur;
	size_t depth = 0;
	size_t pos = 0;

	if (!cwd || !cwd->mnt || !cwd->dentry || !root_dentry)
		return -ENOENT;
	cur = *cwd;
	while (!path_is_root(&cur)) {
		if (depth >= 32)
			return -ENAMETOOLONG;
		if (cur.dentry == cur.mnt->mnt_root && !cur.mnt->mnt_is_root) {
			stack[depth++] = cur.mnt->mnt_mountpoint;
			cur.dentry = cur.mnt->mnt_mountpoint;
			cur.mnt = cur.mnt->mnt_parent;
			cur.dentry = vfs_dentry_parent(cur.dentry);
			continue;
		}
		stack[depth++] = cur.dentry;
		if (cur.dentry == vfs_dentry_parent(cur.dentry))
			return -ENOENT;
		cur.dentry = vfs_dentry_parent(cur.dentry);
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

static int follow_dotdot(struct path *path)
{
	struct path parent_path;
	struct dentry *parent_dentry;
	int ret;

	if (!path || !path->dentry || !path->mnt)
		return -ENOENT;

	ret = vfs_follow_dotdot_mount(path);
	if (ret < 0)
		return ret;

	parent_dentry = path->dentry->d_parent ? path->dentry->d_parent :
						 path->dentry;
	if (!parent_dentry)
		return -ENOENT;
	if (parent_dentry == path->dentry)
		return 0;

	parent_path.mnt = path->mnt;
	parent_path.dentry = parent_dentry;
	path_get(&parent_path);
	path_put(path);
	*path = parent_path;
	return vfs_follow_mount(path);
}

static int ensure_directory_dentry(struct dentry *dentry)
{
	if (!dentry || !dentry->d_inode)
		return -ENOENT;
	if (!S_ISDIR(dentry->d_inode->i_mode))
		return -ENOTDIR;

	return 0;
}

static int lookup_start_path(const struct path *base, const char *path,
			     struct path *res)
{
	if (!res)
		return -EINVAL;
	res->mnt = NULL;
	res->dentry = NULL;
	if (*path == '/')
		return fs_get_root_path(task_fs(current), res);
	if (base) {
		*res = *base;
		path_get(res);
		return 0;
	}

	return fs_get_cwd_path(task_fs(current), res);
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

static int walk_path(struct path *base, const char *path, uint32_t flags,
		     struct namei_context *ctx, struct path *res);

/*
 * 跟随符号链接：读取 link 指向的目标路径，并从合适的起点解析出目标
 * dentry。dir 是 link 所在的目录（用于解析相对目标），调用者保留其
 * 引用；link 的引用由本函数释放。成功时通过 res 返回目标 dentry（已
 * dget），失败返回负错误码。
 */
static int follow_symlink(const struct path *dir, struct path *link,
			  uint32_t flags, struct namei_context *ctx,
			  struct path *res)
{
	char *target;
	struct path base;
	int len;
	int ret;

	if (ctx->symlink_depth >= MAXSYMLINKS) {
		path_put(link);
		return -ELOOP;
	}
	ctx->symlink_depth++;

	target = get_free_page(0);
	if (!target) {
		path_put(link);
		return -ENOMEM;
	}

	len = vfs_readlink(link->dentry, target, VFS_PATH_MAX - 1);
	path_put(link);
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
		ret = fs_get_root_path(task_fs(current), &base);
	else {
		base = *dir;
		path_get(&base);
		ret = 0;
	}
	if (ret < 0) {
		free_page(target, 0);
		return ret;
	}
	ret = walk_path(&base, target, flags, ctx, res);
	free_page(target, 0);
	return ret;
}

/*
 * 从 base 起逐分量解析 path。消费 base 的引用：成功时返回最终 dentry
 * （持有引用），失败时释放 base 并返回负错误码。中间分量总是跟随
 * 符号链接；末端分量在未设置 LOOKUP_NOFOLLOW 时也跟随。
 */
static int walk_path(struct path *base, const char *path, uint32_t flags,
		     struct namei_context *ctx, struct path *res)
{
	struct path cur = *base;

	res->mnt = NULL;
	res->dentry = NULL;
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
			path_put(&cur);
			return -ENAMETOOLONG;
		}

		if (is_dotdot(name, len)) {
			int ret = follow_dotdot(&cur);
			if (ret < 0) {
				path_put(&cur);
				return ret;
			}
			continue;
		}

		int ret = ensure_directory_dentry(cur.dentry);
		if (ret < 0) {
			path_put(&cur);
			return ret;
		}

		next = vfs_lookup_one(cur.dentry, name, len);
		if (!next) {
			path_put(&cur);
			return -ENOENT;
		}
		struct path next_path = {
			.mnt = cur.mnt,
			.dentry = next,
		};
		mntget(next_path.mnt);
		if (!(is_last && (flags & LOOKUP_NO_MOUNT))) {
			ret = vfs_follow_mount(&next_path);
			if (ret < 0) {
				path_put(&next_path);
				path_put(&cur);
				return ret;
			}
		}

		if (d_is_symlink(next_path.dentry) &&
		    !(is_last && (flags & LOOKUP_NOFOLLOW))) {
			struct path target;
			ret = follow_symlink(&cur, &next_path, flags, ctx,
					     &target);

			path_put(&cur);
			if (ret < 0)
				return ret;
			cur = target;
			continue;
		}

		path_put(&cur);
		cur = next_path;
	}

	*res = cur;
	return 0;
}

int path_lookupat_path(const struct path *base, const char *path, uint32_t flags,
		       struct path *res)
{
	struct namei_context ctx = {0};
	struct path start;
	int ret;

	if (res)
		memset(res, 0, sizeof(*res));
	if (!res)
		return -EINVAL;
	if (!path || !*path)
		return -ENOENT;

	ret = lookup_start_path(base, path, &start);
	if (ret < 0)
		return ret;
	ret = vfs_follow_mount(&start);
	if (ret < 0) {
		path_put(&start);
		return ret;
	}

	return walk_path(&start, path, flags, &ctx, res);
}

int path_parent_lookupat_path(const struct path *base, const char *path,
			      char *name, size_t *namelen, struct path *res)
{
	struct namei_context ctx = {0};
	struct path parent;
	const char *last;
	size_t last_len;
	int ret;

	if (res) {
		res->mnt = NULL;
		res->dentry = NULL;
	}
	if (!res || !name || !namelen)
		return -EINVAL;
	if (!path || !*path)
		return -ENOENT;

	ret = lookup_start_path(base, path, &parent);
	if (ret < 0)
		return ret;

	ret = vfs_follow_mount(&parent);
	if (ret < 0) {
		path_put(&parent);
		return ret;
	}

	path = skip_slashes(path);
	if (!*path)
		path_put(&parent);
	if (!*path)
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
		if (len > VFS_NAME_MAX) {
			path_put(&parent);
			return -ENAMETOOLONG;
		}

		if (!*path) {
			last = component;
			last_len = len;
			break;
		}

		ret = ensure_directory_dentry(parent.dentry);
		if (ret < 0) {
			path_put(&parent);
			return ret;
		}

		if (is_dotdot(component, len)) {
			ret = follow_dotdot(&parent);
			if (ret < 0) {
				path_put(&parent);
				return ret;
			}
			continue;
		}

		struct dentry *next =
			vfs_lookup_one(parent.dentry, component, len);
		if (!next) {
			path_put(&parent);
			return -ENOENT;
		}
		struct path next_path = {
			.mnt = parent.mnt,
			.dentry = next,
		};
		mntget(next_path.mnt);

		ret = vfs_follow_mount(&next_path);
		if (ret < 0) {
			path_put(&next_path);
			path_put(&parent);
			return ret;
		}

		if (d_is_symlink(next_path.dentry)) {
			struct path target;

			ret = follow_symlink(&parent, &next_path, 0, &ctx,
					     &target);
			path_put(&parent);
			if (ret < 0)
				return ret;
			parent = target;
			continue;
		}

		path_put(&parent);
		parent = next_path;
	}

	if (!last || last_len == 0 || last_len > VFS_NAME_MAX ||
	    is_dot(last, last_len) || is_dotdot(last, last_len)) {
		path_put(&parent);
		return last_len > VFS_NAME_MAX ? -ENAMETOOLONG : -ENOENT;
	}

	ret = ensure_directory_dentry(parent.dentry);
	if (ret < 0) {
		path_put(&parent);
		return ret;
	}

	memcpy(name, last, last_len);
	name[last_len] = '\0';
	*namelen = last_len;
	*res = parent;
	return 0;
}

int vfs_chdir_path(const struct path *path)
{
	struct inode *inode;

	if (!path || !path->dentry)
		return -ENOENT;
	inode = vfs_dentry_inode(path->dentry);
	if (!inode)
		return -ENOENT;
	if (!S_ISDIR(vfs_inode_mode(inode)))
		return -ENOTDIR;

	return fs_set_cwd_path(task_fs(current), path);
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
