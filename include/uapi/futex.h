#ifndef _CUTEOS_UAPI_FUTEX_H
#define _CUTEOS_UAPI_FUTEX_H

#define FUTEX_WAIT	   0
#define FUTEX_WAKE	   1
#define FUTEX_PRIVATE_FLAG 128
#define FUTEX_CMD_MASK	   0x7f
#define FUTEX_WAITERS	   0x80000000U
#define FUTEX_OWNER_DIED   0x40000000U
#define FUTEX_TID_MASK	   0x3fffffffU

struct robust_list {
	struct robust_list *next;
};

struct robust_list_head {
	struct robust_list list;
	long futex_offset;
	struct robust_list *list_op_pending;
};

#endif
