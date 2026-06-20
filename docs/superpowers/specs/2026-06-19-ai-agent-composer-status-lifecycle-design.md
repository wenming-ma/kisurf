# AI Agent Composer Status Lifecycle Design

## Context

The wxFormBuilder composer footer now exposes `m_ComposerStatus`, but the label is still static. The agent pane already has enough model state to explain what is happening: active mode, background-agent toggle, latest request id, cancellation state, and pending preview suggestions. This slice turns that footer into a model-readable and human-readable lifecycle signal.

## Goals

- Show a concise composer status that changes after send, stop, background toggle, and suggestion preview lifecycle actions.
- Expose the same status through the semantic UI tree so the model can inspect the panel state without relying on screenshots.
- Keep the status derived from existing model/runtime state instead of introducing a separate state machine.
- Preserve the wxFormBuilder layout from the previous slice.

## Non-Goals

- No streaming progress indicator in this slice.
- No provider-specific request status beyond request id and cancellation.
- No new toolbar controls or wxFormBuilder layout edits.

## Design

Add a small value type, `AI_AGENT_COMPOSER_STATUS_VIEW`, and a pure helper, `AiAgentComposerStatusText()`. The panel will build this view from the model and input field, then set `m_ComposerStatus`. Tests can exercise the text contract without constructing wx widgets.

Status precedence is deterministic:

1. Cancelled latest request: `Stopped request #<id>`
2. Latest completed request: `Last response #<id>`
3. Active preview suggestion: `Preview ready`
4. Background Agent enabled: `Background Agent on - <mode title>`
5. User typed input: `Ready to send`
6. Idle: `Ask about the current board`

The model will expose `LastRequestId()` and `LastRequestCancelled()`. `LastRequestCancelled()` reads the runtime trace records and returns true only when the last request id has a matching cancelled trace.

The semantic view gains `m_ComposerStatusText`, and `AiAgentPanelSemanticTree()` emits a stable `agent.composer.status` node. The panel-state summary includes `composer_status=<text>` so logs and external agent memory can consume a compact snapshot.

## Files

- `include/kisurf/ai/ai_agent_panel.h`: declare composer status view/helper and panel updater.
- `common/kisurf/ai/ai_agent_panel.cpp`: implement status text and keep the wx label synchronized.
- `include/kisurf/ai/ai_agent_panel_model.h`: expose latest request id and cancellation status.
- `common/kisurf/ai/ai_agent_panel_model.cpp`: implement the model accessors.
- `include/kisurf/ai/ai_agent_panel_semantic.h`: add composer status to the semantic view.
- `common/kisurf/ai/ai_agent_panel_semantic.cpp`: add status node and panel-state summary text.
- `qa/tests/common/test_ai_agent_panel.cpp`: cover pure status text precedence.
- `qa/tests/common/test_ai_agent_panel_model.cpp`: cover latest request id/cancellation state.
- `qa/tests/common/test_ai_agent_panel_semantic.cpp`: cover semantic status projection.

## Testing

Use TDD. First add tests that fail because the helper, accessors, and semantic node are absent. Then implement the minimal production changes and run the targeted common test subset. Finish with the full `Ai*` common suite, a `pcbnew` build, and Computer Use GUI smoke to catch Windows popups.

## Self-Review

- Completeness scan: no marker text remains.
- Consistency: the status strings are listed once and the implementation plan will use the same names.
- Scope: this is one panel-status slice and does not expand into streaming or provider execution.
- Ambiguity: cancelled request status intentionally wins over completed request status for the latest request id.
