---
name: agent-change-workflow
description: Mandatory workflow for all code change tasks performed by an agent, including feature development, bug fixes, refactoring, rewrites, mixed development and refactoring, and project documentation changes tied to code. Use when Codex is asked to modify code, tests, configuration, build or CI files, or docs in a software project. Enforces explicit requirements, baseline capture, authority levels, pre-execution confirmation, Codex-managed or git worktree isolation, continuous verification, documentation updates, and final reporting.
---

# Agent Change Workflow

Apply this as a mandatory protocol for every software project change that modifies code, tests, configuration, build or CI files, or project documentation.

Do not treat this as optional guidance. The purpose is to keep human and agent aligned before action, constrain freedom by explicit authority, and verify continuously.

## Core Principles

- **Explicit over Implicit**: Make requirements, boundaries, permissions, goals, constraints, risks, and validation explicit.
- **Alignment before Action**: Before modifying files or running side-effecting commands, build shared understanding and obtain confirmation.
- **Authority Defines Freedom**: Choose the best solution within the authorized level. Do not default to the smallest patch when the user authorized a broader change.
- **Verification as a Continuous Process**: Validate assumptions, behavior, structure, tests, warnings, docs, and risks throughout the workflow.
- **Baseline First**: Treat the current project state as the comparison point for all changes.
- **No Automatic Integration**: Do not merge, rebase, squash, delete the worktree, delete the branch, publish, deploy, or release unless the user explicitly authorizes that after delivery.

## Scope

Classify each change request before acting:

- `feature`: Add or change behavior.
- `fix`: Correct a bug.
- `refactor`: Improve structure while preserving behavior by default.
- `rewrite`: Replace an existing implementation.
- `mixed`: Combine behavior changes and structural changes.

For `mixed` tasks, separate behavior changes from structure changes before planning.

Read-only analysis, code review, investigation, and planning do not require worktree creation. Any file modification does.

## Hard Gates

Before modifying files:

1. Capture baseline.
2. Build project understanding.
3. Model the task.
4. Analyze impact.
5. Propose a plan and verification strategy.
6. Submit a Pre-Execution Confirmation.
7. Wait for user approval.

Exception: if the same user request explicitly grants execution authority, submit a brief Pre-Execution Confirmation, state that the user authorized execution without a second confirmation, then proceed without waiting.

Allowed before approval:

- Read files.
- Search code.
- Inspect git state.
- Run read-only commands.
- Run existing tests or build commands if they do not mutate project state beyond ordinary caches/artifacts.

Not allowed before approval unless explicitly authorized:

- Modify files.
- Install, remove, or upgrade dependencies.
- Change configuration.
- Run migrations.
- Execute commands that mutate external systems.
- Publish, deploy, release, or merge.

## Authority Levels

Use one of these levels for every task:

1. **Local Change**: Modify a small local area, such as one function, component, file, route, or tightly coupled file set.
2. **Module Change**: Reorganize or extend one module or subsystem, including internal APIs, internal files, and module-local tests.
3. **Architecture Change**: Change cross-module boundaries, dependency direction, layering, shared abstractions, public interfaces, or system structure.
4. **Rewrite / Replacement**: Replace an implementation with a new one, delete old paths, or rebuild a module around a new internal model.

In development, this controls how much existing structure may change. In refactoring, this maps to local refactor, module refactor, architecture refactor, and rewrite.

If execution requires a higher authority level than approved, stop and ask for confirmation.

## Baseline

Record baseline for every code change:

- Current branch.
- Current commit.
- Worktree status.
- Existing user changes.
- Known test failures, warnings, lint issues, or build failures.
- Key affected behavior.

For refactor, rewrite, and mixed tasks, also record:

- Current public API or external contracts.
- Current data formats or persistence contracts.
- Current module boundaries.
- Behavior invariants that must remain true.
- Structural problems being addressed.
- Tests that protect baseline behavior.
- Missing tests that weaken behavior preservation.

If the worktree is dirty, distinguish user-existing changes from agent changes. Do not revert user changes.

## Project Understanding

Read enough docs, code, tests, configuration, and CI to answer:

- What is the project goal?
- What architecture and module boundaries exist?
- What modules are affected by this task?
- What code style and naming patterns are used?
- What test, lint, typecheck, build, and CI commands exist?
- What constraints affect the task?
- What documentation may need updates?

Use repository-specific instructions such as AGENTS.md when present.

## Task Modeling

Always define:

- Goal.
- Non-goals.
- Success criteria.
- Boundary conditions.
- Red-line behaviors.
- Key assumptions.
- Acceptance criteria.

For `feature` and `fix`, also define:

- Desired behavior.
- User-visible behavior.
- Inputs and outputs.
- Error and edge cases.
- Existing behavior that must not regress.

For `refactor`, also define:

- Current structural problem.
- Target structure.
- Behavior invariants.
- Interfaces that must remain unchanged.
- Whether public API changes are allowed.
- Whether data format changes are allowed.
- Whether opportunistic bug fixes are allowed.

For `rewrite`, also define:

- Replacement boundary.
- Compatibility scope.
- Migration or cutover strategy.
- Rollback or fallback approach.

For `mixed`, explicitly separate:

- Behavior changes.
- Structure changes.
- Behavior invariants.
- Authorized scope for each part.

## Impact Analysis

State the task radius:

- Files expected to change.
- Modules affected.
- Interfaces affected.
- Data structures or schemas affected.
- Configuration, build, CI, deployment, or migration impact.
- Tests to add or update.
- Docs to add or update.
- Compatibility, performance, security, and operational risks.

Use this grouping:

- **Definite impact**
- **Possible impact**
- **Needs confirmation**
- **Explicitly not impacted**

## Plan and Verification Strategy

Before execution, propose:

- Implementation steps.
- Purpose of each step.
- Expected modification scope.
- Reasoning and tradeoffs.
- Alternative considered when relevant.
- Verification plan.
- Documentation plan.
- Stop conditions.

For development, prove the new behavior is correct and old behavior did not regress.

For refactoring, prove baseline behavior is preserved and the structural target was achieved.

For rewrite, prove compatibility within the approved boundary and show what old behavior or structure was intentionally replaced.

## Pre-Execution Confirmation Template

Use this template before modifying files. Compress for small tasks, but do not silently omit fields. Use `N/A` with a reason when a field does not apply.

```text
Pre-Execution Confirmation

Task Type:
Authority Level:
Baseline:
Project Understanding:
Task Model:
Impact Analysis:
Proposed Plan:
Verification Plan:
Documentation Plan:
Stop Conditions:
Confirmation Needed:
```

If the user already authorized execution in the same request, set `Confirmation Needed` to:

```text
User explicitly authorized execution in the request. I am providing this confirmation package for alignment and will proceed without waiting for a second confirmation.
```

Otherwise, stop after the confirmation package and wait.

## Worktree Execution

Prefer Codex-managed worktrees when running in the Codex app.

If the current thread already runs in a Codex-managed Worktree, treat that checkout as satisfying the worktree isolation requirement. Do not create a nested git worktree.

If the current thread runs in Local and the task will modify project files, ask the user to start the task in a Codex Worktree or use Handoff to move the thread to Worktree when that app capability is available.

If Codex-managed Worktree is unavailable, forbidden, not selected by the user, or the user explicitly authorizes manual fallback, create a dedicated git worktree before editing files.

Worktree isolation is required unless the project is not a git repository, the user explicitly forbids worktrees, or the task is read-only.

Use branch names in this form:

```text
feature/task-goal
fix/task-goal
refactor/task-goal
rewrite/task-goal
mixed/task-goal
```

Use a safe worktree path, for example:

```text
.worktrees/feature-task-goal
.worktrees/refactor-task-goal
```

Prefer a slug derived from the task goal. If a branch or worktree path already exists, ask before reusing or replacing it.

When working in an isolated checkout:

- Work only inside that checkout.
- Follow the approved plan.
- Run targeted verification after meaningful steps.
- Update the plan if discoveries remain inside the approved authority.
- Stop and ask for confirmation if scope, authority, API, dependency, data, or behavior assumptions change.

## Stop Conditions

Stop and realign with the user if:

- Impact expands beyond the approved plan.
- A higher authority level is required.
- An unapproved public API change is needed.
- A dependency must be added, removed, or upgraded.
- Data formats, persistence, migrations, or deployment behavior must change unexpectedly.
- Tests reveal a requirements misunderstanding.
- Refactoring cannot proceed without changing behavior.
- The baseline is unclear or too weak to verify safely.
- Existing user changes conflict with the task.

## Verification

Run the strongest practical verification for the repository:

- Targeted tests.
- Full test suite when feasible.
- Lint.
- Typecheck.
- Build.
- CI-equivalent commands.
- Manual or scripted behavior checks when needed.
- Documentation consistency checks.

Distinguish:

- Existing failures.
- New failures introduced by this change.
- Failures fixed by this change.
- Tests not run and why.

The target is no new errors and no new warnings. If the project already has warnings or failures, record the baseline and prove the change did not add more.

## Documentation

Update docs when the change affects:

- User-visible behavior.
- Public APIs.
- Architecture.
- Configuration.
- Build, CI, deployment, or operations.
- Data formats or migrations.

If no docs need updates, state why in the final report.

## Final Report Template

After execution and verification, report:

```text
Final Report

Task Type:
Task Summary:
Authority Level:
Branch / Worktree:
Baseline:
Changes Made:
Added:
Removed:
Behavior Changes:
Structure Changes:
Verification:
Documentation:
Risks and Limitations:
Unfinished Work:
User Review Focus:
Not Performed:
```

Always include `Not Performed` and explicitly state that merge, publish, deploy, branch deletion, and worktree deletion were not performed unless the user separately authorized them.
