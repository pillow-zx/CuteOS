# Static musl BusyBox user-space, rootfs, and filesystem image build.

USER_CC = $(TOOLPREFIX)gcc
USER_READELF = $(TOOLPREFIX)readelf
USER_ARCH_FLAGS = -march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany
USER_ELF_CHECK = scripts/tools/check-user-elf.sh

USER_OUTROOT = $(OUTROOT)/user
USER_OUT = $(USER_OUTROOT)

MUSL_SRC = user/libc/musl
MUSL_OUT = $(USER_OUTROOT)/musl
MUSL_BUILD = $(MUSL_OUT)/build
MUSL_SYSROOT = $(MUSL_OUT)/sysroot
MUSL_CONFIG = $(MUSL_BUILD)/config.mak
MUSL_STAMP = $(MUSL_OUT)/.installed
MUSL_SPECS_IN = user/libc/musl-gcc.specs.in
MUSL_SPECS = $(MUSL_OUT)/musl-gcc.specs
MUSL_LINUX_UAPI_SRC = user/linux-uapi/include
MUSL_LINUX_UAPI_HEADERS := $(sort $(shell find $(MUSL_LINUX_UAPI_SRC) -type f -name '*.h'))
MUSL_LINUX_UAPI_STAMP = $(MUSL_SYSROOT)/.linux-uapi-installed

COMPILER_RT_SRC = user/runtime/compiler_rt.zig
COMPILER_RT_OUT = $(USER_OUTROOT)/runtime
COMPILER_RT_A = $(COMPILER_RT_OUT)/libcompiler_rt.a

MUSL_CFLAGS = $(USER_ARCH_FLAGS) -Os -fno-pie -fno-stack-protector
MUSL_LDFLAGS = $(USER_ARCH_FLAGS) -no-pie -Wl,-z,max-page-size=4096

$(MUSL_CONFIG): $(MUSL_SRC)/configure $(MUSL_SRC)/VERSION
	$(QUIET_MUSL)
	$(Q)rm -rf $(MUSL_BUILD) $(MUSL_SYSROOT) $(MUSL_STAMP)
	$(Q)mkdir -p $(MUSL_BUILD) $(MUSL_SYSROOT)
	$(Q)cd $(MUSL_BUILD) && \
		$(abspath $(MUSL_SRC))/configure \
		--target=riscv64-linux-musl \
		--prefix=$(abspath $(MUSL_SYSROOT)) \
		--exec-prefix=$(abspath $(MUSL_SYSROOT)) \
		--syslibdir=$(abspath $(MUSL_SYSROOT))/lib \
		--disable-shared --enable-wrapper=gcc \
		CC=$(USER_CC) CROSS_COMPILE=$(TOOLPREFIX) \
		CFLAGS='$(MUSL_CFLAGS)' LDFLAGS='$(MUSL_LDFLAGS)'

$(MUSL_STAMP): $(MUSL_CONFIG)
	$(QUIET_MUSL)
	$(Q)$(MAKE) -s -C $(MUSL_BUILD) install
	$(Q)touch $@

$(MUSL_LINUX_UAPI_STAMP): $(MUSL_STAMP) $(MUSL_LINUX_UAPI_HEADERS)
	$(Q)mkdir -p $(MUSL_SYSROOT)/include
	$(Q)cp -a $(MUSL_LINUX_UAPI_SRC)/. $(MUSL_SYSROOT)/include/
	$(Q)touch $@

$(MUSL_SPECS): $(MUSL_SPECS_IN) $(MUSL_LINUX_UAPI_STAMP)
	$(QUIET_MUSL)
	$(Q)sed 's|@MUSL_SYSROOT@|$(abspath $(MUSL_SYSROOT))|g' $< > $@
	$(Q)printf '\n' >> $@

$(COMPILER_RT_A): $(COMPILER_RT_SRC)
	$(QUIET_MUSL)
	$(Q)command -v $(ZIG) >/dev/null 2>&1 || { \
		echo "ERROR: Zig 0.16 or newer is required for the BusyBox user-space runtime." >&2; \
		exit 1; \
	}
	$(Q)major=$$($(ZIG) version | cut -d. -f1); \
	minor=$$($(ZIG) version | cut -d. -f2); \
	if [ "$$major" -lt 1 ] && [ "$$minor" -lt 16 ]; then \
		echo "ERROR: Zig 0.16 or newer is required; found $$($(ZIG) version)." >&2; \
		exit 1; \
	fi
	$(Q)mkdir -p $(dir $@)
	$(Q)$(ZIG) build-lib $< -target riscv64-freestanding \
		-mcpu=generic_rv64+m+a+c -OReleaseSmall -fcompiler-rt -static \
		-femit-bin=$(abspath $@)

BUSYBOX_SRC = user/busybox
BUSYBOX_OUT = $(USER_OUTROOT)/busybox
BUSYBOX_BUILD = $(BUSYBOX_OUT)/build
BUSYBOX_INSTALL = $(BUSYBOX_OUT)/install
BUSYBOX_MENUCONFIG = scripts/tools/busybox-config.sh
BUSYBOX_CONFIG_SRC = configs/busybox_defconfig
BUSYBOX_CONFIG = $(BUSYBOX_BUILD)/.config
BUSYBOX_ELF = $(BUSYBOX_BUILD)/busybox
BUSYBOX_INSTALL_STAMP = $(BUSYBOX_OUT)/.installed

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
	$(Q)rm -rf $(BUSYBOX_BUILD) $(BUSYBOX_INSTALL) $(BUSYBOX_INSTALL_STAMP)
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

USER_ELFS = $(BUSYBOX_ELF)

USER_ROOTFS = $(USER_OUTROOT)/rootfs
USER_ROOTFS_STAMP = $(USER_OUTROOT)/rootfs.stamp
USER_ROOTFS_DEPS = $(BUSYBOX_INSTALL_STAMP) user/rootfs/busybox/inittab

$(USER_ROOTFS_STAMP): $(USER_ROOTFS_DEPS) $(AUTO_CONF)
	$(QUIET_ROOTFS)
	$(Q)rm -rf $(USER_ROOTFS)
	$(Q)mkdir -p $(USER_ROOTFS)/bin $(USER_ROOTFS)/dev $(USER_ROOTFS)/etc \
		$(USER_ROOTFS)/fixtures
	$(Q)cp -a $(BUSYBOX_INSTALL)/. $(USER_ROOTFS)/
	$(Q)cp user/rootfs/busybox/inittab $(USER_ROOTFS)/etc/inittab
	$(Q)ln -s readlink-target $(USER_ROOTFS)/fixtures/readlink-link
	$(Q)touch $@

$(USER_ELFS): check-gcc-version

busybox-menuconfig: 
	$(Q)$(BUSYBOX_MENUCONFIG)

user: check-gcc-version $(USER_ELFS)

user-rootfs: check-gcc-version $(USER_ROOTFS_STAMP)

$(KERNEL_IMG): check-gcc-version $(USER_ROOTFS_STAMP) $(MKIMG) $(AUTO_CONF)
	$(Q)mkdir -p $(dir $@)
	$(QUIET_FSIMG)
	$(Q)MKIMG_SIZE_MB=$(CONFIG_ROOTFS_IMAGE_SIZE_MB) $(MKIMG) $@ $(USER_ROOTFS)

user-image: check-gcc-version $(KERNEL_IMG)

$(KERNEL_NAME).img: $(KERNEL_IMG)

clean-user:
	$(Q)rm -rf $(USER_OUTROOT)

.PHONY: user user-rootfs user-image clean-user $(KERNEL_NAME).img busybox_menuconfig
