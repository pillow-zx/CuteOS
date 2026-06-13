/*
 * kernel/exec.c - ELF 加载与 execve 替换语义
 *
 * 当前阶段只支持内核内嵌的用户 ELF，不从路径读取磁盘文件。
 * exec 成功时替换当前进程的 mm/satp/trap_frame，保留 PID、父子关系、
 * 打开的 fd 和信号占位状态。
 */

#include <kernel/elf.h>
#include <kernel/errno.h>
#include <kernel/buddy.h>
#include <kernel/mm.h>
#include <kernel/printk.h>
#include <kernel/string.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <asm/csr.h>
#include <asm/page.h>
#include <asm/pte.h>
#include <asm/trap.h>

#define EXEC_MAX_ARGS	  16
#define EXEC_MAX_ARG_LEN  128
#define EXEC_MAX_PATH_LEN 64

struct exec_args {
	int argc;
	char argv[EXEC_MAX_ARGS][EXEC_MAX_ARG_LEN];
};

extern char _user_init_start[];
extern char _user_init_end[];

static pte_t elf_flags_to_pte(uint32_t p_flags)
{
	const bool r = p_flags & PF_R;
	const bool w = p_flags & PF_W;
	const bool x = p_flags & PF_X;

	if (r && w && x)
		return PTE_USER_RWX;
	if (r && w)
		return PTE_USER_RW;
	if (r && x)
		return PTE_USER_RX;
	if (r)
		return PTE_USER_R;

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

static int copy_user_string(char *dst, const char *user, size_t max_len)
{
	if (!user || max_len == 0)
		return -EFAULT;

	bool had_sum = user_access_begin();
	for (size_t i = 0; i < max_len; i++) {
		if (!access_ok(user + i, 1)) {
			user_access_end(had_sum);
			return -EFAULT;
		}

		char c = user[i];
		dst[i] = c;
		if (c == '\0') {
			user_access_end(had_sum);
			return 0;
		}
	}
	user_access_end(had_sum);

	dst[max_len - 1] = '\0';
	return -E2BIG;
}

static int copy_exec_args(const char *const *uargv, struct exec_args *args)
{
	memset(args, 0, sizeof(*args));

	if (!uargv)
		return 0;

	for (int i = 0; i < EXEC_MAX_ARGS; i++) {
		const char *uarg;

		if (copy_from_user(&uarg, &uargv[i], sizeof(uarg)) != 0)
			return -EFAULT;
		if (!uarg)
			return 0;

		int ret =
			copy_user_string(args->argv[i], uarg, EXEC_MAX_ARG_LEN);
		if (ret < 0)
			return ret;

		args->argc++;
	}

	const char *extra;
	if (copy_from_user(&extra, &uargv[EXEC_MAX_ARGS], sizeof(extra)) != 0)
		return -EFAULT;
	if (extra)
		return -E2BIG;

	return 0;
}

static int lookup_exec_image(const char *path, void **bin_start,
			     size_t *bin_size)
{
	if (strcmp(path, "init") != 0 && strcmp(path, "/init") != 0 &&
	    strcmp(path, "/bin/init") != 0)
		return -ENOENT;

	*bin_start = _user_init_start;
	*bin_size = (size_t)(_user_init_end - _user_init_start);
	return 0;
}

static void destroy_partial_mm(struct mm_struct *mm)
{
	if (mm)
		mm_destroy(mm);
}

static int setup_user_stack(struct mm_struct *mm, const struct exec_args *args,
			    vaddr_t *sp_out)
{
	void *stack_page = get_free_page(0);
	if (!stack_page)
		return -ENOMEM;
	memset(stack_page, 0, PAGE_SIZE);

	map_page(mm->pgd, USER_STACK_BASE, __pa((uintptr_t)stack_page),
		 PTE_USER_RW);

	uintptr_t user_argv[EXEC_MAX_ARGS + 1];
	uintptr_t sp = USER_STACK_TOP;

	for (int i = args->argc - 1; i >= 0; i--) {
		size_t len = strlen(args->argv[i]) + 1;
		if (sp < USER_STACK_BASE + len)
			return -E2BIG;

		sp -= len;
		memcpy((uint8_t *)stack_page + (sp - USER_STACK_BASE),
		       args->argv[i], len);
		user_argv[i] = sp;
	}
	user_argv[args->argc] = 0;

	sp &= ~(uintptr_t)0xf;

	size_t argv_bytes = (size_t)(args->argc + 1) * sizeof(uintptr_t);
	size_t frame_bytes = sizeof(uintptr_t) + argv_bytes;
	size_t padding = frame_bytes & 0xf ? 16 - (frame_bytes & 0xf) : 0;

	if (sp < USER_STACK_BASE + frame_bytes + padding)
		return -E2BIG;

	sp -= frame_bytes + padding;

	uint8_t *frame = (uint8_t *)stack_page + (sp - USER_STACK_BASE);
	*(uintptr_t *)frame = (uintptr_t)args->argc;
	memcpy(frame + sizeof(uintptr_t), user_argv, argv_bytes);

	*sp_out = sp;
	return 0;
}

static int load_elf_image(void *bin_start, size_t bin_size,
			  const struct exec_args *args,
			  struct mm_struct **mm_out, vaddr_t *entry_out,
			  vaddr_t *sp_out)
{
	*mm_out = NULL;
	*entry_out = 0;
	*sp_out = 0;

	printk("exec: loading ELF binary (%lu bytes)\n", (size_t)bin_size);

	if (bin_size < sizeof(Elf64_Ehdr))
		return -ENOEXEC;

	Elf64_Ehdr *ehdr = (Elf64_Ehdr *)bin_start;

	if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
	    ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
	    ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
	    ehdr->e_ident[EI_MAG3] != ELFMAG3)
		return -ENOEXEC;
	if (ehdr->e_ident[EI_CLASS] != ELFCLASS64)
		return -ENOEXEC;
	if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB)
		return -ENOEXEC;
	if (ehdr->e_type != ET_EXEC || ehdr->e_machine != EM_RISCV)
		return -ENOEXEC;
	if (ehdr->e_phoff == 0 || ehdr->e_phnum == 0)
		return -ENOEXEC;
	if (ehdr->e_phoff + (uint64_t)ehdr->e_phnum * ehdr->e_phentsize >
	    bin_size)
		return -ENOEXEC;

	struct mm_struct *mm = mm_alloc();
	if (!mm)
		return -ENOMEM;

	mm->pgd = mm_create_user_pgd();
	if (!mm->pgd) {
		destroy_partial_mm(mm);
		return -ENOMEM;
	}

	Elf64_Phdr *phdrs =
		(Elf64_Phdr *)((uint8_t *)bin_start + ehdr->e_phoff);
	vaddr_t first_vaddr = 0;
	vaddr_t last_end = 0;
	int vma_idx = 0;

	for (int i = 0; i < ehdr->e_phnum; i++) {
		Elf64_Phdr *ph = &phdrs[i];
		if (ph->p_type != PT_LOAD)
			continue;
		if (ph->p_memsz == 0)
			continue;

		printk("exec: PT_LOAD vaddr=%p filesz=%llu memsz=%llu "
		       "flags=0x%x\n",
		       (void *)ph->p_vaddr, ph->p_filesz, ph->p_memsz,
		       ph->p_flags);

		if (ph->p_offset + ph->p_filesz > bin_size) {
			destroy_partial_mm(mm);
			return -ENOEXEC;
		}

		vaddr_t seg_start = ph->p_vaddr;
		vaddr_t seg_end = ph->p_vaddr + ph->p_memsz;
		if (seg_end < seg_start || seg_end > USER_STACK_BASE) {
			destroy_partial_mm(mm);
			return -ENOEXEC;
		}

		if (first_vaddr == 0 || seg_start < first_vaddr)
			first_vaddr = seg_start;
		if (seg_end > last_end)
			last_end = seg_end;

		vaddr_t page_start = PFN_DOWN(seg_start) << PAGE_SHIFT;
		vaddr_t page_end = PFN_UP(seg_end) << PAGE_SHIFT;

		for (vaddr_t va = page_start; va < page_end; va += PAGE_SIZE) {
			void *page = get_free_page(0);
			if (!page) {
				destroy_partial_mm(mm);
				return -ENOMEM;
			}
			memset(page, 0, PAGE_SIZE);

			vaddr_t src_start =
				ph->p_offset +
				(va < seg_start ? 0 : va - seg_start);
			vaddr_t src_end = ph->p_offset + ph->p_filesz;

			if (src_start < src_end) {
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

			map_page(mm->pgd, va, __pa((uintptr_t)page),
				 elf_flags_to_pte(ph->p_flags));
		}

		if (vma_idx >= NR_VMA) {
			destroy_partial_mm(mm);
			return -E2BIG;
		}

		struct vm_area_struct *vma = &mm->vma[vma_idx++];
		vma->vm_start = seg_start;
		vma->vm_end = seg_end;
		vma->vm_flags = elf_flags_to_vma(ph->p_flags);
		vma->vm_type = VMA_CODE;
		vma->used = true;
	}

	if (last_end == 0) {
		destroy_partial_mm(mm);
		return -ENOEXEC;
	}

	fence_i();

	mm->code_start = first_vaddr;
	mm->code_end = PFN_UP(last_end) << PAGE_SHIFT;
	mm->brk = mm->code_end;

	vaddr_t user_sp;
	int ret = setup_user_stack(mm, args, &user_sp);
	if (ret < 0) {
		destroy_partial_mm(mm);
		return ret;
	}

	if (vma_idx >= NR_VMA) {
		destroy_partial_mm(mm);
		return -E2BIG;
	}

	struct vm_area_struct *stack_vma = &mm->vma[vma_idx++];
	stack_vma->vm_start = USER_STACK_BASE;
	stack_vma->vm_end = USER_STACK_TOP;
	stack_vma->vm_flags = VM_READ | VM_WRITE;
	stack_vma->vm_type = VMA_STACK;
	stack_vma->used = true;

	*mm_out = mm;
	*entry_out = ehdr->e_entry;
	*sp_out = user_sp;
	return 0;
}

static void flush_old_exec(struct mm_struct *oldmm)
{
	if (!oldmm)
		return;

	csr_write(satp, kernel_satp());
	sfence_vma_all();
	mm_destroy(oldmm);
}

static void install_exec_mm(struct mm_struct *mm, struct trap_frame *tf,
			    vaddr_t entry, vaddr_t sp)
{
	struct mm_struct *oldmm = current->mm;
	paddr_t user_pgd_pa = __pa((uintptr_t)mm->pgd);
	uintptr_t satp_val = SATP_MODE_SV39 | (user_pgd_pa >> PAGE_SHIFT);

	current->mm = mm;
	current->satp = satp_val;
	current->tf = tf;

	flush_old_exec(oldmm);

	printk("exec: switching to user mode (sepc=%p, sp=%p, pgd=%p, "
	       "brk=%p)\n",
	       (void *)entry, (void *)sp, (void *)user_pgd_pa, (void *)mm->brk);

	memset(tf, 0, sizeof(*tf));
	tf->sepc = entry;
	tf->sp = sp;
	tf->sstatus = SSTATUS_SPIE;
}

void exec_user_elf(void *bin_start, size_t bin_size)
{
	struct exec_args args;
	memset(&args, 0, sizeof(args));
	args.argc = 1;
	strcpy(args.argv[0], "init");

	struct mm_struct *mm;
	vaddr_t entry;
	vaddr_t sp;
	int ret = load_elf_image(bin_start, bin_size, &args, &mm, &entry, &sp);
	if (ret < 0)
		panic("exec: initial ELF load failed (%d)", ret);

	struct trap_frame tf_storage;
	install_exec_mm(mm, &tf_storage, entry, sp);

	trapret_to_user(&tf_storage);
}

ssize_t sys_execve(struct trap_frame *tf)
{
	const char *upath = (const char *)tf->a0;
	const char *const *uargv = (const char *const *)tf->a1;

	char path[EXEC_MAX_PATH_LEN];
	int ret = copy_user_string(path, upath, sizeof(path));
	if (ret < 0)
		return ret;

	struct exec_args args;
	ret = copy_exec_args(uargv, &args);
	if (ret < 0)
		return ret;

	void *bin_start;
	size_t bin_size;
	ret = lookup_exec_image(path, &bin_start, &bin_size);
	if (ret < 0)
		return ret;

	struct mm_struct *mm;
	vaddr_t entry;
	vaddr_t sp;
	ret = load_elf_image(bin_start, bin_size, &args, &mm, &entry, &sp);
	if (ret < 0)
		return ret;

	install_exec_mm(mm, tf, entry, sp);
	return 0;
}
