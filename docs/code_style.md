# cuteOS Code Style

This document defines the project coding style for `cuteOS`.

It applies to:
- `*.c`
- `*.h`
- `*.S`
- `*.mk`
- `Kconfig`

It applies to both kernel-space and user-space code in this repository.

For formatting, `.clang-format` is the source of truth.
For ABI-facing code, ABI constraints take precedence over style rules.

In particular, do not break Linux riscv64 ABI compatibility for style:
- syscall numbers
- UAPI-visible structure layout
- errno values
- signal ABI
- trap-frame calling conventions used by syscall dispatch

`include/uapi/*` and duplicated user-space ABI headers must preserve layout
and semantics even when that conflicts with cosmetic style preferences.

This document is intentionally conservative. It is not a license for wide
style-only churn or large refactors.

## 1. Core Principles

- Follow `.clang-format` for mechanical formatting.
- Prefer clear subsystem boundaries over clever local style.
- Preserve existing architecture assumptions:
  - single-core execution
  - non-preemptive kernel
  - user timer-interrupt preemption
  - polling I/O
- When ABI details are uncertain, check Linux riscv64/UAPI behavior first and
  implement the smallest compatible semantics.
- Style is subordinate to correctness, ABI stability, and boundary hygiene.

## 2. Formatting Baseline

This section restates the effective formatting rules from `.clang-format` in
plain language. A contributor should be able to format code correctly from
this document alone, even without opening `.clang-format`.

### 2.1 Indentation and spacing

- Use tabs for indentation.
- Indent width is 8 columns.
- Tab width is 8 columns.
- For `*.c`, `*.h`, `*.mk`, `*.s`, `*.S`, and `Kconfig`, follow
  `.editorconfig` and keep LF line endings with a final newline.

Example:

```c
int example(int x)
{
	if (x > 0) {
		return x;
	}

	return 0;
}
```

### 2.2 Braces

- Function opening braces go on the next line.
- Control-statement opening braces stay on the same line.
- `struct`, `union`, and `enum` opening braces stay on the same line.
- Single-statement bodies of `if`, `for`, and `while` may omit braces.
- When braces are omitted, the controlled statement must appear on the next
  line with normal indentation.
- Empty blocks should remain explicit and readable.

Examples:

```c
int foo(void)
{
	return 0;
}

if (ready) {
	run();
}

if (ready)
	run();

struct point {
	int x;
	int y;
};
```

Do not write:

```c
int foo(void) { return 0; }
if (ready) { run(); }
if (ready) run();
```

### 2.3 Line length

- The default hard limit is 80 columns.
- Very rare exceptions are allowed only for:
  - ABI/UAPI constants, field names, or assembly symbol names where splitting
    hurts cross-reference value
  - format strings, `static_assert` conditions, or compiler-attribute
    combinations that cannot be split cleanly
  - macros where splitting would materially harm readability or alter
    preprocessor semantics
- When a line exceeds 80 columns, prefer:
  - rearranging code
  - introducing a local variable
  - extracting a helper
  rather than accepting the long line

Example:

```c
ret = vfs_lookup_at(dirfd, path, flags, &dentry);
```

If that no longer fits cleanly, prefer:

```c
ret = vfs_lookup_at(dirfd, path, flags,
		    &dentry);
```

or:

```c
struct dentry *target = &dentry;

ret = vfs_lookup_at(dirfd, path, flags, target);
```

### 2.4 Alignment and wrapping

- Do not manually chase vertical alignment unless it survives formatting and
  improves readability.
- Pointer style follows `.clang-format`: `type *ptr`.
- Keep include order semantic; do not alphabetically sort includes.
- Continuation lines use one additional indentation level.
- Long argument lists may wrap across multiple lines, and wrapped arguments are
  aligned for readability.
- Binary operators normally stay at the end of the continued line.
- Ternary operators may wrap before `?` and `:`.

Examples:

```c
struct task_struct *task;
const struct sys_timespec *req;

ret = validate_user_buffer(addr, len, flags,
			   current);

mask = TASK_RUNNING | TASK_INTERRUPTIBLE |
       TASK_UNINTERRUPTIBLE;
```

Do not write:

```c
struct task_struct* task;
const struct sys_timespec * req;
```

### 2.5 Spaces

- Put a space before assignment operators.
- Put a space after control keywords before `(`:
  - `if (cond)`
  - `for (...)`
  - `while (...)`
  - `switch (...)`
- Do not put a space before the `(` of ordinary function calls.
- Do not put extra spaces inside parentheses.
- Do not put extra spaces inside array subscripts.
- Do not put extra spaces inside C-style casts.
- Do not put a space after a C-style cast.

Examples:

```c
if (task != NULL) {
	ret = copy_to_user(ubuf, buf, len);
	idx = table[i];
	val = (int)x;
}

ret = do_syscall(tf);
```

Do not write:

```c
if( task != NULL ) {
	ret=copy_to_user( ubuf, buf, len );
	idx = table[ i ];
	val = (int) x;
}

ret = do_syscall (tf);
```

### 2.6 Short forms forbidden by formatter

- Do not compress short `if` statements onto one line.
- Do not compress short loop bodies onto one line.
- Do not compress short braced block bodies onto one line.
- Do not compress short functions onto one line.
- Do not keep short `case` labels and actions on one line.

Examples:

```c
if (err)
	return err;

for (i = 0; i < n; i++) {
	sum += a[i];
}

case SEEK_SET:
	pos = off;
	break;
```

Do not write:

```c
if (err) return err;
for (i = 0; i < n; i++) sum += a[i];
case SEEK_SET: pos = off; break;
```

### 2.7 Empty lines

- Do not leave empty lines at the start of a block.
- Keep at most one consecutive empty line.

Example:

```c
int foo(void)
{
	int ret;

	ret = bar();
	if (ret < 0)
		return ret;

	return 0;
}
```

Do not write:

```c
int foo(void)
{

	int ret;


	ret = bar();
}
```

### 2.8 Preprocessor directives

- Preprocessor directives stay at the left margin.
- Do not indent `#if`, `#ifdef`, `#define`, `#endif` to match surrounding code.

Example:

```c
#ifdef CONFIG_KERNEL_TEST
void kernel_test(void);
#endif
```

### 2.9 `switch` indentation

- `case` labels are aligned with `switch`, not indented one extra level.
- Statements inside a `case` are indented normally inside the block.

Example:

```c
switch (cmd) {
case CMD_READ:
	ret = do_read();
	break;
case CMD_WRITE:
	ret = do_write();
	break;
default:
	BUG();
}
```

### 2.10 `for`-like macros

- The following macros are formatted as loop constructs:
  - `list_for_each`
  - `list_for_each_safe`
  - `list_for_each_entry`
  - `list_for_each_entry_safe`
  - `hash_table_for_each_possible`
- Write their bodies as ordinary loop bodies with braces and normal
  indentation.

Example:

```c
list_for_each_entry(pos, head, member) {
	handle_entry(pos);
}
```

## 3. Scope and Precedence

- `.clang-format` governs mechanical style.
- This document governs semantic style not fully expressed in
  `.clang-format`.
- ABI and hardware contracts override local style preferences.
- Existing simplifications are preserved unless a change intentionally
  redesigns them and all affected assumptions are updated.

## 4. File and Include Rules

### 4.1 Header guards

- Use include guards in all headers.
- Continue the existing all-caps guard style.
- Do not switch to `#pragma once`.

### 4.2 Header self-sufficiency

- Headers should be self-contained when practical.
- A `.c` file should not rely on unrelated prior includes to make a header
  compile.

### 4.3 Include ordering

In `.c` files, order includes by meaning rather than alphabetically:

1. The module's own public header, if any
2. Same-subsystem headers
3. Generic kernel headers
4. UAPI headers
5. Architecture-specific headers
6. Local private headers such as `"internal.h"`

Example:

```c
#include <kernel/mm.h>
#include <kernel/page.h>
#include <kernel/task.h>
#include <uapi/mman.h>

#include "internal.h"
```

### 4.4 Forward declarations

- Prefer forward declarations when they clearly reduce coupling.
- If a complete type, inline helper, or macro contract is needed, include the
  real header directly.
- Do not create brittle include games just to save one include.

### 4.5 Dependency hygiene

- Do not introduce circular includes.
- If a cycle appears, break it by moving declarations or reducing coupling.
- Do not collapse UAPI, kernel, and user-space headers into shared
  implementation headers.

## 5. Naming

### 5.1 General naming

- Use `snake_case` for:
  - functions
  - variables
  - structure members
  - typedef names
- Do not introduce `CamelCase` naming schemes.

### 5.2 Global symbol naming

- Global functions and global variables should carry subsystem context.
- Prefer names such as:
  - `mm_*`
  - `task_*`
  - `sched_*`
  - `vfs_*`
  - `ext2_*`
  - `sys_*`
  - `arch_*`

### 5.3 File-local naming

- File-local helpers may be shorter, but still need clear context.
- Avoid context-free names such as:
  - `init`
  - `handle`
  - `check`
  - `update`

### 5.4 Boolean naming

- Boolean variables and predicate helpers should read like predicates.
- Prefer names such as:
  - `task_is_runnable()`
  - `clone_wants_thread`
- Do not give a boolean-returning helper an action-style name.

### 5.5 Accessor naming

- Use `get_*` and `set_*` only for light accessors or mutators.
- If a helper allocates memory, changes references, acquires locks, or has
  other important side effects, do not disguise it as `get_*` or `set_*`.

### 5.6 Internal execution naming

- Reserve `do_*` for a small number of internal execution helpers.
- Do not use `do_*` as a generic naming escape hatch.

## 6. Types

### 6.1 Preferred type families

- New code should prefer semantically appropriate standard-width or
  platform-width names already defined by the project:
  - `uint32_t`
  - `int64_t`
  - `size_t`
  - `ssize_t`
  - `uintptr_t`
- `u32`, `u64`, `i32`, and similar short aliases may remain in existing code,
  and may be used in register-heavy or architecture-near code where width and
  density matter.
- For new general code, prefer the longer standard names.

### 6.2 Platform-width types

- Prefer `size_t` and `ssize_t` when the value represents size, count,
  capacity, or a size-like result.
- Prefer semantic platform-width types to avoid unnecessary friction for
  future 32-bit support.

### 6.3 Signedness and booleans

- Predicate functions should return `bool`.
- Do not mix errno values, lengths, bitmasks, and booleans into ambiguous
  pseudo-boolean APIs.
- Use `char` only for character or byte-buffer semantics.
- For signedness-sensitive byte data, prefer `uint8_t` or `int8_t`.

### 6.4 Pointer/integer conversions

- Convert pointers through explicit bridge types such as:
  - `uintptr_t`
  - `paddr_t`
  - `vaddr_t`
- Do not cast pointers to arbitrary integer types such as
  `unsigned long long` without a real contract reason.

### 6.5 Plain integer types

- Avoid bare `short`, `int`, and `long` for width-sensitive data.
- Use machine-width integer types only when the ABI or hardware contract is
  intentionally machine-width.

## 7. Constants, Casts, and Conversions

### 7.1 Integer literals

- Use explicit suffixes when width matters.
- Examples:
  - `1UL << order`
  - `0x10000000UL`
  - `1ULL << 40`

### 7.2 Shift safety

- Ensure the left operand has sufficient width before shifting.
- Do not write width-dependent expressions such as `1 << 40`.

### 7.3 Signed/unsigned comparisons

- Avoid implicit signed/unsigned mixing.
- Prefer unifying the involved values first.
- When interacting with negative errno values or sentinel values, split the
  logic clearly instead of relying on implicit conversion.

### 7.4 Cast discipline

- Cast only at real boundary points.
- Acceptable boundary casts include:
  - trap-frame ABI argument unpacking
  - pointer/address/integer bridge conversions
  - MMIO and page-table access
  - checked narrowing conversions
- Do not use casts as warning suppressors.

### 7.5 Narrowing conversions

- Narrowing from `size_t` or `uint64_t` to smaller integer types should
  normally be preceded by an explicit range check.
- When an ABI requires a narrow type, keep the narrowing point obvious.

### 7.6 Pointer arithmetic

- Perform byte-granularity pointer arithmetic on `uint8_t *`, `char *`, or
  explicit address integer types.
- Do not rely on GNU `void *` arithmetic in ordinary code.

### 7.7 Null pointers

- Use `NULL` in C code.
- Do not introduce new `nullptr` usage.

Example:

```c
if (mm == NULL)
	return -EINVAL;
```

## 8. Macro, Enum, and Inline Boundaries

### 8.1 Macro preference

- This project prefers macros over enums for named constants.
- Except for the existing `false` and `true` definition pattern, new enums are
  generally discouraged.

### 8.2 Use `#define` for

- include guards
- preprocessor-controlled logic
- token-pasting or stringification
- ABI, ISA, MMIO, and assembly-shared constants
- bit masks, shifts, register fields, and page-table flags
- a small number of zero-overhead generic facilities that cannot be replaced
  cleanly by functions

### 8.3 Use `static inline` for

- small typed operations with evaluation semantics
- accessors and converters
- helpers where a function form improves type safety or debugability
- cases where a function-like macro would cause repeated evaluation or obscure
  types

### 8.4 Enum usage

- Do not introduce enums for ordinary internal constant groups.
- Use macros instead unless there is an unusually strong reason and the result
  is clearly better than the macro form.

## 9. Macro Writing Rules

### 9.1 Object-like macros

- If the replacement text is an expression, parenthesize it as needed.
- Single literal constants may remain unwrapped.
- Any macro involving operators should be explicitly grouped.

Examples:

```c
#define UART_BASE 0x10000000UL
#define KSTACK_SIZE (PAGE_SIZE << KSTACK_ORDER)
```

### 9.2 Function-like macros

- Parenthesize every parameter use.
- Parenthesize the whole result expression unless the macro intentionally
  expands to declarations or statements.

Example:

```c
#define BIT(nr) (1UL << (nr))
```

### 9.3 Multi-statement macros

- Wrap multi-statement macros in `do { ... } while (0)`.
- Do not expose naked multi-statement expansions to call sites.

Example:

```c
#define FOO(x)								\
	do {								\
		bar(x);							\
		baz(x);							\
	} while (0)
```

### 9.4 Side effects

- Ordinary subsystem code should not introduce macros that evaluate arguments
  multiple times.
- If a macro must accept side-effectful arguments, its contract must be clear.
- If a macro can reasonably become `static inline`, prefer the function form.

### 9.5 Statement expressions

- GNU statement expressions are allowed only in low-level shared
  infrastructure macros where the value is clear and the benefit is real.
- Do not spread statement-expression style through ordinary subsystem code.

### 9.6 Macro scope

- Keep file-local macros in `.c` files when possible.
- Public headers should expose only broadly useful, ABI-related, ISA-related,
  MMIO-related, or generic facility macros.

### 9.7 Conditional compilation

- Keep `#ifdef` regions coarse-grained.
- Prefer wrapping whole definitions or feature blocks.
- Do not slice function bodies into many preprocessor fragments.

## 10. Compiler Extensions and Attributes

### 10.1 General rule

- GNU C and compiler extensions are allowed, but should be centrally
  encapsulated when practical.
- Ordinary subsystem code should not scatter raw `__attribute__`,
  `__builtin_*`, statement expressions, `typeof`, or `_Generic` unless there
  is a real low-level need and no better project wrapper exists.

### 10.2 Preferred wrapper locations

- compiler attributes: `include/compiler/compiler_attribute.h`
- compiler builtins: `include/compiler/compiler_builtin.h`
- generic low-level helpers: `include/kernel/tools.h`

### 10.3 Inline rules

- Small header helpers default to `static inline`.
- Ordinary `.c` functions should not be marked `inline` by default.
- `__always_inline` is reserved for helpers that are all of:
  - extremely short
  - extremely hot
  - expensive to abstract as a real call boundary
  - tightly coupled to low-level ABI, register, trap, or similar machine-near
    behavior
- If those conditions are not met, do not use `__always_inline`.

Examples:

```c
static inline bool task_is_runnable(unsigned int state)
{
	return state == TASK_RUNNING;
}
```

```c
static __always_inline uintptr_t pte_phys_addr(pte_t pte)
{
	return pte & PAGE_MASK;
}
```

### 10.4 Attribute use

- Use project wrappers such as:
  - `__always_inline`
  - `__noinline`
  - `__packed`
  - `__aligned`
  - `__section`
  - `__weak`
  - `__printf`
- Do not write raw attributes in ordinary code when an existing wrapper exists.

### 10.5 Branch prediction and builtins

- Use `likely()` and `unlikely()` only when branch probability is obvious and
  relevant.
- Do not mechanically decorate every error check.
- Use `unreachable()` only for genuinely unreachable logic, not wishful
  thinking.

### 10.6 Volatile and inline assembly

- Do not use `volatile` as a general synchronization tool.
- Restrict `volatile` to MMIO or similarly justified low-level boundaries.
- Keep inline assembly in architecture-specific or truly machine-near code.

## 11. Structures, Unions, and Layout-Sensitive Code

### 11.1 Field ordering

- Order structure fields by semantics and lifecycle, unless an external layout
  contract dictates otherwise.
- Do not reorder ABI-facing layouts for cosmetic alignment.

### 11.2 ABI-sensitive structures

- For UAPI, hardware, trap-frame, signal-frame, disk-format, and similar
  layout-sensitive objects:
  - follow the external specification first
  - use `__packed` or `__aligned(n)` only when justified
  - document why the layout is sensitive
  - add `static_assert` on size or offset where useful

### 11.3 Internal structures

- Ordinary internal kernel structures should not use `__packed` by default.
- Do not trade natural alignment and code quality for superficial byte savings.

### 11.4 Unions

- Use unions only for true multiple-view storage semantics.
- Do not use unions as loose type-punning shortcuts when a clearer mechanism
  exists.

### 11.5 Bit-fields

- C bit-fields are banned by default.
- Express register fields, flags, and PTE fields with explicit masks and
  shifts.

### 11.6 Flexible array members

- Use flexible array members only when a trailing variable-size layout is
  truly required.
- Pair them with an explicit size/length contract.

## 12. Functions and Control Flow

### 12.1 Function size guidance

- Small helpers should usually stay within about 40 effective lines.
- Normal core functions should usually stay within about 80 effective lines.
- Functions beyond roughly 120 effective lines should be treated as exceptions
  and justified by the nature of the logic.
- Do not split naturally cohesive logic into meaningless helpers just to hit a
  number.

### 12.2 Parameter count

- Prefer at most 5 parameters.
- More than 6 parameters should usually trigger reconsideration, except for
  fixed ABI, trap, SBI, or similar boundary interfaces.

### 12.3 Local variable declarations

- Function-local variables should generally be declared near the top of the
  function.
- The main exception is ultra-short-lifetime syntax-local declarations such as
  `for (int i = 0; i < n; i++)`.
- Do not pack unrelated variables into the same declaration line.

Example:

```c
int foo(struct mm_struct *mm)
{
	int ret;
	size_t i;
	int retry;
	struct vm_area_struct *vma;

	for (retry = 0; retry < 2; retry++) {
		ret = try_lock(mm);
		if (ret == 0)
			break;
	}

	for (i = 0; i < NR_VMA; i++) {
		vma = &mm->vma[i];
	}

	return 0;
}
```

### 12.4 Early return

- Use early returns for precondition failures and simple error exits.
- Keep the main success path as shallow as practical.

Example:

```c
if (!mm)
	return -EINVAL;

if (!buf)
	return -EFAULT;
```

### 12.5 Goto use

- `goto` is allowed for cleanup and rollback paths.
- It may also be used sparingly for clear multi-level exits.
- Do not use `goto` as a substitute for ordinary structured control flow.
- Cleanup labels should be semantic and ordered by reverse release sequence.

Example:

```c
ret = alloc_a(&a);
if (ret < 0)
	return ret;

ret = alloc_b(&b);
if (ret < 0)
	goto out_free_a;

return 0;

out_free_a:
	free_a(a);
	return ret;
```

### 12.6 Multiple returns

- Simple helpers may use multiple return points.
- Resource-owning functions may use mixed style:
  - early error checks
  - cleanup `goto`
  - one final success return
- Single-exit style is not required.

## 13. Switch and Case Rules

- `switch` is for discrete branch selection, not arbitrary boolean logic.
- `case` labels are not indented beyond `.clang-format` defaults.
- Non-empty cases should normally end with explicit
  `break`, `return`, `goto`, or `continue`.
- Fallthrough is allowed only with explicit annotation such as
  `/* fall through */`.
- Every `switch` must have a `default`.
- If all valid values are already covered, `default` must still exist and must
  clearly express that reaching it is a bug or impossible control flow.
- Suitable `default` behavior includes:
  - `BUG()`
  - `BUG_ON(1)`
  - `unreachable()`
  - an equivalent fatal or clearly impossible-path construct
- The chosen form must make the intended semantics obvious.

Example:

```c
switch (state) {
case TASK_RUNNING:
	return true;
case TASK_INTERRUPTIBLE:
	return false;
default:
	BUG();
}
```

## 14. Loop Rules

- Use `for` for counted or boundary-explicit iteration.
- Use `while` for condition-driven loops whose natural form is not counted.
- Use `for (;;)` only for deliberate infinite-loop structure.
- Do not rewrite ordinary `while` loops into `for (;;)` merely for style.
- Loop bodies should use braces even for single statements.
- Keep loop exit conditions clear.
- Avoid hiding multiple unrelated updates in the `for` header.
- Prefer container iteration macros for kernel containers where appropriate.
- Use safe iterator variants when the current element may be removed.
- For multi-level exits, prefer a helper return or a small cleanup `goto`
  rather than state-flag spaghetti.

Examples:

```c
for (i = 0; i < NR_VMA; i++) {
	inspect_vma(&mm->vma[i]);
}

while (pending_signal(current)) {
	deliver_one_signal(current);
}

for (;;) {
	cpu_relax();
}
```

## 15. Expressions and Operators

- Do not hide assignment inside conditions in ordinary code.
- Do not use the comma operator except for clear and ordinary `for`-header
  usage.
- Use the ternary operator only for very short and simple expressions.
- Do not nest ternary operators.
- If a condition mixes too many concepts, split it.
- For bitmask tests, prefer explicit tests such as `(flags & MASK) != 0`.
- Do not write `flag == true`.
- Add parentheses when combining arithmetic and bitwise operators in ways that
  make the intended grouping non-obvious.
- Keep one main side effect per statement when practical.
- Prefer `sizeof(*ptr)` or `sizeof(obj)` over repeating a type name, unless
  ABI/layout intent is clearer with the type form.

Examples:

```c
ret = copy_to_user(ubuf, kbuf, sizeof(*kbuf));

if ((flags & TASK_ANY_SLEEP) != 0)
	return true;
```

Do not write:

```c
if (flag == true)
	return true;
```

## 16. Error Handling, Assertions, and Logging

- Internal kernel code should return Linux-style negative errno values on
  failure.
- Syscall boundary code should unpack ABI arguments, validate user pointers,
  delegate to subsystem logic, and return underlying errors faithfully.
- Expected runtime failures return error codes; they do not `panic`.
- Reserve `panic`, `BUG`, and `BUG_ON` for broken kernel invariants,
  irrecoverable corruption, or startup-fatal conditions.
- User mistakes, resource exhaustion, not-found conditions, and unimplemented
  features must not crash the kernel.
- Do not use `BUG_ON` for ordinary argument validation.
- Avoid redundant logging of the same failure at multiple layers.

## 17. Comments and Documentation Style

### 17.1 Comment purpose

- Comments should explain:
  - design intent
  - ABI or hardware constraints
  - invariants
  - why something is done a certain way
- Avoid comments that merely restate the code.

### 17.2 File headers

- Keep file-header comments in `.c` files.
- The file header should explain:
  - what the file is responsible for
  - what boundary it belongs to
  - what it intentionally does not own when that matters

### 17.3 Function comments

- Public headers and non-trivial core functions may have brief explanatory
  comments.
- Not every small helper needs a large comment block.

### 17.4 Comment language

- Use Chinese prose with English technical terms, identifiers, and ABI names
  preserved as written.
- Do not translate terms such as:
  - Linux ABI
  - `trap_frame`
  - `CLOCK_MONOTONIC`

### 17.5 TODO and related markers

- Use:
  - `TODO(subsystem): ...`
  - `FIXME(subsystem): ...`
  - `HACK(subsystem): ...`
  - `WORKAROUND(subsystem): ...`
- Every such marker must state a concrete gap, limitation, or reason.
- Do not leave empty placeholders such as `TODO: optimize`.

## 18. Temporary Code and Debug Residue

- Do not leave commented-out old code in committed changes.
- Do not keep temporary `printk` noise after the investigation is over.
- Do not keep dead `#if 0` or similar temporary scaffolding without a very
  strong reason.
- If a debug aid deserves to survive, convert it into a clear and controlled
  mechanism.

## 19. Assembly (`*.S`) Appendix

- Use tab indentation with 8-column width.
- Keep meaningful file-header comments describing entry semantics, calling
  convention, register clobbers, and ABI boundaries.
- Use named constants and macros for offsets, flags, CSR values, and page-table
  details instead of scattering magic numbers.
- Labels should be semantic when they are visible over non-trivial distance.
- Comments should focus on calling convention, register preservation,
  transition semantics, and invariants, not literal instruction-by-instruction
  translation.
- The 80-column rule still applies by default, with rare symbol-related
  exceptions.

## 20. Makefile (`*.mk`) Appendix

- Respect tab-indentation requirements of `make`.
- Keep variable naming consistent with existing make style.
- Avoid embedding large or obscure shell programs inside make variables.
- When adding source files, update the relevant `*.mk` files explicitly.
- Prefer explicit dependency structure over clever auto-discovery.

## 21. Kconfig Appendix

- Keep option names explicit and all-caps with clear prefixes.
- Help text should describe feature meaning, dependency expectations, and
  default rationale.
- Do not bury implementation details in help text.
- Keep the configuration surface small and coherent.
- Express defaults and dependencies conservatively; do not expose incomplete
  features casually.

## 22. Non-Goals

This style guide does not authorize:
- ABI-changing cleanup
- broad style-only rewrites
- bypassing VFS, block, syscall, or MM layering for convenience
- moving core logic into `syscall/`
- replacing simple existing mechanisms with more advanced ones without a
  deliberate redesign

## 23. Review Rule

When a style rule conflicts with:
- ABI correctness
- hardware interface correctness
- subsystem boundary integrity
- or clear local readability

prefer the technically correct result, and document the exception if it is not
obvious.
