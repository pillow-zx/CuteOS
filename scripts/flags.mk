# Kernel compiler/linker flags.

KERNEL_ARCH_FLAGS = -march=rv64gc -mabi=lp64 -mcmodel=medany

CFLAGS  = $(KERNEL_ARCH_FLAGS)
ASFLAGS = -march=rv64gc -mabi=lp64

CFLAGS += -Wall -Werror
CFLAGS += -Wno-unknown-attributes
CFLAGS += -Wno-main
CFLAGS += -fno-omit-frame-pointer
CFLAGS += -std=gnu17
CFLAGS += -I include
ASFLAGS += -I include

CFLAGS += -ffreestanding -fno-common -nostdlib -fno-builtin -nostdinc
CFLAGS += $(call cc-option,-fno-stack-protector)
CFLAGS += $(call cc-option,-fno-pie)
CFLAGS += $(call cc-option,-no-pie)
CFLAGS += $(call cc-option,-nopie)
CFLAGS += -Wno-maybe-uninitialized

LDFLAGS = -z max-page-size=4096

KERNEL_LD = $(LD)
KERNEL_LD_SCRIPT = -T kernel.ld
KERNEL_LDFLAGS = $(LDFLAGS)
KERNEL_LINK_WITH_CC = 0
KERNEL_NO_LTO_OBJS = lib/string.o lib/softfloat.o

DEBUG_CFLAGS   = -O0 -g3 -ggdb -gdwarf-4 -DDEBUG
DEBUG_CFLAGS  += -fno-inline -fno-optimize-sibling-calls
DEBUG_ASFLAGS  = -g
DEBUG_LDFLAGS  =

RELEASE_CFLAGS  = -O3 -DNDEBUG
RELEASE_CFLAGS += -ffunction-sections -fdata-sections
RELEASE_CFLAGS += -fomit-frame-pointer
RELEASE_CFLAGS += $(call cc-option,-fipa-pta)
RELEASE_CFLAGS += $(call cc-option,-fweb)
RELEASE_CFLAGS += $(call cc-option,-frename-registers)
RELEASE_CFLAGS += $(call cc-option,-fgcse-after-reload)
RELEASE_CFLAGS += $(call cc-option,-fno-semantic-interposition)
RELEASE_CFLAGS += $(call cc-option,-fno-unwind-tables)
RELEASE_CFLAGS += $(call cc-option,-fno-asynchronous-unwind-tables)
RELEASE_LTO_CFLAGS = $(call cc-option,-flto=auto)
RELEASE_ASFLAGS =
RELEASE_LDFLAGS = --gc-sections

ifeq ($(BUILD),debug)
	CFLAGS  += $(DEBUG_CFLAGS)
	ASFLAGS += $(DEBUG_ASFLAGS)
	LDFLAGS += $(DEBUG_LDFLAGS)
	CFLAGS  += -DCONFIG_KERNEL_TEST
else ifeq ($(BUILD),release)
	CFLAGS  += $(RELEASE_CFLAGS)
	ASFLAGS += $(RELEASE_ASFLAGS)
	LDFLAGS += $(RELEASE_LDFLAGS)
ifeq ($(RELEASE_LTO),1)
ifneq ($(RELEASE_LTO_CFLAGS),)
	CFLAGS  += $(RELEASE_LTO_CFLAGS)
	KERNEL_LD = $(CC)
	KERNEL_LINK_WITH_CC = 1
	KERNEL_LD_SCRIPT = -Wl,-T,kernel.ld
	KERNEL_LDFLAGS = $(KERNEL_ARCH_FLAGS)
	KERNEL_LDFLAGS += -nostdlib -nostartfiles -fno-pie -no-pie
	KERNEL_LDFLAGS += $(RELEASE_LTO_CFLAGS)
	KERNEL_LDFLAGS += -Wl,-z,max-page-size=4096 -Wl,--gc-sections
	KERNEL_LDFLAGS += -Wl,--build-id=none
endif
endif
endif

SANITIZE_CFLAGS =
SANITIZE_ASFLAGS =
SANITIZE_LDFLAGS =
SANITIZE_UBSAN_TRAP_CFLAGS = $(call cc-option,-fsanitize-trap=undefined)

ifeq ($(SANITIZE),none)
else ifeq ($(SANITIZE),undefined)
	SANITIZE_CFLAGS += -fsanitize=undefined
ifneq ($(SANITIZE_UBSAN_TRAP_CFLAGS),)
	SANITIZE_CFLAGS += $(SANITIZE_UBSAN_TRAP_CFLAGS)
else
	SANITIZE_CFLAGS += -fsanitize-undefined-trap-on-error
endif
	SANITIZE_CFLAGS += -fno-sanitize-recover=all
else
$(error Unsupported SANITIZE='$(SANITIZE)'; expected 'none' or 'undefined')
endif

CFLAGS  += $(SANITIZE_CFLAGS)
ASFLAGS += $(SANITIZE_ASFLAGS)
LDFLAGS += $(SANITIZE_LDFLAGS)

ifeq ($(KERNEL_LINK_WITH_CC),1)
	KERNEL_LDFLAGS += $(SANITIZE_CFLAGS)
endif

CFLAGS += -MD
