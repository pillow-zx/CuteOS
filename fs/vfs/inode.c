/*
 * fs/vfs/inode.c - Inode 缓存
 *
 * 功能：
 *   管理 VFS 层 inode 的分配、查找和引用计数。inode 是 VFS 中表示
 *   文件/目录元数据的核心对象。icache 使用 64 桶哈希表，以 (dev, ino)
 *   为键进行索引。iget 先在哈希表中查找，命中则返回，未命中则分配新
 *   inode 并从磁盘读取。iput 减少引用计数，但不移出哈希表（inode
 *   在缓存中持久保留）。struct inode 包含 i_private 指针，供底层
 *   文件系统（如 ext2）挂载私有数据。
 *
 * 主要函数：
 *   iget(sb, ino)  - 在哈希表中查找 inode，未命中则分配并从磁盘读取
 *   iput(inode)    - 减少引用计数，inode 保留在哈希表中不被驱逐
 *   icache_init()  - 初始化 64 桶哈希表
 *
 * 关键数据结构：
 *   struct inode   - 包含 i_private 指针，用于文件系统特定数据
 *   icache 哈希表  - 64 桶，以 (dev, ino) 为键
 */
