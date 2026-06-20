# AI Native Preview Adapter Design

Date: 2026-06-16

## Purpose

Phase 9 can resolve AI object references back to native PCB and schematic
objects. Phase 7 and Phase 8 can move suggestion records into a previewing
state, but their preview adapter in tests is still fake. The next layer must
connect those two pieces to KiCad's native preview mechanism.

This phase adds editor-specific `AI_PREVIEW_ADAPTER` implementations that map
AI object references to native objects and show them through `KIGFX::VIEW`
preview groups. The preview remains non-persistent and must not select, modify,
or commit any editor object.

## Goals

- Add a PCB preview adapter that resolves `AI_OBJECT_REF` values to
  `BOARD_ITEM*` and adds resolved items to the `KIGFX::VIEW` preview group.
- Add a schematic preview adapter that resolves `AI_OBJECT_REF` values to
  `SCH_ITEM*` and adds resolved items to the `KIGFX::VIEW` preview group.
- Use existing `VIEW::AddToPreview(...)` and `VIEW::ClearPreview()` lifecycle
  APIs instead of creating a parallel preview manager.
- Pass `aTakeOwnership = false` when adding editor-owned objects to the preview
  group.
- Track the current preview id so stale `ShowObject(...)` calls cannot append
  to a cleared or superseded preview.
- Expose lightweight read-only counters for tests and diagnostics.
- Keep preview adapters independent from suggestion generation, model provider
  code, and accepted edit materialization.

## Non-Goals

- No accepted edit, `BOARD_COMMIT`, or `SCH_COMMIT` integration in this phase.
- No model-generated placement, routing, or geometry synthesis.
- No object cloning or persistent document changes.
- No selection-state mutation.
- No custom painter, color theme, or overlay item design.
- No UI button wiring for accepting a preview.
- No change to `AI_PREVIEW_ADAPTER::ShowObject(...)` return type in this phase.

## Architecture

The adapters live beside the editor-specific object resolvers:

- `KISURF_AI_PCB_PREVIEW_ADAPTER`
  - Implements `AI_PREVIEW_ADAPTER`.
  - Holds references to `KISURF_AI_PCB_OBJECT_RESOLVER` and `KIGFX::VIEW`.
  - On `BeginPreview(...)`, clears the view preview group and records the active
    preview id.
  - On `ShowObject(...)`, resolves the object and adds it to the view preview
    group without taking ownership.
  - On `ClearPreview(...)`, clears the active preview and resets diagnostic
    state.
- `KISURF_AI_SCH_PREVIEW_ADAPTER`
  - Implements the same contract with `KISURF_AI_SCH_OBJECT_RESOLVER`.

The common AI layer continues to know only about `AI_PREVIEW_SESSION` and
`AI_OBJECT_REF`. The editor layer owns the native lookup and view operations.

## Preview Lifecycle

The adapters must follow this lifecycle:

1. `BeginPreview(id)`
   - Clear any existing preview group content.
   - Store `id` as the active preview id.
   - Reset diagnostic resolved-object counters.
2. `ShowObject(id, ref)`
   - Ignore the call if `id` is not the active preview id.
   - Resolve `ref`.
   - Ignore unresolved references.
   - Add resolved editor-owned objects to the view preview group using
     `AddToPreview(object, false)`.
   - Record the resolved object pointer in an adapter-owned diagnostics vector.
3. `ClearPreview(id)`
   - Ignore the call if `id` is not active.
   - Clear the view preview group.
   - Reset the active preview id and diagnostics.

`AI_PREVIEW_SESSION::Show(...)` already starts a preview automatically when one
is not active, so the adapter does not need to synthesize preview ids.

## Safety Model

The adapter must not own editor objects. It must not delete, clone, move, select,
or otherwise mutate resolved items. It only asks the view to draw editor-owned
items in the preview group. `VIEW::ClearPreview()` owns the transient preview
group lifecycle.

Because `AI_PREVIEW_ADAPTER::ShowObject(...)` currently returns `void`,
unresolved references are treated as skipped preview objects. Strict failure
propagation can be added later by revisiting the common preview adapter
interface.

## Relationship To Existing KiCad Preview APIs

KiCad tools already use `KIGFX::VIEW::AddToPreview(...)` for transient objects,
for example schematic drawing tools and PCB drawing tools. This phase reuses
that mechanism rather than inventing a separate AI overlay system.

The use of `aTakeOwnership = false` is important: AI preview adapters show
objects already owned by the board or schematic screen. The view preview group
must not delete those objects when the preview is cleared.

## Testing

Unit tests must cover:

- PCB preview begins with an active preview id and clears old diagnostics.
- PCB preview resolves a context-emitted pad reference and records one previewed
  native object.
- PCB preview skips unknown references.
- PCB preview ignores stale preview ids.
- PCB preview clear resets active id and diagnostics.
- Schematic preview resolves a context-emitted symbol reference and records one
  previewed native object.
- Schematic preview skips unknown references.
- Schematic preview ignores stale preview ids.
- Schematic preview clear resets active id and diagnostics.

Tests should use a headless `KIGFX::VIEW`; no GUI canvas is required for this
phase.

## Acceptance Criteria

- `qa_pcbnew` targeted tests pass for:
  - `AiPcbPreviewAdapter`
  - `AiPcbObjectResolver`
  - `AiPcbContextAdapter`
- `qa_eeschema` targeted tests pass for:
  - `AiSchPreviewAdapter`
  - `AiSchObjectResolver`
  - `AiSchContextAdapter`
- No editor object ownership transfer is introduced.
- No selection or document mutation occurs.
- Existing preview session tests in `qa_common` still pass.

## Spec Self-Review

- Placeholder scan: no placeholder text remains.
- Scope check: this phase only connects resolved editor objects to native view
  preview groups.
- Ambiguity check: accepted edits and UI accept buttons are explicitly deferred.
- Consistency check: the design reuses existing `AI_PREVIEW_SESSION`,
  `AI_PREVIEW_ADAPTER`, resolver, and `KIGFX::VIEW` APIs.
