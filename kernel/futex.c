#include <kernel/futex.h>
#include <kernel/errno.h>
#include <kernel/list.h>
#include <kernel/mm.h>
#include <kernel/sched.h>
#include <kernel/string.h>
#include <kernel/sync.h>
#include <kernel/syscall.h>
#include <kernel/task.h>
#include <kernel/wait.h>
#include <asm/trap.h>

#define FUTEX_BUCKETS 32

struct futex_key {
	struct mm_struct *mm;
	uintptr_t uaddr;
};

struct futex_waiter {
	struct futex_key key;
	struct wait_queue_head wait;
	struct list_head node;
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

static int futex_wait(int *uaddr, int expected, const void *timeout)
{
	struct futex_key key;
	struct futex_bucket *bucket;
	struct futex_waiter waiter;
	irq_flags_t flags;
	int ret;
	int value;

	if (timeout)
		return -ENOSYS;

	ret = futex_make_key(current ? current->mm : NULL, uaddr, &key);
	if (ret < 0)
		return ret;
	if (user_range_probe(uaddr, sizeof(*uaddr), false) < 0)
		return -EFAULT;

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

	list_add_tail(&waiter.node, &bucket->waiters);
	prepare_to_wait_locked(&waiter.wait);
	spin_unlock_irqrestore(&bucket->lock, flags);

	schedule();

	spin_lock_irqsave(&bucket->lock, &flags);
	finish_wait(&waiter.wait);
	if (!list_empty(&waiter.node))
		list_del_init(&waiter.node);
	spin_unlock_irqrestore(&bucket->lock, flags);

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
