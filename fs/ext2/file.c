/*
 * fs/ext2/file.c - EXT2 文件读写
 *
 * 功能：
 *   实现 EXT2 文件系统中常规文件的数据读写操作。ext2_read_file 和
 *   ext2_write_file 对部分块写入采用 read-modify-write 策略：先读入
 *   整个块，修改其中部分字节，然后写回。采用写透（write-through）
 *   策略，数据立即写回磁盘。
 *
 * 主要函数：
 *   ext2_read_file(inode, buf, count, pos)  - 读取文件数据
 *   ext2_write_file(inode, buf, count, pos) - 写入文件数据，
 *                       部分块使用 read-modify-write，写透策略
 */
