# Staged root filesystem assembly.

USER_ROOTFS       = $(USER_OUTROOT)/rootfs
USER_ROOTFS_STAMP = $(USER_OUTROOT)/rootfs.stamp

ifeq ($(CONFIG_USERSPACE_BUSYBOX),y)
USER_ROOTFS_DEPS = $(USER_INIT_ELF) $(BUSYBOX_INSTALL_STAMP)

$(USER_ROOTFS_STAMP): $(USER_ROOTFS_DEPS) $(AUTO_CONF)
	$(QUIET_ROOTFS)
	$(Q)rm -rf $(USER_ROOTFS)
	$(Q)mkdir -p $(USER_ROOTFS)/bin $(USER_ROOTFS)/dev \
		$(USER_ROOTFS)/fixtures
	$(Q)cp -a $(BUSYBOX_INSTALL)/. $(USER_ROOTFS)/
	$(Q)cp $(USER_INIT_ELF) $(USER_ROOTFS)/init
	$(Q)cp $(USER_INIT_ELF) $(USER_ROOTFS)/bin/init
	$(Q)ln -s readlink-target $(USER_ROOTFS)/fixtures/readlink-link
	$(Q)touch $@
else
USER_ROOTFS_DEPS = $(USER_INIT_ELF) $(USER_SH_ELF) $(USER_BIN_ELFS)

$(USER_ROOTFS_STAMP): $(USER_ROOTFS_DEPS) $(AUTO_CONF)
	$(QUIET_ROOTFS)
	$(Q)rm -rf $(USER_ROOTFS)
	$(Q)mkdir -p $(USER_ROOTFS)/bin $(USER_ROOTFS)/dev \
		$(USER_ROOTFS)/fixtures
	$(Q)cp $(USER_INIT_ELF) $(USER_ROOTFS)/init
	$(Q)cp $(USER_INIT_ELF) $(USER_ROOTFS)/bin/init
	$(Q)cp $(USER_SH_ELF) $(USER_ROOTFS)/bin/sh
	$(Q)for elf in $(USER_BIN_ELFS); do \
		name=$$(basename $$elf .elf); \
		cp $$elf $(USER_ROOTFS)/bin/$$name; \
	done
	$(Q)ln -s readlink-target $(USER_ROOTFS)/fixtures/readlink-link
	$(Q)touch $@
endif
