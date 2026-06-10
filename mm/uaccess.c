/*
 * mm/uaccess.c - 用户空间内存访问
 *
 * 功能：
 *   提供内核与用户空间之间安全的数据拷贝函数。
 *   access_ok() 检查地址范围是否合法（无溢出 + 不超过 TASK_SIZE）。
 *   copy_to_user / copy_from_user 在 SUM 位保护下进行 memcpy。
 *
 * 当前实现：
 *   不做异常处理。若用户地址无效（未映射），访问将触发缺页异常。
 *   由于 sys_brk 采用立即分配策略，合法堆地址的页面已全部映射。
 *
 * 后续计划：
 *   Stage 6 可添加异常处理机制，当用户地址无效时优雅地返回 -EFAULT
 *   而非触发内核崩溃。
 *
 * 主要函数：
 *   access_ok(addr, size)        - 检查用户地址范围是否合法
 *   copy_to_user(to, from, n)    - 从内核空间复制数据到用户空间
 *   copy_from_user(to, from, n)  - 从用户空间复制数据到内核空间
 */

#include <kernel/mm.h>
#include <kernel/string.h>
#include <kernel/syscall.h>
#include <asm/page.h>

bool access_ok(const void *addr, size_t size)
{
	uintptr_t a = (uintptr_t)addr;

	if (size == 0)
		return true;

	if (a + size < a) /* 溢出检查 */
		return false;

	if (a + size > TASK_SIZE)
		return false;

	return true;
}

size_t copy_to_user(void *to, const void *from, size_t n)
{
	if (!access_ok(to, n))
		return n;

	bool had_sum = user_access_begin();
	memcpy(to, from, n);
	user_access_end(had_sum);

	return 0;
}

size_t copy_from_user(void *to, const void *from, size_t n)
{
	if (!access_ok(from, n))
		return n;

	bool had_sum = user_access_begin();
	memcpy(to, from, n);
	user_access_end(had_sum);

	return 0;
}
