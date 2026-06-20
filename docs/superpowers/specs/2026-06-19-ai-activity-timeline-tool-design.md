# AI Activity Timeline Tool Design

Date: 2026-06-19

## Goal

Add a model-callable read-only tool that returns a bounded, optionally filtered timeline of recent user/model/tool activity from the current Agent context.

This gives the Agent a direct answer to "what has the engineer just been doing?" without requiring a full context snapshot.

## Existing Foundation

KiSurf already has a user activity path:

- `MakeAiActivityRecordFromToolEvent()` maps selected editor events into `AI_ACTIVITY_RECORD`.
- It records command actions, selection events, selected-item moves, mouse clicks, and double-clicks.
- It ignores high-frequency motion and drag events.
- PCB and schematic frames register tool-event observers and call `AI_AGENT_PANEL::RecordActivity()`.
- `AI_AGENT_PANEL_MODEL::SendUserText()` merges `ActivityRecords()` into `AI_CONTEXT_SNAPSHOT::m_RecentActivity`.

The missing piece is a focused tool-call read interface for that activity timeline.

## Requirements

1. Provide a new semantic tool named `kisurf_get_activity_timeline`.
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
   - `status="activity_ready"`
   - `activity_count`
   - `activity`
5. Activity records must come from `AI_PROVIDER_REQUEST::m_ContextSnapshot.m_RecentActivity`.
6. The caller may set `max_activity`.
7. Missing `max_activity` defaults to `64`.
8. `max_activity` is clamped to `128`.
9. The caller may set `kind` to one of:
   - `user_action`
   - `model_tool_request`
   - `policy_decision`
   - `tool_result`
10. Missing `kind` returns all activity kinds.
11. The caller may set `action_contains` to a non-empty string for case-sensitive substring filtering on `action`.
12. Invalid argument types or unknown fields produce `malformed_arguments`.
13. The returned `activity_count` is the number of records after filtering, before max truncation.

## Interface

Tool name:

```text
kisurf_get_activity_timeline
```

Parameters:

```json
{
  "max_activity": 64,
  "kind": "user_action",
  "action_contains": "selected"
}
```

All properties are optional.

Response:

```json
{
  "tool": "kisurf_get_activity_timeline",
  "allowed": true,
  "executed": false,
  "status": "activity_ready",
  "activity_count": 2,
  "activity": [
    {
      "sequence": 7,
      "request_id": 0,
      "tool_call_id": "",
      "kind": "user_action",
      "editor": "pcb",
      "action": "common.Interactive.selected",
      "arguments_json": "{\"category\":\"message\",\"action\":\"event\"}",
      "result_json": "",
      "error_code": "",
      "allowed": true,
      "executed": true,
      "message": "..."
    }
  ]
}
```

## Non-goals

This slice does not add:

- new event observers
- raw mouse motion streams
- live subscription/streaming
- UI click replay
- automatic background prompting
- privacy controls beyond bounded read semantics

Those belong in later activity-sensing work.

## Self-check

- The tool reads the same activity records already sent through context snapshots.
- It gives the model a lightweight alternative to fetching full context.
- It is bounded and filterable.
- It does not introduce new event capture risk.
- It supports the AI-native goal of making user activity easy for the Agent to inspect.
