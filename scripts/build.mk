# Build profile, artifact layout, and command verbosity.

MIN_QEMU_VERSION = 7.2

BUILD       ?= debug
SANITIZE    ?= none
RELEASE_LTO ?= 1
OUTROOT     ?= build
OUTDIR      = $(OUTROOT)/$(BUILD)/sanitize-$(SANITIZE)

ifeq ($(BUILD),debug)
KERNEL_TEST_ENABLE := 1
else ifeq ($(BUILD),release)
KERNEL_TEST_ENABLE := 0
else
$(error Unsupported BUILD='$(BUILD)'; expected 'debug' or 'release')
endif

V ?= 0

ifeq ($(V),1)
  Q :=
else
  Q := @
endif

quiet_cmd_CC = CC
quiet_cmd_AS = AS
quiet_cmd_LD = LD
quiet_cmd_LD_STAGE1 = LD-SYM
quiet_cmd_OBJDUMP_S = OBJDUMP
quiet_cmd_OBJDUMP_T = OBJDUMP
quiet_cmd_FSIMG = FSIMG

cmd = $(if $(filter 1,$(V)),$(cmd_$(1)),$(Q)printf '  %-7s %s\n' '$(quiet_cmd_$(1))' '$@' && $(cmd_$(1)))
