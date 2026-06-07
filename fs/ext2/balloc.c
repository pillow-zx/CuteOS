/*
 * fs/ext2/balloc.c - EXT2 块与 inode 分配
 *
 * 功能：
 *   管理 EXT2 文件系统的数据块和 inode 号的分配与释放。
 *   ext2_alloc_block 优先从 inode 所属的块组中分配空闲块，扫描
 *   块位图找到空闲位。ext2_alloc_inode 选择有空闲 inode 的块组，
 *   扫描 inode 位图。分配后更新块组描述符中的 bg_free 计数。
 *
 * 主要函数：
 *   ext2_alloc_block(inode)      - 优先从 inode 所属块组分配，扫描块位图
 *   ext2_alloc_inode(sb)         - 选择有空闲 inode 的块组，扫描 inode 位图
 *   ext2_free_block(sb, block)   - 释放数据块，更新 bg_free 计数
 *   ext2_free_inode(sb, ino)     - 释放 inode 号，更新 bg_free 计数
 */
