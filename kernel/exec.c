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

#define EXEC_MAX_ARGS	 16
#define EXEC_MAX_ARG_LEN 128
#define EXEC_MAX_ENVS	 16
#define EXEC_MAX_ENV_LEN 128

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

struct elf_phdr_table {
	Elf64_Phdr *entries;
	int count;
};

struct elf_load_layout {
	vaddr_t first_vaddr;
	vaddr_t last_end;
	int vma_idx;
	bool loaded_segment;
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
	ssize_t len = strncpy_from_user(dst, user, max_len);

	if (len == -ENAMETOOLONG)
		return -E2BIG;
	return len < 0 ? (int)len : 0;
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

static int read_elf_header(struct exec_image *image, Elf64_Ehdr *ehdr)
{
	int ret;

	if (!image || !ehdr)
		return -EINVAL;
	if (image->size < sizeof(*ehdr))
		return -ENOEXEC;

	ret = read_exec_image(image, 0, ehdr, sizeof(*ehdr));
	if (ret < 0)
		return ret;

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
	if (ehdr->e_phentsize != sizeof(Elf64_Phdr))
		return -ENOEXEC;

	return 0;
}

static int read_elf_phdr_table(struct exec_image *image,
			       const Elf64_Ehdr *ehdr,
			       struct elf_phdr_table *table)
{
	uint64_t phdr_bytes;
	int ret;

	if (!image || !ehdr || !table)
		return -EINVAL;

	memset(table, 0, sizeof(*table));
	phdr_bytes = (uint64_t)ehdr->e_phnum * sizeof(Elf64_Phdr);
	if (ehdr->e_phoff > image->size ||
	    phdr_bytes > image->size - ehdr->e_phoff)
		return -ENOEXEC;

	table->entries = kmalloc((size_t)phdr_bytes);
	if (!table->entries)
		return -ENOMEM;

	ret = read_exec_image(image, ehdr->e_phoff, table->entries,
			      (size_t)phdr_bytes);
	if (ret < 0) {
		kfree(table->entries);
		table->entries = NULL;
		return ret;
	}

	table->count = ehdr->e_phnum;
	return 0;
}

static void free_elf_phdr_table(struct elf_phdr_table *table)
{
	if (!table || !table->entries)
		return;

	kfree(table->entries);
	table->entries = NULL;
	table->count = 0;
}

static int create_exec_mm(struct mm_struct **mm_out)
{
	struct mm_struct *mm;

	if (!mm_out)
		return -EINVAL;
	*mm_out = NULL;

	mm = mm_alloc();
	if (!mm)
		return -ENOMEM;

	mm->pgd = mm_create_user_pgd();
	if (!mm->pgd) {
		mm_destroy(mm);
		return -ENOMEM;
	}

	*mm_out = mm;
	return 0;
}

static bool elf_phdr_loadable(const Elf64_Phdr *ph)
{
	return ph->p_type == PT_LOAD && ph->p_memsz != 0;
}

static int validate_load_segment(struct exec_image *image, const Elf64_Phdr *ph,
				 vaddr_t *seg_start, vaddr_t *seg_end)
{
	uint64_t start;
	uint64_t end;

	if (!(ph->p_flags & PF_R))
		return -ENOEXEC;
	if (ph->p_filesz > ph->p_memsz)
		return -ENOEXEC;
	if (ph->p_offset > image->size ||
	    ph->p_filesz > image->size - ph->p_offset)
		return -ENOEXEC;

	start = ph->p_vaddr;
	end = ph->p_vaddr + ph->p_memsz;
	if (end < start || end > USER_STACK_BASE)
		return -ENOEXEC;

	*seg_start = (vaddr_t)start;
	*seg_end = (vaddr_t)end;
	return 0;
}

static int copy_segment_page(struct exec_image *image, const Elf64_Phdr *ph,
			     void *page, vaddr_t va, vaddr_t seg_start)
{
	uint64_t page_file_start = va < seg_start ? 0 : va - seg_start;
	uint64_t file_off;
	uint64_t remaining;
	size_t copy_off;
	size_t copy_len;

	if (page_file_start >= ph->p_filesz)
		return 0;

	file_off = ph->p_offset + page_file_start;
	copy_off = va < seg_start ? seg_start - va : 0;
	copy_len = PAGE_SIZE - copy_off;
	remaining = ph->p_filesz - page_file_start;
	if (copy_len > remaining)
		copy_len = (size_t)remaining;

	return read_exec_image(image, file_off, (uint8_t *)page + copy_off,
			       copy_len);
}

static int map_segment_page(struct exec_image *image, struct mm_struct *mm,
			    const Elf64_Phdr *ph, vaddr_t va, vaddr_t seg_start)
{
	void *page;
	int ret;

	page = get_free_page(0);
	if (!page)
		return -ENOMEM;
	memset(page, 0, PAGE_SIZE);

	ret = copy_segment_page(image, ph, page, va, seg_start);
	if (ret < 0) {
		free_page(page, 0);
		return ret;
	}

	map_page(mm->pgd, va, __pa((uintptr_t)page),
		 elf_flags_to_pte(ph->p_flags));
	return 0;
}

static int map_load_segment(struct exec_image *image, struct mm_struct *mm,
			    const Elf64_Phdr *ph, vaddr_t seg_start,
			    vaddr_t seg_end)
{
	vaddr_t page_start = PFN_DOWN(seg_start) << PAGE_SHIFT;
	vaddr_t page_end = PFN_UP(seg_end) << PAGE_SHIFT;

	for (vaddr_t va = page_start; va < page_end; va += PAGE_SIZE) {
		int ret = map_segment_page(image, mm, ph, va, seg_start);

		if (ret < 0)
			return ret;
	}

	return 0;
}

static void record_load_bounds(struct elf_load_layout *layout, vaddr_t start,
			       vaddr_t end)
{
	if (!layout->loaded_segment || start < layout->first_vaddr)
		layout->first_vaddr = start;
	if (!layout->loaded_segment || end > layout->last_end)
		layout->last_end = end;
	layout->loaded_segment = true;
}

static int add_load_vma(struct mm_struct *mm, struct elf_load_layout *layout,
			const Elf64_Phdr *ph, vaddr_t start, vaddr_t end)
{
	struct vm_area_struct *vma;

	if (layout->vma_idx >= NR_VMA)
		return -E2BIG;

	vma = &mm->vma[layout->vma_idx++];
	vma->vm_start = start;
	vma->vm_end = end;
	vma->vm_flags = elf_flags_to_vma(ph->p_flags);
	vma->vm_type = VMA_CODE;
	vma->used = true;
	return 0;
}

static int load_elf_segment(struct exec_image *image, struct mm_struct *mm,
			    const Elf64_Phdr *ph,
			    struct elf_load_layout *layout)
{
	vaddr_t seg_start;
	vaddr_t seg_end;
	int ret;

	if (!elf_phdr_loadable(ph))
		return 0;

	ret = validate_load_segment(image, ph, &seg_start, &seg_end);
	if (ret < 0)
		return ret;
	ret = map_load_segment(image, mm, ph, seg_start, seg_end);
	if (ret < 0)
		return ret;
	ret = add_load_vma(mm, layout, ph, seg_start, seg_end);
	if (ret < 0)
		return ret;

	record_load_bounds(layout, seg_start, seg_end);
	return 0;
}

static int load_elf_segments(struct exec_image *image,
			     const struct elf_phdr_table *phdrs,
			     struct mm_struct *mm,
			     struct elf_load_layout *layout)
{
	for (int i = 0; i < phdrs->count; i++) {
		int ret = load_elf_segment(image, mm, &phdrs->entries[i], layout);

		if (ret < 0)
			return ret;
	}

	return layout->loaded_segment ? 0 : -ENOEXEC;
}

static int add_stack_vma(struct mm_struct *mm, struct elf_load_layout *layout)
{
	struct vm_area_struct *vma;

	if (layout->vma_idx >= NR_VMA)
		return -E2BIG;

	vma = &mm->vma[layout->vma_idx++];
	vma->vm_start = USER_STACK_BASE;
	vma->vm_end = USER_STACK_TOP;
	vma->vm_flags = VM_READ | VM_WRITE;
	vma->vm_type = VMA_STACK;
	vma->used = true;
	return 0;
}

static int finish_exec_mm(struct mm_struct *mm,
			  const struct exec_args_envp *args,
			  struct elf_load_layout *layout, vaddr_t *sp_out)
{
	int ret;

	fence_i();

	mm->code_start = layout->first_vaddr;
	mm->code_end = PFN_UP(layout->last_end) << PAGE_SHIFT;
	mm->brk = mm->code_end;

	ret = setup_user_stack(mm, args, sp_out);
	if (ret < 0)
		return ret;

	return add_stack_vma(mm, layout);
}

static int load_elf_file(struct exec_image *image,
			 const struct exec_args_envp *args,
			 struct mm_struct **mm_out, vaddr_t *entry_out,
			 vaddr_t *sp_out)
{
	Elf64_Ehdr ehdr;
	struct elf_phdr_table phdrs;
	struct elf_load_layout layout = { 0 };
	struct mm_struct *mm = NULL;
	vaddr_t user_sp = 0;
	int ret;

	*mm_out = NULL;
	*entry_out = 0;
	*sp_out = 0;

	ret = read_elf_header(image, &ehdr);
	if (ret < 0)
		return ret;

	ret = read_elf_phdr_table(image, &ehdr, &phdrs);
	if (ret < 0)
		return ret;

	ret = create_exec_mm(&mm);
	if (ret < 0)
		goto fail;
	ret = load_elf_segments(image, &phdrs, mm, &layout);
	if (ret < 0)
		goto fail;
	ret = finish_exec_mm(mm, args, &layout, &user_sp);
	if (ret < 0)
		goto fail;

	free_elf_phdr_table(&phdrs);
	*mm_out = mm;
	*entry_out = ehdr.e_entry;
	*sp_out = user_sp;
	return 0;

fail:
	mm_destroy(mm);
	free_elf_phdr_table(&phdrs);
	return ret;
}

static void flush_old_exec(struct mm_struct *oldmm)
{
	if (!oldmm)
		return;

	csr_write(satp, kernel_satp());
	sfence_vma_all();
	mm_put(oldmm);
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
	char *path;
	ssize_t path_len;
	int ret;

	if (task_group_has_other_threads(current))
		return -EINVAL;

	path = get_free_page(0);
	if (!path)
		return -ENOMEM;

	path_len = strncpy_from_user(path, upath, VFS_PATH_MAX);
	if (path_len < 0) {
		free_page(path, 0);
		return (int)path_len;
	}
	if (path_len == 0) {
		free_page(path, 0);
		return -ENOENT;
	}

	struct exec_args_envp args;
	ret = copy_exec_args(uargv, uenvp, &args);
	if (ret < 0) {
		free_page(path, 0);
		return ret;
	}

	struct exec_image image;
	ret = open_exec_image(path, &image);
	free_page(path, 0);
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

	/* 关闭所有设置了 FD_CLOEXEC 的文件描述符 */
	struct files_struct *efiles = current->files;

	if (efiles) {
		unsigned long cloexec = efiles->close_on_exec;

		efiles->close_on_exec = 0;
		for (int fd = 0; cloexec; fd++, cloexec >>= 1)
			if (cloexec & 1)
				fd_close(fd);
	}

	return 0;
}
