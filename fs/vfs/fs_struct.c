/*
 * fs/vfs/fs_struct.c - 可共享的进程文件系统上下文
 */

#include <kernel/errno.h>
#include <kernel/fs_struct.h>
#include <kernel/slab.h>
#include <kernel/string.h>
#include <kernel/task.h>
#include <kernel/vfs.h>

static void fs_set_initial_root(struct fs_struct *fs)
{
	if (!fs || !root_dentry)
		return;

	fs->root = root_dentry;
	fs->cwd = root_dentry;
	dget(fs->root);
	dget(fs->cwd);
}

struct fs_struct *fs_alloc(void)
{
	struct fs_struct *fs = kmalloc(sizeof(*fs));

	if (!fs)
		return NULL;

	memset(fs, 0, sizeof(*fs));
	refcount_set(&fs->refcount, 1);
	mutex_init(&fs->lock);
	fs->umask = 0022;
	fs_set_initial_root(fs);
	return fs;
}

struct fs_struct *fs_dup(struct fs_struct *old)
{
	struct fs_struct *fs = fs_alloc();

	if (!fs)
		return NULL;
	if (!old)
		return fs;

	if (fs->root) {
		dput(fs->root);
		fs->root = NULL;
	}
	if (fs->cwd) {
		dput(fs->cwd);
		fs->cwd = NULL;
	}

	mutex_lock(&old->lock);
	fs->umask = old->umask;
	fs->root = old->root;
	fs->cwd = old->cwd;
	if (fs->root)
		dget(fs->root);
	if (fs->cwd)
		dget(fs->cwd);
	mutex_unlock(&old->lock);

	return fs;
}

void fs_get(struct fs_struct *fs)
{
	if (fs)
		refcount_inc(&fs->refcount);
}

void fs_put(struct fs_struct *fs)
{
	if (!fs)
		return;

	if (!refcount_dec_and_test(&fs->refcount))
		return;

	if (fs->root)
		dput(fs->root);
	if (fs->cwd)
		dput(fs->cwd);
	kfree(fs);
}

struct dentry *fs_get_root_dentry(struct fs_struct *fs)
{
	struct dentry *dentry = NULL;

	if (fs) {
		mutex_lock(&fs->lock);
		dentry = fs->root;
		if (dentry)
			dget(dentry);
		mutex_unlock(&fs->lock);
	}

	if (!dentry && root_dentry) {
		dentry = root_dentry;
		dget(dentry);
	}

	return dentry;
}

struct dentry *fs_get_cwd_dentry(struct fs_struct *fs)
{
	struct dentry *dentry = NULL;

	if (fs) {
		mutex_lock(&fs->lock);
		dentry = fs->cwd ? fs->cwd : fs->root;
		if (dentry)
			dget(dentry);
		mutex_unlock(&fs->lock);
	}

	if (!dentry && root_dentry) {
		dentry = root_dentry;
		dget(dentry);
	}

	return dentry;
}

int fs_set_cwd(struct fs_struct *fs, struct dentry *dentry)
{
	struct dentry *old;

	if (!fs || !dentry) {
		dput(dentry);
		return -EINVAL;
	}

	mutex_lock(&fs->lock);
	old = fs->cwd;
	fs->cwd = dentry;
	mutex_unlock(&fs->lock);

	if (old)
		dput(old);
	return 0;
}

uint32_t fs_get_umask(struct fs_struct *fs)
{
	uint32_t umask = 0022;

	if (!fs)
		return umask;

	mutex_lock(&fs->lock);
	umask = fs->umask;
	mutex_unlock(&fs->lock);
	return umask;
}

uint32_t fs_set_umask(struct fs_struct *fs, uint32_t mask)
{
	uint32_t old = 0022;

	if (!fs)
		return old;

	mutex_lock(&fs->lock);
	old = fs->umask;
	fs->umask = mask;
	mutex_unlock(&fs->lock);
	return old;
}

void fs_set_root_if_empty(struct fs_struct *fs, struct dentry *root)
{
	if (!fs || !root)
		return;

	mutex_lock(&fs->lock);
	if (!fs->root) {
		fs->root = root;
		dget(fs->root);
	}
	if (!fs->cwd) {
		fs->cwd = root;
		dget(fs->cwd);
	}
	mutex_unlock(&fs->lock);
}

int init_fs(struct task_struct *task)
{
	if (!task)
		return -EINVAL;

	task->fs = fs_alloc();
	return task->fs ? 0 : -ENOMEM;
}

int copy_fs(struct task_struct *child, bool share)
{
	struct fs_struct *fs;

	if (!child)
		return -EINVAL;

	if (share) {
		fs = current ? current->fs : NULL;
		if (!fs)
			return init_fs(child);
		fs_get(fs);
	} else {
		fs = fs_dup(current ? current->fs : NULL);
		if (!fs)
			return -ENOMEM;
	}

	exit_fs(child);
	child->fs = fs;
	return 0;
}

void exit_fs(struct task_struct *task)
{
	if (!task || !task->fs)
		return;

	fs_put(task->fs);
	task->fs = NULL;
}
