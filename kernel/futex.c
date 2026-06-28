#include <kernel/futex.h>
#include <kernel/errno.h>
#include <kernel/list.h>
#include <kernel/mm.h>
#include <kernel/sched.h>
#include <kernel/string.h>
#include <kernel/sync.h>
#include <kernel/task.h>
#include <kernel/timer.h>
#include <kernel/wait.h>
#include <asm/csr.h>
#include <asm/uaccess.h>

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

static bool futex_key_equal(const struct futex_key *a,
			    const struct futex_key *b)
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

static int futex_read_user_value_checked(int *uaddr, int *value)
{
	if (!access_ok(uaddr, sizeof(*uaddr)))
		return -EFAULT;

	bool had_sum = user_access_begin();

	*value = *(volatile int *)uaddr;
	user_access_end(had_sum);
	return 0;
}

static int futex_wait(int *uaddr, int expected,
		      const struct futex_deadline *deadline)
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

	ret = futex_make_key(task_mm(current), uaddr, &key);
	if (ret < 0)
		return ret;
	if (user_range_probe(uaddr, sizeof(*uaddr), false) < 0)
		return -EFAULT;
	has_timeout = deadline && deadline->active;
	expires = has_timeout ? deadline->expires : 0;

	bucket = futex_bucket_for(&key);
	memset(&waiter, 0, sizeof(waiter));
	waiter.key = key;
	init_waitqueue_head(&waiter.wait);
	INIT_LIST_HEAD(&waiter.node);

	spin_lock_irqsave(&bucket->lock, &flags);
	ret = futex_read_user_value_checked(uaddr, &value);
	if (ret < 0) {
		spin_unlock_irqrestore(&bucket->lock, flags);
		return ret;
	}
	if (value != expected) {
		spin_unlock_irqrestore(&bucket->lock, flags);
		return -EAGAIN;
	}
	if (has_timeout && expires <= arch_timer_now()) {
		spin_unlock_irqrestore(&bucket->lock, flags);
		return -ETIMEDOUT;
	}
	local_timer_wait = has_timeout && !sched_has_runnable();

	list_add_tail(&waiter.node, &bucket->waiters);
	prepare_to_wait_interruptible(&waiter.wait);
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
		while (!timer_wait_fired(&timer) && !waiter.woken &&
		       !task_signal_pending(current))
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
	if (task_signal_pending(current))
		return -EINTR;
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

	ret = futex_make_key(task_mm(current), uaddr, &key);
	if (ret < 0)
		return ret;
	if (!access_ok(uaddr, sizeof(*uaddr)))
		return -EFAULT;

	return futex_wake_mm(key.mm, uaddr, nr);
}

static void robust_wake_owner(struct task_struct *task,
			      struct robust_list *node, long futex_offset)
{
	int old_value;
	int new_value;
	int *uaddr;

	if (!task || !task_mm(task) || !node)
		return;

	uaddr = (int *)((char *)node + futex_offset);
	if ((uintptr_t)uaddr & (sizeof(int) - 1))
		return;
	if (copy_from_user(&old_value, uaddr, sizeof(old_value)) != 0)
		return;
	if ((old_value & FUTEX_TID_MASK) != (int)task_pid(task))
		return;

	new_value = (old_value & FUTEX_WAITERS) | FUTEX_OWNER_DIED;
	if (copy_to_user(uaddr, &new_value, sizeof(new_value)) != 0)
		return;

	(void)futex_wake_mm(task_mm(task), uaddr, 1);
}

void futex_exit_robust_list(struct task_struct *task)
{
	struct robust_list_head head;
	struct robust_list_head *head_ptr;
	struct robust_list *entry;
	struct robust_list *pending;

	/*
	 * Linux robust futex ABI requires exit-time traversal of a user-owned
	 * linked list, then OWNER_DIED writeback and futex wake. This is an
	 * explicit ABI side effect, not ordinary syscall argument copying.
	 */
	head_ptr = task_robust_list(task);
	if (!task || !head_ptr)
		return;
	if (task_robust_list_len(task) != sizeof(struct robust_list_head))
		return;
	if (copy_from_user(&head, head_ptr, sizeof(head)) != 0)
		return;

	entry = head.list.next;
	for (int i = 0; entry && entry != &head_ptr->list && i < ROBUST_LIST_LIMIT;
	     i++) {
		struct robust_list current;

		robust_wake_owner(task, entry, head.futex_offset);
		if (copy_from_user(&current, entry, sizeof(current)) != 0)
			break;
		entry = current.next;
	}

	pending = head.list_op_pending;
	if (pending && pending != &head_ptr->list)
		robust_wake_owner(task, pending, head.futex_offset);
}

int futex_set_robust_list(struct task_struct *task,
			  struct robust_list_head *head, size_t len)
{
	if (len != sizeof(struct robust_list_head))
		return -EINVAL;
	if (!task)
		return -EFAULT;

	task_set_robust_list(task, head, len);
	return 0;
}

int futex_get_robust_list(struct task_struct *task,
			  struct robust_list_head **head, size_t *len)
{
	if (!task || !head || !len)
		return -EFAULT;

	*head = task_robust_list(task);
	*len = task_robust_list_len(task);
	return 0;
}

int kernel_futex(int *uaddr, int op, int val,
		 const struct futex_deadline *deadline)
{
	int cmd = op & FUTEX_CMD_MASK;

	switch (cmd) {
	case FUTEX_WAIT:
		return futex_wait(uaddr, val, deadline);
	case FUTEX_WAKE:
		return futex_wake(uaddr, val);
	default:
		return -ENOSYS;
	}
}
