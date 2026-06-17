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

struct files_share_args {
	volatile int state;
	int fd;
};

struct fs_share_args {
	volatile int state;
	long old_umask;
};

static void *stack_top(char *stack)
{
	return (void *)((unsigned long)(stack + STACK_SIZE) & ~15UL);
}

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

static int wait_for_clean_exit(long pid, const char *name)
{
	int status = -1;
	long waited = wait4(pid, &status, 0, NULL);

	if (waited != pid) {
		printf("thread-test: %s wait returned %ld\n", name, waited);
		return -1;
	}
	if (status != 0) {
		printf("thread-test: %s status=%d\n", name, status);
		return -1;
	}
	return 0;
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

static int files_share_main(void *arg)
{
	struct files_share_args *args = arg;

	args->state = close(args->fd) == 0 ? 1 : -1;
	return 0;
}

static int fs_share_main(void *arg)
{
	struct fs_share_args *args = arg;

	args->old_umask = umask(0007);
	args->state = args->old_umask == 0022 ? 1 : -1;
	return 0;
}

static int sighand_share_main(void *arg)
{
	struct sigaction act;

	(void)arg;
	memset(&act, 0, sizeof(act));
	act.sa_handler = SIG_IGN;
	return sigaction(SIGUSR1, &act, NULL) == 0 ? 0 : 1;
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

	struct files_share_args *files_args =
		mmap(NULL, 4096, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)files_args < 0) {
		printf("thread-test: mmap files args failed: %ld\n",
		       (long)files_args);
		return 1;
	}
	memset(files_args, 0, sizeof(*files_args));

	struct fs_share_args *fs_args =
		mmap(NULL, 4096, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)fs_args < 0) {
		printf("thread-test: mmap fs args failed: %ld\n",
		       (long)fs_args);
		return 1;
	}
	memset(fs_args, 0, sizeof(*fs_args));

	char *files_stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
				 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)files_stack < 0) {
		printf("thread-test: mmap files stack failed: %ld\n",
		       (long)files_stack);
		return 1;
	}

	char *fs_stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
			      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)fs_stack < 0) {
		printf("thread-test: mmap fs stack failed: %ld\n",
		       (long)fs_stack);
		return 1;
	}

	char *combo_stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
				 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)combo_stack < 0) {
		printf("thread-test: mmap combo stack failed: %ld\n",
		       (long)combo_stack);
		return 1;
	}

	int parent_tid = 0;
	int child_tid = -1;
	int futex_word = 7;
	unsigned long flags = CLONE_VM | CLONE_SIGHAND | CLONE_THREAD |
			      CLONE_PARENT_SETTID | CLONE_CHILD_SETTID |
			      CLONE_CHILD_CLEARTID;

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
	long wake_child = clone_thread(CLONE_VM | CLONE_SIGHAND | CLONE_THREAD,
				       wake_child_stack, 0, 0, 0,
				       futex_waiter_main, futex_args);
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

	if (clone(CLONE_VM, stack_top(stack), 0, 0, 0) != -EINVAL) {
		printf("thread-test: CLONE_VM without SIGHAND accepted\n");
		return 1;
	}
	if (clone(CLONE_THREAD, 0, 0, 0, 0) != -EINVAL) {
		printf("thread-test: CLONE_THREAD without CLONE_VM accepted\n");
		return 1;
	}
	if (clone(CLONE_VM | CLONE_SIGHAND | CLONE_THREAD, 0, 0, 0, 0) !=
	    -EINVAL) {
		printf("thread-test: CLONE_THREAD without stack accepted\n");
		return 1;
	}
	if (clone(CLONE_VM | CLONE_THREAD, stack_top(stack), 0, 0, 0) !=
	    -EINVAL) {
		printf("thread-test: CLONE_THREAD without SIGHAND accepted\n");
		return 1;
	}
	if (clone(SIGCHLD | CLONE_VM | CLONE_SIGHAND, 0, 0, 0, 0) !=
	    -EINVAL) {
		printf("thread-test: shared VM without stack accepted\n");
		return 1;
	}
	if (clone(SIGCHLD | CLONE_SIGHAND, 0, 0, 0, 0) != -EINVAL) {
		printf("thread-test: CLONE_SIGHAND without CLONE_VM accepted\n");
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

	files_args->fd = dup(1);
	if (files_args->fd < 0) {
		printf("thread-test: dup stdout failed: %ld\n",
		       (long)files_args->fd);
		return 1;
	}
	void *files_child_stack = files_stack + STACK_SIZE;
	files_child_stack =
		(void *)((unsigned long)files_child_stack & ~15UL);
	long files_child = clone_thread(CLONE_VM | CLONE_SIGHAND |
						CLONE_THREAD | CLONE_FILES,
					files_child_stack, 0, 0, 0,
					files_share_main, files_args);
	if (files_child < 0) {
		printf("thread-test: CLONE_FILES clone failed: %ld\n",
		       files_child);
		return 1;
	}
	if (wait_for_state(&files_args->state, 1) < 0) {
		printf("thread-test: CLONE_FILES child state=%ld\n",
		       (long)files_args->state);
		return 1;
	}
	if (write(files_args->fd, "", 0) != -EBADF) {
		printf("thread-test: shared fd remained open\n");
		return 1;
	}

	long original_umask = umask(0022);
	void *fs_child_stack = fs_stack + STACK_SIZE;
	fs_child_stack = (void *)((unsigned long)fs_child_stack & ~15UL);
	long fs_child = clone_thread(CLONE_VM | CLONE_SIGHAND |
					     CLONE_THREAD | CLONE_FS,
				     fs_child_stack, 0, 0, 0, fs_share_main,
				     fs_args);
	if (fs_child < 0) {
		printf("thread-test: CLONE_FS clone failed: %ld\n", fs_child);
		umask((unsigned int)original_umask);
		return 1;
	}
	if (wait_for_state(&fs_args->state, 1) < 0) {
		printf("thread-test: CLONE_FS child state=%ld old=%ld\n",
		       (long)fs_args->state, fs_args->old_umask);
		umask((unsigned int)original_umask);
		return 1;
	}
	if (umask(0022) != 0007) {
		printf("thread-test: shared umask was not observed\n");
		umask((unsigned int)original_umask);
		return 1;
	}
	umask((unsigned int)original_umask);

	int isolated_fd = dup(1);
	if (isolated_fd < 0) {
		printf("thread-test: dup isolated stdout failed: %ld\n",
		       (long)isolated_fd);
		return 1;
	}
	files_args->fd = isolated_fd;
	long isolated_child = clone_thread(SIGCHLD, stack_top(combo_stack), 0,
					   0, 0, files_share_main, files_args);
	if (isolated_child < 0) {
		printf("thread-test: fork-like clone failed: %ld\n",
		       isolated_child);
		close(isolated_fd);
		return 1;
	}
	if (wait_for_clean_exit(isolated_child, "fork-like files copy") < 0) {
		close(isolated_fd);
		return 1;
	}
	if (write(isolated_fd, "", 0) != 0) {
		printf("thread-test: copied fd was closed in parent\n");
		close(isolated_fd);
		return 1;
	}
	close(isolated_fd);

	files_args->fd = dup(1);
	if (files_args->fd < 0) {
		printf("thread-test: dup fork-like stdout failed: %ld\n",
		       (long)files_args->fd);
		return 1;
	}
	long files_proc = clone_thread(SIGCHLD | CLONE_FILES,
				       stack_top(combo_stack), 0, 0, 0,
				       files_share_main, files_args);
	if (files_proc < 0) {
		printf("thread-test: fork-like CLONE_FILES failed: %ld\n",
		       files_proc);
		close(files_args->fd);
		return 1;
	}
	if (wait_for_clean_exit(files_proc, "fork-like CLONE_FILES") < 0)
		return 1;
	if (write(files_args->fd, "", 0) != -EBADF) {
		printf("thread-test: fork-like shared fd remained open\n");
		close(files_args->fd);
		return 1;
	}

	original_umask = umask(0022);
	long fs_copy = clone_thread(SIGCHLD, stack_top(combo_stack), 0, 0, 0,
				    fs_share_main, fs_args);
	if (fs_copy < 0) {
		printf("thread-test: fork-like fs copy failed: %ld\n", fs_copy);
		umask((unsigned int)original_umask);
		return 1;
	}
	if (wait_for_clean_exit(fs_copy, "fork-like fs copy") < 0) {
		umask((unsigned int)original_umask);
		return 1;
	}
	if (umask(0022) != 0022) {
		printf("thread-test: copied umask changed parent\n");
		umask((unsigned int)original_umask);
		return 1;
	}

	long fs_proc = clone_thread(SIGCHLD | CLONE_FS, stack_top(combo_stack),
				    0, 0, 0, fs_share_main, fs_args);
	if (fs_proc < 0) {
		printf("thread-test: fork-like CLONE_FS failed: %ld\n",
		       fs_proc);
		umask((unsigned int)original_umask);
		return 1;
	}
	if (wait_for_clean_exit(fs_proc, "fork-like CLONE_FS") < 0) {
		umask((unsigned int)original_umask);
		return 1;
	}
	if (umask(0022) != 0007) {
		printf("thread-test: fork-like shared umask was not observed\n");
		umask((unsigned int)original_umask);
		return 1;
	}
	umask((unsigned int)original_umask);

	struct sigaction act;
	struct sigaction oldact;

	memset(&act, 0, sizeof(act));
	act.sa_handler = SIG_DFL;
	if (sigaction(SIGUSR1, &act, NULL) < 0) {
		printf("thread-test: reset SIGUSR1 failed\n");
		return 1;
	}
	long sighand_proc = clone_thread(SIGCHLD | CLONE_VM | CLONE_SIGHAND,
					 stack_top(combo_stack), 0, 0, 0,
					 sighand_share_main, NULL);
	if (sighand_proc < 0) {
		printf("thread-test: fork-like CLONE_SIGHAND failed: %ld\n",
		       sighand_proc);
		return 1;
	}
	if (wait_for_clean_exit(sighand_proc,
				"fork-like CLONE_VM|CLONE_SIGHAND") < 0)
		return 1;
	memset(&oldact, 0, sizeof(oldact));
	if (sigaction(SIGUSR1, NULL, &oldact) < 0) {
		printf("thread-test: read SIGUSR1 failed\n");
		return 1;
	}
	if (oldact.sa_handler != SIG_IGN) {
		printf("thread-test: shared sighand was not observed\n");
		return 1;
	}
	if (sigaction(SIGUSR1, &act, NULL) < 0) {
		printf("thread-test: restore SIGUSR1 failed\n");
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
