# fs/fs.mk — 文件系统

VFS_OBJS = \
	fs/vfs/super.o          \
	fs/vfs/inode.o          \
	fs/vfs/dentry.o         \
	fs/vfs/mount.o          \
	fs/vfs/namei.o          \
	fs/vfs/namei_mutation.o \
	fs/vfs/fs_struct.o      \
	fs/vfs/fdtable.o        \
	fs/vfs/file.o           \
	fs/vfs/read_write.o

EXT2_OBJS = \
	fs/ext2/super.o         \
	fs/ext2/inode.o         \
	fs/ext2/dir.o           \
	fs/ext2/file.o          \
	fs/ext2/balloc.o

ifeq ($(CONFIG_EXT2_FS),y)
FS_EXT2_OBJS = $(EXT2_OBJS)
else
FS_EXT2_OBJS =
endif

FS_OBJS = \
	fs/pipe.o               \
	$(VFS_OBJS)             \
	$(FS_EXT2_OBJS)
