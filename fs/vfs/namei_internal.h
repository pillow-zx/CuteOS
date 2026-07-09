/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _CUTEOS_FS_VFS_NAMEI_INTERNAL_H
#define _CUTEOS_FS_VFS_NAMEI_INTERNAL_H

#include <kernel/fs.h>
#include <kernel/types.h>

struct dentry *__must_check vfs_lookup_one(struct dentry *parent,
					   const char *name, size_t len);
struct dentry *__must_check vfs_lookup_one_any(struct dentry *parent,
					       const char *name, size_t len);
void vfs_set_root_dentry(struct dentry *dentry);

#endif
