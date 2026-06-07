/*
 * block/blkdev.c - 块设备注册与查找
 *
 * 功能：
 *   管理块设备的注册与查找。使用 dev_table[32] 静态数组，以主设备号
 *   （major number）为索引存储已注册的块设备信息。
 *
 * 数据结构：
 *   dev_table[32]  - 静态数组，索引为主设备号
 *
 * 主要函数：
 *   register_device(bdev)  - 注册块设备到 dev_table，以主设备号为索引
 */
