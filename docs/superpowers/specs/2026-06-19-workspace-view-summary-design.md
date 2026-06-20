# Workspace View Summary Design

## Purpose

`kisurf_get_workspace_view` is the closest current implementation of the
single Agent interface the AI-native architecture needs: it can return semantic
context, visual frame data, and recent activity in one tool result. The payload
already contains the raw material, but the model must inspect nested sections to
answer basic routing questions such as:

- which views were returned;
- what dynamic context is active;
- whether this is routing, placement, panel, or general work;
- how many selected objects, anchors, panels, and activity records exist;
- whether a visual frame is available.

This slice adds a compact top-level `workspace_view.summary` object. It gives
the model a stable orientation header before it reads deeper context, visual,
or activity sections.

## Requirements

1. Keep the existing tool name: `kisurf_get_workspace_view`.
2. Keep existing `workspace_view.context`, `workspace_view.visual`, and
   `workspace_view.activity` behavior unchanged.
3. Always include `workspace_view.summary`, even when the caller requests only
   one view.
4. `summary.included_views` must list the sections included in this response:
   `context`, `visual`, and/or `activity`.
5. `summary.editor` must reflect the current context editor.
6. `summary.dynamic_context_kind` and `summary.dynamic_context_source` must
   match the current context's dynamic-context projection.
7. `summary.tool_state_kind` must expose the active tool-state kind.
8. `summary.selected_object_count`, `summary.visible_object_count`,
   `summary.anchor_count`, `summary.panel_state_count`, and
   `summary.recent_activity_count` must reflect the current snapshot counts.
9. `summary.visual_source` and `summary.visual_has_pixels` must describe the
   current visual snapshot without exposing pixel data.
10. The summary must not contain `data_uri` or other raw visual payloads.
11. The change must be covered by a failing test before implementation.

## Architecture

Add a helper in `ai_semantic_tool_call_handler.cpp`:

```cpp
nlohmann::json workspaceViewSummaryJson(
        const AI_CONTEXT_SNAPSHOT& aSnapshot,
        const WORKSPACE_VIEW_TOOL_OPTIONS& aOptions );
```

The helper reads directly from the current `AI_CONTEXT_SNAPSHOT` and the parsed
workspace-view options. `workspaceViewResult()` inserts the summary before any
optional section:

```cpp
workspaceView["summary"] = workspaceViewSummaryJson(...);
```

The helper reuses existing JSON naming helpers where possible. It does not
parse the generated context JSON, so it avoids duplicating large nested payloads
or accidentally copying visual pixels.

## Payload Shape

Example:

```json
{
  "workspace_view": {
    "summary": {
      "included_views": ["context", "visual", "activity"],
      "editor": "pcb",
      "dynamic_context_kind": "routing",
      "dynamic_context_source": "tool_state",
      "tool_state_kind": "routing_track",
      "selected_object_count": 2,
      "visible_object_count": 3,
      "anchor_count": 4,
      "panel_state_count": 1,
      "recent_activity_count": 4,
      "visual_source": "pcbnew.canvas",
      "visual_has_pixels": true
    },
    "context": {},
    "visual": {},
    "activity": {}
  }
}
```

## Non-Goals

- No new tool name.
- No changes to visual pixel inclusion rules.
- No changes to anchor generation.
- No changes to panel state schema.
- No GUI redesign.

## Verification

1. Add failing `WorkspaceViewToolReturnsSummaryHeader` coverage.
2. Verify the test fails because `summary` is missing.
3. Implement the summary helper.
4. Run the targeted semantic tool handler tests.
5. Run broad `Ai*` common tests.
6. Build `pcbnew`.
7. Attempt Computer Use GUI smoke and record approval timeout if it recurs.

## Self-Review

- Scope check: this is one non-breaking payload addition to the existing
  workspace-view tool.
- Safety check: summary explicitly avoids raw visual payloads.
- Consistency check: summary fields mirror existing context counts and dynamic
  context names instead of inventing a second state source.
