/*
 * mm/page_fault.c - 缺页异常处理
 *
 * 功能：
 *   处理由 MMU 触发的缺页异常（Page Fault）。仅处理两种情况：
 *   - 合法缺页（legal fault）：虚拟地址落在合法 VMA 内，分配物理页
 *     并建立映射。
 *   - 非法访问：向进程发送 SIGSEGV（当前直接 do_exit）。
 *
 *   支持三种缺页类型：
 *     - 指令缺页（scause=12）：检查 VMA 的 VM_EXEC 权限
 *     - 加载缺页（scause=13）：检查 VMA 的 VM_READ 权限
 *     - 存储缺页（scause=15）：检查 VMA 的 VM_WRITE 权限
 *
 * 内核态缺页：
 *   支持内核态下由 copy_to_user / copy_from_user 触发的缺页，
 *   此时自动为对应的用户页分配物理页（而非发送信号）。
 *   若内核态访问非法地址，打印警告后 do_exit 当前进程。
 *
 * 主要函数：
 *   do_page_fault(tf) - 缺页异常总入口。读取 stval 获取出错虚拟地址，
 *             读取 scause 区分读/写缺页类型，查找 VMA 判断合法性，
 *             调用对应处理（分配页面或终止进程）。
 */

#include <kernel/mm.h>
#include <kernel/buddy.h>
#include <kernel/exit.h>
#include <kernel/printk.h>
#include <kernel/signal.h>
#include <kernel/string.h>
#include <kernel/task.h>
#include <asm/page.h>
#include <asm/pte.h>
#include <asm/trap.h>
#include <asm/csr.h>

/* ---- 内部辅助函数 ---- */

/*
 * vma_flags_to_pte - 将 VMA 权限标志转换为 PTE 权限位
 * @vm_flags: VM_READ | VM_WRITE | VM_EXEC 的组合
 *
 * 返回对应的 PTE 权限位（含 PTE_V, PTE_U, PTE_A, PTE_D）。
 */
static pte_t vma_flags_to_pte(uint32_t vm_flags)
{
	pte_t perm = PTE_V | PTE_U | PTE_A | PTE_D;

	if (vm_flags & VM_READ)
		perm |= PTE_R;
	if (vm_flags & VM_WRITE)
		perm |= PTE_W;
	if (vm_flags & VM_EXEC)
		perm |= PTE_X;

	return perm;
}

/*
 * fault_type_name - 获取缺页类型名称（调试用）
 */
static const char *fault_type_name(uint64_t scause)
{
	switch (scause) {
	case EXC_INST_PAGE_FAULT:
		return "instruction";
	case EXC_LOAD_PAGE_FAULT:
		return "load";
	case EXC_STORE_PAGE_FAULT:
		return "store";
	default:
		return "unknown";
	}
}

/*
 * check_vma_permission - 检查缺页类型是否与 VMA 权限匹配
 * @scause: 异常码（已剥离中断位）
 * @vma:    匹配到的 VMA
 *
 * 返回 true 表示权限匹配，false 表示权限冲突。
 */
static bool check_vma_permission(uint64_t scause,
				 struct vm_area_struct *vma)
{
	switch (scause) {
	case EXC_INST_PAGE_FAULT:
		return (vma->vm_flags & VM_EXEC) != 0;
	case EXC_LOAD_PAGE_FAULT:
		return (vma->vm_flags & VM_READ) != 0;
	case EXC_STORE_PAGE_FAULT:
		return (vma->vm_flags & VM_WRITE) != 0;
	default:
		return false;
	}
}

static bool pte_allows_fault(uint64_t scause, pte_t pte)
{
	switch (scause) {
	case EXC_INST_PAGE_FAULT:
		return (pte & PTE_X) != 0;
	case EXC_LOAD_PAGE_FAULT:
		return (pte & PTE_R) != 0;
	case EXC_STORE_PAGE_FAULT:
		return (pte & PTE_W) != 0;
	default:
		return false;
	}
}

static void signal_or_exit_segv(bool from_user_mode)
{
	if (from_user_mode) {
		if (force_signal(SIGSEGV, current) < 0)
			do_exit(SIGNAL_EXIT_CODE(SIGSEGV));
		return;
	}

	do_exit(SIGNAL_EXIT_CODE(SIGSEGV));
}

/* ---- 公共接口 ---- */

/*
 * do_page_fault - 缺页异常处理总入口
 * @tf: 指向当前 trap_frame
 *
 * 处理流程：
 *   1. 从 tf->stval 获取故障虚拟地址
 *   2. 从 tf->scause 获取缺页类型（inst/load/store）
 *   3. 判断来源（用户态 / 内核态）
 *   4. find_vma 查找合法性
 *   5. 合法且权限匹配：分配物理页，清零，建立映射，刷新 TLB
 *   6. 非法或权限不匹配：do_exit（杀掉当前进程）
 *
 * 注意：不修改 tf->sepc，缺页指令在 sret 后重新执行。
 */
void do_page_fault(struct trap_frame *tf)
{
	vaddr_t fault_addr = tf->stval;
	uint64_t scause = tf->scause & ~SCAUSE_IRQ_FLAG;
	bool from_user_mode = from_user(tf);

	/* 内核线程没有 mm，不应发生缺页 */
	if (!current || !current->mm) {
		panic("page fault in kernel thread: type=%s addr=%p "
		      "sepc=%p",
		      fault_type_name(scause), (void *)fault_addr,
		      (void *)tf->sepc);
	}

	/* 查找 VMA */
	struct vm_area_struct *vma = find_vma(current->mm, fault_addr);

	if (!vma) {
		printk("page fault: illegal access (no VMA) "
		       "type=%s addr=%p sepc=%p origin=%s pid=%d\n",
		       fault_type_name(scause), (void *)fault_addr,
		       (void *)tf->sepc,
		       from_user_mode ? "user" : "kernel",
		       current->pid);
		signal_or_exit_segv(from_user_mode);
		return;
	}

	/* 检查权限 */
	if (!check_vma_permission(scause, vma)) {
		printk("page fault: permission denied "
		       "type=%s addr=%p vma_flags=0x%x sepc=%p "
		       "origin=%s pid=%d\n",
		       fault_type_name(scause), (void *)fault_addr,
		       vma->vm_flags, (void *)tf->sepc,
		       from_user_mode ? "user" : "kernel",
		       current->pid);
		signal_or_exit_segv(from_user_mode);
		return;
	}

	/* 页对齐的虚拟地址 */
	vaddr_t page_addr = fault_addr & PAGE_MASK;

	/* 防御性检查：是否已映射 */
	pte_t *existing = walk_page_table(current->mm->pgd, page_addr, false);
	if (existing && (*existing & PTE_V)) {
		if (pte_allows_fault(scause, *existing)) {
			/*
			 * 已有合法映射但仍然缺页，可能是 TLB 一致性问题。
			 * 刷新 TLB 后直接返回，让指令重新执行。
			 */
			sfence_vma_addr(page_addr);
			return;
		}

		printk("page fault: mapped page permission denied "
		       "type=%s addr=%p pte=0x%lx sepc=%p origin=%s pid=%d\n",
		       fault_type_name(scause), (void *)fault_addr,
		       (size_t)*existing, (void *)tf->sepc,
		       from_user_mode ? "user" : "kernel", current->pid);
		signal_or_exit_segv(from_user_mode);
		return;
	}

	/* 分配物理页 */
	void *page = get_free_page(0);
	if (!page) {
		printk("page fault: OOM at addr=%p pid=%d\n",
		       (void *)fault_addr, current->pid);
		do_exit(1);
		unreachable();
	}

	/* 清零物理页（防止用户看到内核残留数据） */
	memset(page, 0, PAGE_SIZE);

	/* 建立映射 */
	pte_t perm = vma_flags_to_pte(vma->vm_flags);
	map_page(current->mm->pgd, page_addr, __pa((uintptr_t)page), perm);

	/* 刷新 TLB 使新映射生效 */
	sfence_vma_addr(page_addr);
}
