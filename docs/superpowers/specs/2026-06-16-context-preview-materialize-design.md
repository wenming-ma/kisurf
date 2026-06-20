# Context Preview Materialize Design

Date: 2026-06-16

## Purpose

This spec defines how KiSurf observes the active KiCad workspace, shows AI proposals as non-persistent previews, and materializes accepted proposals through native commit/undo mechanisms.

This is the core of the AI-native product loop:

1. Observe the current design context.
2. Generate a suggestion.
3. Show a reviewable preview.
4. Accept or reject.
5. Materialize through native editor commits.

## Goals

- Build context from native editor state, not stale file exports.
- Track model changes using existing PCB and schematic listener seams.
- Keep previews separate from persistent board/schematic data.
- Apply accepted edits only through `BOARD_COMMIT` or `SCH_COMMIT`.
- Detect stale suggestions before preview or materialization.

## Non-Goals

- No full board placement solver in the first implementation.
- No automatic router integration in the first implementation.
- No direct mutation of KiCad objects from model output.
- No external IPC dependency for the native context loop.

## Context Index

### Responsibilities

`AI_CONTEXT_INDEX` tracks a summarized, versioned view of editor state.

Inputs:

- PCB model changes from `BOARD_LISTENER`.
- Schematic model changes from `SCHEMATIC_LISTENER`.
- Selection state from selection tools where available.
- Active document key and sheet path from editor frame/context.
- View state from canvas/view adapters where available.
- Property changes from `PROPERTY_MANAGER` in later tasks.

Outputs:

- `AI_CONTEXT_VERSION`.
- Context summary for provider prompts.
- Object reference lookup for preview and edit modules.
- Delta summaries for trace and invalidation.

### Revision Rules

- Board listener changes increment `boardRevision`.
- Schematic listener changes increment `schematicRevision`.
- Connectivity/ratsnest updates increment `connectivityRevision` when surfaced.
- View or active document changes increment `viewRevision`.
- Any change that affects an active suggestion marks that suggestion stale.

## Preview Session

### Responsibilities

`AI_PREVIEW_SESSION` owns visual proposals that have not been accepted.

Preview rules:

- Preview items live in KiCad view preview/overlay structures.
- Preview items do not enter board or schematic containers.
- Preview items do not enter undo/redo.
- Preview items are cleared when rejected, cancelled, or stale.
- Preview items are rebuilt when a suggestion is explicitly refreshed.

Existing source anchors:

- `include/view/view.h`
- `common/view/view.cpp`
- `include/preview_items/simple_overlay_item.h`
- `pcbnew/router/router_preview_item.h`

### First Preview Types

The first implementation should support safe, minimal preview types:

- Text/status preview in the Agent pane.
- Optional simple canvas overlay marker for a target object or bounding box.

The first implementation does not need full routed trace preview. It should still design the preview plan structure so routing preview can be added later.

## Edit Session

### Responsibilities

`AI_EDIT_SESSION` materializes accepted suggestions.

Rules:

- Resolve `AI_OBJECT_REF` to native items only inside editor-specific adapters.
- Check context version before applying.
- Stage changes through `BOARD_COMMIT` or `SCH_COMMIT`.
- Push exactly one user-visible undo step per accepted suggestion unless a future spec allows grouped multi-step edits.
- Run post-apply validation when configured.
- Clear preview after successful apply or failed apply.

Existing source anchors:

- `pcbnew/board_commit.cpp`
- `eeschema/sch_commit.cpp`
- `common/api/api_handler_editor.cpp`
- `include/tool/tool_manager.h`

### First Edit Types

The first accepted edit should be deliberately small:

- Chat-only suggestion: no edit.
- Object annotation/status suggestion: no document edit.
- Later first document edit: move a selected PCB item by a deterministic offset in a test board, guarded by context version and undo.

The first document edit must be added only after the foundation, Agent pane, and context index are testable.

## Staleness Policy

Preview and materialize requests must compare their stored context version with the latest context version.

Outcomes:

- Same version: proceed.
- Different version, no affected object overlap: allow preview refresh only after adapter says safe.
- Different version, affected object overlap: reject with stale context error.
- Missing version: reject.

## Error Handling

Errors:

- Context unavailable.
- Object reference cannot be resolved.
- Object type does not match expected adapter.
- Active tool is busy.
- Preview cannot be built.
- Commit fails.
- Validation blocks apply.

All errors must clear any preview owned by the failed suggestion unless the preview remains valid and user-visible as a rejected card.

## Testing Requirements

The implementation plan must use test-first development for:

- Context revision increments when fake board/schematic listener events arrive.
- Context version comparison detects stale suggestions.
- Preview session clear removes owned preview entries from a fake view adapter.
- Edit session refuses to apply with stale context.
- Edit session adapter calls fake commit stage/push exactly once for an accepted edit.

Initial tests can use fake adapters and do not need to instantiate full PCB or schematic frames.

## Acceptance Criteria

- Runtime can request a context summary from active editor adapters.
- A suggestion can carry a context version.
- A stale suggestion cannot be materialized.
- Preview ownership is separate from document ownership.
- Accepted document edits, when implemented, pass through native commit adapters.
