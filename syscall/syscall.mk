# syscall/syscall.mk — 系统调用分发与实现

SYSCALL_OBJS = \
	syscall/syscall.o       \
	syscall/sys_proc.o      \
	syscall/sys_file.o      \
	syscall/sys_mm.o        \
	syscall/sys_signal.o    \
	syscall/sys_misc.o      \
	syscall/sys_stub.o	\
	syscall/sys_time.o
