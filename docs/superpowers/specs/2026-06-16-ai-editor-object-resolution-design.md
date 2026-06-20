# AI Editor Object Resolution Design

Date: 2026-06-16

## Purpose

Earlier phases let the AI layer observe editor context as stable
`AI_OBJECT_REF` values. Those references are useful for prompts and suggestion
records, but they are not enough for real editor collaboration. A canvas preview
adapter must highlight native view items, and a commit-backed edit adapter must
operate on native board or schematic objects.

This phase adds the read-only resolver layer that maps an AI object reference
back to the editor-owned object it was derived from. It is deliberately small:
it creates no overlays, writes no document state, and does not accept AI edits.

## Goals

- Resolve PCB `AI_OBJECT_REF` values back to native `BOARD_ITEM` objects.
- Resolve schematic `AI_OBJECT_REF` values back to native `SCH_ITEM` objects.
- Match by both UUID and KiCad item type to prevent stale or cross-type
  references from binding to the wrong object.
- Preserve the current context adapter scope:
  - PCB context currently emits pads from board footprints.
  - Schematic context currently emits screen items.
- Return `nullptr` for invalid, missing, or type-mismatched references.
- Provide batch resolution helpers that skip unresolved references while
  preserving the order of resolved objects.
- Keep the resolver free of mutation, UI, provider, preview, and commit
  dependencies.

## Non-Goals

- No GAL canvas drawing or transient highlight groups in this phase.
- No `BOARD_COMMIT` or `SCH_COMMIT` materialization in this phase.
- No placement, routing, footprint generation, or net editing.
- No broad object indexing cache is required before there is a measured
  performance need.
- No IPC surface is added in this phase.
- No model-generated geometry or editor action execution is added in this
  phase.

## Architecture

The resolver classes live beside the editor-specific context adapters:

- `KISURF_AI_PCB_OBJECT_RESOLVER`
  - Owns no objects.
  - Holds a reference to a `BOARD`.
  - Resolves currently emitted PCB references by walking footprints and pads.
  - Returns `BOARD_ITEM*` so later adapters can use the normal PCB item APIs.
- `KISURF_AI_SCH_OBJECT_RESOLVER`
  - Owns no objects.
  - Holds a reference to an `SCH_SCREEN`.
  - Resolves currently emitted schematic references by walking screen items.
  - Returns `SCH_ITEM*` so later adapters can use the normal schematic APIs.

This keeps object lookup in the native editor layer instead of the common AI
module. The common AI module remains editor-agnostic and continues to exchange
only serializable records.

## Resolution Contract

For a single reference, `Resolve(...)` must:

- Reject invalid references.
- Reject references whose type is not in the resolver's supported editor scope.
- Locate an object with the same `m_Uuid`.
- Verify that the located object's `Type()` equals `m_Type`.
- Return the native object pointer when all checks pass.
- Return `nullptr` when any check fails.

For a vector of references, `ResolveAll(...)` must:

- Resolve each input independently.
- Append only non-null results.
- Preserve the input order among resolved objects.
- Never mutate the board, screen, selection, or view.

## PCB Scope

The first PCB resolver scope is pads because `KISURF_AI_PCB_CONTEXT_ADAPTER`
currently exposes pads as visible and selected objects. The resolver walks:

1. `BOARD::Footprints()`
2. each footprint's `Pads()`

It returns the matching `PAD*` as a `BOARD_ITEM*` only when the reference type is
`PCB_PAD_T` and the UUID matches the pad UUID.

Future phases may extend this resolver to tracks, vias, zones, drawings,
footprints, and groups after those object types are included in AI context
snapshots.

## Schematic Scope

The first schematic resolver scope is all top-level screen items because
`KISURF_AI_SCH_CONTEXT_ADAPTER` currently emits `SCH_SCREEN::Items()`.

The resolver walks `SCH_SCREEN::Items()` and returns the matching `SCH_ITEM*`
only when the UUID and type both match.

Future phases may extend this through schematic sheet paths, hierarchical
context, symbol fields, pins, and connectivity graph views once those references
are represented explicitly in AI context.

## Safety Model

Object resolution is read-only. The returned pointer is only valid while the
owning board or screen object remains alive and unchanged by normal editor
lifetime rules. Callers that intend to mutate must still go through a later
preview adapter or commit-backed edit adapter.

The resolver must not:

- Store resolved object pointers.
- Keep a hidden cache across document mutations.
- Change selection state.
- Trigger redraws.
- Start undo commands.
- Execute model-provided action arguments.

## Relationship To IPC

An IPC implementation can expose object references and explicit lookup calls,
but it cannot safely hand a plugin raw in-process object pointers. This native
resolver is the seam that makes the AI-native path more direct for real-time
preview and commit integration. IPC can still project the same reference
identity outward later, while native code remains the source of truth for object
binding.

## Testing

Unit tests must cover:

- PCB resolver returns a pad for a reference emitted by the PCB context adapter.
- PCB resolver returns `nullptr` for an unknown UUID.
- PCB resolver returns `nullptr` for a type mismatch.
- PCB batch resolution skips missing references and preserves resolved order.
- Schematic resolver returns a symbol for a reference emitted by the schematic
  context adapter.
- Schematic resolver returns `nullptr` for an unknown UUID.
- Schematic resolver returns `nullptr` for a type mismatch.
- Schematic batch resolution skips missing references and preserves resolved
  order.

## Acceptance Criteria

- `qa_pcbnew` targeted tests pass for `AiPcbObjectResolver`.
- `qa_eeschema` targeted tests pass for `AiSchObjectResolver`.
- Existing context adapter tests still pass.
- No editor mutation, preview overlay, or commit behavior is introduced.
- Resolver classes are compiled into their editor modules next to the existing
  KiSurf AI context adapters.

## Spec Self-Review

- Placeholder scan: no placeholder text remains.
- Scope check: this phase adds only read-only native object lookup.
- Ambiguity check: real preview drawing and accepted edits are explicitly
  deferred.
- Consistency check: resolver inputs use existing `AI_OBJECT_REF` values and do
  not introduce a parallel object identity model.
