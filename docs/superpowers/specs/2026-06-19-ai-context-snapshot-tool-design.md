# AI Context Snapshot Tool Design

Date: 2026-06-19

## Goal

Add one model-callable, read-only context interface that lets the KiSurf Agent ask for the current editor context on demand.  The tool should expose the same unified context surface that engineers expect the Agent to perceive: semantic objects, available actions, recent activity, tool state, context anchors, panel state, and visual frame metadata.

This is the first model-facing read interface above the common context model.  It does not replace the prompt-time summary; it gives the model a bounded way to refresh or narrow context during a tool-call turn.

## Problem

KiSurf now builds a unified `AI_CONTEXT_SNAPSHOT` containing:

- visible and selected object references
- action descriptors
- recent activity records
- active tool state
- visual frame metadata
- semantic anchors
- panel state records

The provider currently sends a prompt summary and structured JSON up front, but the model cannot actively request a fresh bounded context packet through the same tool-call path it uses for editor actions and previews.  That makes later multi-step Agent behavior less composable: the Agent can propose edits, but it cannot first ask the runtime for just the sections it needs.

## Requirements

1. Provide a new semantic tool named `kisurf_get_context_snapshot`.
2. The tool must be read-only:
   - `allowed=true`
   - `executed=false`
   - no suggestion creation
   - no editor mutation
3. The tool must work even when no suggestion sink is installed.
4. The tool response must be valid JSON and include:
   - `tool`
   - `allowed`
   - `executed`
   - `status="context_ready"`
   - `context`
5. The returned `context` must be derived from `AI_PROVIDER_REQUEST::m_ContextSnapshot`.
6. The caller must be able to omit sections:
   - `include_visible_objects`
   - `include_selected_objects`
   - `include_actions`
   - `include_recent_activity`
   - `include_tool_state`
   - `include_anchors`
   - `include_panels`
   - `include_visual`
7. The caller must be able to bound list sizes:
   - `max_objects`
   - `max_actions`
   - `max_activity`
   - `max_anchors`
   - `max_panels`
8. Missing include flags default to `true`.
9. Missing max values default to the existing `AI_CONTEXT_SNAPSHOT::AsJsonText()` limits.
10. Max values must be clamped to safe upper bounds so a model cannot request an unbounded tool result.
11. Unknown properties are rejected by the provider schema.

## Interface

Tool name:

```text
kisurf_get_context_snapshot
```

Parameters:

```json
{
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
}
```

All properties are optional.

Response:

```json
{
  "tool": "kisurf_get_context_snapshot",
  "allowed": true,
  "executed": false,
  "status": "context_ready",
  "context": {
    "editor": "pcb",
    "version": {
      "document": 3,
      "selection": 2,
      "view": 7,
      "text": "doc=3;sel=2;view=7"
    },
    "summary": "...",
    "visible_object_count": 120,
    "visible_objects": [],
    "selected_object_count": 1,
    "selected_objects": [],
    "action_count": 20,
    "actions": [],
    "recent_activity_count": 8,
    "recent_activity": [],
    "tool_state": {},
    "anchor_count": 12,
    "anchors": [],
    "panel_state_count": 1,
    "panel_states": [],
    "visual": {
      "source": "pcbnew.canvas",
      "mime_type": "image/png",
      "width_px": 1280,
      "height_px": 720,
      "byte_size": 0,
      "has_pixels": false
    }
  }
}
```

When a section is omitted, its corresponding payload field is removed. Count fields remain so the model can tell whether content exists beyond the returned slice.

## Limits

Default limits:

- `max_objects=64`
- `max_actions=128`
- `max_activity=64`
- `max_anchors=64`
- `max_panels=16`

Hard caps:

- `max_objects<=128`
- `max_actions<=256`
- `max_activity<=128`
- `max_anchors<=128`
- `max_panels<=32`

Negative, non-integer, or non-boolean values produce `malformed_arguments`.

## Non-goals

This slice does not add:

- pixel payload transport
- viewport crop requests
- image-tile retrieval
- live subscription or streaming
- editor mutation
- automatic preview generation

Those belong in later tools layered on the same context spine.

## Self-check

- This design uses the common `AI_CONTEXT_SNAPSHOT` instead of duplicating PCB-specific data plumbing.
- The tool is model-facing and uses the existing tool-call dispatcher path.
- The interface is safe for the model to call repeatedly because it is read-only and bounded.
- The design keeps visual metadata in scope while leaving large visual pixel payloads for a dedicated follow-up interface.
- The design supports the long-term AI-native direction while remaining compatible with the IPC-like tool model.
