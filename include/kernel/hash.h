#ifndef _CUTEOS_KERNEL_HASH_H
#define _CUTEOS_KERNEL_HASH_H

/*
 * include/kernel/hash.h - Intrusive hash table helpers
 *
 * The table owns only bucket heads. Objects embed a struct list_head and
 * callers provide the hash value plus key comparison during lookup.
 */

#include <kernel/list.h>
#include <kernel/types.h>

struct hash_table {
	struct list_head *buckets;
	uint32_t bits;
};

#define HASH_TABLE_SIZE(hash_bits) (1u << (hash_bits))

#define HASH_TABLE_DECLARE(name, hash_bits)                                    \
	struct list_head name##_buckets[HASH_TABLE_SIZE(hash_bits)];           \
	struct hash_table name = {                                             \
		.buckets = name##_buckets,                                     \
		.bits = (hash_bits),                                           \
	}

#define HASH_TABLE_DECLARE_STATIC(name, hash_bits)                             \
	static struct list_head name##_buckets[HASH_TABLE_SIZE(hash_bits)];    \
	static struct hash_table name = {                                      \
		.buckets = name##_buckets,                                     \
		.bits = (hash_bits),                                           \
	}

static __always_inline void hash_table_init(struct hash_table *table)
{
	for (uint32_t i = 0; i < HASH_TABLE_SIZE(table->bits); i++)
		INIT_LIST_HEAD(&table->buckets[i]);
}

static __always_inline struct list_head *
hash_table_bucket(struct hash_table *table, uint64_t hash)
{
	return &table->buckets[(uint32_t)hash &
			       (HASH_TABLE_SIZE(table->bits) - 1u)];
}

static __always_inline void
hash_table_add(struct hash_table *table, uint64_t hash, struct list_head *node)
{
	list_add(node, hash_table_bucket(table, hash));
}

static __always_inline void hash_table_del(struct list_head *node)
{
	list_del(node);
}

#define hash_table_for_each_possible(pos, table, hash)                         \
	list_for_each (pos, hash_table_bucket((table), (hash)))

#endif
