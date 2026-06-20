# AI Footprint Placement Anchor Design

## Purpose

When the PCB editor is in active footprint placement, KiSurf should expose
deterministic semantic placement anchors in the shared AI context. The chat Agent
and the background Agent can then reason about named candidate points instead of
guessing board pixels or inventing coordinates.

This is a lower-layer context feature, not a placement optimizer. It gives the
model stable, inspectable anchor choices that subsequent preview and accept
workflows can use.

## Context

The existing AI context anchor provider already generates dynamic routing anchors
from `AI_TOOL_STATE_KIND::RoutingTrack`. Those anchors are inserted into
`AI_CONTEXT_SNAPSHOT::m_Anchors` with ids under `tool.routing.*`, and they are
available to model-facing tools such as context snapshot reads, visual frame
overlays, anchor focus previews, and route-to-anchor previews.

The PCB tool-state provider already classifies footprint placement as
`AI_TOOL_STATE_KIND::PlacingFootprint` and emits a mode context similar to:

```json
{
  "mode": "placing_footprint",
  "cursor": { "x": 1000, "y": 2000 }
}
```

This design extends the same anchor-provider boundary for active footprint
placement.

## Requirements

1. `AppendAiToolStateAnchors()` must remove stale generated anchors whose ids
   start with `tool.placement.` on every call.
2. For PCB snapshots whose tool state is `PlacingFootprint`, the provider must
   generate placement anchors only when it can determine a cursor board position.
3. The cursor position may come from `tool_state.mode_context.cursor`. If the
   mode context has no cursor, the provider may fall back to
   `AI_TOOL_STATE_SNAPSHOT::m_CursorBoardPosition` when
   `m_HasCursorBoardPosition` is true.
4. The provider must generate four deterministic anchors around the cursor:
   `tool.placement.cursor`, `tool.placement.grid.east`,
   `tool.placement.grid.south`, and `tool.placement.grid.diagonal`.
5. The non-cursor candidate positions must be offset by a positive placement
   pitch. The pitch is read from the first positive integer among
   `pitch`, `grid_pitch`, and `placement_pitch` in the mode context. If no
   positive pitch is present, the fallback pitch is `1000000` internal units.
6. Generated placement anchors must use
   `AI_CONTEXT_ANCHOR_KIND::PlacementCandidate`, `AI_EDITOR_KIND::Pcb`, stable
   labels, positions, confidence values, and JSON details.
7. Details JSON must include at least `source`, `mode`, `role`, `cursor`,
   `position`, and `pitch`.
8. Existing user/static anchors and routing anchors must not be removed by
   placement generation.

## Anchor Semantics

The initial anchor set is intentionally simple:

| Anchor id | Role | Position |
| --- | --- | --- |
| `tool.placement.cursor` | `placement_cursor` | Cursor position |
| `tool.placement.grid.east` | `grid_east` | Cursor plus `(pitch, 0)` |
| `tool.placement.grid.south` | `grid_south` | Cursor plus `(0, pitch)` |
| `tool.placement.grid.diagonal` | `grid_diagonal` | Cursor plus `(pitch, pitch)` |

The four anchors are not a claim that these are final legal placements. They are
semantic starting points for preview work: place here, continue grid to the
right, continue grid downward, or continue on the next row/column intersection.

## Non-Goals

- No footprint collision checking.
- No board-edge, courtyard, ratsnest, or thermal optimization.
- No visual renderer changes in this slice.
- No new modifying placement tool call.
- No Anthropic-compatible runtime implementation.

## Testing

Add common tests for:

1. Placement anchors generated from a `placing_footprint` mode-context cursor and
   explicit pitch.
2. Placement anchors generated from the snapshot cursor when the mode context
   omits cursor.
3. Default fallback pitch used when the mode context has no positive pitch.
4. Stale `tool.placement.*` anchors removed when active footprint placement has
   no cursor.
5. Existing routing and inactive-state behavior remains unchanged.

Build and verification must include command-line evidence and a Windows desktop
inspection pass for unexpected modal error dialogs after compile or executable
runs.

## Self-Review

- Placeholder scan: no unfinished marker or deferred requirement remains.
- Consistency check: ids, roles, and JSON fields match the planned provider and
  tests.
- Scope check: this is one focused context-layer feature and does not include a
  placement solver.
- Ambiguity check: cursor fallback, pitch fallback, and stale-anchor removal are
  explicit.
