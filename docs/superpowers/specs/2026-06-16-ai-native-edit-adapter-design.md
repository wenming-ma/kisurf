# AI Native Edit Adapter Design

Date: 2026-06-16

## Purpose

Phase 10 can preview resolved AI object references in PCB and schematic views.
The next missing native foundation is accepted edit materialization: when a user
accepts an AI suggestion, KiSurf must change the active design through KiCad's
native commit/undo pipeline, not by marking a suggestion as accepted in memory.

This spec defines the first commit-backed editor edit adapters. The first edit
operation is intentionally small and deterministic: move resolved PCB or
schematic objects by a caller-supplied `VECTOR2I` delta. This proves the
transaction boundary, rollback behavior, and undo grouping before adding routing,
placement, or property-specific operations.

## Source Research Anchors

Local source anchors:

- `include/kisurf/ai/ai_edit_session.h` and
  `common/kisurf/ai/ai_edit_session.cpp` currently apply objects one at a time.
  They need begin/end/abort hooks so one accepted suggestion becomes one native
  edit transaction.
- `pcbnew/board_commit.cpp` stages modifications before mutation with
  `COMMIT::Modify(...)`, updates board/view/connectivity on `Push(...)`, and can
  restore staged changes on `Revert()`.
- `eeschema/sch_commit.cpp` similarly requires a `BASE_SCREEN*` when staging
  schematic items and refreshes screen/view/connectivity on `Push(...)`.
- `pcbnew/kisurf_ai_pcb_object_resolver.*` and
  `eeschema/kisurf_ai_sch_object_resolver.*` map `AI_OBJECT_REF` values back to
  native editor objects.
- `include/board_item.h` and `eeschema/sch_item.h` expose `Move( VECTOR2I )`
  on the common item base classes needed for the first materialized edit.

External practice anchors:

- Language Server Protocol code actions support a cheap candidate first and a
  later resolved edit; `CodeAction` can carry a `WorkspaceEdit`, and
  `codeAction/resolve` exists to compute edits lazily:
  https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/
- VS Code's `CodeActionProvider` similarly separates initial actions from
  `resolveCodeAction(...)`, which fills in the edit property while leaving other
  properties stable:
  https://code.visualstudio.com/api/references/vscode-api

The KiSurf design follows the same broad shape: model/context code may produce a
reviewable candidate, but the native editor resolves and commits the concrete
edit only when the user accepts it.

## Goals

- Extend `AI_EDIT_ADAPTER` with a transaction lifecycle:
  - `BeginApply(...)`
  - `ApplyObject(...)`
  - `EndApply()`
  - `AbortApply()`
- Keep existing simple adapters source-compatible by providing default
  implementations for the new lifecycle hooks.
- Make `AI_EDIT_SESSION::Apply(...)` call the lifecycle in a safe order:
  validation gate, begin, object applications, end, and abort on failure.
- Add PCB and schematic move edit adapters that:
  - resolve refs with the Phase 9 resolvers,
  - stage each resolved item with `COMMIT::Modify(...)`,
  - mutate only after staging,
  - push one native commit after all objects apply,
  - revert on any failure before end.
- Keep this phase independent of Agent pane buttons. UI accept controls can be
  added after the native materialization boundary exists.

## Non-Goals

- No autorouting, placement solver, footprint generation, or net editing.
- No model-generated JSON edit parser in this phase.
- No Agent pane Preview/Accept/Reject buttons in this phase.
- No direct object mutation without `COMMIT::Modify(...)`.
- No silent accepted status when a native edit fails.
- No IPC transport changes.

## Design Decision

Use a two-layer approach:

1. Common `AI_EDIT_SESSION` owns validation and transaction sequencing.
2. Editor-specific adapters own object resolution, native staging, mutation, and
   commit push/revert.

This avoids baking PCB/SCH concepts into the common AI layer while still giving
the common suggestion orchestrator a reliable success/failure result.

The first operation is configured on the adapter instead of stored in
`AI_SUGGESTION_RECORD`. A later phase can add a typed edit-plan data contract and
instantiate the right adapter from suggestion arguments. For now, the move
adapter proves that `AI_EDIT_SESSION` can drive real commit-backed edits safely.

## Common Edit Lifecycle

`AI_EDIT_ADAPTER` gains default hooks:

```cpp
virtual bool BeginApply( const AI_VALIDATION_SUMMARY& aValidation, size_t aObjectCount );
virtual bool ApplyObject( const AI_OBJECT_REF& aObject ) = 0;
virtual bool EndApply();
virtual void AbortApply();
```

`AI_EDIT_SESSION::Apply(...)` behavior:

1. Return `false` immediately when validation has a blocking issue.
2. Call `BeginApply(...)`; return `false` if it fails.
3. Apply each object in order.
4. On the first failed object, call `AbortApply()` exactly once and return
   `false`.
5. Call `EndApply()` after all objects apply.
6. If `EndApply()` fails, call `AbortApply()` and return `false`.
7. Store `LastValidation()` only after `EndApply()` succeeds.

An empty object list is still allowed at the common layer for compatibility, but
editor adapters may reject it in `BeginApply(...)`.

## PCB Move Edit Adapter

Create `KISURF_AI_PCB_MOVE_EDIT_ADAPTER`.

Constructor inputs:

- `KISURF_AI_PCB_OBJECT_RESOLVER&`
- `COMMIT&` so tests can use a spy commit and production can pass
  `BOARD_COMMIT`
- `VECTOR2I` move delta
- optional commit message, defaulting to `Apply AI PCB edit`

Behavior:

- `BeginApply(...)` clears diagnostics, stores object count, and rejects zero
  objects.
- `ApplyObject(...)` resolves the ref to `BOARD_ITEM*`.
- If resolution fails, return `false`.
- Stage before mutation:

```cpp
m_Commit.Modify( item );
item->Move( m_Delta );
```

- Record moved items for diagnostics.
- `EndApply()` pushes exactly one commit when at least one object moved.
- `AbortApply()` reverts the commit, clears moved-item diagnostics, and
  preserves failed-object diagnostics so tests and future UI can explain why the
  edit did not materialize.

Diagnostics for tests:

- `MovedItems() const`
- `FailedObjects() const`
- `WasReverted() const`
- `WasCommitted() const`

These diagnostics are not product UI state; they exist to keep the first native
adapter testable without a full frame.

## Schematic Move Edit Adapter

Create `KISURF_AI_SCH_MOVE_EDIT_ADAPTER`.

Constructor inputs:

- `KISURF_AI_SCH_OBJECT_RESOLVER&`
- `SCH_SCREEN&`
- `COMMIT&` so production can pass `SCH_COMMIT`
- `VECTOR2I` move delta
- optional commit message, defaulting to `Apply AI schematic edit`

Behavior is the same as the PCB adapter except staging must include the screen:

```cpp
m_Commit.Modify( item, &m_Screen );
item->Move( m_Delta );
```

This matches `SCH_COMMIT::pushSchEdit(...)`, which expects a screen on staged
schematic entries.

## Failure And Rollback Rules

- A missing object ref fails the edit.
- A type mismatch fails the edit through the resolver.
- A failed second object after a successful first object must call
  `AbortApply()` so the first object is restored by `COMMIT::Revert()`.
- `EndApply()` failure also aborts.
- The suggestion orchestrator must keep a suggestion pending/previewing when
  `AI_EDIT_SESSION::Apply(...)` returns false.
- The adapter must never call `Push(...)` after an object failure.

## Testing

Common tests:

- Existing `AI_EDIT_SESSION` tests continue to pass with default lifecycle hooks.
- Add a lifecycle test proving begin, per-object apply, and end are called in
  order.
- Add a failure test proving abort is called once and `LastValidation()` remains
  unset.
- Add an end-failure test proving abort runs and the session returns false.

PCB tests:

- Build a board fixture with a footprint and pads.
- Resolve a pad ref through `KISURF_AI_PCB_CONTEXT_ADAPTER`.
- Use a spy `COMMIT` to assert one `Modify(...)` and one `Push(...)`.
- Assert the pad position changed by the configured delta.
- Assert unknown refs call `Revert()` and do not push.
- Assert a second-object failure reverts the already moved first item.

Schematic tests:

- Build an `SCH_SCREEN` fixture with symbols.
- Resolve a symbol ref through `KISURF_AI_SCH_CONTEXT_ADAPTER`.
- Use a spy `COMMIT` to assert `Modify(...)` received the screen pointer and
  `Push(...)` ran once.
- Assert symbol position changed by the configured delta.
- Assert unknown refs and mid-edit failures revert and do not push.

## Acceptance Criteria

- `AI_EDIT_SESSION` has a transaction lifecycle and common tests pass.
- PCB and schematic move adapters compile into their editor targets.
- Targeted PCB and schematic tests prove real item mutation happens only after
  commit staging.
- Targeted tests prove failed edit application reverts staged changes and does
  not push.
- Existing suggestion orchestrator/model lifecycle tests still pass.
- No Agent UI claims that a suggestion is accepted unless the edit session
  returned success.

## Spec Self-Review

- Placeholder scan: no placeholders or deferred fill-in sections remain.
- Consistency check: the common lifecycle and editor adapters both use begin,
  apply, end, and abort semantics.
- Scope check: this is one bounded native materialization slice; richer edit-plan
  parsing and UI buttons are explicitly deferred.
- Ambiguity check: the first materialized operation is exactly move-by-delta for
  resolved PCB/SCH items through native commits.
