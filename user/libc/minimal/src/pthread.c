#include <pthread.h>
#include <ulib.h>

#define PTHREAD_CONTROL_BYTES 4096UL
#define PTHREAD_STACK_SIZE    (32UL * 1024UL)
#define PTHREAD_MAPPING_SIZE  (PTHREAD_CONTROL_BYTES + PTHREAD_STACK_SIZE)
#define PTHREAD_MAGIC	      0x7074687265616401UL

struct pthread_control {
	unsigned long magic;
	pthread_t tid;
	volatile int tid_word;
	int detached;
	int joined;
	void *mapping;
	size_t mapping_size;
	void *(*start)(void *);
	void *arg;
	void *retval;
	struct pthread_control *next;
};

static volatile int pthread_registry_lock;
static struct pthread_control *pthread_registry;

static void pthread_destroy_control(struct pthread_control *ctrl);

static void pthread_lock_registry(void)
{
	while (__atomic_exchange_n(&pthread_registry_lock, 1,
				   __ATOMIC_ACQUIRE) != 0)
		yield();
}

static void pthread_unlock_registry(void)
{
	__atomic_store_n(&pthread_registry_lock, 0, __ATOMIC_RELEASE);
}

static void pthread_registry_add(struct pthread_control *ctrl)
{
	pthread_lock_registry();
	ctrl->next = pthread_registry;
	pthread_registry = ctrl;
	pthread_unlock_registry();
}

static void pthread_registry_remove_locked(struct pthread_control *ctrl)
{
	struct pthread_control **link;

	link = &pthread_registry;
	while (*link) {
		if (*link == ctrl) {
			*link = ctrl->next;
			ctrl->next = NULL;
			break;
		}
		link = &(*link)->next;
	}
}

static void pthread_registry_remove(struct pthread_control *ctrl)
{
	pthread_lock_registry();
	pthread_registry_remove_locked(ctrl);
	pthread_unlock_registry();
}

static struct pthread_control *pthread_find_locked(pthread_t thread)
{
	struct pthread_control *ctrl;

	for (ctrl = pthread_registry; ctrl; ctrl = ctrl->next) {
		int tid_word =
			__atomic_load_n(&ctrl->tid_word, __ATOMIC_ACQUIRE);

		if (ctrl->tid == thread || (thread != 0 && tid_word != 0 &&
					    (pthread_t)tid_word == thread))
			return ctrl;
	}

	return NULL;
}

static void pthread_reap_detached(void)
{
	struct pthread_control *local = NULL;
	struct pthread_control **link;

	pthread_lock_registry();
	link = &pthread_registry;
	while (*link) {
		struct pthread_control *ctrl = *link;

		if (!ctrl->detached ||
		    __atomic_load_n(&ctrl->tid_word, __ATOMIC_ACQUIRE) != 0) {
			link = &ctrl->next;
			continue;
		}

		*link = ctrl->next;
		ctrl->next = local;
		local = ctrl;
	}
	pthread_unlock_registry();

	while (local) {
		struct pthread_control *next = local->next;

		pthread_destroy_control(local);
		local = next;
	}
}

static struct pthread_control *pthread_current_control(void)
{
	struct pthread_control *ctrl;

	pthread_lock_registry();
	ctrl = pthread_find_locked(pthread_self());
	pthread_unlock_registry();
	return ctrl;
}

pthread_t pthread_self(void)
{
	return (pthread_t)gettid();
}

static void pthread_destroy_control(struct pthread_control *ctrl)
{
	void *mapping;
	size_t mapping_size;

	if (!ctrl)
		return;

	mapping = ctrl->mapping;
	mapping_size = ctrl->mapping_size;
	ctrl->magic = 0;
	munmap(mapping, mapping_size);
}

static int pthread_registry_has_live_threads(void)
{
	struct pthread_control *ctrl;
	int live = 0;

	pthread_lock_registry();
	for (ctrl = pthread_registry; ctrl; ctrl = ctrl->next) {
		if (__atomic_load_n(&ctrl->tid_word, __ATOMIC_ACQUIRE) != 0) {
			live = 1;
			break;
		}
	}
	pthread_unlock_registry();
	return live;
}

static void pthread_exit_main(void *retval)
{
	(void)retval;

	for (;;) {
		pthread_reap_detached();
		if (!pthread_registry_has_live_threads())
			break;
		yield();
	}

	exit(0);
	__builtin_unreachable();
}

void pthread_exit(void *retval)
{
	struct pthread_control *ctrl = pthread_current_control();

	if (ctrl && ctrl->magic == PTHREAD_MAGIC) {
		__atomic_store_n(&ctrl->retval, retval, __ATOMIC_RELEASE);
		exit(0);
		__builtin_unreachable();
	}

	pthread_exit_main(retval);
	__builtin_unreachable();
}

static int pthread_start(void *arg)
{
	struct pthread_control *ctrl = arg;
	void *retval = NULL;

	if (ctrl && ctrl->magic == PTHREAD_MAGIC && ctrl->start)
		retval = ctrl->start(ctrl->arg);
	pthread_exit(retval);
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
		   void *(*start_routine)(void *), void *arg)
{
	void *mapping;
	struct pthread_control *ctrl;
	void *stack_top;
	long child;
	unsigned long flags;

	if (!thread || !start_routine)
		return EINVAL;
	if (attr)
		return EINVAL;

	pthread_reap_detached();
	mapping = mmap(NULL, PTHREAD_MAPPING_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)mapping < 0)
		return EAGAIN;

	ctrl = (struct pthread_control *)mapping;
	memset(ctrl, 0, sizeof(*ctrl));
	ctrl->magic = PTHREAD_MAGIC;
	ctrl->tid_word = -1;
	ctrl->mapping = mapping;
	ctrl->mapping_size = PTHREAD_MAPPING_SIZE;
	ctrl->start = start_routine;
	ctrl->arg = arg;

	pthread_registry_add(ctrl);
	stack_top = (char *)mapping + PTHREAD_MAPPING_SIZE;
	stack_top = (void *)((unsigned long)stack_top & ~15UL);
	flags = CLONE_VM | CLONE_SIGHAND | CLONE_THREAD | CLONE_FILES |
		CLONE_FS | CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID;

	child = clone_thread(flags, stack_top, NULL, 0, (int *)&ctrl->tid_word,
			     pthread_start, ctrl);
	if (child < 0) {
		pthread_registry_remove(ctrl);
		pthread_destroy_control(ctrl);
		return (int)-child;
	}

	ctrl->tid = (pthread_t)child;
	*thread = ctrl->tid;
	return 0;
}

int pthread_join(pthread_t thread, void **retval)
{
	struct pthread_control *ctrl;
	int tid;
	int ret = 0;

	if (thread == pthread_self())
		return EDEADLK;

	pthread_reap_detached();
	pthread_lock_registry();
	ctrl = pthread_find_locked(thread);
	if (!ctrl || ctrl->magic != PTHREAD_MAGIC) {
		ret = ESRCH;
	} else if (ctrl->detached || ctrl->joined) {
		ret = EINVAL;
	} else {
		ctrl->joined = 1;
	}
	pthread_unlock_registry();
	if (ret != 0)
		return ret;

	while ((tid = __atomic_load_n(&ctrl->tid_word, __ATOMIC_ACQUIRE)) != 0)
		(void)futex((int *)&ctrl->tid_word,
			    FUTEX_WAIT | FUTEX_PRIVATE_FLAG, tid, NULL, 0, 0);

	if (retval)
		*retval = __atomic_load_n(&ctrl->retval, __ATOMIC_ACQUIRE);
	pthread_registry_remove(ctrl);
	pthread_destroy_control(ctrl);
	return 0;
}

int pthread_detach(pthread_t thread)
{
	struct pthread_control *ctrl;
	struct pthread_control *destroy = NULL;
	int ret = 0;

	pthread_reap_detached();
	pthread_lock_registry();
	ctrl = pthread_find_locked(thread);
	if (!ctrl || ctrl->magic != PTHREAD_MAGIC) {
		ret = ESRCH;
	} else if (ctrl->joined || ctrl->detached) {
		ret = EINVAL;
	} else {
		ctrl->detached = 1;
		if (__atomic_load_n(&ctrl->tid_word, __ATOMIC_ACQUIRE) == 0) {
			pthread_registry_remove_locked(ctrl);
			destroy = ctrl;
		}
	}
	pthread_unlock_registry();

	if (destroy)
		pthread_destroy_control(destroy);
	if (ret != 0)
		return ret;
	return 0;
}

int pthread_mutex_init(pthread_mutex_t *mutex, const void *attr)
{
	if (!mutex)
		return EINVAL;
	if (attr)
		return EINVAL;
	__atomic_store_n(&mutex->value, 0, __ATOMIC_RELEASE);
	return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
	int expected;

	if (!mutex)
		return EINVAL;

	expected = 0;
	if (__atomic_compare_exchange_n(&mutex->value, &expected, 1, 0,
					__ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
		return 0;

	for (;;) {
		int old =
			__atomic_exchange_n(&mutex->value, 2, __ATOMIC_ACQUIRE);
		if (old == 0)
			return 0;
		(void)futex((int *)&mutex->value,
			    FUTEX_WAIT | FUTEX_PRIVATE_FLAG, 2, NULL, 0, 0);
	}
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	int old;

	if (!mutex)
		return EINVAL;

	old = __atomic_exchange_n(&mutex->value, 0, __ATOMIC_RELEASE);
	if (old == 0)
		return EINVAL;
	if (old == 2)
		(void)futex((int *)&mutex->value,
			    FUTEX_WAKE | FUTEX_PRIVATE_FLAG, 1, NULL, 0, 0);
	return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
	pthread_reap_detached();
	if (!mutex)
		return EINVAL;
	if (__atomic_load_n(&mutex->value, __ATOMIC_ACQUIRE) != 0)
		return EBUSY;
	return 0;
}
