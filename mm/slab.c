/*
 * mm/slab.c - SLAB 对象缓存
 *
 * 功能：
 *   在伙伴系统之上实现简化版 SLAB 分配器。提供 8 个固定大小级别：
 *   16 / 32 / 64 / 128 / 256 / 512 / 1024 / 2048 字节。每个 kmem_cache
 *   维护一个 free_list 空闲对象链表。无 full/partial/empty 链表跟踪。
 *
 * 数据结构：
 *   kmem_cache {size, free_list}  - 共 8 个缓存，对应 8 个大小级别
 *
 * 分配流程：
 *   kmalloc(size) 遍历 8 个缓存，找到第一个 size >= 请求大小的缓存，
 *   从其 free_list 取出对象返回。若 free_list 为空，则向伙伴系统
 *   请求一个物理页，按缓存的对象大小切割为多个对象挂入 free_list。
 *
 * 回收流程：
 *   kfree(ptr) 根据对象大小确定所属缓存，将对象归还 free_list。
 *   不回收物理页回伙伴系统（简单实现）。
 */
