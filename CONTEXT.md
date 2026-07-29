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

**User-space runtime** is the static musl BusyBox image installed into the
root filesystem. Its `init` applet is PID 1 and its `ash` applet provides the
interactive shell. Do not call this a selectable profile: cuteOS builds this
one user-space runtime.

**User-space regression suite** is the serial corpus that validates cuteOS's
user-visible contracts from static musl programs. It distinguishes promised
behavior, documented probe failures, and expected future failures; an XPASS
is a suite failure, while a crash or timeout is never an expected failure.
Each case owns a process group so its descendants cannot outlive its result.

**Test root filesystem** is the generated root filesystem that runs only the
user-space regression suite before shutting the machine down. It is separate
from the interactive user-space runtime.

**Test probe ELF** is a small static `ET_EXEC` program installed only in the
test root filesystem and executed by the regression suite to validate a fresh
program image. It is not a runner mode and does not link the test framework.

**User-test sentinel** is the final serial line emitted by the user-space
regression suite before it requests poweroff. It is the host workflow's
authoritative result, with only FAIL, XPASS, CRASH, and TIMEOUT causing failure.

**vfork calling task** is the task whose `clone()` request includes
`CLONE_VFORK` and whose syscall return is suspended until vfork completion.
Avoid calling it the parent process: the clone caller and the child's reaper
parent are distinct roles. Ordinary signals do not end this suspension; a
pending `SIGKILL` may terminate the calling task without waiting for
completion.

**vfork child** is the child task created by a `CLONE_VFORK` request.

**vfork completion** is the lifecycle event at which the vfork child stops
using its pre-exec virtual memory, either through successful exec or
termination. A failed exec attempt is not vfork completion.

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
| `drivers/` | UART and virtio MMIO transport drivers |
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

Task allocation reserves a PID and owns one base reference, but it does not
publish a PID-to-task mapping. After resources and links are coherent, the
creator calls `task_publish()`, which marks the task published and installs
the PID mapping under the PID-registry lock as one transaction.
`task_find_thread()` and
`task_find_group_leader()` return a lifecycle-pinned task that every caller
must release with `task_put()`; they never lend a raw registry pointer. Reaping
first calls `task_unpublish()`, preventing new lookup references, then drops
the base reference. This makes PID reuse impossible until all prior lookup
users have left the task lifetime.

Process identity (`SID`/`PGID`) is owned by task/process code. Cross-subsystem
callers read the pair through `task_process_snapshot()` under the process
identity lock; raw field access is limited to that owner or unpublished task
construction. Identity mutations are task-private and reached only through the
session coordinator. The session coordinator owns the linearization of operations
that jointly change process identity and controlling-TTY policy, but does not
write identity fields or retain task pointers. A
controlling-TTY attachment contains only
TTY-owned state and is protected by the TTY mutex. TTY passes foreground input
signals to the coordinator, which snapshots `(sid, pgid)` through TTY and
invokes signal delivery after unlocking; signal code performs the pinned task
scan. The coordinator mutex is outermost for joint policy operations. It may
enter task/process or TTY operations separately, but those two subsystem locks
must never be nested. PID-registry lookup remains internal to task and signal
operations.

A controlling-TTY loss caused by its session leader's detach or exit, or by a
privileged forced takeover, is a session hangup: the former foreground process
group receives `SIGHUP` followed by `SIGCONT`. Detaching a non-leader affects
only that process and is not a session hangup.

For a non-thread clone, session identity inheritance and controlling-TTY
attachment inheritance are one session-coordinator prepare transaction before
the child is published. A failed prepare or a later clone abort removes the
tentative attachment completely. Threads resolve their leader's attachment and
never copy it.

Session leader exit changes controlling-TTY visibility before the task becomes
a zombie. It removes the session relationship and emits any resulting hangup;
the exiting leader's attachment is then released. Reaping and clone-abort use
an idempotent cleanup fallback and do not repeat a normal exit's hangup.

A successful `setsid()` only detaches its calling process from its former
controlling TTY before creating a new session and process group. It does not
change the former session's terminal relationship or emit a session hangup.

The minimal terminal model keeps a foreground PGID only while that session has
a live member in the group. Session transitions clear an empty foreground
group to `0`; terminal input then has no foreground recipient until an explicit
foreground-group selection. This avoids treating a later reused PID number as
the old foreground group without introducing a general process-group object.

The scheduler owns runnable-task selection and architecture switch
orchestration. Wait channels own waiter registration and wakeup observation.
Every new shared mutable object needs a documented owner, lifetime, lock,
lock order, IRQ/preemption state, and wakeup rule. Prefer a small deep module
interface over exposing lock choreography to callers.

Task owns every published parent/child relation and its wait-visible lifecycle
edges. Each child keeps an ordered event FIFO with a monotonically increasing
sequence; the task interface atomically observes or claims events, registers
waiters, and commits or aborts claims. A failed userspace result copy aborts
the claim, so it cannot discard the event. Task publishes the edge and wakes
the parent while holding its relation source lock, pins a non-idle published
parent, then delivers `SIGCHLD` only after unlocking. `exit` owns wait4
selector and status policy; syscall code owns only Linux ABI validation,
uaccess, and the final commit or abort.

Until stage 2 is complete, do not add a feature that is only correct because
execution happens on one hart. During stages 2--5, keep task state transitions,
runqueue membership, remote wakeup and migration as separate, explicit
contracts; do not fold their policy into syscall handlers.

### Generic kernel containers

`kfifo` and `klifo` hold copies of fixed-size kernel objects in caller-owned
storage. They do not own the copied objects, allocate memory, synchronize
access, or provide wait/wake policy. The owning subsystem keeps the storage
live and supplies the lock, IRQ, and lifetime contract. Prefer intrusive lists
when an object participates in multiple memberships or needs identity rather
than a copied value.

### Memory, VFS, and storage

MM owns VMA layout and page-table changes behind `include/kernel/mm.h`.
Callers do not take `mm->mmap_lock` or manipulate VMAs. VFS owns file
lifetime, fd lookup, path lookup, mount traversal and filesystem dispatch;
syscalls and filesystems do not bypass it. The page cache is the authoritative
cached file-data path; raw block aliases must preserve page-cache coherence.
VFS also owns inode mode/uid/gid mutation through `vfs_inode_setattr()`;
syscalls only adapt the ABI and pass a VFS-owned attribute request, while
filesystem implementations persist the resulting inode state.

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
  address space. Exit performs signal/futex/task cleanup, task-owned child
  event publication, and later reaping.
- **File I/O:** syscall fd/path adaptation enters VFS; VFS owns lookup and
  file lifetime; filesystem data reaches the page cache and block device.
- **Shutdown:** BusyBox init broadcasts TERM/KILL to user processes, calls
  VFS-wide sync for page-cache and filesystem-global state, then requests
  restart/halt/poweroff through the platform-independent reset seam.

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
| time and timers | `docs/architecture/time.md`, `kernel/time.c`, `arch/riscv/timer.c` |
| VM or user access | `docs/architecture/memory.md`, `include/kernel/mm.h`, `mm/` |
| VFS or paths | `docs/architecture/vfs.md`, `include/kernel/fs.h`, `fs/vfs/` |
| ext2 or cached I/O | `docs/architecture/ext2.md`, `docs/architecture/block.md`, `fs/ext2/`, `block/` |
| build and boot | `Makefile`, `scripts/build.mk`, `scripts/kernel.mk`, `scripts/workflows.mk` |
| shutdown and reset | `SYSCALL.md`, `kernel/signal.c`, `syscall/sys_misc.c`, `include/kernel/reboot.h` |

Use `make help` to discover targets. `make ktest` runs kernel self-tests with a
temporary test image; `make utest` runs user-space regression from its separate
test root filesystem; `make check` runs both serially. When adding a source
file, update the object manifest in `scripts/kernel.mk`.
