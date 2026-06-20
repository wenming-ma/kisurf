# AI Placement Visual Defaults Design

## Purpose

When the PCB editor is actively placing a footprint, model-facing visual reads
should automatically highlight semantic placement candidate anchors. The model
should see the same placement choices that the context layer exposes without
having to restate `highlight_anchor_ids` on every `kisurf_get_visual_frame` or
`kisurf_get_workspace_view` call.

This keeps the layout state aligned across text context, visual overlays, and
future preview tools.

## Context

The visual-frame tool already supports optional render directives:

- `focus_layer`
- `focus_net`
- `dim_unfocused_layers`
- `highlight_anchor_ids`

Routing mode already applies defaults: it reads net/layer from the routing tool
state, dims unrelated layers, and highlights routing anchors. Footprint placement
now has generated anchors under `tool.placement.*`, all with
`AI_CONTEXT_ANCHOR_KIND::PlacementCandidate`.

## Requirements

1. For PCB snapshots whose tool state is `PlacingFootprint`, visual reads must
   default `highlight_anchor_ids` to positional placement candidate anchors.
2. Placement visual defaults must apply to both `kisurf_get_visual_frame` and the
   nested visual section of `kisurf_get_workspace_view`.
3. Explicit `highlight_anchor_ids` supplied by the model must not be overwritten.
4. The default list must include only positional anchors whose kind is
   `PlacementCandidate`.
5. The default list must respect the existing 32-anchor render-directive cap.
6. Placement visual defaults must not set `focus_net`, `focus_layer`, or
   `dim_unfocused_layers`; those remain routing-specific until placement has
   stronger layer/object semantics.
7. Existing routing defaults must keep their current behavior.
8. Non-placement, non-routing states must still omit render directives by
   default.

## Testing

Add common tests for:

1. `kisurf_get_visual_frame` in footprint-placement context automatically
   returns placement candidate ids in `render_directives.highlight_anchor_ids`.
2. Explicit `highlight_anchor_ids` remain authoritative in placement context.
3. `kisurf_get_workspace_view` nested visual reads inherit placement defaults.
4. Existing routing default tests and outside-routing omission tests remain
   green.
5. After the editor targets build, `pcbnew` must be launched through Computer
   Use for a desktop smoke check that verifies no startup/system-error modal is
   blocking the app and that the AI menu or Agent entry is reachable.

## Non-Goals

- No new placement preview or accept tool.
- No canvas drawing implementation changes.
- No layer dimming for footprint placement in this slice.
- No footprint collision or legality scoring.

## Self-Review

- Placeholder scan: no unfinished marker remains.
- Consistency check: ids, enum kind, and tool names match current code.
- Scope check: this is limited to visual-frame default directives.
- Ambiguity check: explicit arguments override defaults, and only positional
  placement candidates are highlighted.
