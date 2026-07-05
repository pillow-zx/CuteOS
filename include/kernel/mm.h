#ifndef _CUTEOS_KERNEL_MM_H
#define _CUTEOS_KERNEL_MM_H

/*
 * include/kernel/mm.h - 用户地址空间管理与虚拟内存公共接口
 *
 * mm_struct 的布局和 VMA 存储方式属于 mm/ 内部实现细节。外部子系统只能
 * 通过本文件声明的接口创建、复制、查询和修改用户地址空间。
 */

#include <kernel/compiler.h>
#include <kernel/types.h>
#include <kernel/fs.h>

/* ---- 函数声明 ---- */

/*
 * mm_create_user - 创建用户地址空间
 *
 * 分配 mm_struct 和用户页表，并应用用户页表特殊映射。失败返回 NULL。
 */
struct mm_struct *__must_check mm_create_user(void);

void mm_get(struct mm_struct *mm);
void mm_put(struct mm_struct *mm);
void mm_membarrier_register(struct mm_struct *mm, uint32_t cmd);
uint32_t __must_check mm_membarrier_registrations(const struct mm_struct *mm);

/*
 * dup_mm - 深拷贝用户地址空间
 * @oldmm: 父进程地址空间
 *
 * 复制 mm 元数据、VMA 数组以及所有已经映射的用户物理页。未映射的
 * lazy allocation 页面保持未映射，后续访问继续走缺页处理。
 */
struct mm_struct *__must_check dup_mm(struct mm_struct *oldmm);

/*
 * mm_user_satp - 返回该用户地址空间可安装到 task 的 SATP 值
 */
uintptr_t __must_check mm_user_satp(const struct mm_struct *mm);

/*
 * mm_user_page_resident - 查询用户页是否已经有有效用户 PTE 映射
 *
 * 地址没有 VMA 覆盖时返回 -ENOMEM；成功时通过 resident 返回驻留状态。
 */
int __must_check mm_user_page_resident(struct mm_struct *mm, uintptr_t addr,
				       bool *resident);

int __must_check mm_map_page(struct mm_struct *mm, uintptr_t va, void *page,
			     int prot);
int __must_check mm_map_segment(struct mm_struct *mm, uintptr_t start,
				uintptr_t end, int prot);
int __must_check mm_map_file_segment(struct mm_struct *mm, struct file *file,
				     uintptr_t start, uintptr_t end, int prot,
				     uint64_t file_offset);
int __must_check mm_add_stack(struct mm_struct *mm, void *stack_page);
int __must_check mm_finalize(struct mm_struct *mm, uintptr_t first_vaddr,
			     uintptr_t last_end);

/*
 * mm_brk - brk 内部实现
 * @mm:   进程地址空间描述符
 * @addr: 新的 brk 值，0 表示查询当前值
 *
 * 不允许缩小堆。仅更新 VMA，物理页由缺页处理按需分配。
 * 返回新的 brk 值，失败返回当前 brk 值。
 */
uintptr_t __must_check mm_brk(struct mm_struct *mm, uintptr_t addr);

/*
 * mm_mmap - 创建匿名用户映射
 * @mm:     进程地址空间描述符
 * @addr:   建议起始地址，0 表示由内核选择
 * @length: 映射长度，按页向上取整
 * @prot:   Linux PROT_* 权限位
 * @flags:  Linux MAP_* 标志
 * @fd:     file-backed mmap 的文件描述符；匿名映射忽略
 * @offset: file-backed mmap 的字节偏移，必须页对齐
 *
 * 返回映射起始地址，失败返回负 errno。
 */
ssize_t __must_check mm_mmap(struct mm_struct *mm, uintptr_t addr,
			     size_t length, int prot, int flags);
ssize_t __must_check mm_mmap_file(struct mm_struct *mm, uintptr_t addr,
				  size_t length, int prot, int flags, int fd,
				  uint64_t offset);

/*
 * mm_munmap - 解除用户映射
 * @mm:     进程地址空间描述符
 * @addr:   起始地址，必须页对齐
 * @length: 解除长度，按页向上取整
 *
 * 释放范围内已经分配的物理页，并调整受影响的 VMA。
 */
int __must_check mm_munmap(struct mm_struct *mm, uintptr_t addr, size_t length);

int __must_check mm_madvise(struct mm_struct *mm, uintptr_t addr, size_t len,
			    int advice);
int __must_check __nonnull(1) mm_mlock(struct mm_struct *mm, uintptr_t addr,
				       size_t len);
int __must_check __nonnull(1) mm_munlock(struct mm_struct *mm, uintptr_t addr,
					 size_t len);

/*
 * mm_mprotect - 修改地址范围内 VMA 权限并更新 PTE
 * @mm:   进程地址空间描述符
 * @addr: 起始地址（必须页对齐）
 * @len:  字节长度（按页向上取整）
 * @prot: PROT_READ | PROT_WRITE | PROT_EXEC 的组合
 *
 * 在边界处分裂 VMA，更新受影响 VMA 的 vm_flags，并对已映射的页
 * 更新 PTE 权限位后刷新 TLB。
 */
int __must_check mm_mprotect(struct mm_struct *mm, uintptr_t addr, size_t len,
			     int prot);
ssize_t __must_check mm_mremap(struct mm_struct *mm, uintptr_t old_addr,
			       size_t old_size, size_t new_size, int flags,
			       uintptr_t new_addr);
int __must_check mm_msync(struct mm_struct *mm, uintptr_t addr, size_t len,
			  int flags);

/* ---- 用户空间安全访问（uaccess） ---- */

/*
 * access_ok - 检查用户空间地址范围是否合法
 * @addr: 用户空间起始地址
 * @size: 要访问的字节数
 *
 * 返回 true 表示合法，false 表示非法。
 */
bool __must_check access_ok(const void *addr, size_t size);

int __must_check user_range_probe(const void *addr, size_t size, bool write);

/*
 * copy_to_user - 从内核空间复制数据到用户空间
 * @to:   用户空间目标地址
 * @from: 内核空间源地址
 * @n:    要复制的字节数
 *
 * 返回未能复制的字节数（0 表示全部成功）。
 */
size_t __must_check copy_to_user(void *to, const void *from, size_t n)
	__access(write_only, 1, 3) __access(read_only, 2, 3);

/*
 * copy_from_user - 从用户空间复制数据到内核空间
 * @to:   内核空间目标地址
 * @from: 用户空间源地址
 * @n:    要复制的字节数
 *
 * 返回未能复制的字节数（0 表示全部成功）。
 */
size_t __must_check copy_from_user(void *to, const void *from, size_t n)
	__access(write_only, 1, 3) __access(read_only, 2, 3);

/*
 * strncpy_from_user - 复制 NUL 结尾的用户字符串到内核缓冲区
 * @dst:    内核空间目标缓冲区
 * @src:    用户空间源字符串
 * @maxlen: 目标缓冲区大小，包含结尾 NUL
 *
 * 成功时返回不含 NUL 的字符串长度；失败返回负 errno。
 */
ssize_t __must_check strncpy_from_user(char *dst, const char *src,
				       size_t maxlen)
__access(write_only, 1, 3) __access(read_only, 2, 3);

/*
 * do_page_fault - 缺页异常处理总入口
 * @tf: 指向当前 trap_frame
 *
 * 从 trap facade 读取故障地址和缺页类型，
 * 查找 VMA 判断合法性。合法则分配物理页并映射，非法则 do_exit。
 */
void __nonnull(1) do_page_fault(struct trap_frame *tf);

#endif
