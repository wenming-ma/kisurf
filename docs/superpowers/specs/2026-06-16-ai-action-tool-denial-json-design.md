# AI Action Tool Denial JSON Design

Date: 2026-06-16

## Problem

`AI_ACTION_TOOL_CALL_HANDLER` can deny tool calls before they reach `AI_TOOL_EXECUTOR`, such as
unsupported tool names, malformed arguments, or unknown native actions. These denied results currently
carry an error code and message but no stable `m_ResultJson`.

## Goals

- Populate model-visible result JSON for handler-level denials.
- Use a consistent denied envelope with action, allowed, executed, dry-run, status, error code, and
  message.
- Preserve fail-closed behavior.

## Non-Goals

- No change to executor-managed policy denials.
- No new model tools.
- No action lookup or allowlist changes.

## Design

`deniedResult` in `ai_action_tool_call_handler.cpp` will assign:

```json
{
  "action": "missing.action",
  "allowed": false,
  "executed": false,
  "dry_run": false,
  "status": "denied",
  "error_code": "unknown_action",
  "message": "Action is not present in the AI action catalog."
}
```

When no native action can be parsed, `action` may be empty; the error code is the stable discriminator.

## Verification

- Add unit coverage for unknown tool, malformed arguments, and unknown action result JSON.
- Re-run action tool-call handler and runtime tests.

## Self Review

- The denial JSON is informational only and does not bypass policy.
- This keeps provider continuation behavior deterministic because `toolResultContent` can use
  `m_ResultJson` directly.
