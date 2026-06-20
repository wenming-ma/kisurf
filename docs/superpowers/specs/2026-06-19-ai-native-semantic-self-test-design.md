# AI Native Semantic Self-Test Design

Date: 2026-06-19

## Purpose

The AI-native system needs a reliable way to prove that the Agent pane can be
opened, inspected, and operated after builds. External desktop automation is no
longer enough evidence: older smoke records said Computer Use passed, later
observability smoke records show approval timeouts, and the current Computer
Use helper fails during bootstrap before KiSurf can be launched through that
API.

This spec defines the first native semantic self-test slice. It gives KiSurf a
debug/test-facing semantic UI interface for the Agent pane itself, so tests can
inspect stable controls and invoke safe actions without depending on external
desktop automation.

## Source Observations

- `docs/superpowers/specs/2026-06-16-validation-and-self-test-design.md`
  already defines the deferred native semantic-tree interface.
- `docs/superpowers/plans/2026-06-16-agent-pane-computer-use-smoke-test.md`
  is now corrected to mark GUI smoke as unverified.
- `common/kisurf/ai/ai_agent_panel.cpp` owns the Agent pane controls:
  mode choice, background Agent checkbox, Chat/Preview/Log tabs, transcript,
  suggestions, log, input, Send, Stop, Preview, Accept, and Reject.
- `AI_AGENT_PANEL_MODEL` already exposes the state needed for semantic nodes:
  active mode, background Agent enabled state, messages, suggestions,
  observability entries, and pending suggestion status.
- `AI_AGENT_PANEL::SendCurrentText`, `PreviewLatestSuggestion`,
  `AcceptLatestSuggestion`, `RejectLatestSuggestion`, and
  `SetBackgroundAgentEnabled` are already safe high-level actions.

## Goals

1. Add a common semantic UI data model that can describe a visible UI tree with
   stable node IDs, roles, labels, enabled/visible/focused state, optional
   bounds, optional safe text values, action names, and screenshot metadata.
2. Add an Agent-pane semantic tree builder with deterministic node IDs for the
   AI controls that matter for smoke testing.
3. Add an Agent-pane semantic action surface for safe actions:
   set input text, invoke Send, invoke Stop, toggle background Agent, invoke
   Preview, invoke Accept, and invoke Reject.
4. Keep the first slice debug/test-oriented and native. It must not expose
   password fields, secret values, or arbitrary clicking.
5. Keep external automation optional. Computer Use may still be useful, but it
   must not be the only way to verify the Agent pane.

## Non-Goals

- No full KiCad application semantic tree in this slice.
- No arbitrary coordinate clicking in this slice.
- No production remote-control endpoint.
- No credential storage or API key handling changes.
- No replacement for the existing `workspace_view` model-facing board/context
  interface.
- No canvas screenshot implementation here; current board/canvas visual capture
  remains owned by the AI visual snapshot and visual frame tools.

## Semantic UI Data Model

Create a common model under `include/kisurf/ai/ai_semantic_ui.h`.

Core types:

- `AI_SEMANTIC_UI_TEXT_POLICY`
  - `None`: no text value is exposed.
  - `Plain`: safe text may be exposed.
  - `Redacted`: text exists but must be redacted.
- `AI_SEMANTIC_UI_BOUNDS`
  - `m_Available`
  - `m_X`, `m_Y`, `m_Width`, `m_Height`
- `AI_SEMANTIC_UI_NODE`
  - `m_NodeId`
  - `m_ParentNodeId`
  - `m_Role`
  - `m_Label`
  - `m_Enabled`
  - `m_Visible`
  - `m_Focused`
  - `m_ActionName`
  - `m_ToolActionId`
  - `m_TextPolicy`
  - `m_TextValue`
  - `m_Bounds`
- `AI_SEMANTIC_UI_TREE`
  - `m_FrameId`
  - `m_Title`
  - `m_ScreenshotAvailable`
  - `m_ScreenshotUnavailableReason`
  - `m_Nodes`
  - `FindNode(wxString)`
- `AI_SEMANTIC_UI_ACTION_REQUEST`
  - `m_NodeId`
  - `m_Action`
  - `m_Text`
  - `m_Checked`
- `AI_SEMANTIC_UI_ACTION_RESULT`
  - `m_Success`
  - `m_ErrorCode`
  - `m_Message`
  - `m_FocusedNodeId`

Text must pass through a shared redaction helper before being exposed in node
values or action results. The helper must redact API-key-shaped strings,
OpenAI API key assignment text, and common token/credential labels.

## Agent Pane Node IDs

The first Agent pane tree uses these stable IDs:

- `agent.root`
- `agent.mode.choice`
- `agent.background.toggle`
- `agent.tabs.chat`
- `agent.tabs.preview`
- `agent.tabs.log`
- `agent.chat.transcript`
- `agent.preview.suggestions`
- `agent.log.entries`
- `agent.input`
- `agent.send`
- `agent.stop`
- `agent.preview.invoke`
- `agent.accept`
- `agent.reject`

The tree must expose enabled state accurately enough for tests:

- `agent.send` is enabled only when input text is non-empty.
- `agent.preview.invoke`, `agent.accept`, and `agent.reject` are enabled only
  when an active suggestion exists and the corresponding handler is available.
- `agent.background.toggle` reflects `AI_AGENT_PANEL_MODEL::BackgroundAgentEnabled`.

## Agent Pane Actions

`AI_AGENT_PANEL` exposes:

```cpp
AI_SEMANTIC_UI_TREE SemanticUiTree() const;
AI_SEMANTIC_UI_ACTION_RESULT InvokeSemanticUiAction(
        const AI_SEMANTIC_UI_ACTION_REQUEST& aRequest );
```

Supported actions:

- `set_text` on `agent.input`: set the input control text and focus it.
- `invoke` on `agent.send`: call `SendCurrentText()`.
- `invoke` on `agent.stop`: cancel the latest request and refresh the log.
- `toggle` on `agent.background.toggle`: call
  `SetBackgroundAgentEnabled(aRequest.m_Checked.value_or(!current))`.
- `invoke` on `agent.preview.invoke`: call `PreviewLatestSuggestion()`.
- `invoke` on `agent.accept`: call `AcceptLatestSuggestion()`.
- `invoke` on `agent.reject`: call `RejectLatestSuggestion()`.

Unsupported node/action combinations return structured errors:

- `unknown_node`
- `unsupported_action`
- `disabled_node`
- `missing_text`

The implementation must call existing high-level panel methods rather than
performing raw mouse/keyboard behavior.

## Testing Requirements

Common tests must cover:

1. Semantic UI redaction removes key-shaped strings.
2. `AI_SEMANTIC_UI_TREE::FindNode` finds existing nodes and returns no value
   for missing nodes.
3. Agent pane semantic tree builder emits the stable node IDs above.
4. Agent pane semantic tree builder marks Send disabled for empty input and
   enabled for non-empty input.
5. Agent pane semantic tree builder marks suggestion controls disabled without
   an active suggestion and enabled when handlers are available.
6. `AI_AGENT_PANEL` exposes `SemanticUiTree` and `InvokeSemanticUiAction`.

Editor/GUI integration tests remain a follow-up once this native test surface
exists. The first code slice must still build `qa_common`, `pcbnew`, and
`eeschema`.

## Acceptance Criteria

- The stale Computer Use pass record is corrected.
- A native semantic UI data model exists in common code.
- Agent pane semantic nodes can be built deterministically without requiring
  external desktop automation.
- The Agent pane exposes safe semantic action entry points.
- Targeted `qa_common` tests pass.
- `pcbnew` and `eeschema` build after the panel API is added.

## Self-Review

- Spec coverage: This directly implements the deferred semantic-tree path from
  the validation/self-test spec after external automation became unreliable.
- Scope check: The slice is limited to Agent pane controls, not arbitrary KiCad
  windows or canvas actions.
- Safety check: Actions are allowlisted high-level methods, and text exposure is
  redacted by default.
- Ambiguity check: Stable node IDs, supported actions, errors, and tests are
  explicitly named.
