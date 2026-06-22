# Kconfig integration.

KCONFIG       := Kconfig
DEFCONFIG     := configs/cuteos_defconfig
DOT_CONFIG    := .config
AUTO_CONF     := include/config/auto.conf
AUTO_CONF_CMD := include/config/auto.conf.cmd
AUTOCONF_H    := include/generated/autoconf.h

KCONFIG_DIR  := tools/kconfig
CONF         := $(KCONFIG_DIR)/build/conf
MCONF        := $(KCONFIG_DIR)/build/mconf
KCONFIG_SRCS := $(KCONFIG) arch/riscv/Kconfig fs/Kconfig kernel/Kconfig
KCONFIG_SILENT := -s

KCONFIG_SKIP_GOALS := clean clean-user help print-gdbport print-toolprefix format defconfig
ifneq ($(strip $(MAKECMDGOALS)),)
ifneq ($(filter-out $(KCONFIG_SKIP_GOALS),$(MAKECMDGOALS)),)
KCONFIG_NEED_CONFIG := 1
else
KCONFIG_NEED_CONFIG := 0
endif
else
KCONFIG_NEED_CONFIG := 1
endif

$(CONF):
	$(Q)$(MAKE) -s -C $(KCONFIG_DIR) NAME=conf

$(MCONF):
	$(Q)$(MAKE) -s -C $(KCONFIG_DIR) NAME=mconf

$(DOT_CONFIG): $(CONF) $(DEFCONFIG) $(KCONFIG_SRCS)
	$(Q)$(CONF) $(KCONFIG_SILENT) --defconfig=$(DEFCONFIG) $(KCONFIG)

$(AUTO_CONF) $(AUTOCONF_H): $(DOT_CONFIG) $(CONF) $(KCONFIG_SRCS)
	$(Q)$(CONF) $(KCONFIG_SILENT) --syncconfig $(KCONFIG)

ifeq ($(KCONFIG_NEED_CONFIG),1)
include $(AUTO_CONF)
-include $(AUTO_CONF_CMD)
endif

syncconfig: $(AUTO_CONF)

defconfig: $(CONF) $(DEFCONFIG) $(KCONFIG_SRCS)
	$(Q)cp $(DEFCONFIG) $(DOT_CONFIG)
	$(Q)$(CONF) $(KCONFIG_SILENT) --olddefconfig $(KCONFIG)
	$(Q)$(CONF) $(KCONFIG_SILENT) --syncconfig $(KCONFIG)

menuconfig: $(MCONF) $(CONF) $(DOT_CONFIG)
	$(Q)$(MCONF) $(KCONFIG)
	$(Q)$(CONF) $(KCONFIG_SILENT) --syncconfig $(KCONFIG)

.PHONY: syncconfig defconfig menuconfig
