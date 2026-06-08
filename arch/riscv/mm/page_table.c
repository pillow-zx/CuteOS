/*
 * arch/riscv/mm/page_table.c - Sv39 三级页表操作
 *
 * 功能：
 *   实现 RISC-V Sv39 分页机制的页表创建、映射和地址转换。
 *   Sv39 使用三级页表（PGD → PMD → PTE），每级 9 bits 索引，
 *   页大小 4KB，虚拟地址高 25 位为符号扩展，虚拟地址空间 512GB。
 *
 * 主要函数：
 *   kernel_pagetable_init() - 初始化内核全局页表。两步映射：
 *     步骤一：为 256MB DRAM 物理内存建立 4KB 细粒度页映射
 *             (恒等映射 + 高地址映射)，权限为 R+W+X（内核代码/数据）。
 *             恒等映射与高地址映射共享同一组 PMD/PTE 页。
 *     步骤二：pgd[0] 建立 1GB mega page 映射 MMIO 设备空间
 *             (0x10000000 区域)，权限为 R+W（设备寄存器不可执行）。
 *
 *   map_page(pgtbl, va, pa, perm) - 建立单个 4KB 页的虚拟地址到物理地址映射。
 *             自动分配中间页表页（PMD），无需预先创建。
 *
 *   walk_page_table(pgtbl, va) - 三级页表遍历。从 PGD → PMD → PTE 逐级查找，
 *             返回最终 PTE 的指针（虚拟地址）。若中间级页表不存在则分配新页。
 *             是 map_page / unmap_page / va_to_pa 的核心实现。
 *
 * Sv39 地址分解（4KB 页）：
 *   [63:39] 符号扩展  [38:30] PGD 索引  [29:21] PMD 索引
 *   [20:12] PTE 索引  [11:0]  页内偏移
 *
 * PTE 格式：
 *   [63:54] 保留  [53:10] PPN  [9:8] RSW  [7] D  [6] A
 *   [5] G  [4] U  [3] X  [2] W  [1] R  [0] V
 *
 * 注意事项：
 *   - 页表页使用 early bump allocator 从 _end 之后分配（不依赖 buddy）
 *   - buddy_init() 通过 page_table_mem_end() 获取空闲内存起始位置
 */

#include <kernel/printk.h>
#include <kernel/string.h>
#include <asm/page.h>
#include <asm/pte.h>
#include <asm/csr.h>

/* ---- PTE 地址转换辅助宏 ---- */

/* 从 PTE 提取物理地址: PA = PPN << 12, PPN = PTE[53:10] */
#define PTE_TO_PA(pte)	(((pte) >> 10) << 12)

/* 从物理地址构造 PPN 部分: PTE[53:10] = PA >> 12 */
#define PA_TO_PTE(pa)	(((pte_t)(pa) >> PAGE_SHIFT) << 10)

/* ---- Early bump allocator ---- */

/*
 * 早期内存分配指针，从 _end 开始向上增长。
 * 每次分配一页（4KB），用于页表页。
 * buddy_init() 通过 page_table_mem_end() 读取此值，
 * 确定空闲内存起始位置。
 */
static char *early_alloc_ptr;

void *page_table_mem_end(void)
{
	return early_alloc_ptr;
}

/*
 * early_alloc_page - 从 _end 之后分配一个 4KB 对齐的物理页
 *
 * 返回页的虚拟地址（已清零）。
 * 仅在 buddy 初始化前由页表初始化代码使用。
 */
static void *early_alloc_page(void)
{
	void *p = early_alloc_ptr;
	early_alloc_ptr += PAGE_SIZE;
	memset(p, 0, PAGE_SIZE);
	return p;
}

/* ---- 页表遍历与映射 ---- */

/*
 * walk_page_table - 遍历/创建 Sv39 三级页表，返回叶子 PTE 指针
 * @pgd:   PGD 页的虚拟地址
 * @va:    虚拟地址
 * @alloc: 是否允许分配缺失的中间页表页
 *
 * 遍历 PGD → PMD → PTE，若中间级不存在且 alloc 为真，
 * 则分配新页并安装。返回最终 PTE 条目的虚拟地址指针。
 * 若 alloc 为假且中间级缺失，返回 NULL。
 */
static pte_t *walk_page_table(pte_t *pgd, uintptr_t va, bool alloc)
{
	/* L2: PGD 索引 [38:30] */
	int idx2 = (va >> 30) & 0x1FF;
	pte_t *pte2 = &pgd[idx2];

	if (!(*pte2 & PTE_V)) {
		if (!alloc)
			return NULL;
		void *new_page = early_alloc_page();
		/* 安装下一级页表指针: PPN | PTE_TABLE (V=1, R=W=X=0) */
		*pte2 = PA_TO_PTE(__pa((uintptr_t)new_page)) | PTE_TABLE;
	}

	/* L1: PMD — 从 PTE 提取物理地址，转回虚拟地址以读写 */
	pte_t *pmd = (pte_t *)__va(PTE_TO_PA(*pte2));
	int idx1 = (va >> 21) & 0x1FF;
	pte_t *pte1 = &pmd[idx1];

	if (!(*pte1 & PTE_V)) {
		if (!alloc)
			return NULL;
		void *new_page = early_alloc_page();
		*pte1 = PA_TO_PTE(__pa((uintptr_t)new_page)) | PTE_TABLE;
	}

	/* L0: PTE — 返回叶子 PTE 条目的指针 */
	pte_t *pt = (pte_t *)__va(PTE_TO_PA(*pte1));
	int idx0 = (va >> 12) & 0x1FF;
	return &pt[idx0];
}

/*
 * map_page - 建立单个 4KB 页的映射
 * @pgd:  PGD 页虚拟地址
 * @va:   虚拟地址（必须页对齐）
 * @pa:   物理地址（必须页对齐）
 * @perm: 叶子 PTE 权限位（可直接传入 PTE_KERN_* / PTE_USER_*，需包含 PTE_V）
 */
static void map_page(pte_t *pgd, uintptr_t va, uintptr_t pa, pte_t perm)
{
	pte_t *pte = walk_page_table(pgd, va, true);
	if (!pte)
		panic("map_page: walk failed for va=%p", (void *)va);
	*pte = PA_TO_PTE(pa) | perm;
}

/* ---- 公共接口 ---- */

/*
 * kernel_pagetable_init - 初始化正式内核页表并切换 satp
 *
 * 页表布局：
 *   PGD[258] → PMD → PTE pages  高地址映射 (KERNEL_VBASE + DRAM_BASE)
 *   PGD[2]   ────┘               恒等映射 (DRAM_BASE)，共享 PMD/PTE
 *   PGD[0]   → 1GB mega page     MMIO 设备空间 (0x0 ~ 0x3FFFFFFF)
 *
 * 约束：
 *   - 切换时新旧页表映射相同的虚拟地址，无缝切换
 *   - 使用 early bump allocator，不依赖 buddy
 *   - PGD[258] 和 PGD[2] 共享同一 PMD 页，节省 128 个 PTE 页
 */
void kernel_pagetable_init(void)
{
	extern char _end[];

	/* 初始化 early allocator：从 _end 开始，4KB 对齐 */
	uintptr_t end_addr = (uintptr_t)_end;
	early_alloc_ptr = (char *)((end_addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));

	/* 1. 分配 PGD 页 */
	pte_t *pgd = (pte_t *)early_alloc_page();

	/* 2. 映射 256MB DRAM（高地址 KERNEL_VBASE + PA → PA）
	 *    walk_page_table 自动分配 1 个 PMD 页 + 128 个 PTE 页 */
	printk("page_table: mapping %dMB DRAM with 4KB pages...\n",
	       (int)(DRAM_SIZE >> 20));

	for (uintptr_t pa = DRAM_BASE; pa < DRAM_BASE + DRAM_SIZE;
	     pa += PAGE_SIZE) {
		uintptr_t va = KERNEL_VBASE + pa;
		map_page(pgd, va, pa, PTE_KERN_RWX);
	}

	/* 3. 恒等映射：PGD[2] 复用 PGD[258] 的 PMD 页
	 *    两者 PTE 条目完全相同（同一组物理页），无需额外分配 */
	int idx_high = ((KERNEL_VBASE + DRAM_BASE) >> 30) & 0x1FF;
	int idx_id = (DRAM_BASE >> 30) & 0x1FF;
	pgd[idx_id] = pgd[idx_high];

	/* 4. MMIO 映射：1GB mega page at PGD[0]，R+W（不可执行） */
	pgd[0] = PA_TO_PTE(0UL) | PTE_KERN_RW;

	/* 5. 切换到新页表 */
	uintptr_t pgd_pa = __pa((uintptr_t)pgd);
	uintptr_t satp_val = SATP_MODE_SV39 | (pgd_pa >> PAGE_SHIFT);

	csr_write(satp, satp_val);
	sfence_vma_all();

	printk("page_table: switched to kernel page table (pgd=%p, "
	       "early_alloc=%dKB)\n",
	       (void *)pgd_pa,
	       (int)((uintptr_t)early_alloc_ptr - (uintptr_t)_end) / 1024);
}
