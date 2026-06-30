#ifndef _CUTEOS_KERNEL_CLEANUP_H
#define _CUTEOS_KERNEL_CLEANUP_H

#include <kernel/compiler.h>
#include <kernel/types.h>

#define __PASTE(a, b)	    a##b
#define __PASTE2(a, b)	    __PASTE(a, b)
#define __UNIQUE_ID(prefix) __PASTE2(prefix, __COUNTER__)

#define CLEANUP_DEFINE(_name, _type, _cleanup)                                 \
	static __always_inline void __cleanup_##_name(void *p)                 \
	{                                                                      \
		_type _T = *(_type *)p;                                        \
		_cleanup;                                                      \
	}

#define __cleanup_with(_name) __cleanup(__cleanup_##_name)

#define __get_and_null(p, nullvalue)                                           \
	statement_expr(auto __ptr = &(p); auto __val = *__ptr;                 \
		       *__ptr = (nullvalue); __val;)

static __always_inline __must_check const volatile void *
__cleanup_must_check(const volatile void *val)
{
	return val;
}

#define cleanup_take_ptr(p)                                                    \
	((type_of(p))__cleanup_must_check(                                     \
		(const volatile void *)__get_and_null(p, NULL)))

#define cleanup_return_ptr(p) return cleanup_take_ptr(p)

#define cleanup_forget_ptr(p) ((void)__get_and_null(p, NULL))

#define SCOPE_DEFINE(_name, _type, exit_expr, init_expr, init_args...)         \
	typedef _type scope_##_name##_t;                                       \
	static __always_inline void scope_##_name##_exit(_type *p)             \
	{                                                                      \
		_type _T = *p;                                                 \
		exit_expr;                                                     \
	}                                                                      \
	static __always_inline _type scope_##_name##_init(init_args)           \
	{                                                                      \
		_type t = init_expr;                                           \
		return t;                                                      \
	}

#define SCOPE_EXTEND(_name, ext, init_expr, init_args...)                      \
	typedef scope_##_name##_t scope_##_name##ext##_t;                      \
	static __always_inline void scope_##_name##ext##_exit(                 \
		scope_##_name##ext##_t *p)                                     \
	{                                                                      \
		scope_##_name##_exit(p);                                       \
	}                                                                      \
	static __always_inline scope_##_name##_t scope_##_name##ext##_init(    \
		init_args)                                                     \
	{                                                                      \
		scope_##_name##_t t = init_expr;                               \
		return t;                                                      \
	}

#define SCOPE_VAR(_name, var)                                                  \
	scope_##_name##_t var __cleanup(scope_##_name##_exit) =                \
		scope_##_name##_init

#define SCOPE_VAR_INIT(_name, var, init_expr)                                  \
	scope_##_name##_t var __cleanup(scope_##_name##_exit) = (init_expr)

#define SCOPE_GUARD_DEFINE(_name, _type, _lock, _unlock)                       \
	SCOPE_DEFINE(_name, _type, _unlock,                                    \
		     statement_expr(_type _T = _T_init; _lock; _T;),           \
		     _type _T_init)

#define scope_guard(_name) SCOPE_VAR(_name, __UNIQUE_ID(scope_guard_))

#define __with_scope(_name, var, label, args...)                               \
	for (SCOPE_VAR(_name, var)(args);; ({ goto label; }))                  \
		if (0) {                                                       \
		label:                                                         \
			break;                                                 \
		} else

#define with_guard(_name, args...)                                             \
	__with_scope(_name, __UNIQUE_ID(scope_), __UNIQUE_ID(label_), args)

#endif
