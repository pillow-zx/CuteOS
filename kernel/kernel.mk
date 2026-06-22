# kernel/kernel.mk — 核心内核子系统

KERNEL_OBJS = \
	kernel/printk.o         \
	kernel/ksyms.o          \
	kernel/stacktrace.o     \
	kernel/sync.o           \
	kernel/task.o           \
	kernel/fork.o           \
	kernel/futex.o          \
	kernel/exec.o           \
	kernel/exit.o           \
	kernel/pid.o            \
	kernel/signal.o         \
	kernel/wait.o           \
	kernel/time.o           \
	kernel/init_process.o
