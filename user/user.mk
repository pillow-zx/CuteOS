# User-space program build rules.

USER_CC       = $(TOOLPREFIX)gcc
USER_LD       = $(TOOLPREFIX)ld
USER_AR       = $(shell if command -v $(TOOLPREFIX)gcc-ar >/dev/null 2>&1; \
		 then echo $(TOOLPREFIX)gcc-ar; else echo $(TOOLPREFIX)ar; fi)
USER_RANLIB   = $(shell if command -v $(TOOLPREFIX)gcc-ranlib >/dev/null 2>&1; \
		 then echo $(TOOLPREFIX)gcc-ranlib; else echo $(TOOLPREFIX)ranlib; fi)
USER_OBJCOPY  = $(TOOLPREFIX)objcopy

USER_ARCH_FLAGS = -march=rv64gc -mabi=lp64 -mcmodel=medany

USER_CFLAGS   = $(USER_ARCH_FLAGS)
USER_CFLAGS  += -ffreestanding -nostdlib -nostdinc -fno-builtin
USER_CFLAGS  += -Wall -Werror
USER_CFLAGS  += -I user/libc/minimal/include
USER_CFLAGS  += -I include
USER_CFLAGS  += -include include/generated/autoconf.h
USER_LDFLAGS  = -z max-page-size=4096
USER_LD_SCRIPT = -T user/user.ld

ifeq ($(CONFIG_CC_OPTIMIZE_O0),y)
USER_CFLAGS += -O0
else ifeq ($(CONFIG_CC_OPTIMIZE_O1),y)
USER_CFLAGS += -O1
else ifeq ($(CONFIG_CC_OPTIMIZE_O2),y)
USER_CFLAGS += -O2
else ifeq ($(CONFIG_CC_OPTIMIZE_O3),y)
USER_CFLAGS += -O3
else ifeq ($(CONFIG_CC_OPTIMIZE_OZ),y)
USER_CFLAGS += -Oz
else ifeq ($(CONFIG_CC_OPTIMIZE_OS),y)
USER_CFLAGS += -Os
endif

ifneq ($(CONFIG_CC_OPTIMIZE_O0),y)
USER_CFLAGS  += $(COMMON_SECTION_CFLAGS)
USER_LDFLAGS += --gc-sections
endif

ifeq ($(CONFIG_DEBUG_INFO),y)
USER_CFLAGS += $(COMMON_DEBUG_INFO_CFLAGS)
endif

ifeq ($(CONFIG_FRAME_POINTER),y)
USER_CFLAGS += -fno-omit-frame-pointer
endif

ifeq ($(CONFIG_LTO),y)
ifneq ($(COMMON_LTO_CFLAGS),)
USER_CFLAGS   += $(COMMON_LTO_CFLAGS)
USER_LD        = $(USER_CC)
USER_LDFLAGS   = $(USER_ARCH_FLAGS)
USER_LDFLAGS  += -nostdlib -nostartfiles
USER_LDFLAGS  += -fno-pie -no-pie
USER_LDFLAGS  += $(COMMON_LTO_CFLAGS)
USER_LDFLAGS  += -Wl,-z,max-page-size=4096
ifneq ($(CONFIG_CC_OPTIMIZE_O0),y)
USER_LDFLAGS  += -Wl,--gc-sections
endif
USER_LDFLAGS  += -Wl,--build-id=none
USER_LD_SCRIPT = -Wl,-T,user/user.ld
endif
endif

USER_OUTROOT = $(OUTROOT)/user
USER_OUT     = $(USER_OUTROOT)

USER_CRT_OBJS  = $(USER_OUT)/start.o

include user/libc/minimal/libc.mk

USER_INIT_ELF = $(USER_OUT)/init/init.elf
USER_SH_ELF   = $(USER_OUT)/init/sh.elf
USER_BIN_SRCS  = $(sort $(wildcard user/bin/*.c))
USER_BIN_NAMES = $(basename $(notdir $(USER_BIN_SRCS)))
USER_BIN_ELFS  = $(addprefix $(USER_OUT)/bin/, \
		 $(addsuffix .elf,$(USER_BIN_NAMES)))
USER_ELFS      = $(USER_INIT_ELF) $(USER_SH_ELF) $(USER_BIN_ELFS)

USER_INIT_OBJS = $(USER_CRT_OBJS) $(USER_OUT)/init/init.o
USER_SH_OBJS   = $(USER_CRT_OBJS) $(USER_OUT)/init/shell.o

$(USER_OUT)/%.o: user/%.S $(AUTOCONF_H)
	$(Q)mkdir -p $(dir $@)
	$(QUIET_CC)
	$(Q)$(USER_CC) $(USER_CFLAGS) -c -o $@ $<

$(USER_OUT)/%.o: user/%.c $(AUTOCONF_H)
	$(Q)mkdir -p $(dir $@)
	$(QUIET_CC)
	$(Q)$(USER_CC) $(USER_CFLAGS) -c -o $@ $<

$(USER_INIT_ELF): $(USER_INIT_OBJS) $(USER_LIBC_A) user/user.ld
	$(QUIET_LD)
	$(Q)$(USER_LD) $(USER_LDFLAGS) $(USER_LD_SCRIPT) -o $@ $(filter %.o,$^) $(filter %.a,$^)

$(USER_SH_ELF): $(USER_SH_OBJS) $(USER_LIBC_A) user/user.ld
	$(QUIET_LD)
	$(Q)$(USER_LD) $(USER_LDFLAGS) $(USER_LD_SCRIPT) -o $@ $(filter %.o,$^) $(filter %.a,$^)

$(USER_OUT)/bin/%.elf: $(USER_CRT_OBJS) $(USER_OUT)/bin/%.o $(USER_LIBC_A) user/user.ld
	$(QUIET_LD)
	$(Q)$(USER_LD) $(USER_LDFLAGS) $(USER_LD_SCRIPT) -o $@ $(filter %.o,$^) $(filter %.a,$^)
