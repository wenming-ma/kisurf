# AI Workspace View Tool Design

Date: 2026-06-19

## Goal

Add one model-callable read-only interface for the Agent to inspect the current KiSurf workspace view through parameters.  This tool is the preferred single entry point for "what can the engineer and UI currently see?" across:

- structured editor context
- visual frame metadata or bounded pixels
- recent activity timeline
- panel state through the context section

## Problem

KiSurf now has focused read tools:

- `kisurf_get_context_snapshot`
- `kisurf_get_visual_frame`
- `kisurf_get_activity_timeline`

Those are useful primitives, but the product goal asks for a single interface whose parameters can control which layer of workspace state is returned.  The Agent should not have to remember separate tools when it needs a coherent context packet for routing, layout, panel state, and visual inspection.

## Requirements

1. Provide a new semantic tool named `kisurf_get_workspace_view`.
2. The tool must be read-only:
   - `allowed=true`
   - `executed=false`
   - no suggestion creation
   - no editor mutation
3. The tool must work without a suggestion sink.
4. The response must be valid JSON with:
   - `tool`
   - `allowed`
   - `executed`
   - `status="workspace_view_ready"`
   - `workspace_view`
5. The caller selects sections with `views`, an array containing any of:
   - `context`
   - `visual`
   - `activity`
6. Missing `views` defaults to all three sections.
7. Duplicate `views` are allowed but returned once.
8. Unknown view names produce `malformed_arguments`.
9. The `context` object accepts the same include/max options as `kisurf_get_context_snapshot`.
10. The `visual` object accepts the same `include_pixels` and `max_bytes` options as `kisurf_get_visual_frame`.
11. The `activity` object accepts the same `max_activity`, `kind`, and `action_contains` options as `kisurf_get_activity_timeline`.
12. If visual pixels are requested and exceed `max_bytes`, the tool fails closed with `visual_too_large`.
13. Unknown top-level or nested properties produce `malformed_arguments`.

## Interface

Tool name:

```text
kisurf_get_workspace_view
```

Parameters:

```json
{
  "views": ["context", "visual", "activity"],
  "context": {
    "include_visible_objects": true,
    "include_selected_objects": true,
    "include_actions": true,
    "include_recent_activity": true,
    "include_tool_state": true,
    "include_anchors": true,
    "include_panels": true,
    "include_visual": true,
    "max_objects": 64,
    "max_actions": 128,
    "max_activity": 64,
    "max_anchors": 64,
    "max_panels": 16
  },
  "visual": {
    "include_pixels": false,
    "max_bytes": 262144
  },
  "activity": {
    "max_activity": 64,
    "kind": "user_action",
    "action_contains": "selected"
  }
}
```

All properties are optional.

Response:

```json
{
  "tool": "kisurf_get_workspace_view",
  "allowed": true,
  "executed": false,
  "status": "workspace_view_ready",
  "workspace_view": {
    "context": {
      "editor": "pcb",
      "version": {
        "document": 3,
        "selection": 2,
        "view": 9,
        "text": "doc=3;sel=2;view=9"
      },
      "anchors": [],
      "panel_states": []
    },
    "visual": {
      "source": "pcbnew.canvas",
      "mime_type": "image/png",
      "width_px": 1280,
      "height_px": 720,
      "byte_size": 2048,
      "has_pixels": true
    },
    "activity": {
      "activity_count": 2,
      "records": []
    }
  }
}
```

## Compatibility

Keep the focused tools.  They remain useful for smaller calls and tests.  `kisurf_get_workspace_view` is the preferred Agent entry point because it composes the same read surfaces in one bounded result.

## Non-goals

This slice does not add:

- new editor event capture
- new canvas capture mechanics
- crop/tile/downscale visual payloads
- automatic background scheduling
- edit/preview creation

Those are separate layers above this read interface.

## Self-check

- The design gives the Agent one parameterized read interface.
- It reuses the same source-of-truth context, visual, and activity semantics as existing tools.
- It includes panel state via the context section.
- It keeps pixel payload opt-in and bounded.
- It does not remove focused tools or break existing callers.
