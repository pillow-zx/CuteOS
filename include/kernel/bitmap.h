/**
 * @file bitmap.h
 * @brief Fixed-size bitmap helpers.
 */

#ifndef _CUTEOS_KERNEL_BITMAP_H
#define _CUTEOS_KERNEL_BITMAP_H

#include <kernel/types.h>
#include <kernel/compiler.h>

/**
 * @def BITMAP_WORD_BITS
 * @brief Number of bits represented by one uintptr_t bitmap word.
 */
constexpr size_t BITMAP_WORD_BITS = sizeof(uintptr_t) * 8U;

/**
 * @def BITMAP_WORDS
 * @brief Number of uintptr_t storage words required for @p nbits bits.
 */
#define BITMAP_WORDS(nbits)                                                    \
	(((nbits) + BITMAP_WORD_BITS - 1) / BITMAP_WORD_BITS)

/**
 * @struct bitmap
 * @brief Bitmap view over caller-owned uintptr_t storage.
 *
 * @par Fields
 * - @c words: Storage words.
 * - @c nbits: Number of valid bits.
 * - @c nwords: Number of words in @ref words.
 */
struct bitmap {
	uintptr_t *words;
	size_t nbits;
	size_t nwords;
};

/**
 * @def BITMAP_DECLARE
 * @brief Declare automatic bitmap storage and a bitmap descriptor.
 * @param name Base variable name.
 * @param n Number of valid bits.
 */
#define BITMAP_DECLARE(name, n)                                                \
	uintptr_t name##_storage[BITMAP_WORDS(n)];                             \
	struct bitmap name = {                                                 \
		.words = name##_storage,                                       \
		.nbits = (n),                                                  \
		.nwords = BITMAP_WORDS(n),                                     \
	}

/**
 * @def BITMAP_DECLARE_STATIC
 * @brief Declare static bitmap storage and a bitmap descriptor.
 * @param name Base variable name.
 * @param n Number of valid bits.
 */
#define BITMAP_DECLARE_STATIC(name, n)                                         \
	static uintptr_t name##_storage[BITMAP_WORDS(n)];                      \
	static struct bitmap name = {                                          \
		.words = name##_storage,                                       \
		.nbits = (n),                                                  \
		.nwords = BITMAP_WORDS(n),                                     \
	}

static inline size_t bitmap_word_index(size_t bit)
{
	return bit / BITMAP_WORD_BITS;
}

static inline size_t bitmap_word_offset(size_t bit)
{
	return bit % BITMAP_WORD_BITS;
}

static inline uintptr_t bitmap_tail_mask(size_t nbits)
{
	const size_t tail = nbits % BITMAP_WORD_BITS;

	return tail == 0 ? ~0UL : (1UL << tail) - 1UL;
}

static inline void bitmap_zero(struct bitmap *map)
{
	for (size_t i = 0; i < map->nwords; i++)
		map->words[i] = 0UL;
}

static inline void bitmap_set(struct bitmap *map, size_t bit)
{
	map->words[bitmap_word_index(bit)] |= 1UL << bitmap_word_offset(bit);
}

static inline void bitmap_clear(struct bitmap *map, size_t bit)
{
	map->words[bitmap_word_index(bit)] &= ~(1UL << bitmap_word_offset(bit));
}

static inline bool bitmap_test(const struct bitmap *map, size_t bit)
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
