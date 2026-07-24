# Static musl cross-build rules.

MUSL_SRC       = user/libc/musl
MUSL_OUT       = $(USER_OUTROOT)/musl
MUSL_BUILD     = $(MUSL_OUT)/build
MUSL_SYSROOT   = $(MUSL_OUT)/sysroot
MUSL_CONFIG    = $(MUSL_BUILD)/config.mak
MUSL_STAMP     = $(MUSL_OUT)/.installed
MUSL_SPECS_IN  = user/libc/musl-gcc.specs.in
MUSL_SPECS     = $(MUSL_OUT)/musl-gcc.specs

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

$(MUSL_SPECS): $(MUSL_SPECS_IN) $(MUSL_STAMP)
	$(QUIET_MUSL)
	$(Q)sed 's|@MUSL_SYSROOT@|$(abspath $(MUSL_SYSROOT))|g' $< > $@
	$(Q)printf '\n' >> $@
