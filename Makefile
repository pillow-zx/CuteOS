# CuteOS Build System
#
# Top-level Makefile for the CuteOS kernel — a RISC-V 64-bit teaching OS.
# Inspired by xv6-riscv but reorganized for a modular kernel layout.
#
# Quick start:
#   make qemu        — build and run in QEMU
#   make qemu-gdb    — run with GDB server stub
#   make clean       — remove build artifacts
#   make format      — run clang-format on all sources

# =============================================================================
# Toolchain Detection
# =============================================================================

ifndef TOOLPREFIX
TOOLPREFIX := $(shell if riscv64-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-linux-gnu-'; \
	elif riscv64-unknown-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-unknown-elf-'; \
	elif riscv64-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-elf-'; \
	elif riscv64-none-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-none-elf-'; \
	elif riscv64-unknown-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-unknown-linux-gnu-'; \
	else echo "***" 1>&2; \
	echo "*** Error: Couldn't find a riscv64 version of GCC/binutils." 1>&2; \
	echo "*** To turn off this error, run 'make TOOLPREFIX= ...'." 1>&2; \
	echo "***" 1>&2; exit 1; fi)
endif

# =============================================================================
# Tools
# =============================================================================

CC       = $(TOOLPREFIX)gcc
LD       = $(TOOLPREFIX)ld
OBJCOPY  = $(TOOLPREFIX)objcopy
OBJDUMP  = $(TOOLPREFIX)objdump
AR       = $(TOOLPREFIX)ar
QEMU     = qemu-system-riscv64

# Minimum QEMU version required
MIN_QEMU_VERSION = 7.2

# =============================================================================
# Verbosity Control (Linux kernel style)
#   make        — short messages (default): "  CC      init/main.o"
#   make V=1    — full command output
# =============================================================================

V ?= 0

ifeq ($(V),1)
  Q :=
else
  Q := @
endif

# Quiet command name table
quiet_cmd_CC = CC
quiet_cmd_AS = AS
quiet_cmd_LD = LD
quiet_cmd_OBJDUMP_S = OBJDUMP
quiet_cmd_OBJDUMP_T = OBJDUMP

# Execute a command with optional quiet output
#   V=0: print "  CC      xxx.o" then run command silently
#   V=1: make echoes the full command line as-is
cmd = $(if $(filter 1,$(V)),$(cmd_$(1)),$(Q)printf '  %-7s %s\n' '$(quiet_cmd_$(1))' '$@' && $(cmd_$(1)))

# =============================================================================
# Include Per-Directory Object Lists
# =============================================================================

include arch/riscv/arch.mk
include init/init.mk
include kernel/kernel.mk
include mm/mm.mk
include fs/fs.mk
include block/block.mk
include drivers/drivers.mk
include syscall/syscall.mk
include lib/lib.mk
include test/test.mk

# Aggregate all kernel objects
OBJ_REL = \
	$(ARCH_OBJS)        \
	$(INIT_OBJS)        \
	$(KERNEL_OBJS)      \
	$(MM_OBJS)          \
	$(FS_OBJS)          \
	$(BLOCK_OBJS)       \
	$(DRIVER_OBJS)      \
	$(SYSCALL_OBJS)     \
	$(TEST_OBJS)	    \
	$(LIB_OBJS)

# Build profile and artifact layout
BUILD    ?= debug
SANITIZE ?= none
OUTROOT  ?= build
OUTDIR   = $(OUTROOT)/$(BUILD)/sanitize-$(SANITIZE)

# Kernel artifact names
KERNEL_NAME = cuteos
KERNEL      = $(OUTDIR)/$(KERNEL_NAME)
KERNEL_IMG  = $(KERNEL).img
OBJS        = $(addprefix $(OUTDIR)/,$(OBJ_REL))

# =============================================================================
# Compilation Flags
# =============================================================================

# Architecture and ABI
CFLAGS  = -march=rv64gc -mabi=lp64 -mcmodel=medany
ASFLAGS = -march=rv64gc -mabi=lp64

# Warning and error control
CFLAGS += -Wall -Werror
CFLAGS += -Wno-unknown-attributes
CFLAGS += -Wno-main

CFLAGS += -fno-omit-frame-pointer

# Language standard
CFLAGS += -std=gnu17

# Include paths
CFLAGS += -I include

# Freestanding environment (no libc)
CFLAGS += -ffreestanding -fno-common -nostdlib -fno-builtin -nostdinc

# Disable stack protector (not needed in kernel)
CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 \
           && echo -fno-stack-protector)

# Disable PIE (for Ubuntu >= 16.10 toolchains)
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]no-pie'),)
CFLAGS += -fno-pie -no-pie
endif
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]nopie'),)
CFLAGS += -fno-pie -nopie
endif

# Suppress "varargs function has no named parameter" etc.
CFLAGS += -Wno-maybe-uninitialized

# Linker flags
LDFLAGS  = -z max-page-size=4096

DEBUG_CFLAGS   = -O0 -g3 -ggdb -gdwarf-4 -DDEBUG
DEBUG_CFLAGS  += -fno-inline -fno-optimize-sibling-calls
DEBUG_ASFLAGS  = -g
DEBUG_LDFLAGS  =

RELEASE_CFLAGS  = -O3 -DNDEBUG
RELEASE_CFLAGS += -ffunction-sections -fdata-sections
RELEASE_ASFLAGS =
RELEASE_LDFLAGS = --gc-sections

ifeq ($(BUILD),debug)
	CFLAGS  += $(DEBUG_CFLAGS)
	ASFLAGS += $(DEBUG_ASFLAGS)
	LDFLAGS += $(DEBUG_LDFLAGS)
else ifeq ($(BUILD),release)
	CFLAGS  += $(RELEASE_CFLAGS)
	ASFLAGS += $(RELEASE_ASFLAGS)
	LDFLAGS += $(RELEASE_LDFLAGS)
else
$(error Unsupported BUILD='$(BUILD)'; expected 'debug' or 'release')
endif

SANITIZE_CFLAGS =
SANITIZE_ASFLAGS =
SANITIZE_LDFLAGS =

ifeq ($(SANITIZE),none)
else ifeq ($(SANITIZE),undefined)
	SANITIZE_CFLAGS += -fsanitize=undefined
	SANITIZE_CFLAGS += -fsanitize-undefined-trap-on-error
	SANITIZE_CFLAGS += -fno-sanitize-recover=all
else
$(error Unsupported SANITIZE='$(SANITIZE)'; expected 'none' or 'undefined')
endif

CFLAGS  += $(SANITIZE_CFLAGS)
ASFLAGS += $(SANITIZE_ASFLAGS)
LDFLAGS += $(SANITIZE_LDFLAGS)

# Dependencies (.d files)
CFLAGS += -MD

# =============================================================================
# Build Commands (verbose variants, used when V=1)
# =============================================================================

cmd_CC = $(CC) $(CFLAGS) -c -o $@ $<
cmd_AS = $(CC) $(ASFLAGS) -c -o $@ $<
cmd_LD = $(LD) $(LDFLAGS) -T kernel.ld -o $@ $(OBJS)
cmd_OBJDUMP_S = $(OBJDUMP) -S $@ > $@.asm
cmd_OBJDUMP_T = $(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $@.sym

# =============================================================================
# Build Rules
# =============================================================================

# Default target
all: $(KERNEL)

# Backward-compatible aliases
$(KERNEL_NAME): $(KERNEL)

$(KERNEL_NAME).img: $(KERNEL_IMG)

# Link the kernel
$(KERNEL): $(OBJS) kernel.ld
	$(Q)mkdir -p $(dir $@)
	$(call cmd,LD)
	$(call cmd,OBJDUMP_S)
	$(call cmd,OBJDUMP_T)

# Compile C sources
$(OUTDIR)/%.o: %.c
	$(Q)mkdir -p $(dir $@)
	$(call cmd,CC)

# Assemble .S sources
$(OUTDIR)/%.o: %.S
	$(Q)mkdir -p $(dir $@)
	$(call cmd,AS)

# =============================================================================
# Dependency Inclusion
# =============================================================================

-include $(OBJS:.o=.d)

# =============================================================================
# QEMU Targets
# =============================================================================

# Number of CPU cores (single core for now, SMP ready)
CPUS := 1

# QEMU GDB port (based on user ID to avoid conflicts)
GDBPORT = $(shell expr `id -u` % 5000 + 25000)

# QEMU GDB stub
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)

# QEMU options for riscv-virt platform
QEMUOPTS  = -machine virt
QEMUOPTS += -kernel $(KERNEL)
QEMUOPTS += -m 256M
QEMUOPTS += -smp $(CPUS)
QEMUOPTS += -nographic
QEMUOPTS += -global virtio-mmio.force-legacy=false
QEMUOPTS += -drive file=$(KERNEL_IMG),if=none,format=raw,id=x0
QEMUOPTS += -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

# Check QEMU version
QEMU_VERSION := $(shell $(QEMU) --version | head -n 1 | sed -E 's/^QEMU emulator version ([0-9]+\.[0-9]+)\..*/\1/')
check-qemu-version:
	@if [ "$(shell echo "$(QEMU_VERSION) >= $(MIN_QEMU_VERSION)" | bc)" -eq 0 ]; then \
		echo "ERROR: Need qemu version >= $(MIN_QEMU_VERSION)"; \
		exit 1; \
	fi

# Run in QEMU
qemu: check-qemu-version $(KERNEL) $(KERNEL_IMG)
	$(QEMU) $(QEMUOPTS)

# Run in QEMU with GDB
qemu-gdb: $(KERNEL) $(KERNEL_IMG)
	@echo "*** Now run 'gdb' in another window (target remote :$(GDBPORT))." 1>&2
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

# Create a blank disk image for testing
$(KERNEL_IMG):
	$(Q)mkdir -p $(dir $@)
	$(Q)dd if=/dev/zero of=$@ bs=1M count=16 2>/dev/null
	$(Q)echo "Created blank disk image: $@ (16MB)"
	$(Q)echo "Format with: mkfs.ext2 $@"

# =============================================================================
# Utility Targets
# =============================================================================

# Print GDB port
print-gdbport:
	@echo $(GDBPORT)

# Print compiler prefix
print-toolprefix:
	@echo $(TOOLPREFIX)

# Run clang-format on C source and header files only
FMT_FILES := $(shell find . \( -name '*.c' -o -name '*.h' \))
format:
	$(Q)clang-format -i $(FMT_FILES)

# Disassembly and analysis
asm: $(KERNEL)
	$(call cmd,OBJDUMP_S)

sym: $(KERNEL)
	$(call cmd,OBJDUMP_T)

# =============================================================================
# Cleanup
# =============================================================================

clean:
	$(Q)rm -rf $(OUTDIR)
	$(Q)rm -f .gdbinit

clean-all:
	$(Q)rm -rf $(OUTROOT)
	$(Q)rm -f .gdbinit

# Prevent deletion of intermediate .o files
.PRECIOUS: $(OUTDIR)/%.o

# Declare phony targets
.PHONY: all qemu qemu-gdb check-qemu-version clean clean-all format asm sym
.PHONY: $(KERNEL_NAME) $(KERNEL_NAME).img
.PHONY: print-gdbport print-toolprefix
