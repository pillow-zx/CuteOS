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

.DEFAULT_GOAL := all

include scripts/toolchain.mk
include scripts/build.mk
include scripts/flags.mk

# =============================================================================
# Include Per-Directory Object Lists
# =============================================================================

include filelist.mk

ifeq ($(KERNEL_TEST_ENABLE),1)
include test/test.mk
KERNEL_TEST_OBJS = $(TEST_OBJS)
else
KERNEL_TEST_OBJS =
endif

include user/user.mk

# Aggregate all kernel objects
OBJ_REL = \
	$(ARCH_OBJS)        \
	$(INIT_OBJS)        \
	$(KERNEL_OBJS)      \
	$(MM_OBJS)          \
	$(FS_OBJS)          \
	$(BLOCK_OBJS)       \
	$(DRIVER_OBJS)      \
	$(SCHED_OBJS)       \
	$(SYSCALL_OBJS)     \
	$(KERNEL_TEST_OBJS) \
	$(LIB_OBJS)

# Kernel artifact names
KERNEL_NAME = cuteos
KERNEL      = $(OUTDIR)/$(KERNEL_NAME)
KERNEL_IMG  = $(KERNEL).img
OBJS        = $(addprefix $(OUTDIR)/,$(OBJ_REL))

# =============================================================================
# Build Commands (verbose variants, used when V=1)
# =============================================================================

cmd_CC = $(CC) $(CFLAGS) -c -o $@ $<
cmd_AS = $(CC) $(ASFLAGS) -c -o $@ $<
cmd_LD = $(KERNEL_LD) $(KERNEL_LDFLAGS) $(KERNEL_LD_SCRIPT) -o $@ $(OBJS)
cmd_OBJDUMP_S = $(OBJDUMP) -S $@ > $@.asm
cmd_OBJDUMP_T = $(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $@.sym

cmd_FSIMG = $(MKIMG) $@ $(USER_INIT_ELF) $(USER_SH_ELF) $(filter-out $(USER_INIT_ELF) $(USER_SH_ELF),$(USER_ELFS))

# =============================================================================
# Build Rules
# =============================================================================

# Default target
all: $(KERNEL)

help:
	@printf 'CuteOS build usage:\n'
	@printf '  make                         Build kernel ELF (debug, tests enabled)\n'
	@printf '  make qemu                    Build image and boot QEMU\n'
	@printf '  make qemu-gdb                Boot QEMU paused with GDB stub\n'
	@printf '  make .gdbinit                Generate GDB startup file\n'
	@printf '  make user                    Build user-space ELFs only\n'
	@printf '  make cuteos.img              Build filesystem image\n'
	@printf '  make asm | make sym          Generate disassembly or symbol table\n'
	@printf '  make clean | make clean-user  Remove build artifacts\n'
	@printf '\n'
	@printf 'Common variables:\n'
	@printf '  BUILD=debug|release          Select profile (default: debug)\n'
	@printf '  SANITIZE=none|undefined      Enable UBSan trap instrumentation\n'
	@printf '  RELEASE_LTO=0|1              Toggle release LTO (default: 1)\n'
	@printf '  TOOLPREFIX=<prefix>          Override RISC-V toolchain prefix\n'
	@printf '  V=1                          Print full command lines\n'
	@printf '\n'
	@printf 'Examples:\n'
	@printf '  BUILD=release make\n'
	@printf '  BUILD=release SANITIZE=undefined make\n'
	@printf '  TOOLPREFIX=riscv64-linux-gnu- make qemu\n'

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

$(addprefix $(OUTDIR)/,$(KERNEL_NO_LTO_OBJS)): CFLAGS := $(filter-out -flto%,$(CFLAGS)) $(call cc-option,-fno-lto)

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

# GDB startup file
.gdbinit: .gdbinit.tmpl-riscv FORCE
	$(Q)sed -e "s/:1234/:$(GDBPORT)/" -e "s|@KERNEL@|$(KERNEL)|" < $< > $@

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
qemu-gdb: $(KERNEL) $(KERNEL_IMG) .gdbinit
	@echo "*** Now run 'gdb' in another window (target remote :$(GDBPORT))." 1>&2
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

# Create an EXT2 disk image with the test init program.
$(KERNEL_IMG): $(USER_ELFS) $(MKIMG)
	$(Q)mkdir -p $(dir $@)
	$(call cmd,FSIMG)

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

user: $(USER_ELFS)

clean-user:
	$(Q)rm -rf $(USER_OUTROOT)

clean: clean-user
	$(Q)rm -rf $(OUTROOT)
	$(Q)rm -f .gdbinit

# Prevent deletion of intermediate .o files
.PRECIOUS: $(OUTDIR)/%.o

FORCE:

# Declare phony targets
.PHONY: all help qemu qemu-gdb check-qemu-version clean clean-user FORCE
.PHONY: user format asm sym
.PHONY: $(KERNEL_NAME) $(KERNEL_NAME).img
.PHONY: print-gdbport print-toolprefix
