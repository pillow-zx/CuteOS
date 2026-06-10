/*
 * kernel/exec.c - ELF 加载与用户态切换
 *
 * 功能：
 *   实现 exec_user_elf()，将内嵌的 ELF64 可执行文件加载到用户地址空间，
 *   并通过修改 trap_frame 使当前内核线程切换到用户态执行。
 *
 *   exec_user_elf 执行流程：
 *     1. 解析 ELF header，校验 magic / class / endianness
 *     2. 创建 mm_struct + 用户页表
 *     3. 遍历 PT_LOAD 段，逐页分配物理页，复制 filesz + 清零 memsz-filesz
 *     4. 为每个 PT_LOAD 段创建 VMA
 *     5. 分配用户栈页并映射
 *     6. fence.i 刷新指令缓存
 *     7. 设置 trap_frame（sepc=入口, sp=栈顶, SPP=0）
 *     8. 切换 satp → sfence → 设 sscratch → 跳转 __trapret
 *
 * 注意：
 *   此函数不返回。trap_frame 构造在当前 C 栈下方的空闲区域，
 *   完成后不能再发生函数调用。
 */

#include <kernel/printk.h>
#include <kernel/string.h>
#include <kernel/mm.h>
#include <kernel/buddy.h>
#include <kernel/elf.h>
#include <kernel/task.h>
#include <asm/page.h>
#include <asm/pte.h>
#include <asm/csr.h>
#include <asm/trap.h>

/* 用户地址空间常量 */
#define USER_STACK_TOP	0x80000000UL /* 用户栈顶地址 */
#define USER_STACK_BASE 0x7FFFF000UL /* 用户栈底地址（1 页） */

/* entry.S 中的 trap 返回入口 */
extern void __trapret(void);

/* ---- ELF 权限转换辅助函数 ---- */

static pte_t elf_flags_to_pte(uint32_t p_flags)
{
	bool r = p_flags & PF_R;
	bool w = p_flags & PF_W;
	bool x = p_flags & PF_X;

	if (r && w && x)
		return PTE_USER_RWX;
	if (r && w)
		return PTE_USER_RW;
	if (r && x)
		return PTE_USER_RX;
	if (r)
		return PTE_USER_R;

	/* 不应有不可读的段，fallback */
	return PTE_USER_RX;
}

static uint32_t elf_flags_to_vma(uint32_t p_flags)
{
	uint32_t flags = 0;
	if (p_flags & PF_R)
		flags |= VM_READ;
	if (p_flags & PF_W)
		flags |= VM_WRITE;
	if (p_flags & PF_X)
		flags |= VM_EXEC;
	return flags;
}

/*
 * exec_user_elf - 加载 ELF 可执行文件并切换到用户态
 * @bin_start: ELF 文件在内核中的起始地址
 * @bin_size:  ELF 文件的大小（字节）
 *
 * 为当前进程创建用户地址空间，解析 ELF 并加载 PT_LOAD 段，
 * 然后通过 __trapret → sret 进入用户态。
 * 此函数不返回。
 */
void exec_user_elf(void *bin_start, size_t bin_size)
{
	printk("exec: loading ELF binary (%lu bytes)\n",
	       (unsigned long)bin_size);

	/* ---- 1. ELF 基本校验 ---- */

	if (bin_size < sizeof(Elf64_Ehdr))
		panic("exec: ELF too small (%lu bytes)",
		      (unsigned long)bin_size);

	Elf64_Ehdr *ehdr = (Elf64_Ehdr *)bin_start;

	if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
	    ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
	    ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
	    ehdr->e_ident[EI_MAG3] != ELFMAG3)
		panic("exec: bad ELF magic");

	if (ehdr->e_ident[EI_CLASS] != ELFCLASS64)
		panic("exec: not ELF64");

	if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB)
		panic("exec: not little-endian ELF");

	if (ehdr->e_type != ET_EXEC)
		panic("exec: not ET_EXEC (type=%u)", ehdr->e_type);
	if (ehdr->e_machine != EM_RISCV)
		panic("exec: not RISC-V ELF (machine=%u)", ehdr->e_machine);

	if (ehdr->e_phoff == 0 || ehdr->e_phnum == 0)
		panic("exec: no program headers");

	if (ehdr->e_phoff + (uint64_t)ehdr->e_phnum * ehdr->e_phentsize >
	    bin_size)
		panic("exec: program headers out of bounds");

	/* ---- 2. 创建 mm_struct + 用户页表 ---- */

	struct mm_struct *mm = mm_alloc();
	if (!mm)
		panic("exec: failed to allocate mm_struct");

	mm->pgd = mm_create_user_pgd();
	if (!mm->pgd)
		panic("exec: failed to create user pgd");

	/* ---- 3. 遍历 PT_LOAD 段，映射到用户地址空间 ---- */

	auto *phdrs = (Elf64_Phdr *)((uint8_t *)bin_start + ehdr->e_phoff);
	uintptr_t first_vaddr = 0;
	uintptr_t last_end = 0;
	int vma_idx = 0;

	for (int i = 0; i < ehdr->e_phnum; i++) {
		Elf64_Phdr *ph = &phdrs[i];
		if (ph->p_type != PT_LOAD)
			continue;

		printk("exec: PT_LOAD vaddr=%p filesz=%lu memsz=%lu "
		       "flags=0x%x\n",
		       (void *)ph->p_vaddr, (unsigned long)ph->p_filesz,
		       (unsigned long)ph->p_memsz, ph->p_flags);

		if (ph->p_memsz == 0)
			continue;

		/* 校验段在文件范围内 */
		if (ph->p_offset + ph->p_filesz > bin_size)
			panic("exec: PT_LOAD segment out of file bounds");

		/* 校验地址在用户空间内 */
		uintptr_t seg_start = ph->p_vaddr;
		uintptr_t seg_end = ph->p_vaddr + ph->p_memsz;
		if (seg_end > USER_STACK_BASE)
			panic("exec: PT_LOAD segment exceeds user space");

		/* 记录第一个段的起始和最后一个段的结束 */
		if (first_vaddr == 0 || seg_start < first_vaddr)
			first_vaddr = seg_start;
		if (seg_end > last_end)
			last_end = seg_end;

		/* 逐页映射 */
		uintptr_t page_start = PFN_DOWN(seg_start) << PAGE_SHIFT;
		uintptr_t page_end = PFN_UP(seg_end) << PAGE_SHIFT;

		for (uintptr_t va = page_start; va < page_end;
		     va += PAGE_SIZE) {
			/* 分配物理页 */
			void *page = get_free_page(0);
			if (!page)
				panic("exec: OOM allocating page at %p",
				      (void *)va);
			memset(page, 0, PAGE_SIZE);

			/* 计算本页需要复制的范围 */
			uintptr_t src_start =
				ph->p_offset +
				(va < seg_start ? 0 : va - seg_start);
			uintptr_t src_end = ph->p_offset + ph->p_filesz;

			if (src_start < src_end) {
				/* 本页内需要复制数据 */
				uintptr_t copy_off =
					(va < seg_start) ? seg_start - va : 0;
				size_t copy_len = PAGE_SIZE - copy_off;
				uintptr_t remaining = src_end - src_start;
				if (copy_len > remaining)
					copy_len = remaining;

				memcpy((uint8_t *)page + copy_off,
				       (uint8_t *)bin_start + src_start,
				       copy_len);
			}
			/* memsz > filesz 的部分已被 memset 清零 */

			map_page(mm->pgd, va, __pa((uintptr_t)page),
				 elf_flags_to_pte(ph->p_flags));
		}

		/* 为此段创建 VMA */
		if (vma_idx >= NR_VMA)
			panic("exec: too many PT_LOAD segments (max %d)",
			      NR_VMA);

		struct vm_area_struct *vma = &mm->vma[vma_idx++];
		vma->vm_start = seg_start;
		vma->vm_end = seg_end;
		vma->vm_flags = elf_flags_to_vma(ph->p_flags);
		vma->used = true;
	}

	if (last_end == 0)
		panic("exec: no PT_LOAD segments found");

	fence_i();

	/* ---- 4. 设置 code_start/code_end/brk ---- */

	mm->code_start = first_vaddr;
	mm->code_end = PFN_UP(last_end) << PAGE_SHIFT;
	mm->brk = mm->code_end;

	/* ---- 5. 分配用户栈页 ---- */

	void *stack_page = get_free_page(0);
	if (!stack_page)
		panic("exec: failed to allocate user stack page");
	memset(stack_page, 0, PAGE_SIZE);

	map_page(mm->pgd, USER_STACK_BASE, __pa((uintptr_t)stack_page),
		 PTE_USER_RW);

	/* ---- 6. 挂载到当前进程 ---- */

	current->mm = mm;

	/* ---- 7. 准备 satp 切换参数 ---- */

	uintptr_t user_pgd_pa = __pa((uintptr_t)mm->pgd);
	uintptr_t satp_val = SATP_MODE_SV39 | (user_pgd_pa >> PAGE_SHIFT);

	printk("exec: switching to user mode (sepc=%p, sp=%p, pgd=%p, "
	       "brk=%p)\n",
	       (void *)ehdr->e_entry, (void *)USER_STACK_TOP,
	       (void *)user_pgd_pa, (void *)mm->brk);

	/* ---- 8. 构造 trap_frame 并切换到用户态 ---- */

	uintptr_t cur_sp;
	__asm__ volatile("mv %0, sp" : "=r"(cur_sp));
	cur_sp &= ~(uintptr_t)0xF;

	uintptr_t kstack_top = cur_sp - sizeof(uintptr_t);
	struct trap_frame *tf =
		(struct trap_frame *)(kstack_top - sizeof(struct trap_frame));
	memset(tf, 0, sizeof(struct trap_frame));

	BUG_ON((uintptr_t)tf < (uintptr_t)current->kstack);

	tf->sepc = ehdr->e_entry;   /* ELF 入口地址 */
	tf->sp = USER_STACK_TOP;    /* 用户栈顶 */
	tf->sstatus = SSTATUS_SPIE; /* SPP=0: sret 返回 U-mode */

	current->tf = tf;

	/*
	 * 在单个 asm 块中完成所有切换操作：
	 * 切换 satp → sfence.vma → 设 sscratch → 切 sp → 跳 __trapret
	 */
	__asm__ volatile("csrw    satp, %[satp]\n\t"
			 "sfence.vma zero, zero\n\t"
			 "csrw    sscratch, %[ksp]\n\t"
			 "mv      sp, %[tf]\n\t"
			 "j       __trapret\n\t"
			 :
			 : [satp] "r"(satp_val), [ksp] "r"(kstack_top),
			   [tf] "r"((uintptr_t)tf)
			 : "memory");

	unreachable();
}
