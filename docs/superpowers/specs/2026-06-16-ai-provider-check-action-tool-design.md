# AI Provider Check Action Tool Design

Date: 2026-06-16

## Purpose

The runtime and `AI_ACTION_TOOL_CALL_HANDLER` already support both
`kisurf_run_action` and `kisurf_check_action`. The provider request schema,
however, currently declares only `kisurf_run_action`, so a model has no explicit
tool affordance for dry-run policy checks before asking KiSurf to execute an
editor action.

This spec adds `kisurf_check_action` to the OpenAI-compatible tool declaration.
It is a read/check surface only: the handler always forces dry-run behavior for
this tool, regardless of any arguments the model supplies.

## Source Anchors

- `common/kisurf/ai/ai_action_tool_call_handler.cpp` recognizes
  `kisurf_run_action` and `kisurf_check_action`.
- `common/kisurf/ai/ai_tool_execution.cpp` applies policy gates and supports
  dry-run invocation.
- `common/kisurf/ai/ai_provider.cpp` builds the OpenAI-compatible `tools` array.
- `qa/tests/common/test_ai_provider.cpp` validates provider tool declarations.
- `qa/tests/common/test_ai_action_tool_call_handler.cpp` validates that
  `kisurf_check_action` forces dry-run and does not call the runner.

## Goals

- Declare `kisurf_check_action` in the provider `tools` array.
- Reuse the same parameters shape as `kisurf_run_action`.
- Describe it as a policy/availability check that never executes the action.
- Preserve `parallel_tool_calls=false`.
- Preserve all deny-by-default action execution policy.

## Non-Goals

- No new action runner capability.
- No expansion of the action execution allowlist.
- No user-confirmation UI for modifying actions in this slice.
- No tool-result follow-up model round trip in this slice.

## Design

The provider should declare two function tools:

1. `kisurf_run_action`
   - Requests a policy-gated action execution.
2. `kisurf_check_action`
   - Requests a dry-run check for the same action catalog and policy gates.

Both tools share this parameter shape:

```json
{
  "type": "object",
  "properties": {
    "action": {
      "type": "string",
      "description": "Native KiCad/KiSurf action name from the current action catalog."
    },
    "arguments": {
      "type": "object",
      "description": "Optional action-specific arguments.",
      "additionalProperties": true
    },
    "dry_run": {
      "type": "boolean",
      "description": "When true, check policy and preview feasibility without executing."
    }
  },
  "required": ["action"],
  "additionalProperties": false
}
```

The `kisurf_check_action` declaration may include the `dry_run` property for
schema compatibility, but the handler remains the source of truth and forces
dry-run whenever the tool name is `kisurf_check_action`.

## Safety

- `kisurf_check_action` does not invoke `AI_ACTION_RUNNER`.
- Unknown, modifying, or destructive actions still return policy denial results.
- The model receives no new ability to mutate editor state.
- API keys and raw editor pointers are not included in tool schema text.

## Testing Requirements

Provider tests must verify:

- The `tools` array has both `kisurf_run_action` and `kisurf_check_action`.
- Both entries are function tools.
- `parallel_tool_calls` remains false.

Existing handler tests continue to verify:

- `kisurf_check_action` forces dry-run.
- `kisurf_check_action` does not call the action runner.

## Acceptance Criteria

- OpenAI-compatible requests expose both action tools.
- Existing provider tool-call parsing still handles function tool calls.
- Existing action handler dry-run behavior remains unchanged.
- No execution policy is loosened.

## Spec Self-Review

- Open-marker scan: no unresolved placeholders remain.
- Scope check: this is only provider schema declaration for an already-supported
  handler tool.
- Safety check: check-action remains dry-run and cannot mutate editor state.
