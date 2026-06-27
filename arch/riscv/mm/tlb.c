/*
 * arch/riscv/mm/tlb.c - TLB 刷新 (sfence.vma)
 *
 * 功能：
 *   TLB（Translation Lookaside Buffer）是页表缓存的硬件结构。
 *   在修改页表后必须刷新生效的 TLB 条目，否则 CPU 可能使用过时的
 *   地址转换结果。RISC-V 通过 sfence.vma 指令实现 TLB 刷新。
 *
 * 主要函数：
 *   arch_tlb_flush_all()  - 刷新全部 TLB 条目（sfence.vma zero, zero）。
 *                       用于内核页表全局切换或大范围映射变更后，
 *                       如 arch_pt_init() 完成后的全局刷新。
 *
 *   arch_tlb_flush_page(va) - 刷新指定虚拟地址对应的 TLB 条目
 *                       （sfence.vma va, zero）。用于 unmap 或权限修改
 *                       单个页面时，性能优于全量刷新。
 *
 * 注意事项：
 *   - sfence.vma 隐含内存排序屏障，确保之前的页表写操作对后续地址翻译可见
 *   - 在 SMP 环境下，其他 hart 的 TLB 需通过 IPI (SBI remote sfence) 刷新
 */

#include <kernel/types.h>
#include <asm/csr.h>

static inline void local_sfence_vma(uintptr_t addr, uintptr_t asid)
{
	asm volatile("sfence.vma %0, %1" : : "r"(addr), "r"(asid) : "memory");
}

void arch_tlb_flush_all(void)
{
	// addr=0, asid=0 => flush all mappings for current hart
	local_sfence_vma(0, 0);
}

void arch_tlb_flush_page(uintptr_t va)
{
	local_sfence_vma(va, 0);
}

/*
 * arch_icache_flush - 刷新指令缓存（I-Cache）
 *
 * 在修改内存中的指令后调用，确保修改后的指令对取指可见。
 * 典型场景：加载 ELF 代码页后、动态代码生成后。
 *
 * fence.i 保证后续取指能观察到之前所有对指令内存的写入。
 */
void arch_icache_flush(void)
{
	asm volatile("fence.i" : : : "memory");
}
