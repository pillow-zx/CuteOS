#include <kernel/errno.h>
#include <kernel/signal.h>
#include <kernel/task.h>
#include <kernel/test.h>
#include <kernel/timer.h>
#include <kernel/wait.h>

#include "../ktest.h"

struct wait_test_source {
	struct wait_queue_head *first;
	struct wait_queue_head *second;
	struct wait_deadline *deadline;
	uint32_t probes;
	uint32_t ready_probe;
	uint32_t wake_probe;
	uint32_t timeout_probe;
	bool duplicate_first;
};

static int wait_test_probe(struct wait_registrar *registrar, void *arg)
{
	struct wait_test_source *source = arg;
	int ret;

	source->probes++;
	if (source->first) {
		ret = wait_register(registrar, source->first);
		if (ret < 0)
			return ret;
		if (source->duplicate_first) {
			ret = wait_register(registrar, source->first);
			if (ret < 0)
				return ret;
		}
	}
	if (source->second) {
		ret = wait_register(registrar, source->second);
		if (ret < 0)
			return ret;
	}
	if (source->wake_probe == source->probes)
		wake_up(source->first);
	if (source->timeout_probe == source->probes)
		source->deadline->expires = arch_timer_now();
	return source->ready_probe == source->probes;
}

static int wait_test_error_probe(struct wait_registrar *registrar, void *arg)
{
	struct wait_queue_head **queues = arg;
	int ret;

	ret = wait_register(registrar, queues[0]);
	if (ret < 0)
		return ret;
	return wait_register(registrar, queues[1]);
}

int test_wait_complete_timeout(void)
{
	struct wait_deadline deadline = wait_deadline_at(arch_timer_now());
	wait_completion_t completion = 99;

	TEST_BEGIN("wait completion: timeout");
	{
		TEST_ASSERT_EQ(wait_complete(NULL, 0, &deadline, &completion), 0);
		TEST_ASSERT_EQ(completion,
			       (wait_completion_t)WAIT_COMPLETION_TIMEOUT);
		TEST_ASSERT_EQ(task_state(current_task()),
			       (uint32_t)TASK_RUNNING);
	}
	TEST_END("wait completion: timeout");
	return __test_ret;
fail:
	TEST_FAIL("wait completion: timeout", "see above");
	return __test_ret;
}

int test_wait_complete_event(void)
{
	struct wait_queue_head wait_queue;
	struct wait_deadline deadline = wait_deadline_none();
	struct wait_test_source test_source = { 0 };
	struct wait_source source;
	wait_completion_t completion = 0;

	TEST_BEGIN("wait completion: event");
	{
		init_waitqueue_head(&wait_queue);
		test_source.first = &wait_queue;
		test_source.ready_probe = 1;
		source = (struct wait_source){
			.probe = wait_test_probe,
			.arg = &test_source,
			.registration_limit = 1,
		};
		TEST_ASSERT_EQ(wait_complete(&source, 0, &deadline, &completion),
			       0);
		TEST_ASSERT_EQ(completion,
			       (wait_completion_t)WAIT_COMPLETION_EVENT);
		TEST_ASSERT(list_empty(&wait_queue.task_list));
		TEST_ASSERT_EQ(task_state(current_task()),
			       (uint32_t)TASK_RUNNING);
	}
	TEST_END("wait completion: event");
	return __test_ret;
fail:
	TEST_FAIL("wait completion: event", "see above");
	return __test_ret;
}

int test_wait_complete_spurious_retry(void)
{
	struct wait_queue_head wait_queue;
	struct wait_deadline deadline = wait_deadline_at(UINT64_MAX);
	struct wait_test_source test_source = { 0 };
	struct wait_source source;
	wait_completion_t completion = 0;

	TEST_BEGIN("wait completion: spurious retry");
	{
		init_waitqueue_head(&wait_queue);
		test_source.first = &wait_queue;
		test_source.deadline = &deadline;
		test_source.wake_probe = 2;
		test_source.timeout_probe = 3;
		source = (struct wait_source){
			.probe = wait_test_probe,
			.arg = &test_source,
			.registration_limit = 1,
		};
		TEST_ASSERT_EQ(wait_complete(&source, 0, &deadline, &completion),
			       0);
		TEST_ASSERT_EQ(completion,
			       (wait_completion_t)WAIT_COMPLETION_TIMEOUT);
		TEST_ASSERT_EQ(test_source.probes, (uint32_t)3);
		TEST_ASSERT(list_empty(&wait_queue.task_list));
		TEST_ASSERT_EQ(task_state(current_task()),
			       (uint32_t)TASK_RUNNING);
	}
	TEST_END("wait completion: spurious retry");
	return __test_ret;
fail:
	TEST_FAIL("wait completion: spurious retry", "see above");
	return __test_ret;
}

int test_wait_complete_priority(void)
{
	struct wait_queue_head wait_queue;
	struct wait_deadline deadline = wait_deadline_at(arch_timer_now());
	struct wait_test_source test_source = { 0 };
	struct wait_source source;
	uint64_t saved_pending = current_task()->sigctx.pending;
	uint64_t saved_blocked = current_task()->sigctx.blocked;
	wait_completion_t completion = 0;

	TEST_BEGIN("wait completion: priority");
	{
		init_waitqueue_head(&wait_queue);
		current_task()->sigctx.blocked &= ~signal_mask(SIGUSR1);
		TEST_ASSERT_EQ(send_current_signal(SIGUSR1), 0);
		test_source.first = &wait_queue;
		test_source.ready_probe = 1;
		source = (struct wait_source){
			.probe = wait_test_probe,
			.arg = &test_source,
			.registration_limit = 1,
		};
		TEST_ASSERT_EQ(wait_complete(&source, WAIT_F_INTERRUPTIBLE,
					     &deadline, &completion),
			       0);
		TEST_ASSERT_EQ(completion,
			       (wait_completion_t)WAIT_COMPLETION_EVENT);

		test_source.ready_probe = 0;
		test_source.probes = 0;
		completion = 0;
		TEST_ASSERT_EQ(wait_complete(&source, WAIT_F_INTERRUPTIBLE,
					     &deadline, &completion),
			       0);
		TEST_ASSERT_EQ(completion,
			       (wait_completion_t)WAIT_COMPLETION_SIGNAL);
		TEST_ASSERT(list_empty(&wait_queue.task_list));
	}
	TEST_END("wait completion: priority");
	goto cleanup;
fail:
	TEST_FAIL("wait completion: priority", "see above");
cleanup:
	current_task()->sigctx.pending = saved_pending;
	current_task()->sigctx.blocked = saved_blocked;
	return __test_ret;
}

int test_wait_complete_wake_before_block(void)
{
	struct wait_queue_head wait_queue;
	struct wait_deadline deadline = wait_deadline_none();
	struct wait_test_source test_source = { 0 };
	struct wait_source source;
	wait_completion_t completion = 0;

	TEST_BEGIN("wait completion: wake before block");
	{
		init_waitqueue_head(&wait_queue);
		test_source.first = &wait_queue;
		test_source.wake_probe = 2;
		test_source.ready_probe = 3;
		source = (struct wait_source){
			.probe = wait_test_probe,
			.arg = &test_source,
			.registration_limit = 1,
		};
		TEST_ASSERT_EQ(wait_complete(&source, 0, &deadline, &completion),
			       0);
		TEST_ASSERT_EQ(completion,
			       (wait_completion_t)WAIT_COMPLETION_EVENT);
		TEST_ASSERT_EQ(test_source.probes, (uint32_t)3);
		TEST_ASSERT(list_empty(&wait_queue.task_list));
	}
	TEST_END("wait completion: wake before block");
	return __test_ret;
fail:
	TEST_FAIL("wait completion: wake before block", "see above");
	return __test_ret;
}

int test_wait_complete_registration(void)
{
	struct wait_queue_head first;
	struct wait_queue_head second;
	struct wait_deadline deadline = wait_deadline_none();
	struct wait_test_source test_source = { 0 };
	struct wait_source source;
	wait_completion_t completion = 0;

	TEST_BEGIN("wait completion: registration");
	{
		init_waitqueue_head(&first);
		init_waitqueue_head(&second);
		test_source.first = &first;
		test_source.second = &second;
		test_source.duplicate_first = true;
		test_source.ready_probe = 1;
		source = (struct wait_source){
			.probe = wait_test_probe,
			.arg = &test_source,
			.registration_limit = 2,
		};
		TEST_ASSERT_EQ(wait_complete(&source, 0, &deadline, &completion),
			       0);
		TEST_ASSERT_EQ(completion,
			       (wait_completion_t)WAIT_COMPLETION_EVENT);
		TEST_ASSERT(list_empty(&first.task_list));
		TEST_ASSERT(list_empty(&second.task_list));
	}
	TEST_END("wait completion: registration");
	return __test_ret;
fail:
	TEST_FAIL("wait completion: registration", "see above");
	return __test_ret;
}

int test_wait_complete_partial_error_cleanup(void)
{
	struct wait_queue_head first;
	struct wait_queue_head second;
	struct wait_queue_head *queues[2];
	struct wait_deadline deadline = wait_deadline_none();
	struct wait_source source;
	wait_completion_t completion = 99;

	TEST_BEGIN("wait completion: partial error cleanup");
	{
		init_waitqueue_head(&first);
		init_waitqueue_head(&second);
		queues[0] = &first;
		queues[1] = &second;
		source = (struct wait_source){
			.probe = wait_test_error_probe,
			.arg = queues,
			.registration_limit = 1,
		};
		TEST_ASSERT_EQ(wait_complete(&source, 0, &deadline, &completion),
			       -E2BIG);
		TEST_ASSERT_EQ(completion, (wait_completion_t)0);
		TEST_ASSERT(list_empty(&first.task_list));
		TEST_ASSERT(list_empty(&second.task_list));
		TEST_ASSERT_EQ(task_state(current_task()),
			       (uint32_t)TASK_RUNNING);
	}
	TEST_END("wait completion: partial error cleanup");
	return __test_ret;
fail:
	TEST_FAIL("wait completion: partial error cleanup", "see above");
	return __test_ret;
}

int test_wait_complete_signal_only(void)
{
	struct wait_deadline deadline = wait_deadline_none();
	uint64_t saved_pending = current_task()->sigctx.pending;
	uint64_t saved_blocked = current_task()->sigctx.blocked;
	wait_completion_t completion = 0;

	TEST_BEGIN("wait completion: signal only");
	{
		current_task()->sigctx.blocked &= ~signal_mask(SIGUSR1);
		TEST_ASSERT_EQ(send_current_signal(SIGUSR1), 0);
		TEST_ASSERT_EQ(wait_complete(NULL, WAIT_F_INTERRUPTIBLE,
					     &deadline, &completion),
			       0);
		TEST_ASSERT_EQ(completion,
			       (wait_completion_t)WAIT_COMPLETION_SIGNAL);
	}
	TEST_END("wait completion: signal only");
	goto cleanup;
fail:
	TEST_FAIL("wait completion: signal only", "see above");
cleanup:
	current_task()->sigctx.pending = saved_pending;
	current_task()->sigctx.blocked = saved_blocked;
	return __test_ret;
}

int test_wait_complete_validation(void)
{
	struct wait_deadline no_deadline = wait_deadline_none();
	struct wait_deadline deadline = wait_deadline_at(arch_timer_now());
	struct wait_source source = { 0 };
	wait_completion_t completion = 99;

	TEST_BEGIN("wait completion: validation");
	{
		completion = 99;
		TEST_ASSERT_EQ(wait_complete(NULL, 0, NULL, &completion),
			       -EINVAL);
		TEST_ASSERT_EQ(completion, (wait_completion_t)0);
		TEST_ASSERT_EQ(wait_complete(NULL, 0, &deadline, NULL),
			       -EINVAL);
		TEST_ASSERT_EQ(wait_complete(NULL, WAIT_F_MASK << 1, &deadline,
					     &completion),
			       -EINVAL);
		TEST_ASSERT_EQ(completion, (wait_completion_t)0);
		completion = 99;
		TEST_ASSERT_EQ(wait_complete(NULL, 0, &no_deadline,
					     &completion),
			       -EINVAL);
		TEST_ASSERT_EQ(completion, (wait_completion_t)0);
		source.registration_limit = 1;
		completion = 99;
		TEST_ASSERT_EQ(wait_complete(&source, 0, &deadline, &completion),
			       -EINVAL);
		TEST_ASSERT_EQ(completion, (wait_completion_t)0);
		source.probe = wait_test_probe;
		source.registration_limit = WAIT_REGISTRAR_MAX_ENTRIES + 1;
		completion = 99;
		TEST_ASSERT_EQ(wait_complete(&source, 0, &deadline, &completion),
			       -EINVAL);
		TEST_ASSERT_EQ(completion, (wait_completion_t)0);
		task_mark_interruptible_sleep(current_task());
		completion = 99;
		TEST_ASSERT_EQ(wait_complete(NULL, 0, &deadline, &completion),
			       -EINVAL);
		TEST_ASSERT_EQ(completion, (wait_completion_t)0);
		TEST_ASSERT_EQ(task_state(current_task()),
			       (uint32_t)TASK_RUNNING);
	}
	TEST_END("wait completion: validation");
	return __test_ret;
fail:
	TEST_FAIL("wait completion: validation", "see above");
	return __test_ret;
}
