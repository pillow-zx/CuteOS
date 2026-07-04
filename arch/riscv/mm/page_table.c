/*
 * arch/riscv/mm/page_table.c - Sv39 三级页表操作
 */

#include <kernel/printk.h>
#include <kernel/buddy.h>
#include <kernel/errno.h>
#include <asm/page.h>
#include <asm/pte.h>
#include <asm/csr.h>

/* ---- 页表页分配器 ---- */

/*
 * 页表页分配函数指针。
 * 初始使用 early bump allocator（buddy 初始化前），
 * buddy_init() 完成后通过 arch_pt_use_buddy() 切换到 buddy 分配。
 */
typedef void *(*page_alloc_fn)(void);
static page_alloc_fn pt_alloc;
static uintptr_t ksatp_val;

#ifdef CONFIG_KERNEL_TEST
static int32_t pt_alloc_fail_after = -1;
#endif

/* ---- Early bump allocator ---- */

/*
 * 早期内存分配指针，从 _end 开始向上增长。
 * 每次分配一页（4KB），用于页表页。
 * buddy_init() 通过 arch_bootmem_end() 读取此值，
 * 确定空闲内存起始位置。
 */
static char *early_alloc_ptr;

void *arch_bootmem_end(void)
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

/*
 * buddy_alloc_page - 使用 buddy 分配器分配一个清零的 4KB 页
 *
 * 供 buddy_init 之后的页表操作使用（如创建用户页表）。
 */
static void *buddy_alloc_page(void)
{
	void *p = get_free_page(0);
	if (p)
		memset(p, 0, PAGE_SIZE);
	return p;
}

static void *pt_alloc_page(void)
{
	BUG_ON(!pt_alloc);

#ifdef CONFIG_KERNEL_TEST
	if (pt_alloc_fail_after == 0)
		return NULL;
	if (pt_alloc_fail_after > 0)
		pt_alloc_fail_after--;
#endif

	return pt_alloc();
}

#ifdef CONFIG_KERNEL_TEST
void arch_pt_test_fail_alloc_after(uint32_t successful_allocs)
{
	pt_alloc_fail_after = (int32_t)successful_allocs;
}

void arch_pt_test_clear_alloc_failure(void)
{
	pt_alloc_fail_after = -1;
}
#endif

/*
 * arch_pt_use_buddy - 将页表分配器切换到 buddy
 *
 * 在 buddy_init() 之后调用一次。
 */
void arch_pt_use_buddy(void)
{
	pt_alloc = buddy_alloc_page;
}

/* ---- 页表遍历与映射 ---- */

static bool pte_is_leaf(pte_t pte)
{
	return (pte & (PTE_R | PTE_W | PTE_X)) != 0;
}

static int pt_walk_create(pte_t *root, vaddr_t va, pte_t **out)
{
	bool new_l1 = false;

	int idx2 = (va >> 30) & 0x1FF;
	pte_t *l2e = &root[idx2];
	pte_t *l1;

	if (!(*l2e & PTE_V)) {
		l1 = pt_alloc_page();
		if (!l1)
			return -ENOMEM;
		*l2e = PA_TO_PTE(__pa((uintptr_t)l1)) | PTE_TABLE;
		new_l1 = true;
	} else {
		if (pte_is_leaf(*l2e))
			return -EINVAL;
		l1 = (pte_t *)__va(PTE_TO_PA(*l2e));
	}

	int idx1 = (va >> 21) & 0x1FF;
	pte_t *l1e = &l1[idx1];
	pte_t *l0;

	if (!(*l1e & PTE_V)) {
		l0 = pt_alloc_page();
		if (!l0) {
			if (new_l1) {
				*l2e = 0;
				free_page(l1, 0);
			}
			return -ENOMEM;
		}
		*l1e = PA_TO_PTE(__pa((uintptr_t)l0)) | PTE_TABLE;
	} else {
		if (pte_is_leaf(*l1e))
			return -EINVAL;
		l0 = (pte_t *)__va(PTE_TO_PA(*l1e));
	}

	int idx0 = (va >> 12) & 0x1FF;
	*out = &l0[idx0];
	return 0;
}

/*
 * arch_pt_walk - 遍历/创建 Sv39 三级页表，返回叶子 PTE 指针
 * @root:  root page table 页的虚拟地址
 * @va:    虚拟地址
 * @alloc: 是否允许分配缺失的中间页表页
 *
 * 遍历 L2 → L1 → L0，若中间级不存在且 alloc 为真，
 * 则分配新页并安装。返回最终 PTE 条目的虚拟地址指针。
 * 若 alloc 为假且中间级缺失，返回 NULL。
 */
pte_t *arch_pt_lookup(pte_t *root, vaddr_t va)
{
	/* L2: root page table index [38:30] */
	int idx2 = (va >> 30) & 0x1FF;
	pte_t *l2e = &root[idx2];

	if (!(*l2e & PTE_V))
		return NULL;
	if (pte_is_leaf(*l2e))
		return NULL;

	/* L1: 从 L2 PTE 提取物理地址，转回虚拟地址以读写 */
	pte_t *l1 = (pte_t *)__va(PTE_TO_PA(*l2e));
	int idx1 = (va >> 21) & 0x1FF;
	pte_t *l1e = &l1[idx1];

	if (!(*l1e & PTE_V))
		return NULL;
	if (pte_is_leaf(*l1e))
		return NULL;

	/* L0: 返回叶子 PTE 条目的指针 */
	pte_t *l0 = (pte_t *)__va(PTE_TO_PA(*l1e));
	int idx0 = (va >> 12) & 0x1FF;
	return &l0[idx0];
}

/*
 * map_page - 建立单个 4KB 页的映射
 * @root: root page table 页虚拟地址
 * @va:   虚拟地址（必须页对齐）
 * @pa:   物理地址（必须页对齐）
 * @perm: 叶子 PTE 权限位（可直接传入 PTE_KERN_* / PTE_USER_*，需包含 PTE_V）
 */
int map_page(pte_t *root, vaddr_t va, paddr_t pa, uint64_t perm)
{
	pte_t *pte;
	int ret;

	if ((va & (PAGE_SIZE - 1)) || (pa & (PAGE_SIZE - 1)))
		return -EINVAL;
	if (!(perm & PTE_V))
		return -EINVAL;

	ret = pt_walk_create(root, va, &pte);
	if (ret < 0)
		return ret;
	*pte = PA_TO_PTE(pa) | perm;
	return 0;
}

uintptr_t kernel_satp(void)
{
	return ksatp_val;
}

pte_t *current_pt(void)
{
	uintptr_t satp_val = csr_read(satp);
	uintptr_t root_pa = (satp_val & SATP_PPN_MASK) << PAGE_SHIFT;

	return (pte_t *)__va(root_pa);
}

pte_t *kernel_pt(void)
{
	uintptr_t satp_val = kernel_satp();
	uintptr_t root_pa = (satp_val & SATP_PPN_MASK) << PAGE_SHIFT;

	return (pte_t *)__va(root_pa);
}

pte_t *arch_pt_lookup_current(uintptr_t va)
{
	return arch_pt_lookup(current_pt(), va);
}

void arch_pt_write_current(uintptr_t va, uintptr_t pa, pte_t perm)
{
	pte_t *pte = arch_pt_lookup_current(va);

	if (!pte || !(*pte & PTE_V))
		panic("arch_pt_write_current: no mapping for va=%p",
		      (void *)va);

	*pte = PA_TO_PTE(pa) | perm;
	arch_tlb_flush_page(va);
}

/* ---- 公共接口 ---- */

/*
 * arch_pt_init - 初始化正式内核页表并切换 satp
 *
 * 页表布局：
 *   L2[258] → L1 → L0 pages  高地址映射 (KERNEL_VBASE + DRAM_BASE)
 *   L2[2]   ───┘             恒等映射 (DRAM_BASE)，共享 L1/L0 页表
 *   L2[0]   → 1GB mega page  MMIO 设备空间 (0x0 ~ 0x3FFFFFFF)
 *
 * 约束：
 *   - 切换时新旧页表映射相同的虚拟地址，无缝切换
 *   - 使用 early bump allocator，不依赖 buddy
 *   - L2[258] 和 L2[2] 共享同一 L1 页，节省 128 个 L0 页表页
 */
void arch_pt_init(void)
{
	extern char _end[];

	/* 初始化 early allocator：从 _end 开始，4KB 对齐 */
	paddr_t end_addr = (paddr_t)_end;
	early_alloc_ptr =
		(char *)((end_addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));

	/* 页表分配器初始使用 early allocator */
	pt_alloc = early_alloc_page;

	/* 1. 分配 root page table 页 */
	pte_t *root = (pte_t *)early_alloc_page();

	/* 2. 映射 DRAM（高地址 KERNEL_VBASE + PA → PA）
	 *    arch_pt_walk 自动分配 1 个 L1 页 + 128 个 L0 页 */
	pr_info("page_table: mapping %dMB DRAM with 4KB pages...\n",
		(int)(DRAM_SIZE >> 20));

	for (paddr_t pa = DRAM_BASE; pa < DRAM_BASE + DRAM_SIZE;
	     pa += PAGE_SIZE) {
		vaddr_t va = KERNEL_VBASE + pa;
		BUG_ON(map_page(root, va, pa, PTE_KERN_RWX) < 0);
	}

	/* 3. 恒等映射：L2[2] 复用 L2[258] 的 L1 页
	 *    两者 PTE 条目完全相同（同一组物理页），无需额外分配 */
	int idx_high = ((KERNEL_VBASE + DRAM_BASE) >> 30) & 0x1FF;
	int idx_id = (DRAM_BASE >> 30) & 0x1FF;
	root[idx_id] = root[idx_high];

	/* 4. MMIO 映射：1GB mega page at L2[0]，R+W（不可执行） */
	root[0] = PA_TO_PTE(0UL) | PTE_KERN_RW;

	/* 5. 切换到新页表 */
	paddr_t root_pa = __pa((uintptr_t)root);
	uintptr_t satp_val = SATP_MODE_SV39 | (root_pa >> PAGE_SHIFT);
	ksatp_val = satp_val;

	csr_write(satp, satp_val);
	arch_tlb_flush_all();

	pr_info("page_table: switched to kernel page table (root=%p, "
		"early_alloc=%dKB)\n",
		(void *)root_pa,
		(int)((uintptr_t)early_alloc_ptr - (uintptr_t)_end) / 1024);
}
