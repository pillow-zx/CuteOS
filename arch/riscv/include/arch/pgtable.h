#ifndef _CUTEOS_ARCH_RISCV_PGTABLE_H
#define _CUTEOS_ARCH_RISCV_PGTABLE_H

#include <kernel/compiler.h>
#include <kernel/types.h>
#include <arch/page.h>
#include <asm/csr.h>
#include <asm/pte.h>

typedef pte_t pgprot_t;

void pagetable_use_buddy(void);
pte_t *__must_check current_pt(void);
pte_t *__must_check kernel_pt(void);
uintptr_t __must_check kernel_satp(void);
pte_t *__must_check pagetable_lookup_current(uintptr_t va);
pte_t *__must_check __nonnull(1)
	pagetable_lookup(pte_t *root, uintptr_t va);
void pagetable_write_current(uintptr_t va, uintptr_t pa, pte_t perm);
int __must_check __nonnull(1)
	map_page(pte_t *root, uintptr_t va, uintptr_t pa, uint64_t perm);

static inline __must_check __const pgprot_t
pgprot_user(bool read, bool write, bool exec)
{
	pgprot_t flags = PTE_V | PTE_U | PTE_A | PTE_D;

	if (read)
		flags |= PTE_R;
	if (write)
		flags |= PTE_R | PTE_W;
	if (exec)
		flags |= PTE_X;

	return flags;
}

static inline __must_check __const pgprot_t
pgprot_kernel(bool read, bool write, bool exec)
{
	pgprot_t flags = PTE_V | PTE_G | PTE_A | PTE_D;

	if (read)
		flags |= PTE_R;
	if (write)
		flags |= PTE_R | PTE_W;
	if (exec)
		flags |= PTE_X;

	return flags;
}

static inline __must_check __pure bool pte_is_present(pte_t pte)
{
	return asm_pte_present(pte);
}

static inline __must_check __pure bool pte_is_user_page(pte_t pte)
{
	return asm_pte_user_page(pte);
}

static inline __must_check __pure bool
pte_allows_user_read(pte_t pte)
{
	return pte_is_present(pte) && (pte & PTE_U) && (pte & PTE_R);
}

static inline __must_check __pure bool
pte_allows_user_write(pte_t pte)
{
	return pte_is_present(pte) && (pte & PTE_U) && (pte & PTE_W);
}

static inline __must_check __pure bool
pte_allows_user_exec(pte_t pte)
{
	return pte_is_present(pte) && (pte & PTE_U) && (pte & PTE_X);
}

static inline __must_check __pure paddr_t pte_phys_addr(pte_t pte)
{
	return asm_pte_to_pa(pte);
}

static inline __must_check __pure pgprot_t
pte_leaf_prot(pte_t pte)
{
	return pte & MASK(PTE_PPN_SHIFT);
}

static inline __must_check __pure pte_t
pte_make(paddr_t pa, pgprot_t prot)
{
	return PA_TO_PTE(pa) | prot;
}

static inline __nonnull(1) void pte_clear_present(pte_t *pte)
{
	*pte &= ~PTE_V;
}

static inline __must_check __pure uintptr_t
pgtable_make_user_token(const pte_t *pgd)
{
	return SATP_MODE_SV39 | (__pa((uintptr_t)pgd) >> PAGE_SHIFT);
}

static __always_inline void pgtable_activate_kernel(void)
{
	csr_write(satp, kernel_satp());
	tlb_flush_all();
}

static __always_inline void flush_tlb_all(void)
{
	tlb_flush_all();
}

static __always_inline void flush_tlb_page(uintptr_t va)
{
	tlb_flush_page(va);
}

#ifdef KERNEL_SELFTEST
static inline __must_check __nonnull(1) pte_t *pagetable_walk(
	pte_t *root, uintptr_t va, bool alloc)
{
	if (alloc)
		return NULL;
	return pagetable_lookup(root, va);
}

void pagetable_test_fail_alloc_after(uint32_t successful_allocs);
void pagetable_test_clear_alloc_failure(void);
#endif

#endif
