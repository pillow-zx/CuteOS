/*
 * include/kernel/slab.h - 小对象 SLAB 分配器
 *
 * 声明构建在 buddy system 之上的 SLAB 分配器。提供固定大小的对象缓存
 * 以减少内部碎片，以及通用的 kmalloc/kfree 接口。
 *
 * 内部使用 8 个 kmem_cache，对应 8 个大小级别：
 *   16 / 32 / 64 / 128 / 256 / 512 / 1024 / 2048 字节
 *
 * 每个 cache 维护一个 free_list。free_list 为空时向 buddy 请求一个
 * 物理页，按对象大小切割后挂入 free_list。不回收物理页回 buddy。
 *
 * Functions:
 *   slab_init()      - 初始化 8 个 kmem_cache 并执行自测
 *   kmalloc(size)    - 分配 size 字节的内核对象
 *   kfree(ptr)       - 释放先前 kmalloc 的指针
 */

#ifndef _CUTEOS_KERNEL_SLAB_H
#define _CUTEOS_KERNEL_SLAB_H

#include <kernel/types.h>
#include <kernel/compiler.h>

void slab_init(void);
void *kmalloc(size_t size) __must_check __malloc;
void kfree(void *ptr);

#endif
