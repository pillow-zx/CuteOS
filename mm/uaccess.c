/*
 * mm/uaccess.c - 用户空间内存访问
 *
 * 功能：
 *   提供内核与用户空间之间安全的数据拷贝函数。
 *   access_ok() 检查地址范围是否合法（无溢出 + 不超过 TASK_SIZE）。
 *   copy_to_user / copy_from_user 在 SUM 位保护下进行 memcpy。
 *
 * 当前实现：
 *   copy_to_user / copy_from_user 先经 user_range_probe 预探整个范围：
 *   按 VMA 分段逐页校验权限，合法但未映射的页通过 fault_in_user_range
 *   按需 fault-in；
 *   范围非法（无 VMA / 权限不符 / 无法映射）时返回未拷贝字节数 n，
 *   调用方据此返回 -EFAULT，而非触发内核崩溃。
 *
 * 后续计划：
 *   预探是 O(页数 × VMA) 的保守方案；Stage 6 可改为异常表 fixup
 *   （先访问、触发异常再回滚），省去逐页预探的开销。
 *
 * 主要函数：
 *   access_ok(addr, size)        - 检查用户地址范围是否合法
 *   user_range_probe(addr, n, w) - 预探范围：校验 VMA 权限并 fault-in
 *   copy_to_user(to, from, n)    - 从内核空间复制数据到用户空间
 *   copy_from_user(to, from, n)  - 从用户空间复制数据到内核空间
 *   strncpy_from_user(dst, src, n) - 复制 NUL 结尾用户字符串
 */

#include <kernel/mm.h>
#include <kernel/errno.h>
#include <kernel/string.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <asm/page.h>

bool access_ok(const void *addr, size_t size)
{
	vaddr_t a = (vaddr_t)addr;

	if (size == 0)
		return true;

	if (a + size < a) /* 溢出检查 */
		return false;

	if (a + size > TASK_SIZE)
		return false;

	return true;
}

static size_t min_size(size_t a, size_t b)
{
	return a < b ? a : b;
}

int user_range_probe(const void *addr, size_t size, bool write)
{
	struct mm_struct *mm;
	int access;

	if (size == 0)
		return 0;
	if (!access_ok(addr, size))
		return -EFAULT;
	mm = task_mm(current);
	if (!mm)
		return -EFAULT;

	access = write ? USER_FAULT_WRITE : USER_FAULT_READ;
	return fault_in_user_range(mm, (uintptr_t)addr, size, access);
}

size_t copy_to_user(void *to, const void *from, size_t n)
{
	if (user_range_probe(to, n, true) < 0)
		return n;

	bool had_sum = user_access_begin();
	memcpy(to, from, n);
	user_access_end(had_sum);

	return 0;
}

size_t copy_from_user(void *to, const void *from, size_t n)
{
	if (user_range_probe(from, n, false) < 0)
		return n;

	bool had_sum = user_access_begin();
	memcpy(to, from, n);
	user_access_end(had_sum);

	return 0;
}

ssize_t strncpy_from_user(char *dst, const char *src, size_t maxlen)
{
	size_t done = 0;

	if (!dst)
		return -EINVAL;
	if (!src)
		return -EFAULT;
	if (maxlen == 0)
		return -ENAMETOOLONG;

	while (done < maxlen) {
		uintptr_t addr = (uintptr_t)src + done;
		struct vm_area_struct *vma;
		size_t chunk = min_size(maxlen - done,
					PAGE_SIZE - (addr & (PAGE_SIZE - 1)));
		struct mm_struct *mm;

		if (!access_ok((const void *)addr, 1))
			return -EFAULT;
		mm = task_mm(current);
		if (!mm)
			return -EFAULT;
		mm_lock(mm);
		vma = find_vma(mm, addr);
		if (!vma || !(vma->vm_flags & VM_READ)) {
			mm_unlock(mm);
			return -EFAULT;
		}
		chunk = min_size(chunk, vma->vm_end - addr);
		mm_unlock(mm);
		if (chunk == 0)
			return -EFAULT;

		if (user_range_probe((const void *)addr, chunk, false) < 0)
			return -EFAULT;

		bool had_sum = user_access_begin();
		for (size_t i = 0; i < chunk; i++) {
			char c = ((const char *)addr)[i];

			dst[done + i] = c;
			if (c == '\0') {
				user_access_end(had_sum);
				return (ssize_t)(done + i);
			}
		}
		user_access_end(had_sum);
		done += chunk;
	}

	dst[maxlen - 1] = '\0';
	return -ENAMETOOLONG;
}
