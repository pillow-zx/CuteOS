#ifndef _CUTEOS_KERNEL_TASK_H
#define _CUTEOS_KERNEL_TASK_H

/*
 * include/kernel/task.h - 进程控制块与任务管理
 *
 * 声明 struct task_struct，表示进程或内核线程的核心数据结构。
 * 同时定义任务状态常量，并声明 idle/init 任务实例。
 *
 * struct task_struct fields:
 *   pid        - Process ID
 *   state      - Current task state (RUNNING/SLEEPING/ZOMBIE/DEAD)
 *   mm         - Pointer to mm_struct (NULL for kernel threads)
 *   fd_array   - Open file descriptors (fixed array of 32 entries)
 *   sigactions - Signal action table (32 entries)
 *   blocked    - Blocked signal mask
 *   pending    - Pending signal mask
 *   ctx        - Saved callee-saved registers for context switch
 *   tf         - Pointer to trap_frame on kernel stack
 *   kstack     - Kernel stack base (low address)
 *   parent     - Parent task
 *   children   - List of child tasks
 *   sibling    - Linkage in parent's children list
 *   run_list   - Linkage in runqueue
 *
 * Task states:
 *   TASK_RUNNING  - Runnable or currently executing
 *   TASK_SLEEPING - Waiting for an event
 *   TASK_ZOMBIE   - Exited, waiting for parent to reap
 *   TASK_DEAD     - Fully reaped
 *
 * Globals:
 *   idle_task   - PID 0, the idle loop task (BSS static)
 *   current     - Pointer to the currently running task
 */

#include <kernel/types.h>
#include <kernel/list.h>
#include <kernel/wait.h>
#include <kernel/compiler.h>
#include <kernel/signal.h>
#include <asm/page.h>
#include <asm/trap.h>

/* 前向声明，避免循环依赖 */
struct mm_struct;
struct file;
struct dentry;

/* ---- 任务状态 ---- */

#define TASK_RUNNING  0 /* 可运行或正在执行 */
#define TASK_SLEEPING 1 /* 等待事件 */
#define TASK_ZOMBIE   2 /* 已退出，等待父进程回收 */
#define TASK_DEAD     3 /* 已被回收 */
#define TASK_STOPPED  4 /* 被 SIGSTOP 暂停 */

/* ---- 内核栈常量 ---- */

#define KSTACK_ORDER 1				 /* 2^1 = 2 页 = 8KB */
#define KSTACK_SIZE  (PAGE_SIZE << KSTACK_ORDER) /* 8192 字节 */

#define CANARY_MAGIC 0xDEADBEEFDEADBEEFUL

/* ---- 进程控制块 ---- */

struct task_struct {
	pid_t pid;		 /* 进程 ID */
	volatile uint32_t state; /* 当前任务状态 */

	struct context ctx;    /* callee-saved 寄存器保存区 */
	struct trap_frame *tf; /* 指向内核栈上的 trap_frame */

	/* 内核栈 */
	void *kstack; /* 栈底（低地址） */

	/* 内存管理 */
	struct mm_struct *mm; /* 指向 mm_struct，内核线程为 NULL */
	uint64_t satp; /* 预计算的 satp 值，避免 trapret 通过pgd临时计算 */
	int exit_code; /* zombie 状态下保留的退出码 */
	pid_t tgid;    /* 线程组 ID；单线程进程等于 pid */
	struct task_struct *group_leader; /* 所属线程组 leader */
	struct list_head thread_group;    /* leader 持有的线程组成员链表 */
	struct list_head thread_node;     /* 在线程组链表中的节点 */
	int exit_signal;		   /* 线程组 leader 退出时发送给父进程的信号 */
	int *clear_child_tid;		   /* CLONE_CHILD_CLEARTID 用户地址 */

	/* 文件描述符 */
	struct file *fd_array[32]; /* 打开的文件 */
	struct dentry *cwd;	   /* 当前工作目录 */
	uint32_t umask;		   /* 文件创建权限掩码 */
	uid_t uid;		   /* 当前用户 ID */
	gid_t gid;		   /* 当前组 ID */

	/* 信号处理 */
	struct sigaction sigactions[NSIG]; /* 每个信号的处理动作 */
	uint64_t blocked;		   /* 被屏蔽的信号掩码 */
	uint64_t pending;		   /* 待处理的信号掩码 */
	uint64_t in_handler; /* 当前正在运行其 handler 的信号掩码（防重入） */

	/* 进程树 */
	struct task_struct *parent; /* 父进程 */
	struct list_head children;  /* 子进程链表 */
	struct list_head sibling;   /* 在父进程 children 链表中的节点 */
	struct wait_queue_head wait_child_queue; /* 父进程等待子进程退出 */

	/* 调度 */
	struct list_head run_list;     /* 就绪队列节点 */
	struct list_head wait_list;    /* 等待队列节点 */
	volatile uint8_t need_resched; /* 时钟 tick 置位，trap 返回前触发调度 */
	uint8_t sched_level;	       /* MLFQ 当前队列等级 */
	uint8_t time_slice;	       /* 当前等级剩余 tick */
	uint8_t sched_ticks;	       /* 当前等级已消耗 tick */
	uint64_t enqueue_jiffies;      /* 最近一次入队时间 */
};

static_assert(offsetof(struct task_struct, kstack) == 128,
	      "TASK_KSTACK offset in entry.S out of sync with task_struct");
static_assert(offsetof(struct task_struct, satp) == 144,
	      "TASK_SATP offset in entry.S out of sync with task_struct");
static_assert(KSTACK_SIZE == 8192, "entry.S __trapret hardcodes kstack+8192; "
				   "update both if KSTACK_ORDER changes");

/* ---- 全局变量 ---- */

/* idle 进程，PID 0，BSS 段静态分配 */
extern struct task_struct idle_task;

/* 当前正在运行的进程 */
extern struct task_struct *current;

/* PID 1 init 进程。创建后保持有效，用于孤儿进程过继。 */
extern struct task_struct *init_task;

/* ---- 函数声明 ---- */

/**
 * task_init - 初始化进程管理子系统
 *
 * 创建 idle 进程（PID 0，BSS 静态分配），设置 current 指针，
 * 初始化 PID 分配器。
 */
void task_init(void);

/**
 * task_alloc - 分配并初始化一个新的 task_struct
 *
 * 从 SLAB 分配 task_struct，从 buddy 分配 8KB 内核栈，
 * 在栈底写入 CANARY_MAGIC。返回初始化后的 task 指针，
 * 失败返回 NULL。
 */
struct task_struct *task_alloc(void);

/**
 * task_free - 释放 task_struct 及其内核栈
 * @task: 要释放的任务
 *
 * 释放内核栈回 buddy，释放 task_struct 回 SLAB。
 */
void task_free(struct task_struct *task);

/**
 * check_canary - 检查任务内核栈 canary 是否完好
 * @task: 要检查的任务
 *
 * 若 canary 被破坏则触发 panic。
 */
void check_canary(struct task_struct *task);

/**
 * kernel_thread - 创建一个内核线程
 * @fn:  线程入口函数，签名为 void (*fn)(void *arg)
 * @arg: 传递给入口函数的参数
 *
 * 分配 task_struct，在内核栈顶构造 trap_frame，使线程通过
 * __trapret 恢复上下文后进入 fn(arg)。线程以 S-mode 运行，
 * 中断使能（SPIE=1）。创建后自动加入就绪队列。
 *
 * 返回新创建的 task_struct，失败返回 NULL。
 */
struct task_struct *kernel_thread(void (*fn)(void *), void *arg);

void set_init_task(struct task_struct *task);

bool task_is_group_leader(const struct task_struct *task);
bool task_group_has_other_threads(const struct task_struct *task);
struct task_struct *task_find_group_leader(pid_t tgid);
struct task_struct *task_find_thread(pid_t tid);
bool task_in_thread_group(const struct task_struct *task, pid_t tgid);

#endif
