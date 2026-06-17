#include <kernel/futex.h>
#include <kernel/errno.h>
#include <kernel/list.h>
#include <kernel/mm.h>
#include <kernel/sched.h>
#include <kernel/string.h>
#include <kernel/sync.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <kernel/timer.h>
#include <kernel/wait.h>
#include <asm/csr.h>
#include <asm/trap.h>

#define FUTEX_BUCKETS 32
#define ROBUST_LIST_LIMIT 2048

struct futex_key {
	struct mm_struct *mm;
	uintptr_t uaddr;
};

struct futex_waiter {
	struct futex_key key;
	struct wait_queue_head wait;
	struct list_head node;
	bool woken;
};

struct futex_bucket {
	spinlock_t lock;
	struct list_head waiters;
};

static struct futex_bucket futex_buckets[FUTEX_BUCKETS];

void futex_init(void)
{
	for (int i = 0; i < FUTEX_BUCKETS; i++) {
		futex_buckets[i].lock.locked = 0;
		INIT_LIST_HEAD(&futex_buckets[i].waiters);
	}
}

static bool futex_key_equal(const struct futex_key *a, const struct futex_key *b)
{
	return a->mm == b->mm && a->uaddr == b->uaddr;
}

static struct futex_bucket *futex_bucket_for(const struct futex_key *key)
{
	uintptr_t hash = ((uintptr_t)key->mm >> 3) ^ (key->uaddr >> 2);

	return &futex_buckets[hash & (FUTEX_BUCKETS - 1)];
}

static int futex_make_key(struct mm_struct *mm, int *uaddr,
			  struct futex_key *key)
{
	if (!mm || !uaddr)
		return -EFAULT;
	if ((uintptr_t)uaddr & (sizeof(int) - 1))
		return -EINVAL;

	key->mm = mm;
	key->uaddr = (uintptr_t)uaddr;
	return 0;
}

static int futex_read_user_value(int *uaddr, int *value)
{
	bool had_sum = user_access_begin();

	*value = *(volatile int *)uaddr;
	user_access_end(had_sum);
	return 0;
}

struct futex_timespec {
	int64_t tv_sec;
	int64_t tv_nsec;
};

static int futex_timeout_deadline(const void *timeout, bool *has_timeout,
				  uint64_t *expires)
{
	struct futex_timespec ts;
	uint64_t now;
	uint64_t max_delta;
	uint64_t delta;
	uint64_t nsec_delta;

	BUG_ON(!has_timeout);
	BUG_ON(!expires);

	*has_timeout = false;
	*expires = 0;
	if (!timeout)
		return 0;

	if (copy_from_user(&ts, timeout, sizeof(ts)) != 0)
		return -EFAULT;
	if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000LL)
		return -EINVAL;

	now = get_mtime();
	max_delta = UINT64_MAX - now;
	if ((uint64_t)ts.tv_sec > max_delta / MTIME_FREQ) {
		delta = max_delta;
	} else {
		delta = (uint64_t)ts.tv_sec * MTIME_FREQ;
		nsec_delta =
			((uint64_t)ts.tv_nsec * MTIME_FREQ + 999999999ULL) /
			1000000000ULL;
		if (nsec_delta > max_delta - delta)
			delta = max_delta;
		else
			delta += nsec_delta;
	}

	*has_timeout = true;
	*expires = now + delta;
	return 0;
}

static int futex_wait(int *uaddr, int expected, const void *timeout)
{
	struct futex_key key;
	struct futex_bucket *bucket;
	struct futex_waiter waiter;
	struct timer_wait timer;
	irq_flags_t flags;
	int ret;
	int value;
	bool has_timeout;
	bool local_timer_wait;
	bool timer_started = false;
	bool enabled_irq_for_sleep = false;
	uint64_t expires;

	ret = futex_make_key(current ? current->mm : NULL, uaddr, &key);
	if (ret < 0)
		return ret;
	if (user_range_probe(uaddr, sizeof(*uaddr), false) < 0)
		return -EFAULT;
	ret = futex_timeout_deadline(timeout, &has_timeout, &expires);
	if (ret < 0)
		return ret;

	bucket = futex_bucket_for(&key);
	memset(&waiter, 0, sizeof(waiter));
	waiter.key = key;
	init_waitqueue_head(&waiter.wait);
	INIT_LIST_HEAD(&waiter.node);

	spin_lock_irqsave(&bucket->lock, &flags);
	futex_read_user_value(uaddr, &value);
	if (value != expected) {
		spin_unlock_irqrestore(&bucket->lock, flags);
		return -EAGAIN;
	}
	if (has_timeout && expires <= get_mtime()) {
		spin_unlock_irqrestore(&bucket->lock, flags);
		return -ETIMEDOUT;
	}
	local_timer_wait = has_timeout && !sched_has_runnable();

	list_add_tail(&waiter.node, &bucket->waiters);
	prepare_to_wait_locked(&waiter.wait);
	if (has_timeout) {
		timer_wait_init(&timer, current, expires);
		timer_wait_start(&timer);
		timer_started = true;
	}
	spin_unlock_irqrestore(&bucket->lock, flags);

	if (timer_started && irqs_disabled()) {
		csr_set(sstatus, SSTATUS_SIE);
		enabled_irq_for_sleep = true;
	}
	if (local_timer_wait) {
		/* No runnable peer exists, so wait here for the timer IRQ. */
		while (!timer_wait_fired(&timer) && !waiter.woken)
			wfi();
	} else {
		schedule();
	}
	if (enabled_irq_for_sleep)
		csr_clear(sstatus, SSTATUS_SIE);

	if (timer_started)
		timer_wait_cancel(&timer);

	spin_lock_irqsave(&bucket->lock, &flags);
	finish_wait(&waiter.wait);
	if (!list_empty(&waiter.node))
		list_del_init(&waiter.node);
	spin_unlock_irqrestore(&bucket->lock, flags);

	if (waiter.woken)
		return 0;
	if (timer_started && timer_wait_fired(&timer))
		return -ETIMEDOUT;

	return 0;
}

int futex_wake_mm(struct mm_struct *mm, int *uaddr, int nr)
{
	struct futex_key key;
	struct futex_bucket *bucket;
	struct list_head *pos;
	irq_flags_t flags;
	int ret;
	int woken = 0;

	if (nr <= 0)
		return 0;

	ret = futex_make_key(mm, uaddr, &key);
	if (ret < 0)
		return ret;

	bucket = futex_bucket_for(&key);

	spin_lock_irqsave(&bucket->lock, &flags);
	list_for_each (pos, &bucket->waiters) {
		struct futex_waiter *waiter =
			list_entry(pos, struct futex_waiter, node);

		if (!futex_key_equal(&waiter->key, &key))
			continue;
		if (!wake_up_locked(&waiter->wait))
			continue;
		waiter->woken = true;
		woken++;
		if (woken >= nr)
			break;
	}
	spin_unlock_irqrestore(&bucket->lock, flags);

	return woken;
}

static int futex_wake(int *uaddr, int nr)
{
	struct futex_key key;
	int ret;

	ret = futex_make_key(current ? current->mm : NULL, uaddr, &key);
	if (ret < 0)
		return ret;
	if (!access_ok(uaddr, sizeof(*uaddr)))
		return -EFAULT;

	return futex_wake_mm(key.mm, uaddr, nr);
}

static void robust_wake_owner(struct task_struct *task, struct robust_list *node,
			      long futex_offset)
{
	int old_value;
	int new_value;
	int *uaddr;

	if (!task || !task->mm || !node)
		return;

	uaddr = (int *)((char *)node + futex_offset);
	if ((uintptr_t)uaddr & (sizeof(int) - 1))
		return;
	if (copy_from_user(&old_value, uaddr, sizeof(old_value)) != 0)
		return;
	if ((old_value & FUTEX_TID_MASK) != (int)task->pid)
		return;

	new_value = (old_value & FUTEX_WAITERS) | FUTEX_OWNER_DIED;
	if (copy_to_user(uaddr, &new_value, sizeof(new_value)) != 0)
		return;

	(void)futex_wake_mm(task->mm, uaddr, 1);
}

void futex_exit_robust_list(struct task_struct *task)
{
	struct robust_list_head head;
	struct robust_list *entry;
	struct robust_list *pending;

	if (!task || !task->robust_list)
		return;
	if (task->robust_list_len != sizeof(struct robust_list_head))
		return;
	if (copy_from_user(&head, task->robust_list, sizeof(head)) != 0)
		return;

	entry = head.list.next;
	for (int i = 0; entry && entry != &task->robust_list->list &&
			i < ROBUST_LIST_LIMIT;
	     i++) {
		struct robust_list current;

		robust_wake_owner(task, entry, head.futex_offset);
		if (copy_from_user(&current, entry, sizeof(current)) != 0)
			break;
		entry = current.next;
	}

	pending = head.list_op_pending;
	if (pending && pending != &task->robust_list->list)
		robust_wake_owner(task, pending, head.futex_offset);
}

ssize_t sys_set_robust_list(struct trap_frame *tf)
{
	struct robust_list_head *head = (struct robust_list_head *)tf->a0;
	size_t len = (size_t)tf->a1;

	if (len != sizeof(struct robust_list_head))
		return -EINVAL;
	if (!current)
		return -EFAULT;

	current->robust_list = head;
	current->robust_list_len = len;
	return 0;
}

ssize_t sys_get_robust_list(struct trap_frame *tf)
{
	long pid = (long)tf->a0;
	struct robust_list_head **uhead = (struct robust_list_head **)tf->a1;
	size_t *ulen = (size_t *)tf->a2;
	struct task_struct *task;
	struct robust_list_head *head;
	size_t len;

	if (!uhead || !ulen)
		return -EFAULT;
	if (pid < 0)
		return -EINVAL;

	task = pid == 0 ? current : task_find_thread((pid_t)pid);
	if (!task)
		return -ESRCH;

	head = task->robust_list;
	len = task->robust_list_len;
	if (copy_to_user(uhead, &head, sizeof(head)) != 0)
		return -EFAULT;
	if (copy_to_user(ulen, &len, sizeof(len)) != 0)
		return -EFAULT;

	return 0;
}

ssize_t sys_futex(struct trap_frame *tf)
{
	int *uaddr = (int *)tf->a0;
	int op = (int)tf->a1;
	int val = (int)tf->a2;
	const void *timeout = (const void *)tf->a3;
	int cmd = op & FUTEX_CMD_MASK;

	switch (cmd) {
	case FUTEX_WAIT:
		return futex_wait(uaddr, val, timeout);
	case FUTEX_WAKE:
		return futex_wake(uaddr, val);
	default:
		return -ENOSYS;
	}
}
