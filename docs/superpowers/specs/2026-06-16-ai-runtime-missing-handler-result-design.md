# AI Runtime Missing Handler Result Design

Date: 2026-06-16

## Problem

When the model requests a tool but no runtime tool handler is installed, the runtime marks the tool call
as not allowed and not executed. However, it does not record a matching `ToolResult` activity record and
does not assign a stable error code or result JSON.

## Goals

- Treat missing handler as a first-class denied tool result.
- Assign a stable `no_tool_handler` error code.
- Populate model-visible result JSON for the no-handler outcome.
- Record a `ToolResult` activity after the `ModelToolRequest` activity.
- Preserve one bounded continuation turn so the provider can explain the denial.

## Non-Goals

- No new tool handler implementation.
- No change to policy for installed handlers.
- No UI changes.

## Design

In `AI_RUNTIME::Submit`, when a tool call arrives and `m_ToolCallHandler` is null:

- Set:
  - `m_Allowed = false`
  - `m_Executed = false`
  - `m_ErrorCode = "no_tool_handler"`
  - `m_Message = "No tool handler installed."`
  - `m_ResultJson` with a structured denied envelope
- Append a `ToolResult` activity record with the same fields.

The result JSON uses the tool name as `action` because no native action was parsed.

## Verification

- Update runtime unit coverage for the no-handler path to require two activity records:
  `ModelToolRequest` followed by `ToolResult`.
- Verify the tool call result has `no_tool_handler` and parseable result JSON.
- Re-run targeted runtime tests.

## Self Review

- This does not let the model execute anything new; it only makes denial explicit.
- The continuation turn receives better structured evidence without changing provider protocol.
