#include <ulib.h>

#define STACK_SIZE (16 * 1024)

struct thread_args {
	volatile int state;
	volatile int release;
	volatile int tid;
	long parent_pid;
};

struct futex_wait_args {
	volatile int state;
	int word;
	long wait_ret;
};

static int wait_for_state(volatile int *addr, int value)
{
	for (int i = 0; i < 100000; i++) {
		if (*addr == value)
			return 0;
		if (*addr < 0)
			return -2;
		yield();
	}

	return -1;
}

static int child_main(void *arg)
{
	struct thread_args *args = arg;

	if (getpid() != args->parent_pid)
		args->state = -1;
	else if (gettid() == args->parent_pid)
		args->state = -2;
	else if (fork() != -EINVAL)
		args->state = -3;
	else
		args->state = 1;

	args->tid = gettid();
	while (args->release == 0)
		yield();
	for (int i = 0; i < 8; i++)
		yield();
	return 0;
}

static int futex_waiter_main(void *arg)
{
	struct futex_wait_args *args = arg;

	args->state = 1;
	args->wait_ret =
		futex(&args->word, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, 1, NULL, 0,
		      0);
	args->state = args->wait_ret == 0 ? 2 : -1;
	return 0;
}

int main(void)
{
	struct thread_args *args = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)args < 0) {
		printf("thread-test: mmap args failed: %ld\n", (long)args);
		return 1;
	}
	memset(args, 0, sizeof(*args));
	args->parent_pid = getpid();

	struct futex_wait_args *futex_args =
		mmap(NULL, 4096, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)futex_args < 0) {
		printf("thread-test: mmap futex args failed: %ld\n",
		       (long)futex_args);
		return 1;
	}
	memset(futex_args, 0, sizeof(*futex_args));
	futex_args->word = 1;

	char *stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)stack < 0) {
		printf("thread-test: mmap stack failed: %ld\n", (long)stack);
		return 1;
	}

	char *wake_stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)wake_stack < 0) {
		printf("thread-test: mmap wake stack failed: %ld\n",
		       (long)wake_stack);
		return 1;
	}

	int parent_tid = 0;
	int child_tid = -1;
	int futex_word = 7;
	unsigned long flags = CLONE_VM | CLONE_THREAD | CLONE_PARENT_SETTID |
				      CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID;

	if (futex(&futex_word, FUTEX_WAIT, 8, NULL, 0, 0) != -EAGAIN) {
		printf("thread-test: FUTEX_WAIT mismatch did not EAGAIN\n");
		return 1;
	}
	if (futex(&futex_word, FUTEX_WAKE, 1, NULL, 0, 0) != 0) {
		printf("thread-test: FUTEX_WAKE without waiters failed\n");
		return 1;
	}
	if (futex((int *)((char *)&futex_word + 1), FUTEX_WAIT, 7, NULL, 0,
		  0) != -EINVAL) {
		printf("thread-test: unaligned futex accepted\n");
		return 1;
	}
	if (futex((int *)0, FUTEX_WAIT, 0, NULL, 0, 0) != -EFAULT) {
		printf("thread-test: bad futex address did not EFAULT\n");
		return 1;
	}
	if (futex(&futex_word, FUTEX_WAIT, 7, &futex_word, 0, 0) != -ENOSYS) {
		printf("thread-test: futex timeout accepted\n");
		return 1;
	}

	void *wake_child_stack = wake_stack + STACK_SIZE;
	wake_child_stack = (void *)((unsigned long)wake_child_stack & ~15UL);
	long wake_child = clone_thread(CLONE_VM | CLONE_THREAD, wake_child_stack,
				       0, 0, 0, futex_waiter_main, futex_args);
	if (wake_child < 0) {
		printf("thread-test: futex waiter clone failed: %ld\n",
		       wake_child);
		return 1;
	}
	if (wait_for_state(&futex_args->state, 1) < 0) {
		printf("thread-test: futex waiter did not start\n");
		return 1;
	}
	for (int i = 0; i < 8; i++)
		yield();
	if (futex(&futex_args->word, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, 1, NULL,
		  0, 0) != 1) {
		printf("thread-test: FUTEX_WAKE did not wake one waiter\n");
		return 1;
	}
	if (wait_for_state(&futex_args->state, 2) < 0) {
		printf("thread-test: futex waiter state=%ld ret=%ld\n",
		       (long)futex_args->state, futex_args->wait_ret);
		return 1;
	}

	if (clone(CLONE_VM, 0, 0, 0, 0) != -EINVAL) {
		printf("thread-test: CLONE_VM without CLONE_THREAD accepted\n");
		return 1;
	}
	if (clone(CLONE_THREAD, 0, 0, 0, 0) != -EINVAL) {
		printf("thread-test: CLONE_THREAD without CLONE_VM accepted\n");
		return 1;
	}
	if (clone(CLONE_VM | CLONE_THREAD, 0, 0, 0, 0) != -EINVAL) {
		printf("thread-test: CLONE_THREAD without stack accepted\n");
		return 1;
	}
	if (clone(CLONE_VM | CLONE_THREAD | CLONE_FILES, 0, 0, 0, 0) !=
	    -EINVAL) {
		printf("thread-test: CLONE_FILES accepted\n");
		return 1;
	}
	if (clone(SIGCHLD | CLONE_CHILD_SETTID, 0, 0, 0, &child_tid) !=
	    -EINVAL) {
		printf("thread-test: fork-like CLONE_CHILD_SETTID accepted\n");
		return 1;
	}
	if (clone(SIGCHLD | CLONE_CHILD_CLEARTID, 0, 0, 0, &child_tid) !=
	    -EINVAL) {
		printf("thread-test: fork-like CLONE_CHILD_CLEARTID accepted\n");
		return 1;
	}
	if (clone(SIGCHLD | CLONE_SETTLS, 0, 0, 1, 0) != -EINVAL) {
		printf("thread-test: fork-like CLONE_SETTLS accepted\n");
		return 1;
	}

	void *child_stack = stack + STACK_SIZE;
	child_stack = (void *)((unsigned long)child_stack & ~15UL);

	long ret = clone_thread(flags, child_stack, &parent_tid, 0,
				&child_tid, child_main, args);
	if (ret < 0) {
		printf("thread-test: clone failed: %ld\n", ret);
		return 1;
	}
	if (parent_tid != ret) {
		printf("thread-test: parent_tid=%d child=%ld\n", parent_tid,
		       ret);
		return 1;
	}
	int wait_ret = wait_for_state(&args->state, 1);
	if (wait_ret < 0) {
		printf("thread-test: child state=%ld wait=%d\n",
		       (long)args->state, wait_ret);
		return 1;
	}
	if (args->tid != ret) {
		printf("thread-test: shared tid=%ld child=%ld\n",
		       (long)args->tid, ret);
		return 1;
	}

	args->release = 1;
	int saw_wake = 0;
	for (int i = 0; child_tid != 0 && i < 1000; i++) {
		long futex_ret = futex(&child_tid,
				       FUTEX_WAIT | FUTEX_PRIVATE_FLAG, child_tid,
				       NULL, 0, 0);
		if (futex_ret == 0) {
			saw_wake = 1;
			continue;
		}
		if (futex_ret == -EAGAIN)
			continue;
		printf("thread-test: futex wait failed: %ld\n", futex_ret);
		return 1;
	}
	if (child_tid != 0 || !saw_wake) {
		printf("thread-test: child_tid=%d saw_wake=%d\n", child_tid,
		       saw_wake);
		return 1;
	}

	printf("thread-test: ok\n");
	return 0;
}
