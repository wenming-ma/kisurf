# KiSurf AI Native Plan Set Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Coordinate the first implementation-ready KiSurf AI-native plan set before production code begins.

**Architecture:** The plan set follows the committed specs and builds KiSurf in four layers: foundation runtime, docked Agent pane, editor context and preview/edit sessions, then validation and self-test. Each layer is independently reviewable and testable, while later layers depend only on public interfaces from earlier layers.

**Tech Stack:** C++17, wxWidgets, KiCad GAL/AUI/action framework, Boost.Test, CMake/Ninja, KiCad QA targets.

---

## Source Specs

- `docs/superpowers/specs/2026-06-16-kisurf-ai-native-spec-index.md`
- `docs/superpowers/specs/2026-06-16-ai-native-foundation-design.md`
- `docs/superpowers/specs/2026-06-16-agent-window-design.md`
- `docs/superpowers/specs/2026-06-16-context-preview-materialize-design.md`
- `docs/superpowers/specs/2026-06-16-validation-and-self-test-design.md`
- `docs/superpowers/specs/2026-06-16-spec-self-review.md`

## Plan Files

Execute these plans in order:

1. `docs/superpowers/plans/2026-06-16-ai-native-foundation-implementation.md`
2. `docs/superpowers/plans/2026-06-16-ai-agent-window-implementation.md`
3. `docs/superpowers/plans/2026-06-16-ai-context-preview-materialize-implementation.md`
4. `docs/superpowers/plans/2026-06-16-ai-validation-self-test-implementation.md`

The order matters because the Agent pane uses the foundation runtime, preview/edit sessions use foundation types, and validation attaches summaries to suggestions and edit sessions.

## Working Tree Rule

Before implementation starts, create or switch to an implementation branch or worktree:

```powershell
git status --short
git switch -c codex/ai-native-first-slice
```

Expected:

- `git status --short` prints no unrelated tracked changes in files the implementation will touch.
- `git switch` creates the branch, or the worker reports that the branch already exists and switches to it.

## Build Presets And Commands

The checked local CMake preset file defines `x64-release` and `x64-debug`. The existing local build directory is `out/build/x64-release`, so use release first:

```powershell
cmake --preset x64-release -DKICAD_BUILD_QA_TESTS=ON
cmake --build --preset x64-release --target qa_common
$env:KICAD_RUN_FROM_BUILD_DIR='1'; ctest --test-dir out/build/x64-release -R qa_common --output-on-failure
```

Expected:

- Configure ends with build files written to `out/build/x64-release`.
- Build finishes `qa_common` without compiler or linker errors.
- CTest reports the selected QA tests passed.

When a plan adds PCB or schematic tests, extend the build commands:

```powershell
cmake --build --preset x64-release --target qa_pcbnew
cmake --build --preset x64-release --target qa_eeschema
$env:KICAD_RUN_FROM_BUILD_DIR='1'; ctest --test-dir out/build/x64-release -R "qa_pcbnew|qa_eeschema" --output-on-failure
```

Expected:

- `qa_pcbnew` and `qa_eeschema` build.
- CTest reports the selected editor QA tests passed.

## Task 1: Plan Gate

**Files:**
- Read: `docs/superpowers/specs/2026-06-16-spec-self-review.md`
- Read: all four detailed plan files listed above

- [ ] **Step 1: Confirm the spec gate**

Run:

```powershell
git log --oneline -1
git status --short
```

Expected:

- The latest spec commit is present in history.
- The working tree is clean before implementation begins, except for any accepted plan commit.

- [ ] **Step 2: Confirm plan coverage**

Run:

```powershell
rg -n "[T]BD|[T]ODO|[F]IXME|[P]LACEHOLDER|\?\?|待[定]|以后再[说]|后面[补]" docs\superpowers\plans
rg -n "fill in [d]etails|as [a]ppropriate|fix as [n]eeded|Similar to [T]ask" docs\superpowers\plans
```

Expected:

- No output.
- Exit code may be 1 for each command because no matches were found.

- [ ] **Step 3: Commit the plan set before code**

Run:

```powershell
git add docs/superpowers/plans
git commit -m "docs: add ai native implementation plans"
```

Expected:

- Commit succeeds.
- Production source files remain unchanged in the plan commit.

## Acceptance Criteria

- All implementation plans exist under `docs/superpowers/plans`.
- Each plan starts from tests, then minimal implementation, then verification.
- Each plan names concrete files and CMake targets.
- No production code starts before the plan set is committed or explicitly accepted.
