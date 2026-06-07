/*
 * fs/vfs/read_write.c - VFS 读写入口
 *
 * 功能：
 *   提供 VFS 层统一的读写入口点。vfs_read 和 vfs_write 首先检查
 *   f_mode 中的权限位（可读/可写），然后调用 f_op->read/write 分发
 *   到底层具体文件系统实现。每次操作后更新 f_pos 读写位置。
 *
 * 主要函数：
 *   vfs_read(file, buf, count)  - 读入口：检查 f_mode 权限，
 *                                 调用 f_op->read，更新 f_pos
 *   vfs_write(file, buf, count) - 写入口：检查 f_mode 权限，
 *                                 调用 f_op->write，更新 f_pos
 */
