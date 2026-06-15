/*
 * syscall/sys_misc.c - 轻量兼容系统调用
 *
 * 当前只实现不依赖复杂内核子系统的查询/兼容入口。涉及挂载、epoll、
 * futex、权限模型或随机源的系统调用放在 sys_stub.c 中保留 TODO。
 */

#include <kernel/errno.h>
#include <kernel/mm.h>
#include <kernel/string.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <asm/page.h>
#include <asm/trap.h>

#define UTS_FIELD_LEN 65

struct sys_new_utsname {
	char sysname[UTS_FIELD_LEN];
	char nodename[UTS_FIELD_LEN];
	char release[UTS_FIELD_LEN];
	char version[UTS_FIELD_LEN];
	char machine[UTS_FIELD_LEN];
	char domainname[UTS_FIELD_LEN];
};

struct sys_sysinfo {
	int64_t uptime;
	uint64_t loads[3];
	uint64_t totalram;
	uint64_t freeram;
	uint64_t sharedram;
	uint64_t bufferram;
	uint64_t totalswap;
	uint64_t freeswap;
	uint16_t procs;
	uint16_t pad;
	uint64_t totalhigh;
	uint64_t freehigh;
	uint32_t mem_unit;
	char _f[0];
};

static void uts_copy(char dst[UTS_FIELD_LEN], const char *src)
{
	strncpy(dst, src, UTS_FIELD_LEN);
	dst[UTS_FIELD_LEN - 1] = '\0';
}

ssize_t sys_uname(struct trap_frame *tf)
{
	struct sys_new_utsname *u = (struct sys_new_utsname *)tf->a0;
	struct sys_new_utsname k;

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
	(void)tf;
	/*
	 * TODO(thread): 当前没有 clone 线程和 clear_child_tid futex 语义；
	 * 单线程进程下只需返回当前 TID 以兼容 C 运行库探测。
	 */
	return (ssize_t)current->pid;
}

ssize_t sys_setuid(struct trap_frame *tf)
{
	uint32_t uid = (uint32_t)tf->a0;

	/* TODO(cred): 当前所有进程固定 root 身份，暂不保存 uid。 */
	return uid == 0 ? 0 : -EPERM;
}

ssize_t sys_setgid(struct trap_frame *tf)
{
	uint32_t gid = (uint32_t)tf->a0;

	/* TODO(cred): 当前所有进程固定 root 组，暂不保存 gid。 */
	return gid == 0 ? 0 : -EPERM;
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
	uint32_t old = current->umask;

	current->umask = mask;
	return old;
}

ssize_t sys_sysinfo(struct trap_frame *tf)
{
	struct sys_sysinfo *uinfo = (struct sys_sysinfo *)tf->a0;
	struct sys_sysinfo info;

	if (!uinfo)
		return -EFAULT;

	memset(&info, 0, sizeof(info));
	info.totalram = DRAM_SIZE;
	info.mem_unit = 1;
	/*
	 * TODO(mm): buddy 暂未提供可用页统计接口，freeram 先返回 0。
	 * TODO(sched): 进程表遍历接口补齐后再填写 procs。
	 */
	if (copy_to_user(uinfo, &info, sizeof(info)) != 0)
		return -EFAULT;

	return 0;
}
