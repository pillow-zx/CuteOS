# Top-level CuteOS build rules.

.DEFAULT_GOAL := all

include scripts/toolchain.mk
include scripts/kconfig.mk
include scripts/build.mk
include scripts/flags.mk

include filelist.mk

KERNEL_SELFTEST ?= 0

ifeq ($(KERNEL_SELFTEST),1)
CFLAGS += -DKERNEL_SELFTEST
ASFLAGS += -DKERNEL_SELFTEST
include test/test.mk
KERNEL_TEST_OBJS = $(TEST_OBJS)
else
KERNEL_TEST_OBJS =
endif

include user/user.mk

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

all: check-gcc-version $(KERNEL)

TEST_OUTROOT ?= build/test
TEST_KERNEL = $(TEST_OUTROOT)/kernel/$(KERNEL_NAME)
TEST_KERNEL_IMG = $(TEST_KERNEL).img

help:
	@printf 'CuteOS build usage:\n'
	@printf '  make                         Build kernel ELF using .config\n'
	@printf '  make defconfig               Reset .config from configs/cuteos_defconfig\n'
	@printf '  make menuconfig              Configure build options\n'
	@printf '  make qemu                    Build image and boot QEMU\n'
	@printf '  make test                    Build and run kernel self-test regression suite\n'
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

$(KERNEL_NAME): $(KERNEL)

$(KERNEL_NAME).img: $(KERNEL_IMG)

$(KERNEL): check-gcc-version $(OBJS) kernel.ld
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

$(OUTDIR)/%.o: %.c $(AUTOCONF_H)
	$(Q)mkdir -p $(dir $@)
	$(QUIET_CC)
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

ifeq ($(CONFIG_LTO),y)
$(addprefix $(OUTDIR)/,$(KERNEL_NO_LTO_OBJS)): CFLAGS := $(filter-out -flto%,$(CFLAGS)) $(COMMON_NO_LTO_CFLAGS)
endif

$(OUTDIR)/%.o: %.S $(AUTOCONF_H)
	$(Q)mkdir -p $(dir $@)
	$(QUIET_AS)
	$(Q)$(CC) $(ASFLAGS) -c -o $@ $<

-include $(OBJS:.o=.d)

CPUS := $(CONFIG_QEMU_CPUS)

GDBPORT = $(shell expr `id -u` % 5000 + 25000)

QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)

.gdbinit: .gdbinit.tmpl-riscv FORCE
	$(Q)sed -e "s/:1234/:$(GDBPORT)/" -e "s|@KERNEL@|$(KERNEL)|" < $< > $@

QEMUOPTS  = -machine virt
QEMUOPTS += -kernel $(KERNEL)
QEMUOPTS += -m $(CONFIG_DRAM_SIZE_MB)M
QEMUOPTS += -smp $(CPUS)
QEMUOPTS += -nographic
QEMUOPTS += -global virtio-mmio.force-legacy=false
QEMUOPTS += -drive file=$(KERNEL_IMG),if=none,format=raw,id=x0
QEMUOPTS += -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

QEMU_VERSION := $(shell $(QEMU) --version | head -n 1 | sed -E 's/^QEMU emulator version ([0-9]+\.[0-9]+)\..*/\1/')
check-qemu-version:
	@if [ "$(shell echo "$(QEMU_VERSION) >= $(MIN_QEMU_VERSION)" | bc)" -eq 0 ]; then \
		echo "ERROR: Need qemu version >= $(MIN_QEMU_VERSION)"; \
		exit 1; \
	fi

check-gcc-version:
	@if case "$(GCC_MAJOR)" in \
		''|*[!0-9]*) false ;; \
		*) [ "$(GCC_MAJOR)" -ge "$(MIN_GCC_MAJOR)" ] ;; \
	esac; then :; else \
		echo "ERROR: $(CC) version $(GCC_VERSION) is unsupported; need GCC >= $(MIN_GCC_MAJOR)."; \
		exit 1; \
	fi

MIN_GCC_MAJOR = 15
GCC_VERSION = $(shell $(CC) -dumpfullversion -dumpversion 2>/dev/null)
GCC_MAJOR = $(word 1,$(subst ., ,$(GCC_VERSION)))

qemu: check-gcc-version check-qemu-version $(KERNEL) $(KERNEL_IMG)
	$(QEMU) $(QEMUOPTS)

test: check-gcc-version check-qemu-version
	$(Q)rm -f $(TEST_KERNEL_IMG)
	$(Q)$(MAKE) KERNEL_SELFTEST=1 OUTROOT=$(TEST_OUTROOT) all $(KERNEL_NAME).img
	$(Q)scripts/run_kernel_tests.sh \
		"$(QEMU)" "$(TEST_KERNEL)" "$(TEST_KERNEL_IMG)" \
		"$(CONFIG_DRAM_SIZE_MB)" "$(CPUS)"

qemu-gdb: check-gcc-version $(KERNEL) $(KERNEL_IMG) .gdbinit
	@echo "*** Now run 'gdb' in another window (target remote :$(GDBPORT))." 1>&2
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

$(KERNEL_IMG): check-gcc-version $(USER_ELFS) $(MKIMG) $(AUTO_CONF)
	$(Q)mkdir -p $(dir $@)
	$(QUIET_FSIMG)
	$(Q)MKIMG_SIZE_MB=$(CONFIG_ROOTFS_IMAGE_SIZE_MB) $(MKIMG) $@ $(USER_INIT_ELF) $(USER_SH_ELF) $(filter-out $(USER_INIT_ELF) $(USER_SH_ELF),$(USER_ELFS))

print-gdbport:
	@echo $(GDBPORT)

print-toolprefix:
	@echo $(TOOLPREFIX)

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

FMT_FILES := $(shell find . \( -name '*.c' -o -name '*.h' \))
format:
	$(Q)clang-format -i $(FMT_FILES)

ANALYZE_OUT = $(OUTROOT)/analyze
ANALYZE_KERNEL_SRCS = $(wildcard $(OBJ_REL:.o=.c))
ANALYZE_WARN_CFLAGS = -Wall
ANALYZE_WARN_CFLAGS += -Wextra
ANALYZE_WARN_CFLAGS += -Wstrict-prototypes
ANALYZE_WARN_CFLAGS += -Wmissing-prototypes
ANALYZE_WARN_CFLAGS += -Wmissing-declarations
ANALYZE_WARN_CFLAGS += -Wold-style-definition
ANALYZE_WARN_CFLAGS += -Wredundant-decls
ANALYZE_WARN_CFLAGS += -Wswitch-enum
ANALYZE_WARN_CFLAGS += -Wimplicit-fallthrough=5
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
ANALYZE_WARN_CFLAGS += -Wstack-usage=2048
ANALYZE_WARN_CFLAGS += -Wframe-larger-than=2048
ANALYZE_WARN_CFLAGS += -Wshadow=local
ANALYZE_WARN_CFLAGS += -Wformat=2
ANALYZE_WARN_CFLAGS += -Wformat-overflow=2
ANALYZE_WARN_CFLAGS += -Wformat-truncation=2
ANALYZE_WARN_CFLAGS += -Wundef
ANALYZE_WARN_CFLAGS += -Waddress
ANALYZE_WARN_CFLAGS += -Wmissing-field-initializers

ANALYZE_FILTER_OUT = -Werror -MD -flto% -Wno-unknown-attributes

ANALYZE_CFLAGS = $(filter-out $(ANALYZE_FILTER_OUT),$(CFLAGS))
ANALYZE_CFLAGS += -fanalyzer
ANALYZE_CFLAGS += -O2
ANALYZE_CFLAGS += -fdiagnostics-show-option
ANALYZE_CFLAGS += -fdiagnostics-show-path-depths
ANALYZE_CFLAGS += -fdiagnostics-path-format=inline-events
ANALYZE_CFLAGS += -Wanalyzer-double-free
ANALYZE_CFLAGS += -Wanalyzer-use-after-free
ANALYZE_CFLAGS += -Wanalyzer-malloc-leak
ANALYZE_CFLAGS += -Wanalyzer-null-dereference
ANALYZE_CFLAGS += -Wanalyzer-possible-null-dereference
ANALYZE_CFLAGS += -Wanalyzer-use-of-uninitialized-value
ANALYZE_CFLAGS += -Wanalyzer-deref-before-check
ANALYZE_CFLAGS += -Wanalyzer-write-to-const
ANALYZE_CFLAGS += -Wanalyzer-shift-count-negative
ANALYZE_CFLAGS += -Wanalyzer-shift-count-overflow
ANALYZE_CFLAGS += -Wanalyzer-tainted-array-index
ANALYZE_CFLAGS += -Wanalyzer-tainted-allocation-size
ANALYZE_CFLAGS += -Wanalyzer-out-of-bounds
ANALYZE_CFLAGS += -Wanalyzer-overlapping-buffers
ANALYZE_CFLAGS += -Wanalyzer-use-of-pointer-in-stale-stack-frame
ANALYZE_CFLAGS += -Wanalyzer-undefined-behavior-ptrdiff
ANALYZE_CFLAGS += -Wanalyzer-va-arg-type-mismatch
ANALYZE_CFLAGS += $(ANALYZE_WARN_CFLAGS)

ANALYZE_WERROR ?= 0
ifeq ($(ANALYZE_WERROR),1)
ANALYZE_CFLAGS += -Werror
endif

ANALYZE_KERNEL_TARGETS = $(addprefix $(ANALYZE_OUT)/kernel/, \
			 $(ANALYZE_KERNEL_SRCS:.c=.analyze))

analyze: check-gcc-version analyze-kernel

analyze-kernel: $(ANALYZE_KERNEL_TARGETS)

$(ANALYZE_OUT)/kernel/%.analyze: %.c $(AUTOCONF_H) FORCE
	$(QUIET_ANALYZE)
	$(Q)$(CC) $(ANALYZE_CFLAGS) -c -o /dev/null $<

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

$(USER_ELFS): check-gcc-version

user: check-gcc-version $(USER_ELFS)

clean-user:
	$(Q)rm -rf $(USER_OUTROOT)

clean: clean-user
	$(Q)rm -rf $(OUTROOT)
	$(Q)rm -f .gdbinit
	$(Q)rm -f tags GTAGS GRTAGS GPATH ID

.PRECIOUS: $(OUTDIR)/%.o

FORCE:

.PHONY: all help qemu test qemu-gdb check-gcc-version check-qemu-version clean clean-user FORCE
.PHONY: user format analyze analyze-kernel analyze-user asm sym tags gtags
.PHONY: $(KERNEL_NAME) $(KERNEL_NAME).img
.PHONY: print-gdbport print-toolprefix
