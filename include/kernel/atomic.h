#ifndef _CUTEOS_KERNEL_ATOMIC_H
#define _CUTEOS_KERNEL_ATOMIC_H

#include <kernel/sync.h>
#include <kernel/compiler.h>

typedef struct {
	volatile int __aligned(sizeof(int)) counter;
} atomic_t;

#define ATOMIC_INIT(i) {.counter = (i)}

static __always_inline int atomic_read(const atomic_t *v)
{
	return atomic_load_n(&v->counter, ATOMIC_SEQ_CST);
}

static __always_inline void atomic_set(atomic_t *v, int i)
{
	atomic_store_n(&v->counter, i, ATOMIC_SEQ_CST);
}

static __always_inline int atomic_add_return(atomic_t *v, int i)
{
	return atomic_add_fetch(&v->counter, i, ATOMIC_SEQ_CST);
}

static __always_inline void atomic_add(atomic_t *v, int i)
{
	(void)atomic_add_return(v, i);
}

static __always_inline int atomic_inc_return(atomic_t *v)
{
	return atomic_add_return(v, 1);
}

static __always_inline int atomic_dec_return(atomic_t *v)
{
	return atomic_add_return(v, -1);
}

static __always_inline void atomic_inc(atomic_t *v)
{
	(void)atomic_inc_return(v);
}

static __always_inline void atomic_dec(atomic_t *v)
{
	(void)atomic_dec_return(v);
}

static __always_inline bool atomic_dec_and_test(atomic_t *v)
{
	return atomic_dec_return(v) == 0;
}

static __always_inline int atomic_cmpxchg(atomic_t *v, int old, int new)
{
	atomic_compare_exchange_n(&v->counter, &old, new, false, ATOMIC_SEQ_CST,
				  ATOMIC_SEQ_CST);
	return old;
}

#endif
