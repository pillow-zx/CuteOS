#ifndef _CUTEOS_KERNEL_SLAB_H
#define _CUTEOS_KERNEL_SLAB_H

/*
 * include/kernel/slab.h - 小对象 SLAB 分配器
 *
 * 声明构建在 buddy system 之上的 SLAB 分配器。提供固定大小的对象缓存
 * 以减少内部碎片，以及通用的 kmalloc/kfree 接口。
 *
 * Structs:
 *   struct kmem_cache - Describes a cache of same-sized objects
 *
 * Functions:
 *   kmalloc(size) - Allocate a kernel object of the given size
 *   kfree(ptr)    - Free a previously kmalloc'd pointer
 */

#endif
