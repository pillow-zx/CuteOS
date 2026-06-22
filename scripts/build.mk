# Artifact layout and command verbosity.

MIN_QEMU_VERSION = 7.2

OUTROOT     ?= build
OUTDIR      = $(OUTROOT)/kernel

V ?= 0

ifeq ($(V),1)
  Q :=
  QUIET_CC :=
  QUIET_AS :=
  QUIET_LD :=
  QUIET_LD_STAGE1 :=
  QUIET_OBJDUMP_S :=
  QUIET_OBJDUMP_T :=
  QUIET_FSIMG :=
  QUIET_AR :=
  QUIET_RANLIB :=
  QUIET_ANALYZE :=
else
  Q := @
  QUIET_CC = @echo '  CC      $@'
  QUIET_AS = @echo '  AS      $@'
  QUIET_LD = @echo '  LD      $@'
  QUIET_LD_STAGE1 = @echo '  LD-SYM  $@'
  QUIET_OBJDUMP_S = @echo '  OBJDUMP $@'
  QUIET_OBJDUMP_T = @echo '  OBJDUMP $@'
  QUIET_FSIMG = @echo '  FSIMG   $@'
  QUIET_AR = @echo '  AR      $@'
  QUIET_RANLIB = @echo '  RANLIB  $@'
  QUIET_ANALYZE = @echo '  ANALYZE $<'
endif
