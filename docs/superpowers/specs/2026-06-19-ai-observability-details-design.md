# AI Observability Details Design

Date: 2026-06-19

## Purpose

The Agent pane already shows observability entries for model input, tool
calls, tool results, model output, suggestions, and user activity. The details
payload for model input is still too coarse for debugging because it stores the
full serialized context under an `editor` key and does not expose stable counts
for panel state, anchors, recent activity, tool results, or visual frame
metadata.

This spec defines a narrow improvement to the observability details JSON so
developers can inspect why a model made a decision without dumping large visual
payloads or secrets.

## Source Observations

- `AI_AGENT_OBSERVABILITY_LOG::Build()` creates entries from runtime traces,
  activity records, and suggestions.
- `appendTraceEntries()` creates `ModelInput` and `ModelOutput` entries.
- `contextSummaryJson()` currently puts `aSnapshot.AsJsonText(0,0,0)` under
  the `editor` key, which is semantically wrong and hard to read.
- `AI_CONTEXT_SNAPSHOT` already contains the counts and summaries the log
  needs: selected objects, visible objects, actions, anchors, panel states,
  recent activity, tool state, visual frame metadata, and context version.
- Redaction already exists in `ai_observability_log.cpp` and must remain in
  the details payload.

## Goals

1. Replace the model-input details payload with a stable request/context
   summary object.
2. Add explicit fields for:
   - request ID
   - editor
   - user text
   - context version
   - context summary
   - selected object count
   - visible object count
   - action count
   - recent activity count
   - anchor count
   - panel state count
   - tool results count
   - tool state kind
   - visual metadata without pixels
3. Add model-output details with response metadata:
   - request ID
   - title
   - body length
   - tool call count
   - handled tool call summaries
   - cancelled flag
4. Preserve redaction and avoid copying `AI_VISUAL_SNAPSHOT::m_DataUri`.
5. Keep existing observability entry kinds and summaries compatible.

## Non-Goals

- No UI redesign in this slice.
- No new Agent panel controls.
- No provider schema changes.
- No network behavior changes.
- No full context dump in log details.
- No visual pixel payload in logs.

## Model Input Details Shape

`ModelInput.m_DetailsJson` should be valid JSON like:

```json
{
  "request_id": 42,
  "editor": "pcb",
  "user_text": "route selected net",
  "context": {
    "summary": "2 selected pads",
    "version": {
      "document": 3,
      "selection": 2,
      "view": 9,
      "text": "doc=3;sel=2;view=9"
    },
    "selected_count": 2,
    "visible_count": 8,
    "action_count": 11,
    "recent_activity_count": 4,
    "anchor_count": 3,
    "panel_state_count": 1,
    "tool_state_kind": "routing_track",
    "visual": {
      "source": "pcbnew.canvas",
      "mime_type": "image/png",
      "width_px": 1280,
      "height_px": 720,
      "byte_size": 2048,
      "has_pixels": true
    }
  },
  "tool_results_count": 1
}
```

The `user_text`, `summary`, and string fields must be redacted.

## Model Output Details Shape

`ModelOutput.m_DetailsJson` should be valid JSON like:

```json
{
  "request_id": 42,
  "title": "Routing assistant",
  "body_length": 81,
  "tool_call_count": 1,
  "cancelled": false,
  "tool_calls": [
    {
      "id": "call_1",
      "name": "kisurf_get_workspace_view",
      "allowed": true,
      "executed": false,
      "error_code": "",
      "message": "context returned"
    }
  ]
}
```

Tool call arguments and result JSON are intentionally omitted here because they
already appear in activity entries and can be large.

## Testing Requirements

Common tests must cover:

1. Model input details expose typed fields rather than a serialized context in
   `editor`.
2. Model input details include panel state, anchor, recent activity, visual,
   and tool result counts.
3. Model input details redact secrets and omit visual `data_uri`.
4. Model output details include tool call count and handled tool call summary.
5. Existing tool-call and suggestion observability tests remain compatible.

## Acceptance Criteria

- `AI_AGENT_OBSERVABILITY_LOG` emits stable JSON details for model input and
  output.
- The JSON can be parsed by tests.
- Secret-shaped text is redacted in the details payload.
- Visual pixel data is not copied into observability details.
- Targeted common tests pass.
- `pcbnew` and `eeschema` still build.
- `git diff --check` and a secret scan pass.

## Self-Check

- Scope check: This improves log details only; it does not change provider,
  UI layout, or model behavior.
- Safety check: Redaction and no-pixel logging are explicit acceptance
  criteria.
- Architecture check: Observability remains derived from existing runtime
  traces and activity records.
