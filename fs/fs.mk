# fs/fs.mk — 文件系统

VFS_OBJS = \
	fs/vfs/super.o          \
	fs/vfs/inode.o          \
	fs/vfs/dentry.o         \
	fs/vfs/namei.o          \
	fs/vfs/fs_struct.o      \
	fs/vfs/file.o           \
	fs/vfs/read_write.o

EXT2_OBJS = \
	fs/ext2/super.o         \
	fs/ext2/inode.o         \
	fs/ext2/dir.o           \
	fs/ext2/file.o          \
	fs/ext2/balloc.o

FS_OBJS = \
	fs/pipe.o               \
	$(VFS_OBJS)             \
	$(EXT2_OBJS)
