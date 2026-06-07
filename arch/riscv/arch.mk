# arch/riscv/arch.mk — RISC-V 架构相关对象

ARCH_OBJS = \
	arch/riscv/boot.o              \
	arch/riscv/entry.o             \
	arch/riscv/user_elf.o          \
	arch/riscv/trap.o              \
	arch/riscv/trap_init.o         \
	arch/riscv/timer.o             \
	arch/riscv/plic.o              \
	arch/riscv/sbi.o               \
	arch/riscv/mm/page_table.o     \
	arch/riscv/mm/tlb.o
