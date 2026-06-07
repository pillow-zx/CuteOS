/*
 * fs/ext2/super.c - EXT2 超级块
 *
 * 功能：
 *   实现 EXT2 文件系统超级块的读取和解析。ext2_read_super 从块设备
 *   读取块 1（超级块），验证魔数 0xEF53。随后读取块组描述符表。
 *   从超级块中提取关键参数：block_size、inode_size、blocks_per_group
 *   等，使用 mkfs 默认参数。
 *
 * 主要函数：
 *   ext2_read_super(sb, dev) - 读取块 1（超级块），校验 0xEF53 魔数
 *   ext2_read_bgdt(sb)      - 读取块组描述符表
 *   ext2_init()              - 从超级块提取 block_size/inode_size/
 *                              blocks_per_group 等参数
 */
