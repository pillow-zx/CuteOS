#ifndef _CUTEOS_ASM_PTE_H
#define _CUTEOS_ASM_PTE_H

/*
 * include/asm/pte.h - RISC-V Sv39 页表项位定义与类型
 *
 * 根据 RISC-V 特权级规范定义 PTE 权限位、页表指针标志 PTE_TABLE，
 * 以及 pte_t 类型别名。
 *
 * Permission bits (bit position):
 *   PTE_V  = 1   (Valid)
 *   PTE_R  = 2   (Read)
 *   PTE_W  = 4   (Write)
 *   PTE_X  = 8   (Execute)
 *   PTE_U  = 16  (User)
 *   PTE_G  = 32  (Global)
 *   PTE_A  = 64  (Accessed)
 *   PTE_D  = 128 (Dirty)
 *
 * Derived flags:
 *   PTE_TABLE - Indicates a page table pointer (V=1, R=W=X=0)
 *
 * Types:
 *   pte_t - Page table entry type (uint64_t)
 */

#include <kernel/types.h>
#include <kernel/bitops.h>
#include <asm/page.h>

typedef uint64_t pte_t;

#define PTE_V BIT(0)
#define PTE_R BIT(1)
#define PTE_W BIT(2)
#define PTE_X BIT(3)
#define PTE_U BIT(4)
#define PTE_G BIT(5)
#define PTE_A BIT(6)
#define PTE_D BIT(7)

#define PTE_TABLE PTE_V

#define PTE_KERN_RW  (PTE_V | PTE_R | PTE_W | PTE_G | PTE_A | PTE_D)
#define PTE_KERN_RWX (PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A | PTE_D)

#define PTE_USER_R   (PTE_V | PTE_R | PTE_U | PTE_A | PTE_D)
#define PTE_USER_RX  (PTE_V | PTE_R | PTE_X | PTE_U | PTE_A | PTE_D)
#define PTE_USER_RW  (PTE_V | PTE_R | PTE_W | PTE_U | PTE_A | PTE_D)
#define PTE_USER_RWX (PTE_V | PTE_R | PTE_W | PTE_X | PTE_U | PTE_A | PTE_D)

#define PTE_PPN_SHIFT	       10
#define PTE_PPN(pte)	       ((pte) >> PTE_PPN_SHIFT)
#define PTE_CREATE(pfn, flags) (((uint64_t)(pfn) << PTE_PPN_SHIFT) | (flags))
#define PTE_TO_PA(pte)	       ((uint64_t)PTE_PPN(pte) << PAGE_SHIFT)
#define PA_TO_PTE(pa)	       (PTE_CREATE(PHYS_PFN(pa), 0))

static __always_inline bool pte_present(pte_t pte)
{
	return (pte & PTE_V) != 0;
}

static __always_inline bool pte_user_page(pte_t pte)
{
	return (pte & PTE_U) != 0 && PTE_TO_PA(pte) != 0;
}

static __always_inline paddr_t pte_to_pa(pte_t pte)
{
	return PTE_TO_PA(pte);
}

pte_t *__must_check arch_pt_lookup_current(uintptr_t va);

/*
 * arch_pt_write_current - 修改当前 SATP 页表中的已有映射
 * @va:   虚拟地址（必须已存在有效 PTE 映射）
 * @pa:   新的物理地址
 * @perm: 新的 PTE 权限位
 *
 * 读取 satp 获取当前页表，覆盖 PTE 并执行 sfence.vma 刷新 TLB。
 * 若 @va 无有效映射则 panic。仅适用于内核恒等映射页表；
 * 引入进程独立地址空间后需重新设计。
 */
void arch_pt_write_current(uintptr_t va, uintptr_t pa, pte_t perm);

/*
 * map_page - 建立单个 4KB 页的映射
 * @root: root page table 页虚拟地址
 * @va:   虚拟地址（必须页对齐）
 * @pa:   物理地址（必须页对齐）
 * @perm: 叶子 PTE 权限位
 */
int __must_check __nonnull(1) map_page(pte_t *root, uintptr_t va,
                                uintptr_t pa, uint64_t perm);

/*
 * arch_pt_use_buddy - 将页表分配器切换到 buddy
 *
 * 在 buddy_init() 之后调用一次。
 */
void arch_pt_use_buddy(void);

pte_t *__must_check arch_current_pt(void);

/*
 * arch_kernel_satp - 返回全局内核页表的 satp 值
 *
 * 退出当前用户进程并销毁其 mm 之前，必须切回此页表，避免释放当前
 * satp 正在引用的用户 root page table 后继续执行。
 */
uintptr_t __must_check arch_kernel_satp(void);

/*
 * arch_pt_lookup - 遍历 Sv39 三级页表，返回叶子 PTE 指针
 * @root:  root page table 页的虚拟地址
 * @va:    虚拟地址
 */
pte_t *__must_check __nonnull(1) arch_pt_lookup(pte_t *root, uintptr_t va);

#ifdef CONFIG_KERNEL_TEST
static __always_inline __must_check
	__nonnull(1) pte_t *arch_pt_walk(pte_t *root, uintptr_t va, bool alloc)
{
	if (alloc)
		return NULL;
	return arch_pt_lookup(root, va);
}

void arch_pt_test_fail_alloc_after(uint32_t successful_allocs);
void arch_pt_test_clear_alloc_failure(void);
#endif

#endif
