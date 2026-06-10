/*
 * include/kernel/bitmap.h - 位图操作
 *
 * 提供位图（bitmap）的声明、初始化及基本操作。位图使用 uintptr_t
 * 数组作为底层存储，每个 word 占 sizeof(uintptr_t) * 8 位。
 *
 * 接口：
 *   BITMAP_DECLARE(name, nbits)  - 声明并初始化一个全局位图
 *   bitmap_zero(map)             - 清零全部位
 *   bitmap_set(map, bit)         - 置位指定位
 *   bitmap_clear(map, bit)       - 清除指定位
 *   bitmap_test(map, bit)        - 测试指定位是否为 1
 *   bitmap_find_first_zero(map)  - 查找第一个为 0 的位
 */

#ifndef _CUTEOS_KERNEL_BITMAP_H
#define _CUTEOS_KERNEL_BITMAP_H

#include <kernel/types.h>
#include <kernel/compiler.h>

#define BITMAP_WORD_BITS ((size_t)(sizeof(uintptr_t) * 8U))
#define BITMAP_WORDS(nbits)                                                    \
	(((nbits) + BITMAP_WORD_BITS - 1) / BITMAP_WORD_BITS)

struct bitmap {
	uintptr_t *words;
	size_t nbits;
	size_t nwords;
};

/**
 * BITMAP_DECLARE - 声明一个全局位图（含存储空间）
 * @name: 位图变量名
 * @nbits: 位数
 *
 * 若需要 static 作用域，请手动展开：
 *   static uintptr_t name_storage[BITMAP_WORDS(nbits)];
 *   static struct bitmap name = { .words = name_storage, ... };
 */
#define BITMAP_DECLARE(name, n)                                                \
	uintptr_t name##_storage[BITMAP_WORDS(n)];                         \
	struct bitmap name = {                                                 \
		.words = name##_storage,                                       \
		.nbits = (n),                                                  \
		.nwords = BITMAP_WORDS(n),                                     \
	}

#define BITMAP_DECLARE_STATIC(name, n)                                         \
	static uintptr_t name##_storage[BITMAP_WORDS(n)];                  \
	static struct bitmap name = {                                          \
		.words = name##_storage,                                       \
		.nbits = (n),                                                  \
		.nwords = BITMAP_WORDS(n),                                     \
	}

static __always_inline size_t bitmap_word_index(size_t bit)
{
	return bit / BITMAP_WORD_BITS;
}

static __always_inline size_t bitmap_word_offset(size_t bit)
{
	return bit % BITMAP_WORD_BITS;
}

static __always_inline uintptr_t bitmap_tail_mask(size_t nbits)
{
	const size_t tail = nbits % BITMAP_WORD_BITS;

	return tail == 0 ? ~0UL : (1UL << tail) - 1UL;
}

static __always_inline void bitmap_zero(struct bitmap *map)
{
	for (size_t i = 0; i < map->nwords; i++)
		map->words[i] = 0UL;
}

static __always_inline void bitmap_set(struct bitmap *map, size_t bit)
{
	map->words[bitmap_word_index(bit)] |= 1UL << bitmap_word_offset(bit);
}

static __always_inline void bitmap_clear(struct bitmap *map, size_t bit)
{
	map->words[bitmap_word_index(bit)] &= ~(1UL << bitmap_word_offset(bit));
}

static __always_inline bool bitmap_test(const struct bitmap *map, size_t bit)
{
	return !!(map->words[bitmap_word_index(bit)] &
		  (1UL << bitmap_word_offset(bit)));
}

static inline size_t bitmap_find_first_zero(const struct bitmap *map)
{
	for (size_t i = 0; i < map->nwords; i++) {
		uintptr_t word = ~map->words[i];

		if (i + 1 == map->nwords)
			word &= bitmap_tail_mask(map->nbits);

		if (word != 0UL)
			return i * BITMAP_WORD_BITS + (size_t)ctzl(word);
	}

	return map->nbits;
}

#endif
