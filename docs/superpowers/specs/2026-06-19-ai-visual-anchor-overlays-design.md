# AI Visual Anchor Overlays Design

Date: 2026-06-19

## Goal

Extend the current AI visual-frame read surface so a model can request the live editor frame together with bounded semantic anchor overlay metadata.  This links what the model sees in the captured canvas to the same anchor IDs it can use in routing and preview tools.

## Problem

KiSurf already exposes semantic anchors in `kisurf_get_context_snapshot`, and the routing preview tool can consume anchor IDs.  KiSurf also exposes the current canvas frame through `kisurf_get_visual_frame` and through the `visual` section of `kisurf_get_workspace_view`.

Those two surfaces are still separated.  A model can read the screenshot metadata or pixels, and it can read anchors, but the visual response does not tell the model which semantic anchors are intended to be interpreted as visual marks on the current frame.  For routing and placement workflows, this makes the desired AI-native loop weaker: the model should be able to look at the current frame and see the bounded set of actionable anchor overlays without making a second context call or joining separate payloads itself.

## Requirements

1. `kisurf_get_visual_frame` must include semantic anchor overlay metadata by default.
2. `kisurf_get_workspace_view` must use the same visual overlay behavior for its nested `visual` section.
3. Overlay metadata must be derived from `AI_CONTEXT_SNAPSHOT::m_Anchors`.
4. Only anchors with board/view positions are eligible for visual overlays.
5. The visual object must always include `anchor_overlay_count`, the total eligible positional-anchor count before truncation.
6. When overlays are included, the visual object must include `anchor_overlays`, a bounded array of overlay records.
7. Overlay inclusion is controlled by `include_anchor_overlays`, defaulting to `true`.
8. The returned overlay array is bounded by `max_anchor_overlays`, defaulting to `64` and capped at `128`.
9. Invalid overlay argument types must return `malformed_arguments`.
10. Pixel behavior stays unchanged: pixels remain opt-in with `include_pixels=true` and bounded by `max_bytes`.
11. The provider schema must advertise the new visual parameters for both the standalone visual tool and the nested workspace visual object.

## Interface

Tool parameters shared by `kisurf_get_visual_frame` and `workspace_view.visual`:

```json
{
  "include_pixels": false,
  "max_bytes": 262144,
  "include_anchor_overlays": true,
  "max_anchor_overlays": 64
}
```

Response without pixels:

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
    ]
  }
}
```

When `include_anchor_overlays=false`, `anchor_overlay_count` is still returned, but `anchor_overlays` is omitted.  This lets the model know anchors exist without paying for the array.

## Design Choices

### Positional anchors only

The visual response is explicitly about what can be mapped onto the current frame.  Anchors without positions remain available through the context snapshot, but they are not visual overlays.

### Metadata first, rendered overlay later

This slice returns overlay metadata rather than drawing labels into the captured PNG.  That keeps the common-layer contract testable without editor canvas drawing changes.  A later slice can render these same records into canvas highlights, annotated PNGs, or UI previews.

### Default overlays, opt-in pixels

Anchor overlays are small, semantic, and central to AI-native routing and placement.  They default to on.  Pixels remain large and sensitive, so they stay opt-in and byte-limited.

## Non-goals

This slice does not add:

- canvas-drawn anchor labels
- generated annotated PNGs
- viewport-to-screen coordinate transforms
- layer-specific visual filtering
- new anchor generation algorithms
- new preview acceptance UI

Those can build on this overlay contract.

## Self-review

- No placeholder sections remain.
- The interface is compatible with the existing visual tool defaults.
- The design keeps the common-layer implementation bounded and testable.
- The spec explicitly states truncation, default inclusion, and opt-out behavior.
- The scope does not duplicate existing context snapshot anchor payloads; it projects positional anchors into the visual frame contract.
