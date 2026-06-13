#ifndef _CUTEOS_KERNEL_MM_H
#define _CUTEOS_KERNEL_MM_H

/*
 * include/kernel/mm.h - 用户地址空间管理与虚拟内存
 *
 * 声明进程级虚拟内存管理所需的结构体与常量。每个用户进程拥有一个
 * mm_struct 描述其完整地址空间，内部划分为若干 vm_area_struct 区域。
 *
 * 设计决策：
 *   - VMA 使用固定大小数组（NR_VMA=16），不用链表/红黑树
 *   - 用户栈纳入 VMA 管理（缺页处理需要）
 *   - mm_struct 通过 kmalloc 分配，内核线程 mm=NULL
 */

#include <kernel/types.h>
#include <asm/pte.h>

struct trap_frame; /* 前向声明，避免循环依赖 */

/* ---- VMA 权限标志 ---- */

#define VM_READ  0x01  /* 可读 */
#define VM_WRITE 0x02  /* 可写 */
#define VM_EXEC  0x04  /* 可执行 */

/* ---- VMA 类型标志 ---- */

#define VMA_CODE 0x01  /* 代码段 (ELF PT_LOAD) */
#define VMA_HEAP 0x02  /* 堆 (brk 扩展) */
#define VMA_STACK 0x04 /* 栈 */

/* ---- VMA 数量上限 ---- */

#define NR_VMA 16

/* ---- vm_area_struct - 一段连续的虚拟内存区域 ---- */

struct vm_area_struct {
	uintptr_t vm_start; /* 区域起始虚拟地址（含） */
	uintptr_t vm_end;   /* 区域结束虚拟地址（不含） */
	uint32_t vm_flags;  /* VM_READ | VM_WRITE | VM_EXEC */
	uint32_t vm_type;   /* VMA_CODE | VMA_HEAP | VMA_STACK */
	bool used;          /* 该槽位是否在用 */
};

/* ---- mm_struct - 进程地址空间描述符 ---- */

struct mm_struct {
	pte_t *pgd;                          /* 用户页表根（PGD 页虚拟地址） */
	uintptr_t brk;                       /* 当前堆顶 */
	uintptr_t code_start;                /* 代码段起始 */
	uintptr_t code_end;                  /* 代码段结束 */
	struct vm_area_struct vma[NR_VMA];   /* VMA 固定数组 */
};

/* ---- 函数声明 ---- */

/*
 * mm_alloc - 分配并初始化一个 mm_struct
 *
 * 返回初始化后的 mm_struct 指针，失败返回 NULL。
 */
struct mm_struct *mm_alloc(void);

/*
 * dup_mm - 深拷贝用户地址空间
 * @oldmm: 父进程地址空间
 *
 * 复制 mm 元数据、VMA 数组以及所有已经映射的用户物理页。未映射的
 * lazy allocation 页面保持未映射，后续访问继续走缺页处理。
 */
struct mm_struct *dup_mm(struct mm_struct *oldmm);

/*
 * mm_destroy - 销毁用户地址空间
 * @mm: 要销毁的 mm_struct
 *
 * 释放用户页表中所有映射的物理页、页表页，最后 kfree mm_struct。
 * 不释放内核共享的映射（PGD 高 256 项指向的内核页表页）。
 */
void mm_destroy(struct mm_struct *mm);

/*
 * mm_create_user_pgd - 创建用户页表并复制内核映射
 *
 * 分配 PGD 页，清零，复制内核高地址映射（PGD[256-511]），
 * 映射 UART MMIO。返回 PGD 虚拟地址，失败返回 NULL。
 */
pte_t *mm_create_user_pgd(void);

/*
 * find_vma - 查找包含指定地址的 VMA
 * @mm:   进程地址空间描述符
 * @addr: 要查找的虚拟地址
 *
 * 线性扫描 VMA 数组，返回满足 vm_start <= addr < vm_end 的 VMA 指针。
 * 未找到返回 NULL。
 */
struct vm_area_struct *find_vma(struct mm_struct *mm, uintptr_t addr);

/*
 * mm_brk - brk 内部实现
 * @mm:   进程地址空间描述符
 * @addr: 新的 brk 值，0 表示查询当前值
 *
 * 不允许缩小堆。立即分配物理页并建立映射（PTE_USER_RW）。
 * 返回新的 brk 值，失败返回当前 brk 值。
 */
uintptr_t mm_brk(struct mm_struct *mm, uintptr_t addr);

/* ---- 用户空间安全访问（uaccess） ---- */

/*
 * access_ok - 检查用户空间地址范围是否合法
 * @addr: 用户空间起始地址
 * @size: 要访问的字节数
 *
 * 返回 true 表示合法，false 表示非法。
 */
bool access_ok(const void *addr, size_t size);

/*
 * copy_to_user - 从内核空间复制数据到用户空间
 * @to:   用户空间目标地址
 * @from: 内核空间源地址
 * @n:    要复制的字节数
 *
 * 返回未能复制的字节数（0 表示全部成功）。
 */
size_t copy_to_user(void *to, const void *from, size_t n);

/*
 * copy_from_user - 从用户空间复制数据到内核空间
 * @to:   内核空间目标地址
 * @from: 用户空间源地址
 * @n:    要复制的字节数
 *
 * 返回未能复制的字节数（0 表示全部成功）。
 */
size_t copy_from_user(void *to, const void *from, size_t n);

/* ---- 缺页处理 ---- */

struct trap_frame;

/*
 * do_page_fault - 缺页异常处理总入口
 * @tf: 指向当前 trap_frame
 *
 * 从 tf->stval 读取故障地址，从 tf->scause 区分缺页类型，
 * 查找 VMA 判断合法性。合法则分配物理页并映射，非法则 do_exit。
 */
void do_page_fault(struct trap_frame *tf);

#endif
