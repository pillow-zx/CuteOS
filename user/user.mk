# user/build.mk - 用户态程序构建
#
# 编译用户程序为 flat binary (init.bin)，
# 供内核通过 .incbin 嵌入。
#
# 构建流程：
#   .c → .o → .elf → .bin (objcopy -O binary)
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
USER_ELF  = $(USER_OUT)/init.elf
USER_BIN  = $(USER_OUT)/init.bin

USER_OBJS = $(USER_OUT)/start.o $(USER_OUT)/init/init.o

# Verbose control — reuse V / Q from main Makefile
#
#   V=0  "  CC      build/user/init/init.o"
#   V=1  full command line

quiet_user_CC       = CC
quiet_user_LD       = LD
quiet_user_OBJCOPY  = OBJCOPY

user_cmd = $(if $(filter 1,$(V)),$(user_cmd_$(1)),$(Q)printf '  %-7s %s\n' '$(quiet_user_$(1))' '$@' && $(user_cmd_$(1)))

user_cmd_CC      = $(USER_CC) $(USER_CFLAGS) -c -o $@ $<
user_cmd_LD      = $(USER_LD) -T user/user.ld -o $@ $(USER_OBJS)
user_cmd_OBJCOPY = $(USER_OBJCOPY) -O binary $< $@

$(USER_OUT)/%.o: user/%.S
	@mkdir -p $(dir $@)
	$(call user_cmd,CC)

$(USER_OUT)/%.o: user/%.c
	@mkdir -p $(dir $@)
	$(call user_cmd,CC)

$(USER_ELF): $(USER_OBJS) user/user.ld
	$(call user_cmd,LD)

$(USER_BIN): $(USER_ELF)
	$(call user_cmd,OBJCOPY)
