#ifndef _CUTEOS_KERNEL_ATOMIC_H
#define _CUTEOS_KERNEL_ATOMIC_H

#include <kernel/sync.h>

typedef struct {
	volatile int counter;
} atomic_t;

#define ATOMIC_INIT(i) { .counter = (i) }

static __always_inline int atomic_read(const atomic_t *v)
{
	return v->counter;
}

static __always_inline void atomic_set(atomic_t *v, int i)
{
	irq_flags_t flags = local_irq_save();

	v->counter = i;
	local_irq_restore(flags);
}

static __always_inline int atomic_add_return(atomic_t *v, int i)
{
	irq_flags_t flags = local_irq_save();
	int ret;

	v->counter += i;
	ret = v->counter;
	local_irq_restore(flags);
	return ret;
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

#endif
