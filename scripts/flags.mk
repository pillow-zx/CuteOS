# Kernel compiler/linker flags.

KERNEL_ARCH_FLAGS = -march=rv64gc -mabi=lp64 -mcmodel=medany

COMMON_SECTION_CFLAGS   = -ffunction-sections -fdata-sections
COMMON_NO_STACK_PROTECTOR_CFLAGS = -fno-stack-protector
COMMON_NO_PIE_CFLAGS = -fno-pie -no-pie
COMMON_NO_LTO_CFLAGS = -fno-lto
COMMON_DEBUG_INFO_CFLAGS = -g3 -ggdb -gdwarf-4
COMMON_DEBUG_INFO_ASFLAGS = -g
COMMON_LTO_CFLAGS = -flto=auto
COMMON_UBSAN_TRAP_CFLAGS = -fsanitize-trap=undefined

CFLAGS  = $(KERNEL_ARCH_FLAGS)
ASFLAGS = -march=rv64gc -mabi=lp64

CFLAGS += -Wall -Werror
CFLAGS += -Wno-unknown-attributes
CFLAGS += -Wno-main
CFLAGS += -std=gnu17
CFLAGS += -I include
CFLAGS += -I arch/riscv/include
ASFLAGS += -I include
ASFLAGS += -I arch/riscv/include
CFLAGS += -include include/generated/autoconf.h
CFLAGS += -include include/kernel/compiler.h
ASFLAGS += -include include/generated/autoconf.h

CFLAGS += -ffreestanding -fno-common -nostdlib -nostdinc
CFLAGS += $(COMMON_NO_STACK_PROTECTOR_CFLAGS)
CFLAGS += $(COMMON_NO_PIE_CFLAGS)
CFLAGS += -Wno-maybe-uninitialized

LDFLAGS = -z max-page-size=4096

KERNEL_LD = $(LD)
KERNEL_LD_SCRIPT = -T kernel.ld
KERNEL_LDFLAGS = $(LDFLAGS)
KERNEL_LINK_WITH_CC = 0
KERNEL_NO_LTO_OBJS = lib/string.o lib/softfloat.o
KERNEL_GC_SECTIONS = 0

ifeq ($(CONFIG_CC_OPTIMIZE_O0),y)
CFLAGS += -O0
else ifeq ($(CONFIG_CC_OPTIMIZE_O1),y)
CFLAGS += -O1
else ifeq ($(CONFIG_CC_OPTIMIZE_OG),y)
CFLAGS += -Og
else ifeq ($(CONFIG_CC_OPTIMIZE_O2),y)
CFLAGS += -O2
else ifeq ($(CONFIG_CC_OPTIMIZE_O3),y)
CFLAGS += -O3
else ifeq ($(CONFIG_CC_OPTIMIZE_OZ),y)
CFLAGS += -Oz
else ifeq ($(CONFIG_CC_OPTIMIZE_OS),y)
CFLAGS += -Os
endif

ifeq ($(CONFIG_GC_SECTIONS),y)
CFLAGS  += $(COMMON_SECTION_CFLAGS)
LDFLAGS += --gc-sections
KERNEL_GC_SECTIONS = 1
endif

ifeq ($(CONFIG_DEBUG_INFO),y)
CFLAGS  += $(COMMON_DEBUG_INFO_CFLAGS)
ASFLAGS += $(COMMON_DEBUG_INFO_ASFLAGS)
endif

ifeq ($(CONFIG_FRAME_POINTER),y)
CFLAGS += -fno-omit-frame-pointer
endif

ifeq ($(CONFIG_LTO),y)
ifneq ($(COMMON_LTO_CFLAGS),)
CFLAGS += $(COMMON_LTO_CFLAGS)
KERNEL_LD = $(CC)
KERNEL_LINK_WITH_CC = 1
KERNEL_LD_SCRIPT = -Wl,-T,kernel.ld
KERNEL_LDFLAGS = $(KERNEL_ARCH_FLAGS)
KERNEL_LDFLAGS += -nostdlib -nostartfiles -fno-pie -no-pie
KERNEL_LDFLAGS += $(COMMON_LTO_CFLAGS)
KERNEL_LDFLAGS += -Wl,-z,max-page-size=4096
ifeq ($(KERNEL_GC_SECTIONS),1)
KERNEL_LDFLAGS += -Wl,--gc-sections
endif
KERNEL_LDFLAGS += -Wl,--build-id=none
endif
endif

SANITIZE_CFLAGS =

ifeq ($(CONFIG_UBSAN),y)
SANITIZE_CFLAGS += -fsanitize=undefined
SANITIZE_CFLAGS += $(COMMON_UBSAN_TRAP_CFLAGS)
SANITIZE_CFLAGS += -fno-sanitize-recover=all
endif

CFLAGS += $(SANITIZE_CFLAGS)

ifeq ($(KERNEL_LINK_WITH_CC),1)
KERNEL_LDFLAGS += $(SANITIZE_CFLAGS)
endif

CFLAGS += -MD
