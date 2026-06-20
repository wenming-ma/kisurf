# AI Tool Result JSON Design

Date: 2026-06-16

## Problem

Model-facing tool results are currently inconsistent. A dry run returns only `{"dry_run":true}`,
while a successful run returns only `{"status":"executed"}`. The model should receive a stable,
machine-readable result envelope after native policy and runner handling.

## Goals

- Standardize `AI_TOOL_EXECUTOR` result JSON for denied, dry-run, executed, and runner-failed calls.
- Include the action name, allowed flag, executed flag, dry-run flag, status, error code, and message.
- Preserve deny-by-default execution policy.
- Preserve activity logging.

## Non-Goals

- No new model tool.
- No change to action policy.
- No change to unknown-tool or malformed-argument handling outside the executor.
- No UI changes.

## Design

`AI_TOOL_EXECUTOR::Invoke` will assign `m_ResultJson` for every executor-handled outcome:

```json
{
  "action": "common.Control.showAgentPanel",
  "allowed": true,
  "executed": false,
  "dry_run": true,
  "status": "allowed",
  "error_code": "",
  "message": "Dry run allowed."
}
```

Statuses:

- `denied` when policy blocks execution.
- `allowed` when dry-run/check succeeds.
- `executed` when the native runner succeeds.
- `failed` when the native runner rejects or fails after policy allowed it.

## Verification

- Add common unit coverage that dry-run and execution results include the stable fields.
- Add common unit coverage that denied executor calls also produce structured JSON.
- Re-run tool execution and action tool-call handler tests.

## Self Review

- The result JSON is model-facing only and does not grant new permissions.
- The envelope mirrors fields already present in `AI_TOOL_INVOCATION_RESULT`, so it avoids a second
  policy source of truth.
- Unknown tool and malformed argument failures remain handled by the action tool-call handler and
  provider fallback.
