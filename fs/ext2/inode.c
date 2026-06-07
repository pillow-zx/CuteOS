/*
 * fs/ext2/inode.c - EXT2 inode 读写
 *
 * 功能：
 *   实现 EXT2 文件系统 inode 的读取、写入和块映射。ext2_read_inode
 *   根据 inode 号计算所在块组和块内索引，从 inode 表块中读取数据。
 *   ext2_bmap 实现逻辑块到物理块的转换：直接块 (0-11)、一级间接
 *   块 (12)、二级间接块 (13)，单文件最大 64MB。ext2_write_inode
 *   将修改后的 inode 写回磁盘。
 *
 * 主要函数：
 *   ext2_read_inode(inode)    - 计算 group+index，读取 inode 表块
 *   ext2_write_inode(inode)   - 将 inode 写回磁盘
 *   ext2_bmap(inode, block)   - 块地址映射：直接(0-11)+1级间接(12)+
 *                               2级间接(13)，最大 64MB
 */
