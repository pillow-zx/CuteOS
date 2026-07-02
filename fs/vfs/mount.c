/*
 * fs/vfs/mount.c - single-namespace VFS mount table
 */

#include <kernel/blkdev.h>
#include <kernel/errno.h>
#include <kernel/fs.h>
#include <kernel/slab.h>
#include <kernel/stat.h>
#include <kernel/string.h>
#include <kernel/sync.h>
#include <kernel/vfs.h>

static LIST_HEAD(mount_list);
static DEFINE_MUTEX(mount_lock);
static struct vfsmount *root_mount;

static struct vfsmount *mount_find_by_mountpoint(const struct path *mountpoint)
{
	struct vfsmount *mnt;

	if (!mountpoint || !mountpoint->mnt || !mountpoint->dentry)
		return NULL;

	list_for_each_entry (mnt, &mount_list, mnt_list) {
		if (mnt->mnt_parent == mountpoint->mnt &&
		    mnt->mnt_mountpoint == mountpoint->dentry)
			return mnt;
	}

	return NULL;
}

static struct vfsmount *mount_find_by_root(const struct path *root)
{
	struct vfsmount *mnt;

	if (!root || !root->mnt || !root->dentry)
		return NULL;

	list_for_each_entry (mnt, &mount_list, mnt_list) {
		if (mnt == root->mnt && mnt->mnt_root == root->dentry)
			return mnt;
	}

	return NULL;
}

static struct vfsmount *mount_find_by_sb(struct super_block *sb)
{
	struct vfsmount *mnt;

	if (!sb)
		return NULL;

	list_for_each_entry (mnt, &mount_list, mnt_list) {
		if (mnt->mnt_sb == sb)
			return mnt;
	}

	return NULL;
}

void mntget(struct vfsmount *mnt)
{
	if (mnt)
		refcount_inc(&mnt->mnt_refcount);
}

static void mnt_active_get(struct vfsmount *mnt)
{
	if (mnt)
		atomic_inc(&mnt->mnt_active_refs);
}

static void mnt_active_put(struct vfsmount *mnt)
{
	if (mnt)
		atomic_dec(&mnt->mnt_active_refs);
}

static void mount_free(struct vfsmount *mnt)
{
	if (!mnt)
		return;

	if (mnt->mnt_parent)
		mntput(mnt->mnt_parent);
	dput(mnt->mnt_root);
	dput(mnt->mnt_mountpoint);
	kfree(mnt);
}

void mntput(struct vfsmount *mnt)
{
	if (!mnt)
		return;

	if (refcount_dec_and_test(&mnt->mnt_refcount))
		mount_free(mnt);
}

void path_get(const struct path *path)
{
	if (!path)
		return;
	mntget(path->mnt);
	mnt_active_get(path->mnt);
	dget(path->dentry);
}

void path_put(struct path *path)
{
	if (!path)
		return;
	dput(path->dentry);
	mnt_active_put(path->mnt);
	mntput(path->mnt);
	path->mnt = NULL;
	path->dentry = NULL;
}

int vfs_root_path(struct path *path)
{
	if (!path)
		return -EINVAL;

	mutex_lock(&mount_lock);
	if (!root_mount) {
		mutex_unlock(&mount_lock);
		path->mnt = NULL;
		path->dentry = NULL;
		return -ENOENT;
	}
	path->mnt = root_mount;
	path->dentry = root_mount->mnt_root;
	path_get(path);
	mutex_unlock(&mount_lock);
	return 0;
}

int vfs_path_from_dentry(struct dentry *dentry, struct path *path)
{
	struct vfsmount *mnt;

	if (path) {
		path->mnt = NULL;
		path->dentry = NULL;
	}
	if (!dentry || !path)
		return -EINVAL;

	mutex_lock(&mount_lock);
	mnt = mount_find_by_sb(dentry->d_sb);
	if (!mnt)
		mnt = root_mount;
	if (!mnt) {
		mutex_unlock(&mount_lock);
		return -ENOENT;
	}
	path->mnt = mnt;
	path->dentry = dentry;
	path_get(path);
	mutex_unlock(&mount_lock);
	return 0;
}

static int mount_add(const struct path *mountpoint, struct dentry *root,
		     struct super_block *sb, dev_t dev, bool is_root)
{
	struct vfsmount *mnt;

	if (!mountpoint || !mountpoint->dentry || !root || !sb)
		return -EINVAL;

	mnt = kmalloc(sizeof(*mnt));
	if (!mnt)
		return -ENOMEM;

	refcount_set(&mnt->mnt_refcount, 1);
	atomic_set(&mnt->mnt_active_refs, 0);
	mnt->mnt_parent = is_root ? NULL : mountpoint->mnt;
	mnt->mnt_mountpoint = mountpoint->dentry;
	mnt->mnt_root = root;
	mnt->mnt_sb = sb;
	mnt->mnt_dev = dev;
	mnt->mnt_is_root = is_root;
	INIT_LIST_HEAD(&mnt->mnt_list);
	if (mnt->mnt_parent)
		mntget(mnt->mnt_parent);
	dget(mnt->mnt_mountpoint);
	dget(root);

	mutex_lock(&mount_lock);
	if (mount_find_by_mountpoint(mountpoint)) {
		mutex_unlock(&mount_lock);
		mount_free(mnt);
		return -EBUSY;
	}
	list_add_tail(&mnt->mnt_list, &mount_list);
	if (is_root)
		root_mount = mnt;
	mutex_unlock(&mount_lock);

	return 0;
}

int vfs_mount_root(struct dentry *root)
{
	struct path root_path;

	if (!root || !root->d_sb)
		return -EINVAL;

	root_path.mnt = NULL;
	root_path.dentry = root;
	return mount_add(&root_path, root, root->d_sb, root->d_sb->s_dev, true);
}

int vfs_follow_mount(struct path *path)
{
	struct vfsmount *mnt;
	struct path next;

	if (!path || !path->mnt || !path->dentry)
		return -EINVAL;

	mutex_lock(&mount_lock);
	mnt = mount_find_by_mountpoint(path);
	if (mnt && (mnt != path->mnt || mnt->mnt_root != path->dentry)) {
		next.mnt = mnt;
		next.dentry = mnt->mnt_root;
		path_get(&next);
		mutex_unlock(&mount_lock);
		path_put(path);
		*path = next;
		return 0;
	}
	mutex_unlock(&mount_lock);
	return 0;
}

int vfs_follow_dotdot_mount(struct path *path)
{
	struct vfsmount *mnt;
	struct path parent;

	if (!path || !path->mnt || !path->dentry)
		return -EINVAL;

	mutex_lock(&mount_lock);
	mnt = mount_find_by_root(path);
	if (mnt && !mnt->mnt_is_root) {
		parent.mnt = mnt->mnt_parent;
		parent.dentry = mnt->mnt_mountpoint;
		path_get(&parent);
		mutex_unlock(&mount_lock);
		path_put(path);
		*path = parent;
		return 0;
	}
	mutex_unlock(&mount_lock);
	return 0;
}

int vfs_mount(const char *source, const char *target, const char *type,
	      unsigned long flags, const void *data)
{
	struct path source_path = {0};
	struct path target_path = {0};
	struct file_system_type *fs_type;
	struct super_block *sb;
	struct inode *source_inode;
	dev_t dev;
	int ret;

	if (!source || !target || !type)
		return -EFAULT;
	if (flags)
		return -EINVAL;

	fs_type = get_filesystem_type(type);
	if (!fs_type || !fs_type->mount)
		return -ENODEV;

	ret = path_lookupat_path(NULL, source, 0, &source_path);
	if (ret < 0)
		return ret;
	source_inode = source_path.dentry->d_inode;
	if (!source_inode || !S_ISBLK(source_inode->i_mode)) {
		path_put(&source_path);
		return -ENOTBLK;
	}
	dev = source_inode->i_rdev;
	path_put(&source_path);

	if (!lookup_block_device(dev))
		return -ENXIO;

	ret = path_lookupat_path(NULL, target, LOOKUP_NO_MOUNT, &target_path);
	if (ret < 0)
		return ret;
	if (!target_path.dentry->d_inode ||
	    !S_ISDIR(target_path.dentry->d_inode->i_mode)) {
		path_put(&target_path);
		return -ENOTDIR;
	}

	mutex_lock(&mount_lock);
	if (mount_find_by_mountpoint(&target_path)) {
		mutex_unlock(&mount_lock);
		path_put(&target_path);
		return -EBUSY;
	}
	mutex_unlock(&mount_lock);

	sb = fs_type->mount(fs_type, dev, data);
	if (!sb) {
		path_put(&target_path);
		return -EINVAL;
	}

	ret = mount_add(&target_path, sb->s_root, sb, dev, false);
	path_put(&target_path);
	return ret;
}

static bool mount_busy(struct vfsmount *mnt)
{
	if (!mnt || !mnt->mnt_root)
		return true;

	return atomic_read(&mnt->mnt_active_refs) > 0;
}

int vfs_umount(const char *target, int flags)
{
	struct path path = {0};
	struct vfsmount *mnt;
	int ret;

	if (!target)
		return -EFAULT;
	if (flags)
		return -EINVAL;

	ret = path_lookupat_path(NULL, target, LOOKUP_NOFOLLOW, &path);
	if (ret < 0)
		return ret;

	mutex_lock(&mount_lock);
	mnt = mount_find_by_root(&path);
	if (!mnt) {
		mutex_unlock(&mount_lock);
		path_put(&path);
		return -EINVAL;
	}
	mntget(mnt);
	mutex_unlock(&mount_lock);
	path_put(&path);

	mutex_lock(&mount_lock);
	if (mnt->mnt_is_root || mount_busy(mnt)) {
		mutex_unlock(&mount_lock);
		mntput(mnt);
		return -EBUSY;
	}
	list_del(&mnt->mnt_list);
	mutex_unlock(&mount_lock);

	mntput(mnt);
	mntput(mnt);
	return 0;
}
