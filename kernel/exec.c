/*
 * kernel/exec.c - ELF 加载与 execve 替换语义
 *
 * exec 成功时替换当前进程的 mm/satp/trap_frame，保留 PID、父子关系、
 * 打开的 fd 和信号占位状态。
 */

#include <kernel/elf.h>
#include <kernel/errno.h>
#include <kernel/buddy.h>
#include <kernel/fdtable.h>
#include <kernel/fs.h>
#include <kernel/mm.h>
#include <kernel/printk.h>
#include <kernel/slab.h>
#include <kernel/string.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <kernel/vfs.h>
#include <asm/csr.h>
#include <asm/page.h>
#include <asm/pte.h>
#include <asm/trap.h>

#define EXEC_MAX_ARGS	  16
#define EXEC_MAX_ARG_LEN  128
#define EXEC_MAX_PATH_LEN 64
#define EXEC_MAX_ENVS	  16
#define EXEC_MAX_ENV_LEN  128

struct exec_args_envp {
	int argc;
	char argv[EXEC_MAX_ARGS][EXEC_MAX_ARG_LEN];
	int envc;
	char envp[EXEC_MAX_ENVS][EXEC_MAX_ENV_LEN];
};

struct exec_image {
	struct file *file;
	uint64_t size;
};

static pte_t elf_flags_to_pte(uint32_t p_flags)
{
	const bool w = p_flags & PF_W;
	const bool x = p_flags & PF_X;

	if (w && x)
		return PTE_USER_RWX;
	if (w)
		return PTE_USER_RW;
	if (x)
		return PTE_USER_RX;

	return PTE_USER_R;
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
	for (size_t i = 0; i < max_len; i++) {
		char c;

		if (copy_from_user(&c, user + i, sizeof(c)) != 0)
			return -EFAULT;
		dst[i] = c;
		if (c == '\0')
			return 0;
	}

	dst[max_len - 1] = '\0';
	return -E2BIG;
}

static int copy_arg_array(const char *const *user_array,
			  struct exec_args_envp *args)
{
	args->argc = 0;

	if (!user_array)
		return 0;

	for (int i = 0; i < EXEC_MAX_ARGS; i++) {
		const char *user_string;

		if (copy_from_user(&user_string, &user_array[i],
				   sizeof(user_string)) != 0)
			return -EFAULT;
		if (!user_string)
			return 0;

		int ret = copy_user_string(args->argv[i], user_string,
					   EXEC_MAX_ARG_LEN);
		if (ret < 0)
			return ret;

		args->argc++;
	}

	const char *extra;
	if (copy_from_user(&extra, &user_array[EXEC_MAX_ARGS], sizeof(extra)) !=
	    0)
		return -EFAULT;
	if (extra)
		return -E2BIG;

	return 0;
}

static int copy_env_array(const char *const *user_array,
			  struct exec_args_envp *args)
{
	args->envc = 0;

	if (!user_array)
		return 0;

	for (int i = 0; i < EXEC_MAX_ENVS; i++) {
		const char *user_string;

		if (copy_from_user(&user_string, &user_array[i],
				   sizeof(user_string)) != 0)
			return -EFAULT;
		if (!user_string)
			return 0;

		int ret = copy_user_string(args->envp[i], user_string,
					   EXEC_MAX_ENV_LEN);
		if (ret < 0)
			return ret;

		args->envc++;
	}

	const char *extra;
	if (copy_from_user(&extra, &user_array[EXEC_MAX_ENVS], sizeof(extra)) !=
	    0)
		return -EFAULT;
	if (extra)
		return -E2BIG;

	return 0;
}

static int copy_exec_args(const char *const *uargv, const char *const *uenvp,
			  struct exec_args_envp *args)
{
	int ret;

	memset(args, 0, sizeof(*args));

	ret = copy_arg_array(uargv, args);
	if (ret < 0)
		return ret;

	return copy_env_array(uenvp, args);
}

static int open_exec_image(const char *path, struct exec_image *image)
{
	struct dentry *dentry;
	struct file *file;

	memset(image, 0, sizeof(*image));

	dentry = path_lookup(path, 0);
	if (!dentry)
		return -ENOENT;
	if (!dentry->d_inode || !dentry->d_inode->i_fop ||
	    !dentry->d_inode->i_fop->read) {
		dput(dentry);
		return -ENOEXEC;
	}

	file = file_alloc_dentry(dentry, O_RDONLY, FMODE_READ);
	dput(dentry);
	if (!file)
		return -ENOMEM;

	image->file = file;
	image->size = vfs_inode_size(file->f_inode);
	return 0;
}

static void close_exec_image(struct exec_image *image)
{
	if (!image || !image->file)
		return;

	file_put(image->file);
	image->file = NULL;
	image->size = 0;
}

static int read_exec_image(struct exec_image *image, uint64_t offset, void *buf,
			   size_t len)
{
	if (!image || !image->file || !buf)
		return -EINVAL;
	if (offset > image->size || len > image->size - offset)
		return -ENOEXEC;

	image->file->f_pos = (loff_t)offset;
	ssize_t ret = vfs_read(image->file, buf, len);
	if (ret < 0)
		return (int)ret;
	if ((size_t)ret != len)
		return -ENOEXEC;

	return 0;
}

static void *stack_sp_to_kernel(void *stack_page, vaddr_t sp)
{
	return (uint8_t *)stack_page + (sp - USER_STACK_BASE);
}

static int setup_user_stack(struct mm_struct *mm,
			    const struct exec_args_envp *args, vaddr_t *sp_out)
{
	static_assert(USER_STACK_TOP - USER_STACK_BASE == PAGE_SIZE,
		      "setup_user_stack assumes a single mapped stack page");

	void *stack_page = get_free_page(0);
	if (!stack_page)
		return -ENOMEM;
	memset(stack_page, 0, PAGE_SIZE);

	map_page(mm->pgd, USER_STACK_BASE, __pa((uintptr_t)stack_page),
		 PTE_USER_RW);

	uintptr_t user_argv[EXEC_MAX_ARGS + 1];
	uintptr_t user_envp[EXEC_MAX_ENVS + 1];
	uintptr_t sp = USER_STACK_TOP;

	for (int i = args->envc - 1; i >= 0; i--) {
		size_t len = strlen(args->envp[i]) + 1;
		if (sp < USER_STACK_BASE + len)
			return -E2BIG;

		sp -= len;
		memcpy(stack_sp_to_kernel(stack_page, sp), args->envp[i], len);
		user_envp[i] = sp;
	}
	user_envp[args->envc] = 0;

	for (int i = args->argc - 1; i >= 0; i--) {
		size_t len = strlen(args->argv[i]) + 1;
		if (sp < USER_STACK_BASE + len)
			return -E2BIG;

		sp -= len;
		memcpy(stack_sp_to_kernel(stack_page, sp), args->argv[i], len);
		user_argv[i] = sp;
	}
	user_argv[args->argc] = 0;

	sp &= ~(uintptr_t)0xf;

	size_t argv_bytes = (size_t)(args->argc + 1) * sizeof(uintptr_t);
	size_t envp_bytes = (size_t)(args->envc + 1) * sizeof(uintptr_t);
	size_t frame_bytes = sizeof(uintptr_t) + argv_bytes + envp_bytes;
	size_t padding = frame_bytes & 0xf ? 16 - (frame_bytes & 0xf) : 0;

	if (sp < USER_STACK_BASE + frame_bytes + padding)
		return -E2BIG;

	sp -= frame_bytes + padding;

	uint8_t *frame = stack_sp_to_kernel(stack_page, sp);
	*(uintptr_t *)frame = (uintptr_t)args->argc;
	memcpy(frame + sizeof(uintptr_t), user_argv, argv_bytes);
	memcpy(frame + sizeof(uintptr_t) + argv_bytes, user_envp, envp_bytes);

	*sp_out = sp;
	return 0;
}

static int load_elf_file(struct exec_image *image,
			 const struct exec_args_envp *args,
			 struct mm_struct **mm_out, vaddr_t *entry_out,
			 vaddr_t *sp_out)
{
	*mm_out = NULL;
	*entry_out = 0;
	*sp_out = 0;

	printk("exec: loading ELF file (%lu bytes)\n", (size_t)image->size);

	if (image->size < sizeof(Elf64_Ehdr))
		return -ENOEXEC;

	Elf64_Ehdr ehdr;
	int ret = read_exec_image(image, 0, &ehdr, sizeof(ehdr));
	if (ret < 0)
		return ret;

	if (ehdr.e_ident[EI_MAG0] != ELFMAG0 ||
	    ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
	    ehdr.e_ident[EI_MAG2] != ELFMAG2 ||
	    ehdr.e_ident[EI_MAG3] != ELFMAG3)
		return -ENOEXEC;
	if (ehdr.e_ident[EI_CLASS] != ELFCLASS64)
		return -ENOEXEC;
	if (ehdr.e_ident[EI_DATA] != ELFDATA2LSB)
		return -ENOEXEC;
	if (ehdr.e_type != ET_EXEC || ehdr.e_machine != EM_RISCV)
		return -ENOEXEC;
	if (ehdr.e_phoff == 0 || ehdr.e_phnum == 0)
		return -ENOEXEC;
	if (ehdr.e_phentsize != sizeof(Elf64_Phdr))
		return -ENOEXEC;

	uint64_t phdr_bytes = (uint64_t)ehdr.e_phnum * sizeof(Elf64_Phdr);
	if (ehdr.e_phoff > image->size || phdr_bytes > image->size ||
	    ehdr.e_phoff + phdr_bytes > image->size)
		return -ENOEXEC;

	Elf64_Phdr *phdrs = kmalloc((size_t)phdr_bytes);
	if (!phdrs)
		return -ENOMEM;

	ret = read_exec_image(image, ehdr.e_phoff, phdrs, (size_t)phdr_bytes);
	if (ret < 0) {
		kfree(phdrs);
		return ret;
	}

	struct mm_struct *mm = mm_alloc();
	if (!mm) {
		kfree(phdrs);
		return -ENOMEM;
	}

	mm->pgd = mm_create_user_pgd();
	if (!mm->pgd) {
		kfree(phdrs);
		mm_destroy(mm);
		return -ENOMEM;
	}

	vaddr_t first_vaddr = 0;
	vaddr_t last_end = 0;
	int vma_idx = 0;

	for (int i = 0; i < ehdr.e_phnum; i++) {
		Elf64_Phdr *ph = &phdrs[i];
		if (ph->p_type != PT_LOAD)
			continue;
		if (ph->p_memsz == 0)
			continue;

		printk("exec: PT_LOAD vaddr=%p filesz=%llu memsz=%llu "
		       "flags=0x%x\n",
		       (void *)ph->p_vaddr, ph->p_filesz, ph->p_memsz,
		       ph->p_flags);

		if (!(ph->p_flags & PF_R)) {
			ret = -ENOEXEC;
			goto fail;
		}
		if (ph->p_filesz > ph->p_memsz) {
			ret = -ENOEXEC;
			goto fail;
		}
		if (ph->p_offset > image->size ||
		    ph->p_filesz > image->size - ph->p_offset) {
			ret = -ENOEXEC;
			goto fail;
		}

		vaddr_t seg_start = ph->p_vaddr;
		vaddr_t seg_end = ph->p_vaddr + ph->p_memsz;
		if (seg_end < seg_start || seg_end > USER_STACK_BASE) {
			ret = -ENOEXEC;
			goto fail;
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
				ret = -ENOMEM;
				goto fail;
			}
			memset(page, 0, PAGE_SIZE);

			uint64_t page_file_start =
				va < seg_start ? 0 : va - seg_start;
			if (page_file_start < ph->p_filesz) {
				uint64_t file_off =
					ph->p_offset + page_file_start;
				size_t copy_off =
					va < seg_start ? seg_start - va : 0;
				size_t copy_len = PAGE_SIZE - copy_off;
				uint64_t remaining =
					ph->p_filesz - page_file_start;

				if (copy_len > remaining)
					copy_len = (size_t)remaining;

				ret = read_exec_image(
					image, file_off,
					(uint8_t *)page + copy_off, copy_len);
				if (ret < 0) {
					free_page(page, 0);
					goto fail;
				}
			}

			map_page(mm->pgd, va, __pa((uintptr_t)page),
				 elf_flags_to_pte(ph->p_flags));
		}

		if (vma_idx >= NR_VMA) {
			ret = -E2BIG;
			goto fail;
		}

		struct vm_area_struct *vma = &mm->vma[vma_idx++];
		vma->vm_start = seg_start;
		vma->vm_end = seg_end;
		vma->vm_flags = elf_flags_to_vma(ph->p_flags);
		vma->vm_type = VMA_CODE;
		vma->used = true;
	}

	if (last_end == 0) {
		ret = -ENOEXEC;
		goto fail;
	}

	fence_i();

	mm->code_start = first_vaddr;
	mm->code_end = PFN_UP(last_end) << PAGE_SHIFT;
	mm->brk = mm->code_end;

	vaddr_t user_sp;
	ret = setup_user_stack(mm, args, &user_sp);
	if (ret < 0)
		goto fail;

	if (vma_idx >= NR_VMA) {
		ret = -E2BIG;
		goto fail;
	}

	struct vm_area_struct *stack_vma = &mm->vma[vma_idx++];
	stack_vma->vm_start = USER_STACK_BASE;
	stack_vma->vm_end = USER_STACK_TOP;
	stack_vma->vm_flags = VM_READ | VM_WRITE;
	stack_vma->vm_type = VMA_STACK;
	stack_vma->used = true;

	kfree(phdrs);
	*mm_out = mm;
	*entry_out = ehdr.e_entry;
	*sp_out = user_sp;
	return 0;

fail:
	kfree(phdrs);
	mm_destroy(mm);
	return ret;
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

void exec_user_path(const char *path)
{
	struct exec_args_envp args;
	memset(&args, 0, sizeof(args));
	/*
	 * BusyBox 通过 argv[0] 的 basename 派发 applet，必须传入路径作为
	 * argv[0]，否则 applet 派发会解引用空 argv[0] 而崩溃。
	 */
	args.argc = 1;
	strncpy(args.argv[0], path, EXEC_MAX_ARG_LEN - 1);
	args.argv[0][EXEC_MAX_ARG_LEN - 1] = '\0';

	struct exec_image image;
	int ret = open_exec_image(path, &image);
	if (ret < 0)
		panic("exec: open %s failed (%d)", path, ret);

	struct mm_struct *mm;
	vaddr_t entry;
	vaddr_t sp;
	ret = load_elf_file(&image, &args, &mm, &entry, &sp);
	close_exec_image(&image);
	if (ret < 0)
		panic("exec: load %s failed (%d)", path, ret);

	struct trap_frame tf_storage;
	install_exec_mm(mm, &tf_storage, entry, sp);

	trapret_to_user(&tf_storage);
}

ssize_t sys_execve(struct trap_frame *tf)
{
	const char *upath = (const char *)tf->a0;
	const char *const *uargv = (const char *const *)tf->a1;
	const char *const *uenvp = (const char *const *)tf->a2;

	char path[EXEC_MAX_PATH_LEN];
	int ret = copy_user_string(path, upath, sizeof(path));
	if (ret < 0)
		return ret;

	struct exec_args_envp args;
	ret = copy_exec_args(uargv, uenvp, &args);
	if (ret < 0)
		return ret;

	struct exec_image image;
	ret = open_exec_image(path, &image);
	if (ret < 0)
		return ret;

	struct mm_struct *mm;
	vaddr_t entry;
	vaddr_t sp;
	ret = load_elf_file(&image, &args, &mm, &entry, &sp);
	close_exec_image(&image);
	if (ret < 0)
		return ret;

	/*
	 * Success replaces the current trap frame. do_syscall() will only write
	 * the returned 0 into the new frame's a0 before trap return.
	 */
	install_exec_mm(mm, tf, entry, sp);
	return 0;
}
