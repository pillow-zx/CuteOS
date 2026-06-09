/*
 * kernel/task.c - 进程控制块管理
 *
 * 功能：
 *   负责 task_struct 的分配、初始化与回收。task_struct 是内核中
 *   最重要的数据结构之一，包含进程的所有状态信息（调度、内存、文件、
 *   信号等）。
 *
 *   idle 进程（PID 0）在 BSS 段中静态分配，不经过动态分配路径。
 *   其余进程通过 SLAB 分配器动态分配 task_struct。
 *
 *   每个进程拥有 8KB 内核栈，栈底写入 CANARY_MAGIC 魔数用于
 *   栈溢出检测，调度切换时校验 canary 完整性。
 *
 * 主要函数：
 *   task_init()         - 初始化进程管理子系统：
 *                         创建 idle (PID 0, BSS 静态),
 *                         初始化 PID 分配器，设置 current 指针。
 *   task_alloc()        - 从 SLAB cache 分配一个新的 task_struct，
 *                         初始化各字段为默认值，分配 8KB 内核栈，
 *                         在栈底写入 CANARY_MAGIC。
 *   task_free(task)     - 释放 task_struct 及其内核栈回 SLAB cache。
 *   check_canary(task)  - 检查任务内核栈 canary 是否完好。
 *   kernel_thread(fn,arg) - 创建内核线程并通过 __trapret 启动 fn(arg)。
 */

#include <kernel/task.h>
#include <kernel/pid.h>
#include <kernel/slab.h>
#include <kernel/buddy.h>
#include <kernel/printk.h>
#include <kernel/string.h>
#include <kernel/sched.h>
#include <asm/page.h>
#include <asm/csr.h>

/* ---- 全局变量 ---- */

/* idle 进程，BSS 段静态分配 */
struct task_struct idle_task;

/* 当前运行的进程 */
struct task_struct *current;

/* ---- 内联辅助 ---- */

/**
 * stack_canary_ptr - 获取内核栈 canary 的地址
 * @task: 目标任务
 *
 * canary 位于栈底（最低地址）的前 8 字节。
 */
static inline uint64_t *stack_canary_ptr(struct task_struct *task)
{
	return (uint64_t *)task->kstack;
}

/* ---- 公共接口 ---- */

void check_canary(struct task_struct *task)
{
	if (!task->kstack)
		return;

	uint64_t canary = *stack_canary_ptr(task);
	BUG_ON(canary != CANARY_MAGIC);
}

struct task_struct *task_alloc(void)
{
	/* 1. 从 SLAB 分配 task_struct */
	struct task_struct *task = kmalloc(sizeof(struct task_struct));
	if (!task)
		return NULL;

	/* 2. 从 buddy 分配内核栈 (8KB = 2 pages) */
	void *kstack = get_free_page(KSTACK_ORDER);
	if (!kstack) {
		kfree(task);
		return NULL;
	}

	/* 3. 分配 PID */
	int32_t pid = alloc_pid();
	if (pid < 0) {
		free_page(kstack, KSTACK_ORDER);
		kfree(task);
		return NULL;
	}

	/* 4. 初始化 task_struct */
	memset(task, 0, sizeof(struct task_struct));
	task->pid = (pid_t)pid;
	task->state = TASK_RUNNING;
	task->kstack = kstack;
	task->tf = NULL;
	task->mm = NULL;

	INIT_LIST_HEAD(&task->children);
	INIT_LIST_HEAD(&task->sibling);
	INIT_LIST_HEAD(&task->run_list);

	/* 5. 内核栈清零并在栈底写入 canary */
	memset(kstack, 0, KSTACK_SIZE);
	*stack_canary_ptr(task) = CANARY_MAGIC;

	return task;
}

void task_free(struct task_struct *task)
{
	if (!task)
		return;

	/* 释放 PID */
	free_pid(task->pid);

	/* 释放内核栈 */
	if (task->kstack) {
		free_page(task->kstack, KSTACK_ORDER);
		task->kstack = NULL;
	}

	/* 释放 task_struct */
	kfree(task);
}

void task_init(void)
{
	/* 1. 初始化 PID 分配器 */
	pid_init();

	/* 2. 初始化 idle 进程（PID 0，BSS 静态分配） */
	memset(&idle_task, 0, sizeof(struct task_struct));
	idle_task.pid = 0;
	idle_task.state = TASK_RUNNING;
	idle_task.kstack = NULL; /* idle 使用 boot_stack，无独立内核栈 */
	idle_task.mm = NULL;

	INIT_LIST_HEAD(&idle_task.children);
	INIT_LIST_HEAD(&idle_task.sibling);
	INIT_LIST_HEAD(&idle_task.run_list);

	/* 3. 设置 current 指针 */
	current = &idle_task;

	printk("task: idle (PID 0) created\n");
}

/* ---- 内核线程创建 ---- */

/* entry.S 中的 trap 返回入口，switch_to 通过它启动新线程 */
extern void __trapret(void);

/**
 * kernel_thread - 创建一个内核线程
 * @fn:  线程入口函数
 * @arg: 传递给入口函数的参数
 *
 * 在内核栈顶构造一个 trap_frame，设置 sepc=fn, a0=arg,
 * sstatus=SPP|SPIE（S-mode 返回 + 中断使能）。
 * ctx.ra 设为 __trapret，使 switch_to 首次切入时通过
 * __trapret → sret 进入 fn(arg)。
 */
struct task_struct *kernel_thread(void (*fn)(void *), void *arg)
{
	struct task_struct *task = task_alloc();
	if (!task)
		return NULL;

	/* 在内核栈顶预留 trap_frame 空间 */
	struct trap_frame *tf =
		(struct trap_frame *)((uint8_t *)task->kstack + KSTACK_SIZE -
				      sizeof(struct trap_frame));

	memset(tf, 0, sizeof(struct trap_frame));

	/* sret 后 PC 跳转到 fn, a0 传递 arg */
	tf->sepc = (size_t)fn;
	tf->a0 = (uintptr_t)arg;
	/* SPP=1 → 返回 S-mode; SPIE=1 → sret 后 SIE=1 (中断使能) */
	tf->sstatus = SSTATUS_SPP | SSTATUS_SPIE;

	task->tf = tf;

	/* switch_to 加载 ctx 后 ret → __trapret → 恢复 tf → sret → fn(arg) */
	task->ctx.ra = (size_t)__trapret;
	task->ctx.sp = (size_t)tf;

	sched_enqueue(task);

	printk("task: kernel thread (PID %d) created, fn=%p\n", task->pid,
	       (void *)fn);

	return task;
}
