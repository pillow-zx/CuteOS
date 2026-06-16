# user/build.mk - 用户态程序构建
#
# 每个用户程序链接为独立 ELF，由根文件系统镜像写入 /bin。
#
# 构建流程：
#   .c → .o → .elf
#
# 由主 Makefile include 引入，TOOLPREFIX 由主 Makefile 检测。

USER_CC       = $(TOOLPREFIX)gcc
USER_LD       = $(TOOLPREFIX)ld
USER_OBJCOPY  = $(TOOLPREFIX)objcopy

USER_CFLAGS   = -march=rv64gc -mabi=lp64 -mcmodel=medany
USER_CFLAGS  += -ffreestanding -nostdlib -nostdinc -fno-builtin
USER_CFLAGS  += -Wall -Werror -O0 -g
USER_CFLAGS  += -I user/include

USER_OUT  = build/user

USER_INIT_ELF = $(USER_OUT)/init/init.elf
USER_SH_ELF   = $(USER_OUT)/init/sh.elf
USER_BIN_NAMES = ls cat echo touch mkdir rmdir rm pwd cp stat uname id kill \
		 true false
USER_BIN_ELFS  = $(addprefix $(USER_OUT)/bin/, \
		 $(addsuffix .elf,$(USER_BIN_NAMES)))
USER_ELFS      = $(USER_INIT_ELF) $(USER_SH_ELF) $(USER_BIN_ELFS)

USER_COMMON_OBJS = $(USER_OUT)/start.o			\
		   $(USER_OUT)/lib/string.o		\
		   $(USER_OUT)/lib/stdlib.o		\
		   $(USER_OUT)/lib/vsprintf.o		\
		   $(USER_OUT)/lib/printf.o		\
		   $(USER_OUT)/lib/coreutils.o
USER_INIT_OBJS   = $(USER_COMMON_OBJS) $(USER_OUT)/init/init.o
USER_SH_OBJS     = $(USER_COMMON_OBJS) $(USER_OUT)/init/shell.o

# Verbose control — reuse V / Q from main Makefile
#
#   V=0  "  CC      build/user/init/init.o"
#   V=1  full command line

quiet_user_CC       = CC
quiet_user_LD       = LD
quiet_user_OBJCOPY  = OBJCOPY

user_cmd = $(if $(filter 1,$(V)),$(user_cmd_$(1)),$(Q)printf '  %-7s %s\n' '$(quiet_user_$(1))' '$@' && $(user_cmd_$(1)))

user_cmd_CC      = $(USER_CC) $(USER_CFLAGS) -c -o $@ $<
user_cmd_LD      = $(USER_LD) -T user/user.ld -o $@ $(filter %.o,$^)
user_cmd_OBJCOPY = $(USER_OBJCOPY) -O binary $< $@

$(USER_OUT)/%.o: user/%.S
	@mkdir -p $(dir $@)
	$(call user_cmd,CC)

$(USER_OUT)/%.o: user/%.c
	@mkdir -p $(dir $@)
	$(call user_cmd,CC)

$(USER_INIT_ELF): $(USER_INIT_OBJS) user/user.ld
	$(call user_cmd,LD)

$(USER_SH_ELF): $(USER_SH_OBJS) user/user.ld
	$(call user_cmd,LD)

$(USER_OUT)/bin/%.elf: $(USER_COMMON_OBJS) $(USER_OUT)/bin/%.o user/user.ld
	$(call user_cmd,LD)
