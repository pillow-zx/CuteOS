/*
 * mm/user_map.c - 用户页表特殊映射注册表
 *
 * 保持 mm_create_user_pgd() 对平台设备和高层子系统无感。注册表固定大小，
 * 符合当前单核、启动期注册、用户页表创建期应用的内核假设。
 */

#include <kernel/user_map.h>
#include <kernel/errno.h>

#define NR_USER_MAPS 4

struct user_map {
	const char *name;
	user_map_fn_t map;
};

static struct user_map user_maps[NR_USER_MAPS];

int user_map_register(const char *name, user_map_fn_t map)
{
	if (!name || !map)
		return -EINVAL;

	for (int i = 0; i < NR_USER_MAPS; i++) {
		if (user_maps[i].map == map)
			return 0;
	}

	for (int i = 0; i < NR_USER_MAPS; i++) {
		if (user_maps[i].map)
			continue;
		user_maps[i].name = name;
		user_maps[i].map = map;
		return 0;
	}

	return -ENOMEM;
}

int user_map_apply(pte_t *pgd)
{
	int ret;

	if (!pgd)
		return -EINVAL;

	for (int i = 0; i < NR_USER_MAPS; i++) {
		if (!user_maps[i].map)
			continue;

		ret = user_maps[i].map(pgd);
		if (ret < 0)
			return ret;
	}

	return 0;
}
