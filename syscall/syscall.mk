# syscall/syscall.mk — 系统调用分发与实现

SYSCALL_OBJS = \
	syscall/syscall.o        \
	syscall/sys_proc.o       \
	syscall/sys_task.o       \
	syscall/sys_file_helpers.o  \
	syscall/sys_file_io.o    \
	syscall/sys_file_path.o  \
	syscall/sys_file_stat.o  \
	syscall/sys_file_poll.o  \
	syscall/sys_exec.o       \
	syscall/sys_mm.o         \
	syscall/sys_signal.o     \
	syscall/sys_futex.o      \
	syscall/sys_log.o        \
	syscall/sys_membarrier.o \
	syscall/sys_misc.o       \
	syscall/sys_sched.o      \
	syscall/sys_rseq.o       \
	syscall/sys_time.o
