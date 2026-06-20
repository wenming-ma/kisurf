# AI Semantic UI Action Bridge Design

## Purpose

KiSurf already exposes read-only semantic UI state for the Agent panel and can execute semantic UI actions through `AI_AGENT_PANEL::InvokeSemanticUiAction`. The missing layer is a model-facing tool that lets the provider request these actions through the same tool-call loop used for context reads and preview suggestions.

This slice adds a guarded semantic UI action bridge. The model can request actions against current semantic UI node ids, but KiSurf checks the live semantic tree immediately before execution and refuses nodes that require explicit user confirmation.

## Scope

This slice adds:

- A new OpenAI-compatible function tool named `kisurf_invoke_semantic_ui_action`.
- A common-layer callback interface from `AI_SEMANTIC_TOOL_CALL_HANDLER` to the active UI surface.
- Parsing and validation for semantic UI action arguments.
- Live semantic-tree confirmation checks before invoking the UI callback.
- JSON results that report whether the action was allowed, executed, focused a node, or failed.
- Agent panel wiring so model tool calls can set Agent input text, send text, toggle background mode, preview, reject, or stop through existing semantic UI action code.

This slice does not make model tool calls equivalent to human confirmation. The model-facing tool cannot accept a preview whose semantic node is marked `requires_user_confirmation`.

## Tool Contract

Tool name:

```text
kisurf_invoke_semantic_ui_action
```

Parameters:

- `node_id` string, required. Must match a node from the current semantic UI tree.
- `action` string, required. Examples: `set_text`, `toggle`, `invoke`.
- `text` string, optional. Used by text entry actions.
- `checked` boolean, optional. Used by toggle actions.

The schema deliberately does not expose `user_confirmed`. User confirmation must come from a human event path, not from model arguments.

The tool is supported only when the semantic tool-call handler was constructed with both:

- A semantic UI tree provider.
- A semantic UI action invoker.

## Safety Boundary

Before invoking the callback, the handler obtains the current semantic UI tree and checks the target node:

- Unknown node: deny with `unknown_node`.
- Disabled node: deny with `disabled_node`.
- Node marked `requires_user_confirmation`: deny with `confirmation_required`.
- Unsupported or malformed arguments: deny with `malformed_arguments`.
- Missing bridge callbacks: deny with `handler_not_configured`.

If the node passes those checks, the handler calls the UI action invoker with `m_UserConfirmed=false`. This keeps the model path unable to bypass `AI_AGENT_PANEL::InvokeSemanticUiAction`'s internal `agent.accept` confirmation check even if a future semantic tree misses the confirmation flag.

Any user-supplied or model-supplied text returned in messages is passed through `RedactSemanticUiText` before it enters result JSON.

## Result JSON

Successful invocation returns:

```json
{
  "tool": "kisurf_invoke_semantic_ui_action",
  "allowed": true,
  "executed": true,
  "status": "ui_action_executed",
  "node_id": "agent.input",
  "action": "set_text",
  "focused_node_id": "agent.input",
  "message": "Semantic UI action executed."
}
```

Denied invocation returns the existing denied-tool shape with:

- `allowed: false`
- `executed: false`
- `status: denied`
- `error_code`
- `message`

If the UI callback runs but returns failure, the tool result is allowed but not executed:

- `allowed: true`
- `executed: false`
- `status: ui_action_failed`
- `error_code` from the callback
- `message` from the callback, redacted

## Architecture

`AI_SEMANTIC_TOOL_CALL_HANDLER` gains two optional collaborators:

- `AI_SEMANTIC_UI_TREE_PROVIDER`: returns the current semantic UI tree.
- `AI_SEMANTIC_UI_ACTION_INVOKER`: executes an `AI_SEMANTIC_UI_ACTION_REQUEST`.

The existing constructor that only accepts an `AI_SEMANTIC_SUGGESTION_SINK` remains valid for tests and read-only tools. A second constructor accepts all three callbacks.

`AI_AGENT_PANEL::ConfigureActionToolCalls()` installs the bridge by passing lambdas for `SemanticUiTree()` and `InvokeSemanticUiAction(...)`. The existing dispatcher order stays the same: semantic handler first, native action handler second.

The OpenAI-compatible provider advertises `kisurf_invoke_semantic_ui_action` alongside existing context, visual, activity, preview, and native action tools. `parallel_tool_calls` remains false so UI actions and preview generation are serialized through KiSurf policy.

## Tests

Add focused `qa_common` coverage:

- Provider schema includes `kisurf_invoke_semantic_ui_action`, requires `node_id` and `action`, exposes `text` and `checked`, and does not expose `user_confirmed`.
- Handler invokes a non-confirmation semantic UI action and returns `ui_action_executed`.
- Handler refuses confirmation-required nodes before calling the invoker.
- Handler refuses disabled or unknown nodes before calling the invoker.
- Handler rejects malformed arguments and missing callbacks.
- Handler redacts sensitive text from failure messages.
- Agent panel still exposes `ConfigureActionToolCalls`, `SemanticUiTree`, and `InvokeSemanticUiAction` as the wiring surface.
