# AI Route To Active Anchor Design

## Purpose

The routing Agent should be able to choose a semantic anchor as the next route landing point without also naming the current route start. KiSurf already generates dynamic routing anchors such as `tool.routing.start`, `tool.routing.orthogonal.horizontal`, and `tool.routing.fortyfive.horizontal`. The missing piece is making the model-facing preview tool match that semantic workflow: the model points at the target anchor, and KiSurf supplies the active route start from tool-state context.

## Scope

This slice updates the existing `kisurf_preview_route_to_anchor` tool. It does not add a new tool name.

- `target_anchor_id` remains required.
- `start_anchor_id` becomes optional.
- When omitted, `start_anchor_id` defaults to `tool.routing.start`.
- The default is only useful when that anchor exists in the current context and has a board position.
- Explicit `start_anchor_id` keeps the current behavior unchanged.
- Net, layer, and width inference keeps the current order: explicit arguments first, then anchor details, then active routing tool-state details.
- The generated suggestion remains a non-mutating route segment preview. Board edits still require user acceptance.

## Model-Facing Behavior

The provider schema should advertise a smaller natural call shape:

```json
{
  "target_anchor_id": "tool.routing.orthogonal.horizontal"
}
```

The optional explicit form remains valid:

```json
{
  "start_anchor_id": "pcb.track.start",
  "target_anchor_id": "pcb.pad.target",
  "net": "/GPIO",
  "layer": "F.Cu",
  "width": 150000
}
```

This makes `kisurf_preview_route_to_anchor` act like the requested semantic `go_to` operation while preserving the existing preview and accept pipeline.

## Error Handling

- Missing or non-string `target_anchor_id` returns `malformed_arguments`.
- Missing implicit `tool.routing.start` returns the existing `missing_anchor` error.
- Start or target anchors without positions return `anchor_without_position`.
- Missing route parameters after all inference returns `missing_route_parameters`.
- The tool remains PCB-editor only.

## Tests

Add focused common tests:

- Provider schema requires `target_anchor_id` but not `start_anchor_id`.
- Semantic handler accepts a tool call containing only `target_anchor_id`, uses `tool.routing.start`, and creates the same route segment preview operation.
- Existing explicit-start route preview tests continue to pass.

## Self-Review

- Placeholder scan: no TBD or TODO markers.
- Consistency check: the spec matches existing anchor IDs and route preview operation names.
- Scope check: this is one behavior change in the route preview tool, not a routing solver or renderer rewrite.
- Ambiguity check: default start anchor ID is explicitly `tool.routing.start`, and failure semantics reuse existing denial codes.
