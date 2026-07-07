#ifndef _CUTEOS_KERNEL_USER_MAP_H
#define _CUTEOS_KERNEL_USER_MAP_H

/*
 * include/kernel/user_map.h - 用户页表特殊映射注册点
 */

#include <kernel/compiler.h>
#include <kernel/types.h>
#include <kernel/pgtable.h>

typedef int (*user_map_fn_t)(pte_t *pgd);

int __must_check user_map_register(const char *name, user_map_fn_t map);
int __must_check user_map_register_reserved(const char *name, vaddr_t start,
						    vaddr_t end, user_map_fn_t map);
int __must_check user_map_reserve(const char *name, vaddr_t start, vaddr_t end);
int __must_check user_map_apply(pte_t *pgd);
bool __must_check __pure user_map_reserved_contains(vaddr_t addr);
bool __must_check __pure user_map_reserved_overlaps(vaddr_t start, vaddr_t end);

#endif
