# Phase 0 Refactor Guardrails

This document is the Phase 0 guardrail for the cuteOS refactor roadmap.  Its
purpose is to make ABI, layering, and regression checks explicit before moving
code between subsystems.

Phase 0 is documentation-only.  It does not move syscall wrappers, does not
change syscall numbers or user-visible layouts, does not clean up existing
reverse dependencies, and does not implement later-phase work.

## ABI Review Checklist

Linux riscv64 ABI is the user/kernel contract.  Any change that can affect
userspace must start by checking the Linux riscv64 or Linux UAPI convention and
must preserve Linux-style negative errno returns in kernel code.

Always inspect these paths for ABI-facing changes:

- `include/uapi/*.h`
- `include/kernel/syscall_table.h`
- `include/kernel/stat.h`
- `include/asm/trap.h`
- `kernel/signal.c` signal frame and trampoline layout
- `user/libc/minimal/include/user.h`

The following are user-visible ABI surfaces:

- syscall numbers, syscall names used for tracing, and dispatch table entries
- errno values and syscall return conventions
- `struct kstat` and the matching userspace `struct stat`
- signal UAPI constants, `struct sigaction`, `struct stack_t`, signal frame
  layout, and `rt_sigreturn` trampoline behavior
- trap-frame syscall calling convention: `a7` carries the syscall number,
  `a0` through `a5` carry arguments, and `a0` carries the return value
- minimal libc copies of Linux ABI constants and duplicated layouts

The following are kernel-private unless they are copied to userspace or fixed by
assembly-visible offsets:

- VFS `inode`, `file`, `dentry`, and `super_block` internals
- page cache and page mapping internals
- task, mm, scheduler, wait queue, timer, and block-device internals
- ext2 implementation details hidden behind VFS operation tables

For any ABI-facing edit:

1. Check the relevant Linux riscv64 or Linux UAPI definition first.
2. Update both kernel and userspace copies of any duplicated layout.
3. Preserve or add `static_assert(sizeof(...))` and
   `static_assert(offsetof(...))` checks for fixed layouts.
4. Review the final ABI diff manually:

```sh
git diff -- include/uapi include/kernel/stat.h user/libc/minimal/include/user.h
```

## Layering Guardrails

The intended dependency direction is:

- `syscall -> kernel/mm/vfs/sched`
- `ext2 -> vfs/block`
- `block -> blkdev/page_mapping`
- `mm -> buddy/slab/arch page table`

`syscall/` is the ABI boundary.  It should unpack trap-frame arguments, validate
userspace pointers, perform minimal Linux compatibility handling, and delegate
to subsystem logic.  Core subsystem code should prefer kernel-owned buffers,
validated arguments, and Linux-style negative errno returns.

Use these manual checks before and after refactor work:

```sh
rg '#include <kernel/syscall.h>' kernel mm fs block drivers syscall
rg '#include <drivers/' mm kernel fs block syscall
```

These checks are informational in Phase 0.  Existing hits are tracked as the
baseline below and are not fixed in this phase.

## Current Reverse-Dependency Baseline

The following known layering issues exist at the Phase 0 baseline:

- Core files still include `<kernel/syscall.h>`:
  - `kernel/fork.c`
  - `kernel/futex.c`
  - `kernel/exit.c`
  - `mm/mmap.c`
  - `mm/uaccess.c`
- `kernel/fork.c`, `kernel/futex.c`, and `kernel/exit.c` still implement
  syscall handlers directly.
- `kernel/signal.c` still performs userspace access for signal frame delivery
  and `sigreturn` frame restore.  This is signal core behavior, not a syscall
  argument-copying wrapper.
- `mm/mmap.c` still directly depends on signal, syscall, UART, and virtio
  details.
- `block/page_cache.c` still directly depends on task, scheduler, and timer
  details.

Do not expand this baseline casually.  Later phases should reduce it while
preserving the existing single-core, non-preemptive kernel, userspace timer
preemption, and polling I/O assumptions.

## Regression Baseline

Use the active root `.config`; do not assume `configs/cuteos_defconfig` matches
the current workspace.  The Phase 0 regression baseline is:

```sh
make -j4
make user -j4
timeout 20s make qemu
```

When `CONFIG_KERNEL_TEST=y`, the QEMU boot path runs `kernel_test()`, including
the syscall compatibility coverage in `test/syscall_compat_test.c`.

For ABI edits, also perform the manual diff review described in the ABI review
checklist.  For layering edits, run the include checks described in the layering
guardrails section and compare the results with the baseline above.
