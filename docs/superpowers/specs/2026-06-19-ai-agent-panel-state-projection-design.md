# AI Agent Panel State Projection Design

Date: 2026-06-19

## Purpose

The Agent pane now has a native semantic UI tree for self-test and safe
control invocation. The next step is to make that same semantic view visible
through the unified workspace/context interfaces the model already uses.

This spec defines a narrow projection layer from `AI_SEMANTIC_UI_TREE` to
`AI_PANEL_STATE_RECORD`. Once projected, Agent-pane state can flow through
`AI_CONTEXT_SNAPSHOT::m_PanelStates`, `kisurf_get_context_snapshot`, and
`kisurf_get_workspace_view` without adding a second model-facing read path.

## Source Observations

- `AI_PANEL_STATE_RECORD` already exists in `include/kisurf/ai/ai_types.h`.
- `AI_CONTEXT_SNAPSHOT` already stores `m_PanelStates`.
- `AI_CONTEXT_SNAPSHOT::AsJsonText` serializes `panel_state_count` and
  `panel_states`.
- `AI_SEMANTIC_TOOL_CALL_HANDLER` already lets
  `kisurf_get_context_snapshot` and `kisurf_get_workspace_view` include or
  omit panel state with `include_panels`.
- `AI_AGENT_PANEL_SEMANTIC_VIEW` and `AiAgentPanelSemanticTree()` already
  expose stable Agent-pane node IDs, roles, actions, text policies, and safe
  counts.

## Goals

1. Add a pure helper that converts an Agent-pane semantic view/tree into an
   `AI_PANEL_STATE_RECORD`.
2. Preserve stable semantic node IDs and action names in bounded JSON.
3. Expose only safe text. Redacted nodes must not leak input text, prompts, or
   credentials.
4. Add an `AI_AGENT_PANEL` convenience API so live panel state can be appended
   to a context snapshot by callers.
5. Prove that an Agent-pane panel record flows through
   `kisurf_get_workspace_view` when `include_panels` is enabled.

## Non-Goals

- No new IPC endpoint.
- No production remote-control server.
- No canvas pixel capture changes.
- No GUI smoke automation changes.
- No editor-level provider wiring beyond exposing the panel record helper.
- No arbitrary UI clicking or coordinate injection.

## Panel State Record

The Agent pane panel state uses:

- `m_Id`: `agent.panel`
- `m_Title`: `Agent`
- `m_FocusedControlId`: first focused semantic node if one exists, otherwise
  `agent.input`
- `m_FocusedControlLabel`: focused node label if one exists, otherwise
  `Input`
- `m_SelectedText`: empty
- `m_Summary`: bounded operational summary:
  - active mode
  - background Agent on/off
  - message count
  - suggestion count
  - log entry count
  - whether Send is enabled
  - whether an active suggestion exists
- `m_StateJson`: structured semantic tree payload

## State JSON Shape

`m_StateJson` must be valid JSON with this shape:

```json
{
  "frame_id": "agent",
  "title": "Agent",
  "screenshot_available": false,
  "screenshot_unavailable_reason": "string",
  "nodes": [
    {
      "id": "agent.send",
      "parent_id": "agent.root",
      "role": "button",
      "label": "Send",
      "enabled": true,
      "visible": true,
      "focused": false,
      "action": "invoke",
      "tool_action_id": "",
      "text_policy": "none",
      "bounds_available": false
    }
  ]
}
```

Only `Plain` text nodes may include `text_value`. `Redacted` nodes must expose
`text_policy: "redacted"` and omit `text_value`.

## API

Add to `include/kisurf/ai/ai_agent_panel_semantic.h`:

```cpp
KICOMMON_API AI_PANEL_STATE_RECORD AiAgentPanelSemanticStateRecord(
        const AI_AGENT_PANEL_SEMANTIC_VIEW& aView );

KICOMMON_API AI_PANEL_STATE_RECORD AiAgentPanelSemanticStateRecord(
        const AI_SEMANTIC_UI_TREE& aTree );
```

Add to `AI_AGENT_PANEL`:

```cpp
AI_PANEL_STATE_RECORD SemanticPanelStateRecord() const;
```

The live panel method should call `SemanticUiTree()` and project the result.

## Testing Requirements

Common tests must cover:

1. The pure helper returns `agent.panel` with a valid summary and state JSON.
2. The state JSON contains stable nodes such as `agent.send` and
   `agent.background.toggle`.
3. `agent.input` is marked `redacted` and does not contain a `text_value`.
4. Plain count nodes such as `agent.chat.transcript` include safe text values.
5. `AI_AGENT_PANEL` exposes `SemanticPanelStateRecord`.
6. A workspace view result includes the Agent panel record through
   `workspace_view.context.panel_states` when the context snapshot contains
   that record and `include_panels` is true.

## Acceptance Criteria

- Agent semantic panel state can be produced without constructing wx windows.
- The live Agent panel has a convenience API for the same record.
- `kisurf_get_workspace_view` can carry the Agent panel semantic state through
  the existing context section.
- Targeted `qa_common` tests pass.
- `pcbnew` and `eeschema` still build.
- `git diff --check` and a secret scan pass.

## Self-Check

- Scope check: This only projects existing Agent-pane semantic state into the
  existing context model.
- Safety check: Input text remains redacted and omitted from JSON.
- Architecture check: No duplicate model-facing context API is introduced.
- Compatibility check: Existing `panel_states` serialization remains the
  source of truth for model-visible panel state.
