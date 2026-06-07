# kernel/kernel.mk — 核心内核子系统

KERNEL_OBJS = \
	kernel/printk.o         \
	kernel/task.o           \
	kernel/fork.o           \
	kernel/exec.o           \
	kernel/exit.o           \
	kernel/sched.o          \
	kernel/pid.o            \
	kernel/signal.o         \
	kernel/wait.o           \
	kernel/time.o           \
	kernel/init_process.o
