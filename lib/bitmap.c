/*
 * lib/bitmap.c - 位图操作
 *
 * 功能：
 *   提供位图（bitmap）的基本操作函数。内核中多个模块需要位图来
 *   高效管理资源分配（如块位图、inode 位图等）。
 *
 * 主要函数：
 *   bitmap_set_bit(bitmap, bit)    - 设置指定位（置 1）
 *   bitmap_clear_bit(bitmap, bit)  - 清除指定位（置 0）
 *   bitmap_find_free(bitmap, nbits)- 在位图中查找第一个空闲位，
 *                                    返回位索引（从 0 开始）
 */
