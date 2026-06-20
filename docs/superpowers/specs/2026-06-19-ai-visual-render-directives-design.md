# AI Visual Render Directives Design

Date: 2026-06-19

## Goal

Extend the AI visual-frame read surface with a bounded, declarative render-directive contract.  The model can ask the visual surface to focus a layer, focus a net, dim unrelated layers, and highlight specific semantic anchors using the same anchor IDs returned by the context and visual-overlay surfaces.

## Problem

`kisurf_get_visual_frame` now returns frame metadata, optional pixels, and semantic anchor overlays.  That gives the model a shared language for visible points, but it still cannot state which visual emphasis it wants when reading the frame.  Routing and placement workflows need this because the Agent often needs to ask for "the current frame focused on F.Cu and /GPIO, with these candidate anchors highlighted" before deciding which preview action to take.

The common layer should not pretend to draw pixels before editor canvas rendering is wired in.  It should expose a strict, testable directive object that downstream canvas renderers, annotated PNG generation, and UI preview overlays can consume later.

## Requirements

1. `kisurf_get_visual_frame` must accept visual focus directive parameters.
2. `kisurf_get_workspace_view.visual` must accept the same directive parameters.
3. Supported parameters:
   - `focus_layer`: optional non-empty string.
   - `focus_net`: optional non-empty string.
   - `dim_unfocused_layers`: optional boolean, default `false`.
   - `highlight_anchor_ids`: optional array of non-empty strings, maximum `32`.
4. If no directive parameter is supplied, the visual response must not include `render_directives`.
5. If any directive parameter is supplied, the visual response must include `render_directives`.
6. `render_directives` must contain only the supplied focus fields, `dim_unfocused_layers` when `true`, and the requested `highlight_anchor_ids` when present.
7. `highlight_anchor_ids` must be validated against positional anchors in `AI_CONTEXT_SNAPSHOT::m_Anchors`.
8. Unknown anchor IDs, anchors without positions, invalid string fields, non-array highlight fields, non-string highlight entries, empty highlight entries, and more than `32` highlights must fail closed with `malformed_arguments`.
9. Pixel behavior remains unchanged: pixels are opt-in with `include_pixels=true` and bounded by `max_bytes`.
10. Anchor overlay behavior remains unchanged: overlays default to on and are bounded by `max_anchor_overlays`.
11. The provider schema must advertise the new directive parameters for the standalone visual tool and the nested workspace visual object.

## Interface

Tool parameters shared by `kisurf_get_visual_frame` and `workspace_view.visual`:

```json
{
  "include_pixels": false,
  "max_bytes": 262144,
  "include_anchor_overlays": true,
  "max_anchor_overlays": 64,
  "focus_layer": "F.Cu",
  "focus_net": "/GPIO",
  "dim_unfocused_layers": true,
  "highlight_anchor_ids": [
    "tool.routing.start",
    "pcb.pad.target"
  ]
}
```

Response:

```json
{
  "tool": "kisurf_get_visual_frame",
  "allowed": true,
  "executed": false,
  "status": "visual_ready",
  "visual": {
    "source": "pcbnew.canvas",
    "mime_type": "image/png",
    "width_px": 1280,
    "height_px": 720,
    "byte_size": 2048,
    "has_pixels": true,
    "anchor_overlay_count": 2,
    "anchor_overlays": [
      {
        "id": "tool.routing.start",
        "kind": "route_start",
        "label": "Route start",
        "summary": "Current route starts here.",
        "position": {
          "x": 100,
          "y": 200
        },
        "layer": 0,
        "confidence": 1.0
      }
    ],
    "render_directives": {
      "focus_layer": "F.Cu",
      "focus_net": "/GPIO",
      "dim_unfocused_layers": true,
      "highlight_anchor_ids": [
        "tool.routing.start",
        "pcb.pad.target"
      ]
    }
  }
}
```

## Design Choices

### Declarative first

This slice does not draw highlights into pixels.  It records the visual emphasis requested by the model in a renderer-neutral object.  That object can later drive canvas overlay rendering, annotated PNG generation, or UI preview states.

### Positional anchor validation

Highlight directives are only meaningful for anchors that can map onto the current frame.  The tool validates requested IDs against positional anchors, not only against the truncated overlay array, so the model can request any currently positional anchor even if `max_anchor_overlays` is low.

### Omit empty directives

The common visual response stays compact.  `render_directives` appears only when a caller asks for focus or highlighting.

## Non-goals

This slice does not add:

- canvas-drawn highlights
- annotated PNG output
- automatic layer/net inference
- new anchor generation algorithms
- UI preview acceptance controls

Those build on this contract.

## Self-review

- No placeholder sections remain.
- The interface extends the existing visual options without changing defaults.
- The spec explicitly separates requested render emphasis from actual pixel rendering.
- Highlight IDs are bounded and validated against positional anchors.
- The same contract is required for standalone visual reads and nested workspace visual reads.
