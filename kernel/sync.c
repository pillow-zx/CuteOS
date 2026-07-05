#include <kernel/sync.h>
#include <kernel/sched.h>
#include <kernel/task.h>

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
	irq_flags_t flags;

	BUG_ON(!mutex);

	while (true) {
		spin_lock_irqsave(&mutex->lock, &flags);
		if (!mutex->owner) {
			mutex->owner = current_task();
			spin_unlock_irqrestore(&mutex->lock, flags);
			return;
		}

		prepare_to_wait_uninterruptible(&mutex->wait);
		spin_unlock_irqrestore(&mutex->lock, flags);

		wait_schedule(TASK_UNINTERRUPTIBLE);

		spin_lock_irqsave(&mutex->lock, &flags);
		finish_wait(&mutex->wait);
		spin_unlock_irqrestore(&mutex->lock, flags);
	}
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
