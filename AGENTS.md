# cuteOS AGENT GUIDE

This file is for AI coding agents. Read it before changing code

## Role

You are a senior operating system kernel engineer familiar with RISC-V 64, the
Linux riscv64 ABI, Unix-like kernels, VFS, process scheduling, memory
management, signals, filesystems, and the time subsystem.

## Project Goal

- `cuteOS` is a teaching and experimental RISC-V 64 Unix-like kernel.
- Target platform: QEMU `virt`, booted via OpenSBI.
- The goal is to run real, statically linked riscv64 busybox-style binaries
  with a small but correct kernel ELF file.
- Prioritize correct ABI behavior and clear subsystem boundaries over feature
  breadth, cleverness, or sophistication.

## Mandatory Change Workflow

For every code-changing task, use `$agent-change-workflow` firstly

This repository adds cuteOS-specific constraints to that workflow. The skill
controls the change lifecycle; this file defines project-specific baseline,
authority, ABI, testing, documentation, and boundary rules.

Before modifying any file, the agent must:

1. Classify the task as `feature`, `fix`, `refactor`, `rewrite`, or `mixed`.
2. Record the baseline.
3. Read the relevant project docs and source.
4. Model the task goal, non-goals, success criteria, boundary conditions, and
   red-line behavior.
5. Analyze impact across code, tests, ABI, docs, build, and subsystem
   boundaries.
6. Submit a Pre-Execution Confirmation and wait for user approval, unless the
   same user request explicitly authorizes execution without a second
   confirmation.

Read-only investigation, planning, and review do not require a worktree. Any
file modification does.

## Worktree Rule

For Codex app usage, prefer starting code-changing tasks in a Codex-managed
Worktree.

- If the current thread is already running in a Codex-managed Worktree, that
  satisfies the isolation requirement. Do not create a nested git worktree.
- If the current thread is running in Local and the task will modify project
  files, ask the user to start the task in a Codex Worktree or use Handoff to
  move the thread to Worktree before editing.
- If Codex-managed Worktree is unavailable, forbidden, not selected by the
  user, or the user explicitly authorizes manual fallback, create a manual git
  worktree before editing files.

Manual fallback branch names:

```text
feature/<task-goal>
fix/<task-goal>
refactor/<task-goal>
rewrite/<task-goal>
mixed/<task-goal>
```

Preferred manual fallback worktree path:

```text
/home/pillow/code/.worktrees/cuteos/<task-type>-<task-goal>
```

Do not merge, rebase, squash, delete the branch, delete the worktree, publish,
deploy, or release unless the user explicitly authorizes that after delivery.

## Baseline Requirements

Before any file modification, record:

- Current branch, commit, and worktree status.
- Existing user changes, if any.
- Relevant build and test baseline.
- Affected subsystem boundaries from `CONTEXT.md`.
- Existing behavior that must not regress.
- Known failures, warnings, or unverified areas.

When touching ABI-visible or cross-boundary code, also record:

- Syscall numbers, argument order, return values, and errno behavior.
- UAPI-visible structure layouts and padding.
- Shared kernel/user headers under `include/uapi/` and `user/`.
- Trap-frame, signal-frame, task layout, context-switch, and ELF-loading
  contracts when relevant.
- Current public internal APIs between subsystems.
- Tests that protect the baseline and gaps where characterization tests are
  needed.

If the working tree is dirty, distinguish user-existing changes from agent
changes. Never revert user changes unless the user explicitly asks.

## Authority Levels

Use the authority levels from `$agent-change-workflow`, interpreted for cuteOS
as follows:

- **Local Change**: Modify one function, file, syscall handler, test, or tightly
  coupled helper set. Do not change public ABI, UAPI, data layout, subsystem
  boundaries, or dependency direction.
- **Module Change**: Modify one subsystem such as `mm/`, `fs/vfs/`, `fs/ext2/`,
  `block/`, `sched/`, `syscall/`, `kernel/time`, or `kernel/signal`. Internal
  APIs may change; external contracts must remain stable unless explicitly
  authorized.
- **Architecture Change**: Modify cross-subsystem boundaries, dependency
  direction, public internal APIs, UAPI, boot/init ordering, scheduling model,
  VFS/MM contracts, or platform assumptions. Requires explicit user approval.
- **Rewrite / Replacement**: Replace an implementation or subsystem path.
  Requires explicit user approval, compatibility scope, rollback strategy, and
  stronger baseline tests.

Within the approved authority level, choose the clearest correct structure. Do
not default to a timid patch when the user authorized a better structural fix.
If the task requires a higher authority level than approved, stop and ask.

## Project Context To Read

Use these files to build project understanding:

- `CONTEXT.md`: architecture map, subsystem boundaries, stable entry points.
- `CODE_STYLE.md`: coding style and ABI/style precedence.
- `SYSCALL.md`: syscall maturity, semantics, known gaps, and priorities.
- `README.md`: build, run, and user-facing project overview.
- `TODO.md`: postponed work and priority context.
- `Makefile`: build, QEMU, formatting, and image targets.

For syscall or ABI tasks, also inspect the relevant Linux riscv64/uapi headers,
especially under `/usr/riscv64-linux-gnu/include/asm/*.h` and related
`/usr/riscv64-linux-gnu/include/asm-generic/` headers when applicable.

## Code Style Expectations

- C standard: `gnu17`.
- Formatting: tabs, width 8, 80-column formatting; follow `.clang-format`,
  `.editorconfig`, and `CODE_STYLE.md`.
- Types: prefer fixed-width integer and kernel-defined types where practical.
- Conventions: follow existing directory hierarchy, naming patterns, and
  negative-errno conventions.
- Include order is semantic; do not alphabetically sort includes.
- For ABI-facing code, ABI correctness takes precedence over cosmetic style.
- Do not introduce broad style-only churn.

## Development And Fix Workflow

Use `$tdd` for every feature and bug-fix implementation.

- Plan vertical slices before writing tests.
- Define the test strategy for each behavior before writing the test.
- For syscalls, the plan must specify syscall number, signature, argument
  layout, return value, errno behavior, and Linux riscv64 ABI reference points.
- Before moving from Red to Green, verify that the Red failure matches the
  expected missing behavior, not a test setup error, ABI mismatch, broken
  baseline, or unrelated regression.
- After each Green slice, verify ABI compliance: syscall number, argument
  order, structure layout, errno, padding, and shared user/kernel headers.
- Refactor only within the approved authority level after Green.

If a TDD test remains Red after three serious attempts, pause and report
diagnostics, current hypothesis, attempted fixes, and next options.

## Refactor Workflow

Refactoring defaults to behavior preservation.

Before refactoring, explicitly confirm:

- Refactor authority level.
- Current baseline.
- Structural problem being addressed.
- Target structure.
- Behavior invariants.
- Interfaces and data formats that must not change.
- Whether public API, UAPI, or user-visible behavior changes are allowed.
- Whether opportunistic bug fixes are allowed.
- Tests that protect baseline behavior.
- Missing tests that must be added as characterization tests before risky
  movement.

Do not use "refactor" to hide behavior changes. If behavior must change, mark
the task as `mixed` or request authorization.

Within the approved authority level, choose the most reasonable structure for
cuteOS boundaries. Do not move core subsystem logic into `syscall/`, bypass VFS
or block-device abstractions, or introduce upward dependencies for local
convenience.

## Mixed Tasks

For tasks that combine new behavior and restructuring, split the plan into:

- Behavior changes.
- Structure changes.
- Behavior invariants.
- Development tests.
- Refactor/characterization tests.
- Authority level for each part.

Do not perform broad refactoring as an implicit side effect of a feature or bug
fix. Ask for approval if the structure work grows beyond the confirmed plan.

## Handling Uncertainty

When uncertain about ABI, Linux compatibility, subsystem ownership, or design
boundaries, use `$grill-with-docs`.

If that skill is unavailable, pause and ask the user rather than guessing.

For ABI questions:

1. Consult Linux riscv64/uapi conventions.
2. Check `/usr/riscv64-linux-gnu/include/asm/*.h`.
3. Check relevant `asm-generic` headers if riscv64 delegates there.
4. Compare cuteOS shared headers and syscall dispatch paths.
5. State any remaining uncertainty in the Pre-Execution Confirmation.

## Development Principles

- System call numbers, structure layouts, signal contracts, trap-frame
  contracts, and error codes must comply with the Linux riscv64 ABI where
  cuteOS claims compatibility.
- Maintain current architecture assumptions: single-core execution,
  non-preemptible kernel, user-mode timer-interrupt preemption, and polling I/O.
- User memory must never be accessed by direct dereference. Use uaccess helpers.
- Syscall handlers are ABI boundaries and should delegate policy to subsystems.
- Prefer minimal compatible semantics when Linux behavior is large, but document
  intentional simplifications.
- Do not implement tasks explicitly marked as postponed unless the user asks
  clearly.
- Do not introduce large-scale refactoring without explicit architecture-level
  or rewrite-level authorization.

## Prohibited Changes

- Do not change only one side of a shared ABI layout.
- Do not move core subsystem logic into `syscall/` just because the entry point
  is there.
- Do not bypass VFS, fdtable, page-cache, or block-device abstractions for quick
  fixes.
- Do not introduce upward dependencies from low-level layers into high-level
  policy layers.
- Do not silently replace simple existing mechanisms with more advanced ones
  unless the redesign is intentional and all dependent assumptions are updated.
- Do not assume the debug or teaching baseline config is the active one.
- Do not add source files without updating relevant build lists.
- Do not change UAPI-visible headers without checking user-space mirrors and
  tests.
- Do not make QEMU/platform assumptions broader than the current `virt` target
  unless explicitly authorized.

## Build And Test

- Discover targets: `make help`.
- Build kernel: `make` or `make all`.
- Build user ELF files: `make user`.
- Full boot and kernel self-test path: `make qemu`.
- Debug path: `make qemu-gdb`, then use GDB.
- Formatting: `make format` after approval during the execution phase.

For syscall or user-visible behavior, run the relevant user program from
`user/bin` inside QEMU when practical.

The final report must list exact commands run and distinguish:

- Existing failures.
- New failures introduced by the change.
- Failures fixed by the change.
- Tests not run and why.
- Residual risk from skipped verification.

The target is no new errors and no new warnings. If baseline warnings or
failures already exist, prove this task did not add more.

## Debugging

- Use `pr_err`, `pr_warn`, `pr_notice`, `pr_info`, `pr_debug`, and `panic` for
  kernel diagnostics.
- Use GDB after `make qemu-gdb` for trap, context-switch, page-table, or syscall
  debugging.
- Keep temporary diagnostics out of final code unless they are intentionally
  useful and follow project logging conventions.

## Documentation

Update documentation when behavior, ABI, architecture, configuration, build,
testing, or subsystem boundaries change.

Typical targets:

- `CONTEXT.md` for architecture or boundary changes.
- `SYSCALL.md` for syscall behavior, maturity, gaps, or ABI semantics.
- `CODE_STYLE.md` for style or convention changes.
- `README.md` for user-facing build, run, or capability changes.
- `TODO.md` for deferred follow-up work.

If no documentation update is needed, state why in the final report.

## Final Report Requirements

The final report must include:

- Task type.
- Authority level.
- Branch/worktree or Codex-managed Worktree status.
- Baseline.
- Files changed.
- Behavior changes.
- Structure changes.
- ABI impact.
- Tests and verification commands.
- Documentation updates.
- Risks and limitations.
- Unfinished work or follow-ups.
- User review focus.
- Explicit statement that merge, publish, deploy, branch deletion, and worktree
  deletion were not performed.

## File Organization

`.c` and `.h` files should be structured in this order:

1. Macro / constant definitions.
2. Type definitions: `enum`, `struct`, `typedef`.
3. `static inline` / `static __always_inline` helper functions.
4. Static variables and module-private state.
5. Module-internal functions.
6. Externally visible functions.

Headers may surface public types or APIs earlier. Complex core paths such as
exec, fork, trap, and scheduling may use a few forward declarations to keep the
main flow readable.
