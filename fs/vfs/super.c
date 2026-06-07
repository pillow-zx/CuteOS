/*
 * fs/vfs/super.c - VFS 超级块管理
 *
 * 功能：
 *   实现 VFS 层的超级块管理。超级块（super_block）是每个已挂载文件系统
 *   在内核中的核心描述结构。本模块负责文件系统类型的注册和超级块的创建。
 *   register_filesystem() 将文件系统类型注册到全局数组 fs_types[8] 中。
 *   ext2_mount 从磁盘读取超级块，创建对应的 VFS super_block 结构。
 *   super_operations 操作向量包含 read_inode、write_inode、evict_inode，
 *   由底层具体文件系统实现。
 *
 * 主要函数：
 *   register_filesystem(fs_type) - 将文件系统类型注册到 fs_types[8] 数组
 *   ext2_mount(dev, dir)         - 读取磁盘超级块，创建 VFS super_block
 *   super_operations             - {read_inode, write_inode, evict_inode} 操作向量
 */
