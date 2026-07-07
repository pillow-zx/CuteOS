#ifndef _CUTEOS_UAPI_FUTEX_H
#define _CUTEOS_UAPI_FUTEX_H

/**
 * @file futex.h
 * @brief Linux futex command bits and robust-list UAPI layouts.
 */

#define FUTEX_WAIT	   0
#define FUTEX_WAKE	   1
#define FUTEX_PRIVATE_FLAG 128
#define FUTEX_CMD_MASK	   0x7f
#define FUTEX_WAITERS	   0x80000000U
#define FUTEX_OWNER_DIED   0x40000000U
#define FUTEX_TID_MASK	   0x3fffffffU

/**
 * @struct robust_list
 * @brief Intrusive userspace node used by Linux robust futex lists.
 *
 * @par Fields
 * - @c next: Next userspace robust-list node.
 */
struct robust_list {
	struct robust_list *next;
};

/**
 * @struct robust_list_head
 * @brief Userspace robust futex list head registered per task.
 *
 * @par Fields
 * - @c list: Circular list head.
 * - @c futex_offset: Offset from node address to futex word.
 * - @c list_op_pending: Node being modified.
 */
struct robust_list_head {
	struct robust_list list;
	long futex_offset;
	struct robust_list *list_op_pending;
};

#undef offsetof
#define offsetof(t, d) __builtin_offsetof(t, d)

_Static_assert(sizeof(struct robust_list_head) == 24,
	       "robust_list_head ABI size mismatch");
_Static_assert(offsetof(struct robust_list_head, list_op_pending) == 16,
	       "robust_list_head list_op_pending ABI offset mismatch");

#endif
