/*
 * syscall/sys_misc.c - 轻量兼容系统调用
 */

#include <kernel/buddy.h>
#include <kernel/errno.h>
#include <kernel/futex.h>
#include <kernel/fs.h>
#include <kernel/fs_struct.h>
#include <kernel/mm.h>
#include <kernel/pid.h>
#include <kernel/resource.h>
#include <kernel/random.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <kernel/signal.h>
#include <kernel/timer.h>
#include <uapi/random.h>
#include <uapi/sysinfo.h>
#include <uapi/utsname.h>
#include <kernel/page.h>
#include <kernel/trap.h>

#define GRND_VALID_FLAGS (GRND_NONBLOCK | GRND_RANDOM | GRND_INSECURE)

void rlimits_init(struct rlimit64 rlimits[RLIM_NLIMITS])
{
	if (!rlimits)
		return;

	for (int i = 0; i < RLIM_NLIMITS; i++) {
		rlimits[i].rlim_cur = RLIM_INFINITY;
		rlimits[i].rlim_max = RLIM_INFINITY;
	}
	rlimits[RLIMIT_NOFILE].rlim_cur = NR_OPEN;
	rlimits[RLIMIT_NOFILE].rlim_max = NR_OPEN;
}

static void uts_copy(char dst[UTS_FIELD_LEN], const char *src)
{
	size_t len = strnlen(src, UTS_FIELD_LEN - 1);
	memcpy(dst, src, len);
	dst[len] = '\0';
}

ssize_t sys_uname(struct trap_frame *tf)
{
	struct utsname *u = (struct utsname *)syscall_arg(tf, 0);
	struct utsname k;

	if (!u)
		return -EFAULT;

	memset(&k, 0, sizeof(k));
	uts_copy(k.sysname, "CuteOS");
	uts_copy(k.nodename, "cuteos");
	uts_copy(k.release, "0.0.6");
	uts_copy(k.version, "CuteOS teaching kernel");
	uts_copy(k.machine, "riscv64");
	uts_copy(k.domainname, "(none)");

	if (copy_to_user(u, &k, sizeof(k)) != 0)
		return -EFAULT;

	return 0;
}

ssize_t sys_set_tid_addr(struct trap_frame *tf)
{
	task_set_clear_child_tid(current_task(), (int *)syscall_arg(tf, 0));
	return (ssize_t)task_pid(current_task());
}

/*
 * SYSCALL_SUPPORT(B): setuid
 * Current: root may set any uid; non-root may only set its current uid.
 * Unsupported errno: non-root attempts to change to a different uid return
 * -EPERM.
 * Future: replace this with saved/effective uid and capability semantics.
 */
ssize_t sys_setuid(struct trap_frame *tf)
{
	uint32_t uid = (uint32_t)syscall_arg(tf, 0);

	if (task_uid(current_task()) != 0 && task_uid(current_task()) != uid)
		return -EPERM;

	task_set_uid(current_task(), uid);
	return 0;
}

/*
 * SYSCALL_SUPPORT(B): setgid
 * Current: root may set any gid; non-root may only set its current gid.
 * Unsupported errno: non-root attempts to change to a different gid return
 * -EPERM.
 * Future: replace this with saved/effective gid and capability semantics.
 */
ssize_t sys_setgid(struct trap_frame *tf)
{
	uint32_t gid = (uint32_t)syscall_arg(tf, 0);

	if (task_gid(current_task()) != 0 && task_gid(current_task()) != gid)
		return -EPERM;

	task_set_gid(current_task(), gid);
	return 0;
}

/*
 * SYSCALL_SUPPORT(C): getgroups
 * Current: reports a fixed single supplementary group id 0.
 * Unsupported errno: negative size returns -EINVAL; invalid output pointer
 * returns -EFAULT.
 * Future: add real supplementary-group storage with the credential model.
 */
ssize_t sys_getgroups(struct trap_frame *tf)
{
	int size = (int)syscall_arg(tf, 0);
	uint32_t *groups = (uint32_t *)syscall_arg(tf, 1);
	uint32_t group = 0;

	if (size < 0)
		return -EINVAL;

	if (size == 0)
		return 1;
	if (!groups || copy_to_user(groups, &group, sizeof(group)))
		return -EFAULT;

	return 1;
}

/*
 * SYSCALL_SUPPORT(C): setgroups
 * Current: accepts size 0 or a single group id 0 without storing state.
 * Unsupported errno: negative size returns -EINVAL; any other group set
 * returns -EPERM.
 * Future: add real supplementary-group storage with the credential model.
 */
ssize_t sys_setgroups(struct trap_frame *tf)
{
	int size = (int)syscall_arg(tf, 0);
	uint32_t *groups = (uint32_t *)syscall_arg(tf, 1);
	uint32_t group;

	if (size < 0)
		return -EINVAL;
	if (size == 0)
		return 0;
	if (size == 1 && groups &&
	    copy_from_user(&group, groups, sizeof(group)) == 0 && group == 0)
		return 0;

	return -EPERM;
}

ssize_t sys_umask(struct trap_frame *tf)
{
	uint32_t mask = (uint32_t)syscall_arg(tf, 0) & 0777;

	return fs_set_umask(task_fs(current_task()), mask);
}

/*
 * SYSCALL_SUPPORT(B): sysinfo
 * Current: reports uptime, total/free RAM, process count, and mem_unit.
 * Unsupported errno: unsupported load, swap, and high-memory fields are zeroed
 * rather than rejected.
 * Future: document zeroed fields or populate them from future accounting.
 */
ssize_t sys_sysinfo(struct trap_frame *tf)
{
	struct sysinfo *uinfo = (struct sysinfo *)syscall_arg(tf, 0);
	struct sysinfo info;

	if (!uinfo)
		return -EFAULT;

	memset(&info, 0, sizeof(info));
	info.uptime = (int64_t)(jiffies / HZ);
	info.totalram = DRAM_SIZE;
	info.freeram = buddy_free_pages() * PAGE_SIZE;
	info.procs = pid_count_tasks();
	info.mem_unit = 1;
	if (copy_to_user(uinfo, &info, sizeof(info)) != 0)
		return -EFAULT;

	return 0;
}

/*
 * SYSCALL_SUPPORT(B): prlimit64
 * Current: gets/sets per-signal rlimits for self or same-thread-group leader.
 * Unsupported errno: invalid resource returns -EINVAL; cross-task access
 * outside the current thread group returns -EPERM.
 * Future: enforce more resources, starting with NOFILE and AS.
 */
ssize_t sys_prlimit64(struct trap_frame *tf)
{
	long pid = (long)syscall_arg(tf, 0);
	int resource = (int)syscall_arg(tf, 1);
	const struct rlimit64 *unew =
		(const struct rlimit64 *)syscall_arg(tf, 2);
	struct rlimit64 *uold = (struct rlimit64 *)syscall_arg(tf, 3);
	struct task_struct *task;
	struct signal_struct *signal;
	struct rlimit64 new_limit;

	if (resource < 0 || resource >= RLIM_NLIMITS)
		return -EINVAL;
	if (pid < 0)
		return -ESRCH;

	if (pid == 0) {
		task = current_task();
	} else {
		task = task_find_group_leader(pid);
		if (!task)
			return -ESRCH;
		if (!current_task() ||
		    task_tgid(task) != task_tgid(current_task()))
			return -EPERM;
	}

	signal = task_signal_state(task);
	if (!signal)
		return -ESRCH;

	if (unew) {
		if (copy_from_user(&new_limit, unew, sizeof(new_limit)) != 0)
			return -EFAULT;
		if (new_limit.rlim_cur > new_limit.rlim_max)
			return -EINVAL;
	}

	mutex_lock(&signal->lock);
	if (uold) {
		struct rlimit64 old = signal->rlimits[resource];

		mutex_unlock(&signal->lock);
		if (copy_to_user(uold, &old, sizeof(old)) != 0)
			return -EFAULT;
		mutex_lock(&signal->lock);
	}
	if (unew)
		signal->rlimits[resource] = new_limit;
	mutex_unlock(&signal->lock);

	return 0;
}

/*
 * SYSCALL_SUPPORT(B): getrusage
 * Current: reports basic self or children CPU time with many fields zeroed.
 * Unsupported errno: unsupported who values return -EINVAL.
 * Future: document or populate memory and I/O accounting fields.
 */
ssize_t sys_getrusage(struct trap_frame *tf)
{
	int who = (int)syscall_arg(tf, 0);
	struct rusage *uusage = (struct rusage *)syscall_arg(tf, 1);
	struct rusage usage;

	if (who != RUSAGE_SELF && who != RUSAGE_CHILDREN)
		return -EINVAL;
	if (!uusage)
		return -EFAULT;

	if (who == RUSAGE_SELF)
		task_rusage_self(current_task(), &usage);
	else
		task_rusage_children(current_task(), &usage);
	if (copy_to_user(uusage, &usage, sizeof(usage)) != 0)
		return -EFAULT;

	return 0;
}

/*
 * SYSCALL_SUPPORT(C): getrandom
 * Current: returns bytes from a weak xorshift/mtime-seeded generator.
 * Unsupported errno: unknown flags return -EINVAL; NULL output with nonzero
 * count returns -EFAULT.
 * Future: mark this weak random source or connect a real entropy source.
 */
ssize_t sys_getrandom(struct trap_frame *tf)
{
	uint8_t *ubuf = (uint8_t *)syscall_arg(tf, 0);
	size_t count = (size_t)syscall_arg(tf, 1);
	uint32_t flags = (uint32_t)syscall_arg(tf, 2);
	uint8_t chunk[64];
	size_t done = 0;

	if (flags & ~GRND_VALID_FLAGS)
		return -EINVAL;
	if (count == 0)
		return 0;
	if (!ubuf)
		return -EFAULT;

	while (done < count) {
		size_t n = count - done;

		if (n > sizeof(chunk))
			n = sizeof(chunk);
		weak_random_bytes(chunk, n);
		if (copy_to_user(ubuf + done, chunk, n) != 0)
			return done ? (ssize_t)done : -EFAULT;
		done += n;
	}

	return (ssize_t)done;
}
