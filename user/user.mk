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
USER_LDFLAGS  = -z max-page-size=4096
USER_LD_SCRIPT = -T user/user.ld

USER_RELEASE_CFLAGS  = -O3 -DNDEBUG
USER_RELEASE_CFLAGS += -ffunction-sections -fdata-sections
USER_RELEASE_CFLAGS += -fomit-frame-pointer
USER_RELEASE_CFLAGS += $(call cc-option,-fipa-pta)
USER_RELEASE_CFLAGS += $(call cc-option,-fweb)
USER_RELEASE_CFLAGS += $(call cc-option,-frename-registers)
USER_RELEASE_CFLAGS += $(call cc-option,-fgcse-after-reload)
USER_RELEASE_CFLAGS += $(call cc-option,-fno-semantic-interposition)
USER_RELEASE_CFLAGS += $(call cc-option,-fno-unwind-tables)
USER_RELEASE_CFLAGS += $(call cc-option,-fno-asynchronous-unwind-tables)
USER_RELEASE_LTO_CFLAGS = $(call cc-option,-flto=auto)

ifeq ($(BUILD),release)
USER_CFLAGS  += $(USER_RELEASE_CFLAGS)
USER_LDFLAGS += --gc-sections
ifeq ($(RELEASE_LTO),1)
ifneq ($(USER_RELEASE_LTO_CFLAGS),)
USER_CFLAGS   += $(USER_RELEASE_LTO_CFLAGS)
USER_LD        = $(USER_CC)
USER_LDFLAGS   = $(USER_ARCH_FLAGS)
USER_LDFLAGS  += -nostdlib -nostartfiles
USER_LDFLAGS  += -fno-pie -no-pie
USER_LDFLAGS  += $(USER_RELEASE_LTO_CFLAGS)
USER_LDFLAGS  += -Wl,-z,max-page-size=4096 -Wl,--gc-sections
USER_LDFLAGS  += -Wl,--build-id=none
USER_LD_SCRIPT = -Wl,-T,user/user.ld
endif
endif
else
USER_CFLAGS += -O0 -g
endif

USER_OUTROOT = $(OUTROOT)/user
USER_OUT     = $(USER_OUTROOT)/$(BUILD)

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

quiet_user_CC       = CC
quiet_user_LD       = LD
quiet_user_AR       = AR
quiet_user_RANLIB   = RANLIB
quiet_user_OBJCOPY  = OBJCOPY

user_cmd = $(if $(filter 1,$(V)),$(user_cmd_$(1)),$(Q)printf '  %-7s %s\n' '$(quiet_user_$(1))' '$@' && $(user_cmd_$(1)))

user_cmd_CC      = $(USER_CC) $(USER_CFLAGS) -c -o $@ $<
user_cmd_AR      = rm -f $@ && $(USER_AR) rc $@ $(filter %.o,$^)
user_cmd_RANLIB  = $(USER_RANLIB) $@
user_cmd_LD      = $(USER_LD) $(USER_LDFLAGS) $(USER_LD_SCRIPT) -o $@ $(filter %.o,$^) $(filter %.a,$^)
user_cmd_OBJCOPY = $(USER_OBJCOPY) -O binary $< $@

$(USER_OUT)/%.o: user/%.S
	@mkdir -p $(dir $@)
	$(call user_cmd,CC)

$(USER_OUT)/%.o: user/%.c
	@mkdir -p $(dir $@)
	$(call user_cmd,CC)

$(USER_INIT_ELF): $(USER_INIT_OBJS) $(USER_LIBC_A) user/user.ld
	$(call user_cmd,LD)

$(USER_SH_ELF): $(USER_SH_OBJS) $(USER_LIBC_A) user/user.ld
	$(call user_cmd,LD)

$(USER_OUT)/bin/%.elf: $(USER_CRT_OBJS) $(USER_OUT)/bin/%.o $(USER_LIBC_A) user/user.ld
	$(call user_cmd,LD)
