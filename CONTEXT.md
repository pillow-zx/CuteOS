# cuteOS Context

This is the compact architecture contract for maintainers and coding agents.
Read it before changing cross-subsystem behavior. `AGENTS.md` contains
execution rules; `docs/architecture/` contains detailed subsystem references;
`SYSCALL.md` is the source of truth for syscall maturity and known semantic
gaps.

## Project Intent

cuteOS is a compact experimental RISC-V 64 Unix-like kernel. Its long-term
design target is a **monolithic kernel** with complete, reusable kernel
mechanisms and one tested default implementation for each policy category.
Mechanisms stay in the kernel and communicate through explicit subsystem
interfaces; this project does not pursue a microkernel decomposition.

The design must support adding a different policy without rewriting syscall
adapters or generic object-lifetime code. This does not mean every function
gets an `ops` table. Add a strategy seam only when the behavior is genuinely
replaceable, the interface has stable invariants, and a second implementation
or a concrete research need justifies it.

SMP, kernel preemption, and multiple architectures are committed future
targets. They are design constraints now, even though the current runtime is
single-hart and non-preemptible.

The current validation target is to run real, statically linked riscv64
programs and use the Linux riscv64 ABI as a compatibility boundary for the
semantics cuteOS explicitly supports. This is evidence about cuteOS only; it
is not a Linux performance or compatibility claim.

## Kernel Context Terms

**IRQ context** is execution while the current CPU is inside a hard IRQ
handler. It is distinct from disabled interrupts, a synchronous trap, and
preemption-disabled context.

**Preemption-disabled context** is execution while the current CPU has a
nonzero `preempt_count`. It may overlap with IRQ context, but the two
conditions are tracked independently.

**Task context** is execution with a non-null, non-idle `current_task` outside
hard IRQ context. IRQ-disabled and preemption-disabled execution can still be
task context; hardware IRQ state, IRQ nesting, preemption depth, and held-lock
state are separate queries.

**IRQ-off task context** is task context with local hardware interrupts
disabled. It is a valid state for selected handoff paths, but a caller must not
infer that every IRQ-off path may sleep, allocate, or schedule.

**Held-lock state** is the current CPU's record of successfully acquired
spinlocks. The stable generic query is whether any lock is held; address-level
membership is a debug diagnostic and is not a lock-order policy.

The detailed synchronization and context contract is in
[`docs/architecture/sync.md`](docs/architecture/sync.md), with scheduler and
trap-specific details in [`sched.md`](docs/architecture/sched.md) and
[`trap.md`](docs/architecture/trap.md).

## Five-Layer Model

Do not reduce all design decisions to a binary mechanism/policy split. Keep
these layers distinct:

| Layer | Owns | Examples |
| --- | --- | --- |
| Hardware mechanism | ISA, CSRs, page tables, traps, IRQs, timers, IPIs, memory ordering | RISC-V `sfence.vma`, timer programming |
| Generic kernel mechanism | State machines, containers, lifetime, synchronization, shared services | task lifecycle, wait queues, VFS path lookup, page cache |
| Strategy seam | Stable replacement interface plus its lock, lifetime, and error contracts | scheduler operations, block dispatch |
| Default strategy | The one implementation and its parameters used by the product | 4-level MLFQ, current allocator, FIFO-style choices |
| ABI-visible semantics | User-visible layouts, errors, ordering, and observable behavior | syscall numbers, signal frames, `clone`, `f_pos`, ELF loading |

The distinction matters. A runqueue owner and task migration protocol are
generic mechanisms; MLFQ is a policy. COW and signal-frame layout are
user-visible mechanism/ABI behavior, not optional scheduler policy. A fixed
VMA or PID limit is a resource configuration unless an interface explicitly
makes it replaceable.

## ABI and User-Space Terms

**User-space runtime** is the static musl BusyBox image installed into the
interactive root filesystem. Its `init` applet is PID 1 and its `ash` applet
provides the shell. This is the one built-in runtime, not a selectable kernel
profile.

**User-space regression suite** is the serial workload that validates
user-visible contracts from static musl programs. It distinguishes promised
behavior, documented probe failures, and expected future failures. An XPASS
is a suite failure; a crash or timeout is never an expected failure. Each case
owns a process group so descendants cannot outlive its result.

**Test root filesystem** is the generated filesystem used only by the
regression suite before it requests shutdown. It is separate from the
interactive runtime filesystem.

**Test probe ELF** is a small static `ET_EXEC` program installed only in the
test root filesystem to validate a fresh program image.

**User-test sentinel** is the final serial line emitted by the regression
suite before poweroff. The host runner treats FAIL, XPASS, CRASH, and TIMEOUT
as failures.

**vfork calling task** is the task whose `clone()` request includes
`CLONE_VFORK` and whose syscall return is suspended until vfork completion.
It is not necessarily the child's reaper parent.

**vfork child** is the task created by a `CLONE_VFORK` request.

**vfork completion** occurs when the child stops using its pre-exec address
space through successful exec or termination. A failed exec attempt is not
completion.

## Current Runtime Model

These are current facts, not future guarantees:

- Target platform is QEMU `virt`, booted through OpenSBI, with RISC-V `rv64gc`,
  Sv39, S-mode, a high-half kernel mapping, and a static ext2 root image.
- Only hart 0 is online; secondary harts park during boot.
- Scheduling uses one global four-level MLFQ. Timer ticks account execution
  and call `sched_request()` when a policy budget expires; switching occurs at
  explicit scheduling points or the unified user-return handoff.
- The kernel is non-preemptible. `preempt_count` tracks explicit
  preemption-disabled sections, while IRQ context has an independent
  CPU-local nesting depth. Local interrupt masking and held-lock state are
  separate context facts; only hart 0 is online, so SMP execution is not yet
  enabled.
- UART and virtio-blk are primarily polling-oriented. Platform discovery is
  minimal and QEMU `virt` resources are mostly compile-time constants.
- Normal boot uses an ext2 root image through virtio-blk. `KERNEL_SELFTEST=1`
  deliberately skips filesystem registration, virtio-blk initialization, and
  root mounting; kernel tests use in-memory block/file adapters instead.
- User VMAs use a fixed `NR_VMA` array and faults are handled lazily.
- `fork` currently copies user pages rather than providing a complete COW
  implementation. Dynamic linking, PIE, user floating-point context, swap,
  and broad Linux compatibility are outside the current runtime promise.

Do not make new generic code depend on these restrictions. In particular,
local interrupt masking is not inter-hart exclusion, CPU 0 is not a generic
current-CPU implementation, and a global static device request is not a
valid lifetime model for preemptible or SMP execution.

## Architecture Map

| Area | Current ownership | Long-term boundary |
| --- | --- | --- |
| `arch/riscv/` | boot, entry/return, context switch, paging, TLB, SBI, PLIC, timer, RISC-V platform details | architecture mechanisms only; platform details should be separable |
| `init/` | `kernel_main()` and initialization order | generic initialization orchestration |
| `kernel/` | task lifecycle, fork/exec/exit/wait, PID, signals, futex, rseq, time, synchronization, TTY | generic kernel mechanisms and user-visible semantics |
| `sched/` | scheduler orchestration and MLFQ | scheduler mechanism plus one default policy adapter |
| `mm/` | physical allocation, VMAs, faults, mappings, uaccess, vmalloc | generic MM mechanisms behind arch MM interfaces |
| `fs/vfs/` | files, fdtable, paths, mounts, dentries, inodes, poll and ioctl routing | generic VFS lifetime and namespace mechanisms |
| `fs/ext2/` | ext2 on-disk format and operations | filesystem implementation behind VFS/block interfaces |
| `block/` | block devices, page cache, dirty state, writeback, virtio-blk transport | generic I/O lifecycle plus one default dispatch path |
| `drivers/` | UART driver | device mechanisms and driver policy boundaries |
| `syscall/` | thin Linux riscv64 ABI adapters | ABI translation only; no core policy |
| `include/kernel/` | internal types and cross-subsystem contracts | public generic interfaces and invariants |
| `include/uapi/` | user-visible constants and layouts | architecture-independent UAPI where possible |

Future multi-architecture work must separate three concerns:

1. **Architecture:** boot entry, trap entry/return, context switch, atomic
   operations, CPU-local access, timer/IPI, page tables/TLB/cache, uaccess,
   and architecture task state.
2. **Platform:** memory map, device discovery, interrupt controller, timer
   source, CPU topology, firmware services, reset, UART, and virtio resources.
3. **User ABI:** ELF machine and ABI flags, syscall trap convention, signal
   frame, `ucontext`, time/stat layouts, toolchain, rootfs, and test binaries.

The current code combines some architecture and QEMU platform details under
`arch/riscv/`. Do not copy that coupling into generic code or assume that
adding `arch/arm64/` alone completes the multi-architecture target.

## Stable Boundaries

### Architecture and trap

`arch/` owns entry/return assembly, trap-frame layout, address-space
activation, CPU-local access, interrupt control, timer programming, TLB
operations, and platform mechanisms. Generic code requests operations through
architecture interfaces; it does not read CSRs, touch MMIO, or depend on a
RISC-V trap-frame offset.

`struct trap_frame`, assembly offsets, kernel-stack layout, and the ordering
of `rseq_resume_user()` and `do_signal()` are contracts. User PC, SP, and
register state must be changed through the established trap/user-return path.

### Syscall and user ABI

The dispatcher decodes Linux riscv64 registers. Handlers validate arguments,
copy user data, return negative errno or a non-negative result, and delegate
semantics to the owning subsystem. They must not manipulate VMA, ext2,
fdtable, page-cache, or device internals directly.

User pointers are never directly dereferenced. Use `access_ok()`,
`copy_from_user()`, `copy_to_user()`, `strncpy_from_user()`, and the relevant
probe helpers. An installed syscall is not automatically a compatibility
claim; update `SYSCALL.md`, UAPI anchors, and tests when a semantic boundary
changes.

### Task lifecycle and identity

`task_struct` is a lifecycle aggregate, not a dumping ground for subsystem
state. Signal, futex, rseq, session, and TTY helpers remain with their owners.

Task creation reserves a PID and owns a base reference, but does not publish
the PID mapping until resources, links, and state are coherent. `task_publish()`
installs the mapping and marks the task visible as one transaction.
`task_find_thread()` and `task_find_group_leader()` return a lifecycle-pinned
task; every caller releases it with `task_put()`. Reaping first unpublishes
the task, preventing new lookup references, and only then releases the base
reference.

Process identity (`SID`/`PGID`) is owned by task/process code. Other
subsystems use `task_process_snapshot()` rather than writing raw fields. The
session coordinator linearizes operations that jointly change process
identity and controlling-TTY policy. TTY code snapshots the target identity,
unlocks, and then asks signal code to deliver. Do not nest task/process and
TTY subsystem locks in the reverse order.

A controlling-TTY loss caused by session-leader detach/exit or privileged
forced takeover is a session hangup: the former foreground process group gets
`SIGHUP` followed by `SIGCONT`. Detaching a non-leader affects only that
process. Session-leader exit updates TTY visibility before the task becomes a
zombie; cleanup and clone-abort paths are idempotent fallbacks.

Clone uses a prepare/commit/abort transaction. A non-thread clone prepares
session identity and TTY inheritance before publication; threads resolve the
leader's attachment instead of copying it. Syscall code must not bypass this
transaction.

Parent/child relations publish ordered child events with monotonic sequence
numbers. The task interface observes or claims an event, registers waiters,
and commits or aborts the claim atomically. A failed user copy aborts the
claim so the event remains waitable. Exit owns wait4 selector and status
policy; syscall code owns ABI validation and final commit/abort.

### Scheduler mechanism and default policy

The scheduler owns runnable-task state, runqueue ownership, wakeup
orchestration, and architecture switch orchestration. The current MLFQ is the
default policy, not the generic scheduler contract.

`schedule()` is the only immediate-switch entry. It requires task context, no
hard IRQ, no held spinlock, and `preempt_count == 0`; local IRQs may be enabled
or disabled, and the scheduler preserves the entry state. It enters an
allocation-free, nonblocking, non-I/O scheduler core.
`sched_request()` only publishes `need_resched`; `preempt_enable()` consumes a
deferred request only when the ordinary entry is safe. Exited sibling threads
are reaped by the idle loop, outside scheduler core.

Before SMP and preemption are enabled, the scheduler must define:

- whether a task is queued, running, sleeping, exiting, or migrating;
- which CPU owns a running or queued task;
- the linearization point for local and remote wakeup;
- whether a remote wake requires an IPI;
- how exit removes a task from every queue and CPU state;
- what the policy may change in a scheduling entity.

A first SMP implementation may use one globally locked runqueue. Per-CPU
queues, affinity, load balancing, migration, and work stealing are later
policy choices; the no-double-run invariant is not optional.

### Synchronization and wait channels

Every shared object must document:

| Contract | Required answer |
| --- | --- |
| Owner | Which CPU, task, or subsystem owns mutations? |
| Lock | Which fields and transitions does it protect? |
| Lock order | Which locks may be nested, and in what order? |
| IRQ/preemption | Are interrupts disabled? Is preemption disabled? |
| Sleep | May the path block, allocate, or perform I/O? |
| Lifetime | Who holds references, cancels callbacks, and frees storage? |
| Wakeup | Which state change wakes which waiter, and how is loss avoided? |

Synchronization primitives must state their atomicity, IRQ/preemption,
lock-order, sleep, lifetime, wakeup, and error contracts. Holding a spinlock
does not by itself authorize scheduling, sleeping, or allocation; scheduler,
wait, and allocator interfaces own those decisions. A mutex may keep a plain
owner pointer if the complete owner/waiter protocol is protected by its
internal lock; making one field atomic is not a complete mutex design.

No path may schedule while holding a spinlock, in IRQ context, or with an
invalid preemption state. `schedule()` preserves local IRQ state, including an
IRQ-off entry. `preempt_enable()` must service deferred rescheduling when its
count reaches zero when the IRQ-enabled entry is safe. Waiters, timers, and callbacks need
references or cancellation synchronization; raw task pointers and stack
allocated sessions are not lifetime guarantees.

### Memory, uaccess, VFS, and storage

MM owns VMA layout and page-table changes behind `include/kernel/mm.h`.
Callers do not take `mm->mmap_lock` or manipulate VMAs. Any future uaccess
implementation must define the address-space lock/fault-in/page-pin or fault-
fixup contract, SUM restoration, preemption behavior, and error return.

SMP MM requires an active-CPU contract for each `mm`, TLB shootdown for every
relevant PTE change, and a completion guarantee before releasing old pages or
page-table pages. This includes unmap, mprotect, exec, mm destruction, shared
mm, vmalloc mappings, and permission changes, not only explicit `munmap`.

VFS owns file lifetime, fd lookup, path lookup, mount traversal, filesystem
dispatch, and shared open-file state. Filesystems do not bypass VFS. The page
cache is the authoritative cached file-data path; raw block aliases preserve
that coherence. Inode, dentry, file offset, mount, ext2 metadata, page busy,
read-in, writeback, truncate, and invalidate protocols must be defined before
claiming SMP-safe storage.

The kernel self-test seam stops before real storage integration. KTEST crosses
the page-cache, block-device, and file-mapping interfaces with an in-memory
block device and synthetic regular file. On-disk ext2, path-tree, root-mount,
and virtio-blk integration are user-space test responsibilities; normal boot
still owns the production assembly of those adapters.

Do not allocate, sleep, or start I/O under a spinlock. Define allocator,
page-cache, inode, block-request, and writeback lock order globally; local
locks are insufficient when reclaim and error cleanup can call back into
another subsystem.

### Time, signals, futex, and rseq

Timer queues need a per-CPU or explicitly owned expiry model, a cancel-sync
contract, and a callback context contract. After `cancel` returns, the
documented callback state must be true; cancellation alone must not leave a
stack timer or task pointer in the queue.

Signals, futexes, and rseq must define cross-CPU target lookup, wakeup,
delivery, exit cleanup, and migration behavior. `rseq` must not hard-code CPU
0; it needs a real logical CPU identifier and update ordering across switch,
preemption, signal, exec, and exit.

## Policy Seams

Potentially deep strategy seams include:

| Policy | Current default | Mechanism that must remain generic |
| --- | --- | --- |
| Scheduler | four-level MLFQ | runqueue ownership, task states, wakeup, migration, switch |
| Physical allocation | buddy plus slab | page ownership, allocation lifetime, failure semantics |
| VM fault/reclaim | lazy fault, no complete reclaim | fault classification, page install, COW/refcount, OOM contract |
| Page cache | bounded cache with LRU/writeback pieces | page identity, busy state, I/O completion, eviction safety |
| Block dispatch | synchronous polling request path | request lifetime, queue, completion, timeout, cancellation |
| Timer backend | globally ordered timer list | arm/cancel, CPU ownership, expiry, callback context |
| TTY input | console-oriented behavior | input queue, line discipline, blocking and wakeup |
| Filesystem selection | ext2 root filesystem | registration, mount, superblock and VFS operations |

The default policy is statically linked and centrally configured. Policy
parameters must not be duplicated in syscall, VFS, MM, or architecture
callers. A policy replacement must not silently change ABI errors, object
lifetime, or synchronization contracts.

For scheduler work, keep MLFQ fields in policy-owned state. The generic task
interface should expose lifecycle and opaque scheduling state rather than
making every caller understand MLFQ levels, queues, or boost rules.

## Important Flows

- **Boot:** RISC-V entry establishes early mappings and a stack, then
  `kernel_main()` initializes memory, tasking, VFS, devices, traps, and init.
  Normal boot mounts ext2 through virtio-blk; KTEST stops after `vfs_init()` and
  runs with in-memory fixtures. Secondary-hart handling is currently park-only.
- **Syscall:** user `ecall` enters the trap path; the dispatcher decodes the
  number and arguments; a thin handler copies ABI data and calls a subsystem;
  `user_return_work()` performs rseq before signal work.
- **Scheduling:** the timer updates time, expired timers, accounting, and
  `need_resched`; the unified `schedule()` entry switches only at permitted
  context handoffs, including user trap return.
- **Fork/exec/exit:** clone prepares state, commits task/PID/scheduler
  ownership, and publishes; exec replaces the address space; exit publishes
  task-owned child events and later reaping unpublishes the task.
- **File I/O:** fd/path adaptation enters VFS; VFS owns lookup and file
  lifetime; filesystem data reaches page cache and block devices.
- **Shutdown:** BusyBox init terminates user processes, synchronizes cached
  filesystem state, and requests reset/halt/poweroff through the reset seam.

## Non-Negotiable ABI Rules

- Check Linux riscv64 and asm-generic UAPI headers before changing a claimed
  syscall number, structure, flag, layout, or errno.
- Change both sides of every shared layout and preserve size/offset assertions.
- Return negative errno and treat `copy_*_user()` results as uncopied bytes.
- Preserve trap-frame ownership and the rseq-before-signal user-return order.
- Never dereference user memory directly.
- Do not bypass VFS, fdtable, page cache, block-device, or task lifetime APIs.
- Do not put subsystem policy in `syscall/`.
- Do not make generic code depend on one CPU, non-preemption, or polling
  device serialization.

## Implementation Order

The target is mechanism-first. Use this order unless a documented dependency
requires otherwise:

1. Define lock, memory-order, IRQ, preemption, sleep, wakeup, and lifetime
   contracts; add debug assertions and a lock-order inventory.
2. Implement CPU-local access, real spinlocks, atomic publication, per-CPU
   current/idle/kernel-stack state, and the minimum IPI platform seam.
3. Make task ownership, runqueue state, wait/wakeup, remote wake, and reaping
   correct on SMP. A global runqueue is acceptable for the first implementation.
4. Close MM/uaccess/TLB protocols, including shared `mm`, page-table-page
   lifetime, vmalloc, exec, and all PTE-changing paths.
5. Close VFS, page-cache, ext2, and block-request lifetimes and lock order.
6. Enable kernel preemption with unified return-path checks and context rules.
7. Extract only justified strategy seams; keep MLFQ and current implementations
   as the single default adapters.
8. Validate the generic layer with a second architecture and the same ABI and
   self-test contracts. AArch64 is the provisional second-architecture target;
   its detailed boot, platform, and ABI design remains intentionally deferred.

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

Use `make help` to discover targets. `make ktest` runs kernel self-tests in a
diskless QEMU with in-memory fixtures; `make utest` runs the user regression
suite from its test root filesystem and exercises the real rootfs storage path;
`make check` runs both. New source files must be added to
`scripts/filelist.mk`. Changes to user-visible ABI require checking
`include/uapi/`, the vendored user-space headers, and tests.
