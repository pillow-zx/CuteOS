# teste/test.mk — 内核测试代码

TEST_OBJS = \
	test/test.o \
	test/bitmap_test.o \
	test/buddy_test.o \
	test/fs_at_test.o \
	test/hash_test.o \
	test/kthread_test.o \
	test/mm_test.o \
	test/page_cache_metadata_test.o \
	test/page_cache_test.o \
	test/pid_test.o \
	test/resource_test.o \
	test/sched_test.o \
	test/slab_test.o \
	test/sync_test.o \
	test/syscall_compat_test.o \
	test/task_test.o \
	test/timer_test.o \
	test/trap_test.o \
	test/user_trap_test.o \
	test/user_trap_test_stub.o \
	test/virtio_blk_test.o
	
