# AI Semantic UI Confirmation Guard Design

## Problem

The Agent pane already exposes a semantic UI tree and an internal `InvokeSemanticUiAction` API. A future model-facing semantic UI tool must not allow the model to bypass the user acceptance boundary for preview suggestions. The current semantic UI node metadata does not identify actions that require explicit user confirmation.

## Goals

- Mark semantic UI nodes that require user confirmation.
- Require explicit confirmation when invoking the Agent accept action through semantic UI automation.
- Preserve direct human button clicks, which call `AcceptLatestSuggestion()` outside the semantic automation path.
- Keep the change independent from any model-facing invoke tool.

## Non-Goals

- Do not expose a new provider tool in this slice.
- Do not change preview/accept button behavior for direct UI clicks.
- Do not add modal dialogs or new user-facing UI.

## Contract

`AI_SEMANTIC_UI_NODE` gains:

- `m_RequiresUserConfirmation`: true for semantic actions that must not be executed by an autonomous model without a user confirmation signal.

`AI_SEMANTIC_UI_ACTION_REQUEST` gains:

- `m_UserConfirmed`: false by default.

The Agent pane semantic tree serializes `requires_user_confirmation` for each node.

`AI_AGENT_PANEL::InvokeSemanticUiAction()` denies `agent.accept` with `confirmation_required` unless `m_UserConfirmed` is true.

## Acceptance

- `agent.accept` node JSON includes `requires_user_confirmation=true`.
- Non-accept action nodes remain false.
- Semantic accept invocation without user confirmation is denied.
- Semantic accept invocation with user confirmation can continue to the existing accept handler.
