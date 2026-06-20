# Workspace View Panels Section Design

## Context

`kisurf_get_workspace_view` is the preferred read-only interface for model context. It already returns `context`, `visual`, and `activity` sections, and panel states are available inside the context payload. The updated architecture goal calls out panels as a first-class source of real-time work environment state, especially table cells and side panels. The model should be able to request panel state without pulling the full context object.

## Goals

- Add `panels` as a first-class `views` section in `kisurf_get_workspace_view`.
- Preserve the existing context payload behavior for callers that still ask for `context.panel_states`.
- Provide bounded panel options: `max_panels`, `panel_id`, `focused_only`, and `include_state`.
- Keep the section read-only and based on existing `AI_PANEL_STATE_RECORD` snapshots.
- Update the OpenAI-compatible tool schema so models can discover the new section.

## Non-Goals

- No new live panel scraping in this slice.
- No UI mutation or semantic UI action changes.
- No changes to PCB/schematic preview rendering.

## Response Contract

`kisurf_get_workspace_view` accepts:

```json
{
  "views": ["panels"],
  "panels": {
    "max_panels": 8,
    "panel_id": "agent.panel",
    "focused_only": false,
    "include_state": true
  }
}
```

The response includes:

```json
{
  "workspace_view": {
    "summary": {
      "included_views": ["panels"],
      "panel_state_count": 2
    },
    "panels": {
      "panel_state_count": 2,
      "matched_panel_count": 1,
      "records": [
        {
          "id": "agent.panel",
          "title": "Agent",
          "focused_control_id": "agent.input",
          "focused_control_label": "Input",
          "selected_text": "",
          "summary": "mode=Chat",
          "state": {}
        }
      ]
    }
  }
}
```

`include_state:false` omits `state` and `state_raw` while preserving panel identity, focus, selected text, and summary. `focused_only:true` returns records with a non-empty focused control id. `panel_id` returns only the named panel.

## Testing

Use TDD. Add failing tests for:

- requesting only the `panels` workspace section;
- filtering by panel id and focused-only state;
- omitting heavy parsed state;
- rejecting malformed `panels` options.

Then implement parser, JSON projection, provider schema, and run the targeted semantic tool tests plus full `Ai*`.

## Self-Review

- Completeness scan: no marker text remains.
- Scope: first-class panel section only; no unrelated renderer or UI mutation changes.
- Ambiguity: `panel_state_count` is the total available in the snapshot, while `matched_panel_count` is the count after filters.
