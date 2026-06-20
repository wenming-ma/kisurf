# AI Move Preview Adapter Design

Date: 2026-06-16

## Purpose

AI suggestions can now carry inert `arguments` JSON, and PCB/schematic accept
handlers already recognize `{ "operation": "move", "dx": ..., "dy": ... }` for
bounded move edits. The preview handlers, however, still only highlight the
original objects. That means the user cannot inspect the actual proposed move
before accepting it.

This spec adds a narrow move-preview path to the existing PCB and schematic
preview adapters. When a move delta is supplied, preview shows moved clones
owned by the view preview group, while accept continues to move the original
objects through the existing edit adapters and commit flow.

## Source Anchors

- `pcbnew/kisurf_ai_pcb_preview_adapter.*` previews resolved PCB objects.
- `eeschema/kisurf_ai_sch_preview_adapter.*` previews resolved schematic
  objects.
- `pcbnew/pcb_edit_frame.cpp` and `eeschema/sch_edit_frame.cpp` parse move
  arguments before accepting suggestions.
- `pcbnew/kisurf_ai_pcb_move_edit_adapter.*` and
  `eeschema/kisurf_ai_sch_move_edit_adapter.*` apply accepted moves.
- `qa/tests/pcbnew/test_ai_pcb_preview_adapter.cpp` and
  `qa/tests/eeschema/test_ai_sch_preview_adapter.cpp` cover preview adapters.

## Goals

- Let PCB and schematic preview adapters accept an optional move delta.
- Wire PCB and schematic suggestion preview handlers to pass parsed move
  arguments into the preview adapters.
- When no move delta is supplied, preserve current highlight-preview behavior.
- When a move delta is supplied, clone the resolved object, move the clone, and
  add the clone to the view preview with ownership.
- Keep original board/schematic objects unchanged during preview.
- Keep stale preview id and unknown-object behavior unchanged.
- Keep accept behavior delegated to the existing move edit adapters.

## Non-Goals

- No new edit operation beyond move preview.
- No route, place, delete, or geometry synthesis preview.
- No change to the model JSON contract in this slice.
- No shared argument parser extraction in this slice.

## Design

The PCB and schematic preview adapters gain an optional `VECTOR2I` move delta.

For each shown object:

1. Resolve the object from the local board/schematic context.
2. If no move delta exists, add the resolved original object to preview without
   transferring ownership, matching current behavior.
3. If a move delta exists, clone the resolved object, move the clone by the
   delta, add the clone to preview with ownership, and track the clone pointer in
   `PreviewedItems()`.
4. Leave the original object position untouched.

`VIEW::ClearPreview()` already deletes owned preview items, so cloned preview
objects should be passed with `aTakeOwnership = true`.

PCB and schematic frame preview handlers should parse the same move arguments
already used by accept handlers and pass the resulting optional delta to the
preview adapter.

## Safety

- Preview clones are owned by the view preview lifecycle and are cleared through
  existing `ClearPreview()` behavior.
- Original objects are not modified until the user accepts the suggestion.
- Unknown object references remain skipped.
- Stale preview ids remain ignored.

## Testing Requirements

Unit tests must verify for both PCB and schematic adapters:

- Default preview still tracks the original object pointer.
- Move preview tracks a clone, not the original object.
- Move preview leaves the original object position unchanged.
- Move preview clone position equals original position plus delta.
- Existing unknown reference, stale id, and clear-preview tests continue to
  pass.

## Acceptance Criteria

- A model suggestion with move arguments can display a moved preview without
  applying the edit.
- Accept still uses the existing edit session and commit adapters.
- Targeted PCB/SCH preview and move-edit tests pass.

## Spec Self-Review

- Open-marker scan: no unresolved open markers remain.
- Scope check: preview-only clone movement; no accepted edit behavior changes.
- Safety check: original design objects remain unchanged during preview.
