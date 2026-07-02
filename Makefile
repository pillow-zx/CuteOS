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
include scripts/kconfig.mk
include scripts/build.mk
include scripts/flags.mk

# =============================================================================
# Include Per-Directory Object Lists
# =============================================================================

include filelist.mk

ifeq ($(CONFIG_KERNEL_TEST),y)
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
KERNEL_STAGE1 = $(OUTDIR)/$(KERNEL_NAME).stage1
KERNEL_IMG  = $(KERNEL).img
OBJS_NOKSYMS = $(addprefix $(OUTDIR)/,$(OBJ_REL))

ifeq ($(CONFIG_KSYMS),y)
KSYMS_GEN_C = $(OUTDIR)/kernel/ksyms.generated.c
KSYMS_OBJ   = $(OUTDIR)/kernel/ksyms.generated.o
OBJS        = $(OBJS_NOKSYMS) $(KSYMS_OBJ)
else
KSYMS_GEN_C =
KSYMS_OBJ =
OBJS        = $(OBJS_NOKSYMS)
endif

# =============================================================================
# Build Rules
# =============================================================================

# Default target
all: $(KERNEL)

help:
	@printf 'CuteOS build usage:\n'
	@printf '  make                         Build kernel ELF using .config\n'
	@printf '  make defconfig               Reset .config from configs/cuteos_defconfig\n'
	@printf '  make menuconfig              Configure build options\n'
	@printf '  make qemu                    Build image and boot QEMU\n'
	@printf '  make qemu-gdb                Boot QEMU paused with GDB stub\n'
	@printf '  make .gdbinit                Generate GDB startup file\n'
	@printf '  make user                    Build user-space ELFs only\n'
	@printf '  make cuteos.img              Build filesystem image\n'
	@printf '  make analyze                 Run GCC analyzer and extra diagnostics\n'
	@printf '  make tags                    Generate a ctags index for the project\n'
	@printf '  make gtags                   Generate GNU Global tag databases\n'
	@printf '  make asm | make sym          Generate disassembly or symbol table\n'
	@printf '  make clean | make clean-user  Remove build artifacts\n'
	@printf '\n'
	@printf 'Common variables:\n'
	@printf '  TOOLPREFIX=<prefix>          Override RISC-V toolchain prefix\n'
	@printf '  V=1                          Print full command lines\n'
	@printf '\n'
	@printf 'Examples:\n'
	@printf '  make defconfig\n'
	@printf '  make tags\n'
	@printf '  make menuconfig\n'
	@printf '  TOOLPREFIX=riscv64-linux-gnu- make qemu\n'

# Backward-compatible aliases
$(KERNEL_NAME): $(KERNEL)

$(KERNEL_NAME).img: $(KERNEL_IMG)

# Link the kernel
$(KERNEL): $(OBJS) kernel.ld
	$(Q)mkdir -p $(dir $@)
	$(QUIET_LD)
	$(Q)$(KERNEL_LD) $(KERNEL_LDFLAGS) $(KERNEL_LD_SCRIPT) -o $@ $(OBJS)
	$(QUIET_OBJDUMP_S)
	$(Q)$(OBJDUMP) -S $@ > $@.asm
	$(QUIET_OBJDUMP_T)
	$(Q)$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $@.sym

ifeq ($(CONFIG_KSYMS),y)
$(KERNEL_STAGE1): $(OBJS_NOKSYMS) kernel.ld
	$(Q)mkdir -p $(dir $@)
	$(QUIET_LD_STAGE1)
	$(Q)$(KERNEL_LD) $(KERNEL_LDFLAGS) $(KERNEL_LD_SCRIPT) -o $@ $(OBJS_NOKSYMS)
	$(QUIET_OBJDUMP_T)
	$(Q)$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $@.sym

$(KSYMS_GEN_C): $(KERNEL_STAGE1) scripts/gen_ksyms.sh
	$(Q)sh scripts/gen_ksyms.sh $(KERNEL_STAGE1).sym $@

$(KSYMS_OBJ): $(KSYMS_GEN_C)
	$(Q)mkdir -p $(dir $@)
	$(QUIET_CC)
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<
endif

# Compile C sources
$(OUTDIR)/%.o: %.c $(AUTOCONF_H)
	$(Q)mkdir -p $(dir $@)
	$(QUIET_CC)
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

ifeq ($(CONFIG_LTO),y)
$(addprefix $(OUTDIR)/,$(KERNEL_NO_LTO_OBJS)): CFLAGS := $(filter-out -flto%,$(CFLAGS)) $(COMMON_NO_LTO_CFLAGS)
endif

# Assemble .S sources
$(OUTDIR)/%.o: %.S $(AUTOCONF_H)
	$(Q)mkdir -p $(dir $@)
	$(QUIET_AS)
	$(Q)$(CC) $(ASFLAGS) -c -o $@ $<

# =============================================================================
# Dependency Inclusion
# =============================================================================

-include $(OBJS:.o=.d)

# =============================================================================
# QEMU Targets
# =============================================================================

# Number of CPU cores (single core for now, SMP ready)
CPUS := $(CONFIG_QEMU_CPUS)

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
QEMUOPTS += -m $(CONFIG_DRAM_SIZE_MB)M
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
$(KERNEL_IMG): $(USER_ELFS) $(MKIMG) $(AUTO_CONF)
	$(Q)mkdir -p $(dir $@)
	$(QUIET_FSIMG)
	$(Q)MKIMG_SIZE_MB=$(CONFIG_ROOTFS_IMAGE_SIZE_MB) $(MKIMG) $@ $(USER_INIT_ELF) $(USER_SH_ELF) $(filter-out $(USER_INIT_ELF) $(USER_SH_ELF),$(USER_ELFS))

# =============================================================================
# Utility Targets
# =============================================================================

# Print GDB port
print-gdbport:
	@echo $(GDBPORT)

# Print compiler prefix
print-toolprefix:
	@echo $(TOOLPREFIX)

# Source index files. Keep generated output and build directories out of tags.
INDEX_PRUNE_DIRS = \( -path './.git' -o -path './build' -o \
		      -path './tools/kconfig/build' -o -path './.cache' \)
CTAGS_SOURCE_EXPR = \( -name '*.[ch]' -o -name '*.S' -o -name '*.s' -o \
		      -name '*.ld' -o -name '*.mk' -o -name 'Makefile' \)
GTAGS_SOURCE_EXPR = \( -name '*.[ch]' -o -name '*.S' -o -name '*.s' \)

tags:
	@command -v ctags >/dev/null 2>&1 || { \
		echo "ERROR: ctags not found"; exit 1; \
	}
	$(Q)tmp=$$(mktemp); \
	trap 'rm -f "$$tmp"' EXIT; \
	find . $(INDEX_PRUNE_DIRS) -prune -o $(CTAGS_SOURCE_EXPR) -print | sort > "$$tmp"; \
	ctags --quiet=yes -f tags -L "$$tmp" --languages=C,Asm,Make \
		--langmap=Asm:+.S.s --langmap=Make:+.mk \
		--fields=+iaS --extras=+q

gtags:
	@command -v gtags >/dev/null 2>&1 || { \
		echo "ERROR: gtags not found"; exit 1; \
	}
	$(Q)tmp=$$(mktemp); \
	trap 'rm -f "$$tmp"' EXIT; \
	find . $(INDEX_PRUNE_DIRS) -prune -o $(GTAGS_SOURCE_EXPR) -print | sort > "$$tmp"; \
	gtags -q --skip-unreadable -f "$$tmp" .

# Run clang-format on C source and header files only
FMT_FILES := $(shell find . \( -name '*.c' -o -name '*.h' \))
format:
	$(Q)clang-format -i $(FMT_FILES)

# GCC static analyzer plus compile-only diagnostics. This pass is too noisy and
# expensive to run as part of the normal build.
ANALYZE_OUT = $(OUTROOT)/analyze
ANALYZE_KERNEL_SRCS = $(wildcard $(OBJ_REL:.o=.c))
ANALYZE_USER_SRCS = user/init/init.c user/init/shell.c
ANALYZE_USER_SRCS += $(USER_BIN_SRCS) $(USER_LIBC_SRCS)

ANALYZE_WARN_CFLAGS = -Wextra
ANALYZE_WARN_CFLAGS += -Wstrict-prototypes
ANALYZE_WARN_CFLAGS += -Wmissing-prototypes
ANALYZE_WARN_CFLAGS += -Wmissing-declarations
ANALYZE_WARN_CFLAGS += -Wold-style-definition
ANALYZE_WARN_CFLAGS += -Wimplicit-fallthrough=5
ANALYZE_WARN_CFLAGS += -Wswitch-enum
ANALYZE_WARN_CFLAGS += -Wcast-align=strict
ANALYZE_WARN_CFLAGS += -Wcast-qual
ANALYZE_WARN_CFLAGS += -Wwrite-strings
ANALYZE_WARN_CFLAGS += -Wpointer-arith
ANALYZE_WARN_CFLAGS += -Warray-bounds=2
ANALYZE_WARN_CFLAGS += -Wstringop-overflow=4
ANALYZE_WARN_CFLAGS += -Wstringop-overread
ANALYZE_WARN_CFLAGS += -Wnull-dereference
ANALYZE_WARN_CFLAGS += -Wstrict-overflow=2
ANALYZE_WARN_CFLAGS += -Wvla
ANALYZE_WARN_CFLAGS += -Wshadow=local
ANALYZE_WARN_CFLAGS += -Wformat=2

ANALYZE_FILTER_OUT = -Werror -MD -flto% -Wno-unknown-attributes

ANALYZE_CFLAGS = $(filter-out $(ANALYZE_FILTER_OUT),$(CFLAGS))
ANALYZE_CFLAGS += -fanalyzer $(ANALYZE_WARN_CFLAGS)
ANALYZE_USER_CFLAGS = $(filter-out $(ANALYZE_FILTER_OUT),$(USER_CFLAGS))
ANALYZE_USER_CFLAGS += -fanalyzer $(ANALYZE_WARN_CFLAGS)

ANALYZE_WERROR ?= 0
ifeq ($(ANALYZE_WERROR),1)
ANALYZE_CFLAGS += -Werror
ANALYZE_USER_CFLAGS += -Werror
endif

ANALYZE_KERNEL_TARGETS = $(addprefix $(ANALYZE_OUT)/kernel/, \
			 $(ANALYZE_KERNEL_SRCS:.c=.analyze))
ANALYZE_USER_TARGETS = $(addprefix $(ANALYZE_OUT)/user/, \
		       $(patsubst user/%.c,%.analyze,$(ANALYZE_USER_SRCS)))

analyze: analyze-kernel analyze-user

analyze-kernel: $(ANALYZE_KERNEL_TARGETS)

analyze-user: $(ANALYZE_USER_TARGETS)

$(ANALYZE_OUT)/kernel/%.analyze: %.c $(AUTOCONF_H) FORCE
	$(QUIET_ANALYZE)
	$(Q)$(CC) $(ANALYZE_CFLAGS) -c -o /dev/null $<

$(ANALYZE_OUT)/user/%.analyze: user/%.c $(AUTOCONF_H) FORCE
	$(QUIET_ANALYZE)
	$(Q)$(USER_CC) $(ANALYZE_USER_CFLAGS) -c -o /dev/null $<

# Disassembly and analysis
ifeq ($(V),1)
QUIET_ASM :=
QUIET_SYM :=
else
QUIET_ASM = @echo '  OBJDUMP $(KERNEL).asm'
QUIET_SYM = @echo '  OBJDUMP $(KERNEL).sym'
endif

asm: $(KERNEL)
	$(QUIET_ASM)
	$(Q)$(OBJDUMP) -S $(KERNEL) > $(KERNEL).asm

sym: $(KERNEL)
	$(QUIET_SYM)
	$(Q)$(OBJDUMP) -t $(KERNEL) | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(KERNEL).sym

# =============================================================================
# Cleanup
# =============================================================================

user: $(USER_ELFS)

clean-user:
	$(Q)rm -rf $(USER_OUTROOT)

clean: clean-user
	$(Q)rm -rf $(OUTROOT)
	$(Q)rm -f .gdbinit
	$(Q)rm -f tags GTAGS GRTAGS GPATH ID

# Prevent deletion of intermediate .o files
.PRECIOUS: $(OUTDIR)/%.o

FORCE:

# Declare phony targets
.PHONY: all help qemu qemu-gdb check-qemu-version clean clean-user FORCE
.PHONY: user format analyze analyze-kernel analyze-user asm sym tags gtags
.PHONY: $(KERNEL_NAME) $(KERNEL_NAME).img
.PHONY: print-gdbport print-toolprefix
