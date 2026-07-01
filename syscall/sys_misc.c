/*
 * syscall/sys_misc.c - 轻量兼容系统调用
 *
 * 当前只实现不依赖复杂内核子系统的查询/兼容入口。涉及挂载、epoll、
 * futex、权限模型或随机源的系统调用放在 sys_stub.c 中保留 TODO。
 */

#include <kernel/buddy.h>
#include <kernel/errno.h>
#include <kernel/fs.h>
#include <kernel/fs_struct.h>
#include <kernel/mm.h>
#include <kernel/pid.h>
#include <kernel/resource.h>
#include <kernel/string.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <kernel/signal.h>
#include <kernel/timer.h>
#include <uapi/random.h>
#include <uapi/sysinfo.h>
#include <uapi/utsname.h>
#include <asm/page.h>
#include <asm/trap.h>

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
	struct utsname *u = (struct utsname *)tf->a0;
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
	task_set_clear_child_tid(current, (int *)tf->a0);
	return (ssize_t)task_pid(current);
}

ssize_t sys_setuid(struct trap_frame *tf)
{
	uint32_t uid = (uint32_t)tf->a0;

	if (task_uid(current) != 0 && task_uid(current) != uid)
		return -EPERM;

	task_set_uid(current, uid);
	return 0;
}

ssize_t sys_setgid(struct trap_frame *tf)
{
	uint32_t gid = (uint32_t)tf->a0;

	if (task_gid(current) != 0 && task_gid(current) != gid)
		return -EPERM;

	task_set_gid(current, gid);
	return 0;
}

ssize_t sys_getgroups(struct trap_frame *tf)
{
	int size = (int)tf->a0;
	uint32_t *groups = (uint32_t *)tf->a1;
	uint32_t group = 0; /* 当前固定一个补充组 (gid 0) */

	if (size < 0)
		return -EINVAL;
	/*
	 * size == 0 是 libc 探测补充组数量的约定：只返回组数，不写入。
	 * 本内核固定返回 1，与下方 size >= 1 写出单个 gid 0 的语义一致。
	 */
	if (size == 0)
		return 1;
	if (!groups || copy_to_user(groups, &group, sizeof(group)))
		return -EFAULT;

	return 1;
}

ssize_t sys_setgroups(struct trap_frame *tf)
{
	int size = (int)tf->a0;
	uint32_t *groups = (uint32_t *)tf->a1;
	uint32_t group;

	if (size < 0)
		return -EINVAL;
	if (size == 0)
		return 0;
	if (size == 1 && groups &&
	    copy_from_user(&group, groups, sizeof(group)) == 0 && group == 0)
		return 0;

	/* TODO(cred): 需要补充 supplementary groups 存储。 */
	return -EPERM;
}

ssize_t sys_umask(struct trap_frame *tf)
{
	uint32_t mask = (uint32_t)tf->a0 & 0777;

	return fs_set_umask(task_fs(current), mask);
}

ssize_t sys_sysinfo(struct trap_frame *tf)
{
	struct sysinfo *uinfo = (struct sysinfo *)tf->a0;
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

ssize_t sys_prlimit64(struct trap_frame *tf)
{
	long pid = (long)tf->a0;
	int resource = (int)tf->a1;
	const struct rlimit64 *unew = (const struct rlimit64 *)tf->a2;
	struct rlimit64 *uold = (struct rlimit64 *)tf->a3;
	struct task_struct *task;
	struct signal_struct *signal;
	struct rlimit64 new_limit;

	if (resource < 0 || resource >= RLIM_NLIMITS)
		return -EINVAL;
	if (pid < 0)
		return -ESRCH;

	if (pid == 0) {
		task = current;
	} else {
		task = task_find_group_leader(pid);
		if (!task)
			return -ESRCH;
		if (!current || task_tgid(task) != task_tgid(current))
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

static uint64_t random_state;

static uint64_t random_next_u64(void)
{
	uint64_t x = random_state;

	if (x == 0)
		x = arch_timer_now() ^ ((uintptr_t)current << 17) ^
		    0x9e3779b97f4a7c15ULL;

	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	random_state = x;
	return x;
}

ssize_t sys_getrandom(struct trap_frame *tf)
{
	uint8_t *ubuf = (uint8_t *)tf->a0;
	size_t count = (size_t)tf->a1;
	uint32_t flags = (uint32_t)tf->a2;
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
		for (size_t i = 0; i < n; i++) {
			if ((i & 7) == 0) {
				uint64_t r = random_next_u64();

				memcpy(chunk + i, &r,
				       n - i < sizeof(r) ? n - i : sizeof(r));
			}
		}
		if (copy_to_user(ubuf + done, chunk, n) != 0)
			return done ? (ssize_t)done : -EFAULT;
		done += n;
	}

	return (ssize_t)done;
}
