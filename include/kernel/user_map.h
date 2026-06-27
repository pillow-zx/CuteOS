#ifndef _CUTEOS_KERNEL_USER_MAP_H
#define _CUTEOS_KERNEL_USER_MAP_H

/*
 * include/kernel/user_map.h - 用户页表特殊映射注册点
 *
 * mm_create_user_pgd() 只负责创建页表和复制内核高半区映射。必须出现在
 * 每个用户页表中的平台或子系统特殊映射，通过本注册点接入。
 */

#include <kernel/compiler.h>
#include <kernel/types.h>
#include <asm/pte.h>

typedef int (*user_map_fn_t)(pte_t *pgd);

int __must_check user_map_register(const char *name, user_map_fn_t map);
int __must_check user_map_apply(pte_t *pgd);

#endif /* _CUTEOS_KERNEL_USER_MAP_H */
