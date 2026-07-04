#ifndef _CUTEOS_KERNEL_TASK_H
#define _CUTEOS_KERNEL_TASK_H

/*
 * include/kernel/task.h - 进程控制块与任务管理
 *
 * task_struct 是任务生命周期的聚合根。具体子系统状态按所有权分组：
 * arch 保存低级上下文，resources 保存可共享资源，sigctx 保存每线程
 * 信号状态，links 保存进程树/线程组关系，sched 保存调度器私有实体。
 */

#include <kernel/types.h>
#include <kernel/list.h>
#include <kernel/wait.h>
#include <kernel/compiler.h>
#include <asm/page.h>
#include <asm/trap.h>
#include <uapi/futex.h>
#include <uapi/signal.h>

/* ---- 任务状态 ---- */

#define TASK_RUNNING	     0x00u /* 可运行或正在执行 */
#define TASK_UNINTERRUPTIBLE 0x01u /* 不可中断等待 */
#define TASK_INTERRUPTIBLE   0x02u /* 可被未屏蔽信号打断的等待 */
#define TASK_ZOMBIE	     0x04u /* 已退出，等待父进程回收 */
#define TASK_DEAD	     0x08u /* 已被回收 */
#define TASK_STOPPED	     0x10u /* 被 SIGSTOP 暂停 */

/* 任意睡眠状态的位掩码 */
#define TASK_ANY_SLEEP (TASK_UNINTERRUPTIBLE | TASK_INTERRUPTIBLE)

/* ---- 内核栈常量 ---- */

#define KSTACK_ORDER 1				 /* 2^1 = 2 页 = 8KB */
#define KSTACK_SIZE  (PAGE_SIZE << KSTACK_ORDER) /* 8192 字节 */

#define CANARY_MAGIC 0xDEADBEEFDEADBEEFUL

struct files_struct;
struct fs_struct;
struct mm_struct;
struct sighand_struct;
struct signal_struct;
struct task_struct;

bool signal_pending(struct task_struct *task);

/* ---- 进程控制块分组 ---- */

struct task_arch_state {
	struct context ctx;
	struct trap_frame *tf;
	void *kstack;
	uint64_t satp;
};

struct task_identity {
	pid_t pid;
	pid_t tgid;
	pid_t pgid;
	struct task_struct *group_leader;
};

struct task_lifecycle {
	volatile uint32_t state;
	int exit_code;
	int exit_signal;
};

struct task_links {
	struct task_struct *parent;
	struct list_head children;
	struct list_head sibling;
	struct list_head thread_group;
	struct list_head thread_node;
	struct wait_queue_head wait_child_queue;
};

struct task_resources {
	struct mm_struct *mm;
	struct files_struct *files;
	struct fs_struct *fs;
	struct sighand_struct *sighand;
	struct signal_struct *signal;
	uid_t uid;
	gid_t gid;
};

struct task_signal_context {
	uint64_t blocked;
	uint64_t pending;
	uint64_t in_handler;
	struct stack_t sas;
	int *clear_child_tid;
	struct robust_list_head *robust_list;
	size_t robust_list_len;
};

struct task_sched_entity {
	struct list_head run_list;
	struct wait_queue_entry wait_entry;
	volatile uint8_t need_resched;
	uint8_t sched_level;
	uint8_t time_slice;
	uint8_t sched_ticks;
	uint64_t enqueue_jiffies;
};

struct task_cputime {
	uint64_t utime_ticks;
	uint64_t stime_ticks;
};

/* ---- 进程控制块 ---- */

struct task_struct {
	struct task_arch_state arch;
	struct task_identity ids;
	struct task_lifecycle lifecycle;
	struct task_links links;
	struct task_resources resources;
	struct task_signal_context sigctx;
	struct task_sched_entity sched;
	struct task_cputime cputime;
	struct task_cputime child_cputime;
};

static_assert(offsetof(struct task_struct, arch.kstack) == TASK_KSTACK,
	      "TASK_KSTACK offset in entry.S out of sync with task_struct");
static_assert(offsetof(struct task_struct, arch.satp) == TASK_SATP,
	      "TASK_SATP offset in entry.S out of sync with task_struct");
static_assert(KSTACK_SIZE == 8192, "entry.S __trapret hardcodes kstack+8192; "
				   "update both if KSTACK_ORDER changes");
static_assert((KSTACK_SIZE - sizeof(struct trap_frame)) %
			      __alignof__(struct trap_frame) ==
		      0,
	      "kernel trap frame must be aligned at the top of each kstack");

/* ---- 全局变量 ---- */

/* idle 进程，PID 0，BSS 段静态分配 */
extern struct task_struct idle_task;

/* 当前正在运行的进程 */
extern struct task_struct *current;

/* PID 1 init 进程。创建后保持有效，用于孤儿进程过继。 */
extern struct task_struct *init_task;

/* ---- 窄访问器 ---- */

static __always_inline __must_check __pure struct mm_struct *
task_mm(struct task_struct *task)
{
	return task ? task->resources.mm : NULL;
}

static inline void task_set_mm(struct task_struct *task, struct mm_struct *mm)
{
	if (task)
		task->resources.mm = mm;
}

static __always_inline __must_check __pure uint64_t
task_address_space_satp(const struct task_struct *task)
{
	return task ? task->arch.satp : 0;
}

static inline void task_set_satp(struct task_struct *task, uint64_t satp)
{
	if (task)
		task->arch.satp = satp;
}

static __always_inline __must_check __pure struct context *
task_context(struct task_struct *task)
{
	return task ? &task->arch.ctx : NULL;
}

static __always_inline __must_check __pure struct trap_frame *
task_trap_frame(struct task_struct *task)
{
	return task ? task->arch.tf : NULL;
}

static inline void task_set_trap_frame(struct task_struct *task,
				       struct trap_frame *tf)
{
	if (task)
		task->arch.tf = tf;
}

static __always_inline __must_check __pure __nonnull(1) __returns_nonnull
	struct trap_frame *task_kernel_trap_frame(struct task_struct *task)
{
	uintptr_t frame = (uintptr_t)task->arch.kstack + KSTACK_SIZE -
			  sizeof(struct trap_frame);

	return (struct trap_frame *)frame;
}

static __always_inline __must_check __pure __nonnull(1)
void *task_kernel_stack(const struct task_struct *task)
{
	return task->arch.kstack;
}

static __always_inline __must_check __pure void *
task_kernel_stack_safe(const struct task_struct *task)
{
	return task ? task_kernel_stack(task) : NULL;
}

static inline void task_set_kernel_stack(struct task_struct *task, void *kstack)
{
	if (task)
		task->arch.kstack = kstack;
}


static __always_inline __must_check __pure __nonnull(1)
struct files_struct *task_files(struct task_struct *task)
{
	return task->resources.files;
}

static __always_inline __must_check __pure struct files_struct *
task_files_safe(struct task_struct *task)
{
	return task ? task_files(task) : NULL;
}

static inline void task_set_files(struct task_struct *task,
				  struct files_struct *files)
{
	if (task)
		task->resources.files = files;
}

static __always_inline __must_check __pure struct fs_struct *
task_fs(struct task_struct *task)
{
	return task ? task->resources.fs : NULL;
}

static __always_inline void task_set_fs(struct task_struct *task,
					struct fs_struct *fs)
{
	if (task)
		task->resources.fs = fs;
}

static __always_inline __must_check __pure __nonnull(1) uid_t
	task_uid(const struct task_struct *task)
{
	return task->resources.uid;
}

static __always_inline __must_check __pure __nonnull(1) gid_t
	task_gid(const struct task_struct *task)
{
	return task->resources.gid;
}

static __always_inline __must_check __pure __nonnull(1) pid_t
	task_pid(const struct task_struct *task)
{
	return task->ids.pid;
}

static __always_inline __must_check __pure __nonnull(1) pid_t
	task_tgid(const struct task_struct *task)
{
	return task->ids.tgid;
}

static __always_inline __must_check __pure __nonnull(1) pid_t
	task_pgid(const struct task_struct *task)
{
	return task->ids.pgid;
}

static __always_inline __nonnull(1) void task_set_pgid(struct task_struct *task,
						       pid_t pgid)
{
	task->ids.pgid = pgid;
}

static __always_inline void task_set_uid(struct task_struct *task, uid_t uid)
{
	BUG_ON(!task);
	task->resources.uid = uid;
}

static __always_inline void task_set_gid(struct task_struct *task, gid_t gid)
{
	BUG_ON(!task);
	task->resources.gid = gid;
}

static __always_inline __must_check __pure bool
task_signal_pending(struct task_struct *task)
{
	return signal_pending(task);
}

static __always_inline __must_check __pure struct signal_struct *
task_signal_state(struct task_struct *task)
{
	return task ? task->resources.signal : NULL;
}

static __always_inline __must_check __pure struct sighand_struct *
task_sighand(struct task_struct *task)
{
	return task ? task->resources.sighand : NULL;
}

static __always_inline void task_set_sighand(struct task_struct *task,
					     struct sighand_struct *sighand)
{
	if (task)
		task->resources.sighand = sighand;
}

static __always_inline void task_set_signal_state(struct task_struct *task,
						  struct signal_struct *signal)
{
	if (task)
		task->resources.signal = signal;
}

static __always_inline __must_check __pure uint64_t
task_blocked_mask(const struct task_struct *task)
{
	return task ? task->sigctx.blocked : 0;
}

static __always_inline void task_set_blocked_mask(struct task_struct *task,
						  uint64_t mask)
{
	if (task)
		task->sigctx.blocked = mask;
}

static __always_inline void task_or_blocked_mask(struct task_struct *task,
						 uint64_t mask)
{
	if (task)
		task->sigctx.blocked |= mask;
}

static __always_inline void task_and_blocked_mask(struct task_struct *task,
						  uint64_t mask)
{
	if (task)
		task->sigctx.blocked &= mask;
}

static __always_inline __must_check __pure uint64_t
task_pending_mask(const struct task_struct *task)
{
	return task ? task->sigctx.pending : 0;
}

static __always_inline void task_set_pending_mask(struct task_struct *task,
						  uint64_t mask)
{
	if (task)
		task->sigctx.pending = mask;
}

static __always_inline void task_or_pending_mask(struct task_struct *task,
						 uint64_t mask)
{
	if (task)
		task->sigctx.pending |= mask;
}

static __always_inline void task_and_pending_mask(struct task_struct *task,
						  uint64_t mask)
{
	if (task)
		task->sigctx.pending &= mask;
}

static __always_inline __must_check __pure uint64_t
task_in_handler_mask(const struct task_struct *task)
{
	return task ? task->sigctx.in_handler : 0;
}

static __always_inline void task_set_in_handler_mask(struct task_struct *task,
						     uint64_t mask)
{
	if (task)
		task->sigctx.in_handler = mask;
}

static __always_inline void task_or_in_handler_mask(struct task_struct *task,
						    uint64_t mask)
{
	if (task)
		task->sigctx.in_handler |= mask;
}

static __always_inline void task_and_in_handler_mask(struct task_struct *task,
						     uint64_t mask)
{
	if (task)
		task->sigctx.in_handler &= mask;
}

static __always_inline __must_check __pure __nonnull(1) __returns_nonnull
	struct stack_t *task_altstack(struct task_struct *task)
{
	return &task->sigctx.sas;
}

static __always_inline __must_check __pure struct stack_t *
task_altstack_safe(struct task_struct *task)
{
	return task ? task_altstack(task) : NULL;
}

static __always_inline __must_check __pure __nonnull(1) uint32_t
task_state(const struct task_struct *task)
{
	return task->lifecycle.state;
}

static __always_inline __must_check __pure uint32_t
task_state_safe(const struct task_struct *task)
{
	return task ? task_state(task) : TASK_DEAD;
}

static __always_inline void task_set_state(struct task_struct *task,
					   uint32_t state)
{
	if (task)
		task->lifecycle.state = state;
}

static __always_inline void task_mark_running(struct task_struct *task)
{
	task_set_state(task, TASK_RUNNING);
}

static __always_inline void
task_mark_interruptible_sleep(struct task_struct *task)
{
	task_set_state(task, TASK_INTERRUPTIBLE);
}

static __always_inline void
task_mark_uninterruptible_sleep(struct task_struct *task)
{
	task_set_state(task, TASK_UNINTERRUPTIBLE);
}

static __always_inline void task_mark_stopped(struct task_struct *task)
{
	task_set_state(task, TASK_STOPPED);
}

static __always_inline __must_check __pure __nonnull(1)
	struct task_struct *task_group_leader(struct task_struct *task)
{
	return task->ids.group_leader;
}

static __always_inline __must_check __pure struct task_struct *
task_group_leader_safe(struct task_struct *task)
{
	return task ? task_group_leader(task) : NULL;
}

static __always_inline __must_check __pure struct task_struct *
task_parent(struct task_struct *task)
{
	return task ? task->links.parent : NULL;
}

static __always_inline __must_check __pure __nonnull(1) __returns_nonnull
	struct list_head *task_children(struct task_struct *task)
{
	return &task->links.children;
}

static __always_inline __must_check __pure struct list_head *
task_children_safe(struct task_struct *task)
{
	return task ? task_children(task) : NULL;
}

static __always_inline __must_check __pure struct wait_queue_head *
task_wait_child_queue(struct task_struct *task)
{
	return task ? &task->links.wait_child_queue : NULL;
}

static __always_inline __must_check __pure bool
task_has_parent_link(const struct task_struct *task)
{
	return task && !list_empty(&task->links.sibling);
}

static __always_inline void task_set_parent(struct task_struct *task,
					    struct task_struct *parent)
{
	if (task)
		task->links.parent = parent;
}

static __always_inline void task_link_child(struct task_struct *parent,
					    struct task_struct *child)
{
	if (!parent || !child)
		return;
	child->links.parent = parent;
	list_add_tail(&child->links.sibling, &parent->links.children);
}

static __always_inline void task_unlink_child(struct task_struct *task)
{
	if (!task || list_empty(&task->links.sibling))
		return;
	list_del_init(&task->links.sibling);
	task->links.parent = NULL;
}

#define task_for_each_child(pos, parent)                                       \
	list_for_each_entry (pos, task_children(parent), links.sibling)

static __always_inline __must_check __pure struct list_head *
task_thread_group(struct task_struct *task)
{
	return task ? &task->links.thread_group : NULL;
}

static __always_inline __must_check __pure struct list_head *
task_thread_node(struct task_struct *task)
{
	return task ? &task->links.thread_node : NULL;
}

static __always_inline void task_link_thread(struct task_struct *leader,
					     struct task_struct *thread)
{
	if (!leader || !thread)
		return;
	list_add_tail(&thread->links.thread_node, &leader->links.thread_group);
}

static __always_inline void task_unlink_thread(struct task_struct *task)
{
	if (!task || list_empty(&task->links.thread_node))
		return;
	list_del_init(&task->links.thread_node);
}

static __always_inline __must_check __pure int *
task_clear_child_tid(struct task_struct *task)
{
	return task ? task->sigctx.clear_child_tid : NULL;
}

static __always_inline void task_set_clear_child_tid(struct task_struct *task,
						     int *uaddr)
{
	if (task)
		task->sigctx.clear_child_tid = uaddr;
}

static __always_inline __must_check __pure uint64_t
task_user_ticks(const struct task_struct *task)
{
	return task ? task->cputime.utime_ticks : 0;
}

static __always_inline __must_check __pure uint64_t
task_system_ticks(const struct task_struct *task)
{
	return task ? task->cputime.stime_ticks : 0;
}

static __always_inline __must_check __pure struct robust_list_head *
task_robust_list(struct task_struct *task)
{
	return task ? task->sigctx.robust_list : NULL;
}

static __always_inline __must_check __pure size_t
task_robust_list_len(struct task_struct *task)
{
	return task ? task->sigctx.robust_list_len : 0;
}

static __always_inline void task_set_robust_list(struct task_struct *task,
						 struct robust_list_head *head,
						 size_t len)
{
	if (!task)
		return;
	task->sigctx.robust_list = head;
	task->sigctx.robust_list_len = len;
}

static __always_inline __must_check __pure int
task_exit_code(struct task_struct *task)
{
	return task ? task->lifecycle.exit_code : 0;
}

static __always_inline void task_set_exit_code(struct task_struct *task,
					       int code)
{
	if (task)
		task->lifecycle.exit_code = code;
}

static __always_inline __must_check __pure int
task_exit_signal(struct task_struct *task)
{
	return task ? task->lifecycle.exit_signal : 0;
}

static __always_inline void task_set_exit_signal(struct task_struct *task,
						 int sig)
{
	if (task)
		task->lifecycle.exit_signal = sig;
}

static __always_inline __must_check __pure struct list_head *
task_run_list(struct task_struct *task)
{
	return task ? &task->sched.run_list : NULL;
}

static __always_inline __must_check __pure struct wait_queue_entry *
task_wait_entry(struct task_struct *task)
{
	return task ? &task->sched.wait_entry : NULL;
}

static __always_inline __must_check __pure bool
task_is_queued(struct task_struct *task)
{
	return task && !list_empty(&task->sched.run_list);
}

static __always_inline __must_check __pure uint8_t
task_need_resched(struct task_struct *task)
{
	return task ? task->sched.need_resched : 0;
}

static __always_inline void task_set_need_resched(struct task_struct *task,
						  uint8_t val)
{
	if (task)
		task->sched.need_resched = val;
}

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
struct task_struct *__must_check task_alloc(void);
int __must_check task_init_resources(struct task_struct *task);
void task_release_resources(struct task_struct *task);

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
struct task_struct *__must_check kernel_thread(void (*fn)(void *), void *arg);

void set_init_task(struct task_struct *task);

bool __must_check __pure task_is_group_leader(const struct task_struct *task);
bool __must_check __pure
task_group_has_other_threads(const struct task_struct *task);
struct task_struct *__must_check __pure task_find_group_leader(pid_t tgid);
struct task_struct *__must_check __pure task_find_thread(pid_t tid);
bool __must_check __pure task_in_thread_group(const struct task_struct *task,
					      pid_t tgid);
bool __must_check __pure task_pgid_exists(pid_t pgid);
void __nonnull(1) task_set_pgid_all(struct task_struct *leader, pid_t pgid);

#endif
