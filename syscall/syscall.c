/*
 * syscall/syscall.c - 系统调用分发
 */

#include <kernel/syscall.h>
#include <kernel/errno.h>
#include <kernel/futex.h>
#include <kernel/printk.h>
#include <kernel/syscall_table.h>
#include <kernel/task.h>
#include <kernel/trap.h>
#include <uapi/futex.h>
#include <uapi/syscall.h>

typedef ssize_t (*syscall_fn_t)(struct trap_frame *);

static syscall_fn_t syscall_table[NR_SYSCALL];

static bool is_restartable(const struct trap_frame *tf, size_t nr)
{
	switch (nr) {
	case SYS_read:
	case SYS_write:
	case SYS_wait4:
		return true;
	case SYS_futex:
		return (syscall_arg(tf, 1) & FUTEX_CMD_MASK) == FUTEX_WAIT &&
		       syscall_arg(tf, 3) == 0;
	default:
		return false;
	}
}

static void restart_save(struct task_struct *task,
				 const struct trap_frame *tf, size_t nr)
{
	struct restart_context *context =
		&task->restart;

	context->pc = trap_user_pc(tf) - 4;
	for (uint32_t index = 0; index < 6; index++)
		context->args[index] = syscall_arg(tf, index);
	context->nr = nr;
	context->valid = true;
	context->restartable = is_restartable(tf, nr);
}

void restart_clear(struct task_struct *task)
{
	if (task)
		memset(&task->restart, 0, sizeof(task->restart));
}

static void restart_finish(struct task_struct *task, ssize_t ret)
{
	if (ret != -EINTR || !task->restart.restartable)
		restart_clear(task);
}

bool restart_for_signal(struct task_struct *task,
				struct trap_frame *tf, bool sa_restart)
{
	struct restart_context *context;

	if (!task)
		return false;
	context = &task->restart;
	if (!context->valid)
		return false;
	if (!sa_restart || !context->restartable) {
		restart_clear(task);
		return false;
	}

	trap_set_user_pc(tf, context->pc);
	trap_set_arg0(tf, context->args[0]);
	tf->a1 = context->args[1];
	tf->a2 = context->args[2];
	tf->a3 = context->args[3];
	tf->a4 = context->args[4];
	tf->a5 = context->args[5];
	tf->a7 = context->nr;
	restart_clear(task);
	return true;
}

void do_syscall(struct trap_frame *tf)
{
	size_t nr = syscall_nr(tf);
	ssize_t ret;
	struct task_struct *task = current_task();

	if (nr == SYS_rt_sigreturn) {
		ret = syscall_table[nr](tf);
		syscall_set_return(tf, ret);
		return;
	}

	if (task)
		restart_save(task, tf, nr);

	if (nr >= NR_SYSCALL || !syscall_table[nr]) {
		ret = -ENOSYS;
		if (task)
			restart_finish(task, ret);
		syscall_set_return(tf, ret);
		return;
	}

	ret = syscall_table[nr](tf);
	if (task)
		restart_finish(task, ret);
	syscall_set_return(tf, ret);
}

void syscall_init(void)
{
	futex_init();

#define INSTALL_SYSCALL(nr, name, fn) syscall_table[nr] = fn;
	SYSCALL_TABLE(INSTALL_SYSCALL)
#undef INSTALL_SYSCALL

	pr_info("syscall: initialized (%d entries)\n", NR_SYSCALL);
}
