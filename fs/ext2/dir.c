/*
 * fs/ext2/dir.c - EXT2 目录操作
 *
 * 功能：
 *   实现 EXT2 文件系统的目录操作。ext2_lookup 扫描目录数据块查找
 *   文件名。ext2_readdir 使用 filldir 回调函数（VFS 抽象层）向
 *   用户空间传递目录项。还实现目录的创建和删除操作。
 *
 * 主要函数：
 *   ext2_lookup(dir, name)     - 扫描目录数据块查找文件名
 *   ext2_readdir(file, filldir)- 使用 filldir 回调（VFS 抽象）读取目录
 *   ext2_create(dir, name)     - 在目录中创建新文件
 *   ext2_mkdir(dir, name)      - 创建子目录
 *   ext2_unlink(dir, name)     - 删除目录项（文件）
 *   ext2_rmdir(dir, name)      - 删除空目录
 */
