# Kernel self-test objects. Runtime order is owned by test/test.c.

TEST_SRCS = $(shell find test -type f \( -name '*.c' -o -name '*.S' \) | sort)
TEST_OBJS = $(patsubst %.c,%.o,$(patsubst %.S,%.o,$(TEST_SRCS)))
