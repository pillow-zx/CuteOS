#include <kernel/sync.h>
#include <kernel/sched.h>
#include <kernel/task.h>

static int mutex_lock_probe(struct wait_registrar *registrar, void *arg)
{
	mutex_t *mutex = arg;
	irq_flags_t flags;
	int ret;

	spin_lock_irqsave(&mutex->lock, &flags);
	if (!mutex->owner) {
		mutex->owner = current_task();
		spin_unlock_irqrestore(&mutex->lock, flags);
		return 1;
	}

	ret = wait_register(registrar, &mutex->wait);
	spin_unlock_irqrestore(&mutex->lock, flags);
	return ret;
}

void mutex_init(mutex_t *mutex)
{
	BUG_ON(!mutex);

	mutex->lock.locked = 0;
	mutex->owner = NULL;
	init_waitqueue_head(&mutex->wait);
}

bool mutex_trylock(mutex_t *mutex)
{
	irq_flags_t flags;
	bool locked = false;

	BUG_ON(!mutex);

	spin_lock_irqsave(&mutex->lock, &flags);
	if (!mutex->owner) {
		mutex->owner = current_task();
		locked = true;
	}
	spin_unlock_irqrestore(&mutex->lock, flags);

	return locked;
}

void mutex_lock(mutex_t *mutex)
{
	const struct wait_deadline deadline = {
		.active = false,
	};
	struct wait_source source = {
		.probe = mutex_lock_probe,
		.arg = mutex,
		.registration_limit = 1,
	};
	wait_completion_t completion;
	int ret;

	BUG_ON(!mutex);
	if (mutex_trylock(mutex))
		return;

	ret = wait_complete(&source, 0, &deadline, &completion);
	BUG_ON(ret < 0);
	BUG_ON(completion != WAIT_COMPLETION_EVENT);
}

void mutex_unlock(mutex_t *mutex)
{
	irq_flags_t flags;

	BUG_ON(!mutex);

	spin_lock_irqsave(&mutex->lock, &flags);
	BUG_ON(mutex->owner != current_task());

	mutex->owner = NULL;
	(void)wake_up_one(&mutex->wait);
	spin_unlock_irqrestore(&mutex->lock, flags);
}
