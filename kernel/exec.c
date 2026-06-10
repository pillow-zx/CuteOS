/*
 * kernel/exec.c - 程序执行（用户态切换）
 *
 * 功能：
 *   实现 exec_user_binary()，将 flat binary 加载到用户地址空间，
 *   并通过修改 trap_frame 使当前内核线程切换到用户态执行。
 *
 *   exec_user_binary 执行流程：
 *     1. 调用 mm_alloc() 创建 mm_struct
 *     2. 调用 mm_create_user_pgd() 创建用户页表 + 复制内核映射
 *     3. 分配物理页用于用户代码和用户栈
 *     4. 将 flat binary 复制到用户代码页
 *     5. 在用户 pgd 中映射代码页和栈页
 *     6. 创建代码段 VMA（VM_READ | VM_EXEC）
 *     7. 初始化 mm->brk = code_end（页对齐）
 *     8. 设置 trap_frame：sepc=0x10000, sp=0x80000000, SPP=0
 *     9. 在单个 asm 块中：切换 satp → sfence → 设 sscratch → 跳转 __trapret
 *
 * 注意：
 *   当前为 Stage 3.2 最小实现，仅支持 flat binary（无 ELF 解析）。
 *   Stage 3.3 将实现完整的 sys_execve。
 *
 *   用户态 trap 入口会先用 sscratch 指向的 8 字节 scratch slot
 *   暂存用户 sp，因此 trap_frame 顶部额外预留 1 个 word 空间。
 *   该 trap_frame 必须放在当前 C 调用栈的下方未使用区域，构造完成后
 *   不能再发生新的函数调用，否则会被后续栈帧覆盖。
 */

#include <kernel/printk.h>
#include <kernel/string.h>
#include <kernel/mm.h>
#include <kernel/buddy.h>
#include <kernel/task.h>
#include <drivers/uart.h>
#include <asm/page.h>
#include <asm/pte.h>
#include <asm/csr.h>
#include <asm/trap.h>

/* 用户地址空间常量 */
#define USER_CODE_BASE	0x10000UL	/* 用户代码加载地址 */
#define USER_STACK_TOP	0x80000000UL	/* 用户栈顶地址 */
#define USER_STACK_BASE	0x7FFFF000UL	/* 用户栈底地址（1 页） */

/* entry.S 中的 trap 返回入口 */
extern void __trapret(void);

/*
 * exec_user_binary - 加载 flat binary 并切换到用户态
 * @bin_start: flat binary 在内核中的起始地址
 * @bin_size:  flat binary 的大小（字节）
 *
 * 为当前进程创建用户地址空间，加载 binary，然后
 * 通过 __trapret → sret 进入用户态。
 * 此函数不返回。
 */
void exec_user_binary(void *bin_start, size_t bin_size)
{
	printk("exec: loading user binary (%lu bytes)\n", (unsigned long)bin_size);

	/* 1. 创建 mm_struct */
	struct mm_struct *mm = mm_alloc();
	if (!mm)
		panic("exec: failed to allocate mm_struct");

	/* 2. 创建用户页表（PGD + 内核映射 + MMIO） */
	mm->pgd = mm_create_user_pgd();
	if (!mm->pgd)
		panic("exec: failed to create user pgd");

	/* 3. 分配用户代码页 */
	void *code_page = get_free_page(0);
	if (!code_page)
		panic("exec: failed to allocate user code page");
	memset(code_page, 0, PAGE_SIZE);

	/* 4. 复制 flat binary 到代码页 */
	if (bin_size > PAGE_SIZE)
		panic("exec: user binary too large (%lu > %lu)",
		      (unsigned long)bin_size, (unsigned long)PAGE_SIZE);
	memcpy(code_page, bin_start, bin_size);
	__asm__ volatile("fence.i" : : : "memory");

	/* 5. 分配用户栈页 */
	void *stack_page = get_free_page(0);
	if (!stack_page)
		panic("exec: failed to allocate user stack page");
	memset(stack_page, 0, PAGE_SIZE);

	/* 6. 映射用户代码页（0x10000, RX） */
	map_page(mm->pgd, USER_CODE_BASE, __pa((uintptr_t)code_page),
		 PTE_USER_RX);

	/* 7. 映射用户栈页（0x7FFFF000, RW） */
	map_page(mm->pgd, USER_STACK_BASE, __pa((uintptr_t)stack_page),
		 PTE_USER_RW);

	/* 8. 创建代码段 VMA */
	uintptr_t code_end = (USER_CODE_BASE + bin_size + PAGE_SIZE - 1) &
			     PAGE_MASK;
	struct vm_area_struct *code_vma = &mm->vma[0];
	code_vma->vm_start = USER_CODE_BASE;
	code_vma->vm_end = code_end;
	code_vma->vm_flags = VM_READ | VM_EXEC;
	code_vma->used = true;

	/* 9. 初始化 brk = 代码段结束处（页对齐） */
	mm->code_start = USER_CODE_BASE;
	mm->code_end = code_end;
	mm->brk = code_end;

	/* 10. 挂载到当前进程 */
	current->mm = mm;

	/* 11. 准备 satp 切换参数 */
	uintptr_t user_pgd_pa = __pa((uintptr_t)mm->pgd);
	uintptr_t satp_val = SATP_MODE_SV39 | (user_pgd_pa >> PAGE_SHIFT);

	printk("exec: switching to user mode (sepc=%p, sp=%p, pgd=%p, brk=%p)\n",
	       (void *)USER_CODE_BASE, (void *)USER_STACK_TOP,
	       (void *)user_pgd_pa, (void *)mm->brk);

	/* 12. 在当前 sp 下方的空闲区域构造 trap_frame，并预留 8B scratch */
	uintptr_t cur_sp;
	__asm__ volatile("mv %0, sp" : "=r"(cur_sp));
	cur_sp &= ~(uintptr_t)0xF;

	uintptr_t kstack_top = cur_sp - sizeof(uintptr_t);
	struct trap_frame *tf =
		(struct trap_frame *)(kstack_top - sizeof(struct trap_frame));
	memset(tf, 0, sizeof(struct trap_frame));

	BUG_ON((uintptr_t)tf < (uintptr_t)current->kstack);

	tf->sepc = USER_CODE_BASE;	/* 入口地址 */
	tf->sp = USER_STACK_TOP;	/* 用户栈顶 */
	tf->sstatus = SSTATUS_SPIE; /* SPP=0: sret 返回 U-mode */

	current->tf = tf;

	/*
	 * 13. 在单个 asm 块中完成所有切换操作：
	 *     切换 satp → sfence.vma → 设 sscratch → 切 sp → 跳 __trapret
	 *
	 *     这避免在 satp 切换和 __trapret 之间执行任何 C 代码，
	 *     确保指令流在 TLB 刷新后连续不中断。
	 */
	__asm__ volatile(
		"csrw    satp, %[satp]\n\t"
		"sfence.vma zero, zero\n\t"
		"csrw    sscratch, %[ksp]\n\t"
		"mv      sp, %[tf]\n\t"
		"j       __trapret\n\t"
		:
		: [satp] "r"(satp_val),
		  [ksp]  "r"(kstack_top),
		  [tf]   "r"((uintptr_t)tf)
		: "memory");

	unreachable();
}
