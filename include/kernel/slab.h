/**
 * @file slab.h
 * @brief 小对象 slab 分配器接口。
 */

#ifndef _CUTEOS_KERNEL_SLAB_H
#define _CUTEOS_KERNEL_SLAB_H

#include <kernel/types.h>
#include <kernel/compiler.h>

/**
 * @brief Initialize slab caches used by kmalloc.
 */
void slab_init(void);

/**
 * @brief Allocate a small kernel heap object.
 * @param size Requested object size in bytes.
 * @return Allocated object, or NULL.
 */
void *kmalloc(size_t size) __must_check __malloc __alloc_size(1);

/**
 * @brief Free an object allocated by kmalloc/kzalloc.
 * @param ptr Object pointer, or NULL.
 */
void kfree(void *ptr);

/**
 * @brief Allocate and zero a small kernel heap object.
 * @param size Requested object size in bytes.
 * @return Zero-filled object, or NULL.
 */
static __always_inline void *__must_check __malloc __alloc_size(1)
	kzalloc(size_t size)
{
	void *ptr = kmalloc(size);
	if (ptr)
		memset(ptr, 0, size);

	return ptr;
}

#endif
