# cuteOS Context

This is the compact architecture map for maintainers and coding agents. Read
it before changing cross-subsystem behavior. `AGENTS.md` contains execution
rules; detailed subsystem explanations live in `docs/architecture/`; syscall
maturity and caveats live in `SYSCALL.md`.

## Project Intent

cuteOS is a compact experimental RISC-V 64 Unix-like kernel. It uses real,
statically linked riscv64 programs and the Linux riscv64 ABI as compatibility
tests for the behavior it claims to support. It keeps modern-kernel-inspired
core abstractions, while intentionally avoiding the policy and device breadth
of a production kernel.

It is also a pre-validation platform for kernel-mechanism research: implement
and compare a theory in a controlled environment with reusable Linux-ABI
workloads, then move designs that pass this stage to Linux for detailed
measurement and external-validity analysis. cuteOS results establish relative
evidence only; they are not Linux performance claims.

Current target:

- QEMU `virt`, OpenSBI, `rv64gc`, Sv39, and supervisor mode.
- High-half kernel mapping shared by user page tables.
- Static ELF64 RISC-V userspace and a build-generated ext2 root filesystem on
  virtio-blk.
- Linux numeric errno and Linux riscv64 syscall numbers and layouts for every
  supported ABI boundary.

The roadmap is ordered by dependency, not by feature appeal:

1. Deepen syscall semantics against real workloads and regression tests.
2. Establish SMP-safe concurrency foundations: atomics, locking, memory
   ordering, IRQ rules, and wait/wake ownership.
3. Make the single-core kernel preemptible under that contract.
4. Add minimal SMP: hart bring-up, per-hart current state and runqueues, IPI,
   remote wakeups, and required TLB handling.
5. Add SMP policy: affinity, balancing, migration, and work stealing.

Portability is not a separate roadmap stage. New code must keep architecture
mechanism behind narrow `arch/` seams and keep generic policy free of RISC-V
CSR, trap-frame, page-table, SBI, or platform-MMIO knowledge.

## Project Language

**User-space profile** is the mutually exclusive userspace composition chosen
for a system image. The Minimal profile exercises project programs and their
minimal libc; the BusyBox profile keeps the project init while using static
musl BusyBox commands. Avoid calling this a global libc selection, because
independently linked static programs may use different libc implementations.

## Current Runtime Model

These are current facts, not goals:

- Only hart 0 is online; secondary harts park during boot.
- Scheduling is a single global 4-level MLFQ. Timer ticks account execution
  and request rescheduling; switching occurs at explicit scheduling points or
  user-return timer handling.
- The kernel is non-preemptible. Existing irqsave locks prevent local
  interrupt interleaving only; they are not SMP locks.
- UART and virtio-blk are polling-oriented. Platform discovery is minimal and
  QEMU `virt` resources are mostly compile-time driver constants.
- User VMAs use a fixed `NR_VMA` array and fault pages in lazily.

Do not make code depend on these facts unless its interface names the
restriction. In particular, do not use disabling local interrupts as a
substitute for inter-hart exclusion, and do not assume CPU 0 is a valid
generic current-CPU implementation.

## Architecture Map

| Area | Ownership |
| --- | --- |
| `arch/riscv/` | boot, assembly contracts, trap return, context switch, paging, TLB, SBI, PLIC and timer mechanisms |
| `init/` | `kernel_main()` and initialization order |
| `kernel/` | task lifecycle, fork/exec/exit/wait, PID, signals, futex, rseq, time, synchronization and tty |
| `sched/` | scheduler orchestration and MLFQ policy |
| `mm/` | physical allocation, user VM, VMAs, faults, mappings and uaccess |
| `fs/vfs/` | files, fdtable, paths, mounts, dentries, inodes, poll and ioctl routing |
| `fs/ext2/` | ext2 implementation and on-disk rules |
| `block/` | block devices, page cache, dirty state and writeback |
| `drivers/` | console, UART and virtio MMIO drivers |
| `syscall/` | thin Linux riscv64 ABI adapters; no core policy |
| `include/kernel/` | public internal interfaces and cross-subsystem contracts |
| `include/uapi/` | user-visible ABI layouts and constants |

## Stable Boundaries

### Architecture and trap

`arch/` owns entry/return assembly, context layout, address-space activation,
CPU-local access, interrupt control, timer programming, TLB operations and
platform mechanisms. Generic code may request an operation through an
architecture interface, but may not read CSRs, touch MMIO, or rely on a
RISC-V trap-frame layout.

`struct trap_frame`, assembly offsets, kernel stack layout and the ordering of
`rseq_resume_user()` and `do_signal()` are ABI contracts. Check assembly and
architecture accessors before changing them. User PC/SP or register state must
be rewritten through the established trap/user-return path only.

### Syscall and user ABI

The dispatcher decodes a Linux riscv64 trap frame and handlers return negative
errno or a non-negative result. Handlers validate and copy user data, then
delegate to their owning subsystem; they must not access VMA, ext2, fdtable or
device internals directly.

User pointers are never directly dereferenced. Use `access_ok()`,
`copy_from_user()`, `copy_to_user()`, `strncpy_from_user()` and probes as the
ABI requires. An installed syscall is not an implementation claim: update
`SYSCALL.md`, the `SYSCALL_SUPPORT(...)` anchor, and tests whenever a B/C/D
semantic boundary changes.

### Task, scheduling, and concurrency

`task_struct` is a lifecycle aggregate, not a dumping ground for subsystem
state. Signal, futex and rseq helpers remain with their owners. Clone uses a
prepare/commit/abort transaction; syscall code must not bypass it. Exit may
run after the task loses its `mm`, so it must not introduce late user access.

The scheduler owns runnable-task selection and architecture switch
orchestration. Wait channels own waiter registration and wakeup observation.
Every new shared mutable object needs a documented owner, lifetime, lock,
lock order, IRQ/preemption state, and wakeup rule. Prefer a small deep module
interface over exposing lock choreography to callers.

Until stage 2 is complete, do not add a feature that is only correct because
execution happens on one hart. During stages 2--5, keep task state transitions,
runqueue membership, remote wakeup and migration as separate, explicit
contracts; do not fold their policy into syscall handlers.

### Memory, VFS, and storage

MM owns VMA layout and page-table changes behind `include/kernel/mm.h`.
Callers do not take `mm->mmap_lock` or manipulate VMAs. VFS owns file
lifetime, fd lookup, path lookup, mount traversal and filesystem dispatch;
syscalls and filesystems do not bypass it. The page cache is the authoritative
cached file-data path; raw block aliases must preserve page-cache coherence.

Keep lower layers independent of higher policy: drivers do not decide VFS or
scheduler policy, filesystems do not access block-driver MMIO, and arch code
does not absorb generic lifecycle policy.

## Important Flows

- **Boot:** RISC-V entry establishes the early mapping and stack, then
  `kernel_main()` initializes memory, tasking, VFS, devices, traps and the
  init process. Secondary-hart handling is currently park-only.
- **Syscall:** user `ecall` enters the trap path; the dispatcher decodes the
  number and arguments; a thin handler copies ABI data and calls a subsystem;
  `user_return_work()` performs rseq then signal work before return.
- **Scheduling:** timer IRQ updates time, expired timers and MLFQ accounting.
  A runnable task enters through scheduler wakeup; `schedule()` chooses and
  switches tasks only at currently permitted points.
- **Fork/exec/exit:** clone prepares child state, commits it to task/PID and
  scheduler ownership, then exposes it. Successful exec replaces the old
  address space. Exit performs signal/futex/task cleanup before reaping.
- **File I/O:** syscall fd/path adaptation enters VFS; VFS owns lookup and
  file lifetime; filesystem data reaches the page cache and block device.

## Non-Negotiable ABI Rules

- Check Linux riscv64 and asm-generic UAPI headers for syscall numbers,
  structures, flags and errno before changing a claimed ABI.
- Change both sides of every shared layout, and preserve static offset/size
  assertions.
- Return negative errno; treat uaccess copy return values as uncopied bytes.
- Preserve the signal/rseq user-return order and trap-frame ownership.
- Do not dereference user memory, bypass VFS/fdtable/page cache, or place
  subsystem policy in `syscall/`.

## Lookup and Verification

| Question | Start here |
| --- | --- |
| syscall semantics | `SYSCALL.md`, `include/kernel/syscall_table.h`, matching `syscall/sys_*.c` |
| trap, signal, rseq | `docs/architecture/trap.md`, `arch/riscv/trap.c`, `kernel/user_return.c` |
| scheduler and wait/wake | `docs/architecture/sched.md`, `sched/`, `kernel/waitqueue.c` |
| VM or user access | `docs/architecture/memory.md`, `include/kernel/mm.h`, `mm/` |
| VFS or paths | `docs/architecture/vfs.md`, `include/kernel/fs.h`, `fs/vfs/` |
| ext2 or cached I/O | `docs/architecture/ext2.md`, `docs/architecture/block.md`, `fs/ext2/`, `block/` |
| build and boot | `Makefile`, `filelist.mk`, `arch/riscv/arch.mk`, `init/` |

Use `make help` to discover targets. `make test` runs kernel self-tests with a
temporary test image; use the relevant `user/bin/*_test.c` program in QEMU for
user-visible behavior. When adding a source file, update the relevant `*.mk`
object list.
