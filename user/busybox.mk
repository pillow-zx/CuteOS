# Static BusyBox build rules using the project musl sysroot.

BUSYBOX_SRC           = user/busybox
BUSYBOX_OUT           = $(USER_OUTROOT)/busybox
BUSYBOX_BUILD         = $(BUSYBOX_OUT)/build
BUSYBOX_INSTALL       = $(BUSYBOX_OUT)/install
BUSYBOX_CONFIG_SRC    = configs/busybox_defconfig
BUSYBOX_CONFIG        = $(BUSYBOX_BUILD)/.config
BUSYBOX_ELF           = $(BUSYBOX_BUILD)/busybox
BUSYBOX_INSTALL_STAMP = $(BUSYBOX_OUT)/.installed

include user/runtime/runtime.mk

BUSYBOX_CC = $(USER_CC) -specs=$(abspath $(MUSL_SPECS)) \
	$(USER_ARCH_FLAGS) -fno-pie -no-pie -fno-stack-protector -std=gnu11

BUSYBOX_MAKE = $(MAKE) -s -C $(BUSYBOX_SRC) \
	O=$(abspath $(BUSYBOX_BUILD)) \
	CROSS_COMPILE=$(TOOLPREFIX) \
	CC='$(BUSYBOX_CC)' \
	CONFIG_EXTRA_LDFLAGS=-L$(abspath $(COMPILER_RT_OUT)) \
	CONFIG_EXTRA_LDLIBS=:libcompiler_rt.a

$(BUSYBOX_CONFIG): $(BUSYBOX_CONFIG_SRC) $(BUSYBOX_SRC)/Makefile \
		$(MUSL_SPECS) $(COMPILER_RT_A)
	$(QUIET_BUSYBOX)
	$(Q)rm -rf $(BUSYBOX_BUILD) $(BUSYBOX_INSTALL) \
		$(BUSYBOX_INSTALL_STAMP)
	$(Q)mkdir -p $(BUSYBOX_BUILD)
	$(Q)cp $(BUSYBOX_CONFIG_SRC) $@
	$(Q)$(BUSYBOX_MAKE) oldconfig </dev/null >/dev/null

$(BUSYBOX_ELF): $(BUSYBOX_CONFIG) $(MUSL_SPECS) $(COMPILER_RT_A)
	$(QUIET_BUSYBOX)
	$(Q)$(BUSYBOX_MAKE) busybox
	$(Q)$(USER_ELF_CHECK) $(USER_READELF) $@

$(BUSYBOX_INSTALL_STAMP): $(BUSYBOX_ELF)
	$(QUIET_BUSYBOX)
	$(Q)rm -rf $(BUSYBOX_INSTALL)
	$(Q)mkdir -p $(BUSYBOX_INSTALL)
	$(Q)$(BUSYBOX_MAKE) CONFIG_PREFIX=$(abspath $(BUSYBOX_INSTALL)) install
	$(Q)touch $@
