#ifndef _CUTEOS_KERNEL_SYSCALL_TRACE_H
#define _CUTEOS_KERNEL_SYSCALL_TRACE_H

#include <kernel/printk.h>
#include <kernel/syscall_table.h>
#include <kernel/task.h>
#include <kernel/types.h>
#include <uapi/syscall.h>

#if CONFIG_SYSCALL_TRACE
#define SYSCALL_NAME(nr, name, fn) [nr] = name,
static const char *const syscall_names[NR_SYSCALL] = {
	SYSCALL_TABLE(SYSCALL_NAME)};

static __always_inline const char *syscall_trace_name(size_t nr)
{
	if (nr < NR_SYSCALL && syscall_names[nr])
		return syscall_names[nr];
	return "unknown";
}

static __always_inline void syscall_trace_log(size_t nr, const size_t args[6],
					      ssize_t ret)
{
	pr_info("syscall: pid=%d nr=%lu(%s) args=%lx,%lx,%lx,%lx,%lx,%lx "
		"ret=%ld\n",
		current ? current->pid : -1, nr, syscall_trace_name(nr),
		args[0], args[1], args[2], args[3], args[4], args[5],
		(long)ret);
}

#else
static __always_inline const char *syscall_trace_name(size_t nr)
{
	(void)nr;
	return "unknown";
}

static __always_inline void syscall_trace_log(size_t nr, const size_t args[6],
					      ssize_t ret)
{
	(void)nr;
	(void)args;
	(void)ret;
}
#endif

#endif
