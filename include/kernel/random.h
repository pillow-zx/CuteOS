#ifndef _CUTEOS_KERNEL_RANDOM_H
#define _CUTEOS_KERNEL_RANDOM_H

#include <kernel/compiler.h>
#include <kernel/types.h>

/**
 * @brief Fill a buffer from cuteOS's weak boot-time random source.
 *
 * The source is seeded from mtime and task state and is not suitable for
 * cryptographic use.
 */
void __nonnull(1) __access_no_size(write_only, 1)
	weak_random_bytes(void *buf, size_t len);

#endif
