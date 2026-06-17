# Minimal user-space libc build rules.

USER_LIBC_DIR  = user/libc/minimal
USER_LIBC_SRCS = $(sort $(wildcard $(USER_LIBC_DIR)/src/*.c))
USER_LIBC_OBJS = $(patsubst user/%.c,$(USER_OUT)/%.o,$(USER_LIBC_SRCS))
USER_LIBC_A    = $(USER_OUT)/libc/minimal/libc.a

$(USER_LIBC_A): $(USER_LIBC_OBJS)
	@mkdir -p $(dir $@)
	$(call user_cmd,AR)
	$(call user_cmd,RANLIB)
