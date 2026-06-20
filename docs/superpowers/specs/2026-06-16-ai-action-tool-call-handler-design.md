# AI Action Tool Call Handler Design

Date: 2026-06-16

## Purpose

This spec closes the gap between parsed provider tool calls and KiSurf's existing
policy-gated tool executor. The runtime can already parse and record
OpenAI-compatible `tool_calls`, and the executor can already deny or run
allowlisted actions. This handler translates the model-facing stable tool names
into `AI_TOOL_INVOCATION_REQUEST` values without making `AI_RUNTIME` depend on
editor classes.

The handler remains conservative. It is a common-layer bridge over descriptors
already supplied in `AI_CONTEXT_SNAPSHOT.m_Actions`; it does not discover editor
actions on its own, does not run modifying or destructive actions, and does not
implement a multi-turn tool-result provider loop.

Update, 2026-06-19: this early handler design has been tightened by
`2026-06-19-ai-action-tool-preview-acceptance-design.md`. Model-originated
`kisurf_run_action` calls are now preview-first: the handler still uses the
policy executor for validation and logging, but a successful dry run creates an
action preview suggestion. The action is materialized only after explicit user
Accept through the Agent/editor suggestion review path.

## Source Anchors

- `include/kisurf/ai/ai_runtime.h` defines `AI_TOOL_CALL_HANDLER`.
- `common/kisurf/ai/ai_runtime.cpp` invokes an optional handler and records the
  result in runtime traces.
- `include/kisurf/ai/ai_tool_execution.h` defines `AI_TOOL_EXECUTION_POLICY`,
  `AI_TOOL_EXECUTOR`, and `AI_ACTION_RUNNER`.
- `include/kisurf/ai/ai_types.h` defines `AI_CONTEXT_SNAPSHOT`,
  `AI_ACTION_DESCRIPTOR`, `AI_TOOL_CALL_RECORD`, and
  `AI_TOOL_INVOCATION_REQUEST`.
- `common/kisurf/ai/ai_agent_panel_model.cpp` owns `AI_RUNTIME` but currently has
  no public way for editor code to install a runtime tool-call handler.

## Goals

- Add a reusable common-layer `AI_ACTION_TOOL_CALL_HANDLER`.
- Parse `kisurf_run_action` and `kisurf_check_action` arguments from JSON.
- Resolve action descriptors from the current provider request context first.
- Optionally fall back to a supplied descriptor list for tests and later editor
  setup.
- Route resolved requests through `AI_TOOL_EXECUTOR` so all policy decisions and
  runner results use the existing safety gate.
- Add `AI_AGENT_PANEL_MODEL::SetToolCallHandler(...)` so editor integrations can
  attach a handler without changing the panel model again.
- Keep malformed, unknown-tool, unknown-action, and disabled/unsafe requests
  fail-closed.

## Non-Goals

- No direct dependency on `ACTION_MANAGER`, `TOOL_MANAGER`, PCB, or schematic
  editor classes in this common module.
- No execution of modifying or destructive actions by model output.
- No accepted-edit preview/materialization path; that remains owned by preview
  and edit adapters.
- No second provider call with tool results.
- No persistent activity storage.

## Model-Facing Tools

### `kisurf_run_action`

Arguments:

- `action`: required string. Must match an `AI_ACTION_DESCRIPTOR.m_Name`.
- `arguments`: optional object. Preserved as compact JSON.
- `dry_run`: optional bool. Model input is normalized to `true` for
  model-originated calls so action requests are preview-first.

The handler builds an `AI_TOOL_INVOCATION_REQUEST` and lets
`AI_TOOL_EXECUTOR` decide whether the action would be allowed. A successful
dry run creates a pending action preview suggestion instead of immediately
running the action.

### `kisurf_check_action`

Uses the same arguments as `kisurf_run_action`, but forces `dry_run=true`.
This gives the model a safe way to ask whether an action would be allowed.

## Handler Interface

Add a common-layer class:

```cpp
class AI_ACTION_TOOL_CALL_HANDLER : public AI_TOOL_CALL_HANDLER
{
public:
    AI_ACTION_TOOL_CALL_HANDLER( const AI_TOOL_EXECUTION_POLICY& aPolicy,
                                 AI_ACTION_RUNNER& aRunner,
                                 AI_ACTIVITY_LOG& aActivityLog );

    void SetFallbackActions( std::vector<AI_ACTION_DESCRIPTOR> aActions );

    AI_TOOL_INVOCATION_RESULT HandleToolCall(
            const AI_PROVIDER_REQUEST& aRequest,
            const AI_TOOL_CALL_RECORD& aToolCall ) override;
};
```

Resolution order:

1. `aRequest.m_ContextSnapshot.m_Actions`
2. fallback descriptors supplied through `SetFallbackActions(...)`

The handler does not mutate descriptors and does not mark disabled descriptors as
enabled.

## Error Handling

- Unknown provider tool name: result `unknown_tool`, not allowed, not executed.
- Invalid JSON: result `malformed_arguments`, not allowed, not executed.
- Missing or non-string `action`: result `malformed_arguments`, not allowed, not
  executed.
- Unknown action descriptor: result `unknown_action`, not allowed, not executed.
- Policy denials preserve the existing executor error codes.
- Runner failures preserve `runner_failed`.

## Testing Requirements

Use common-layer unit tests only:

- `kisurf_check_action` forces dry-run and does not call the runner.
- `kisurf_run_action` forces dry-run, does not call the runner immediately, and
  creates a pending action preview suggestion when allowed.
- Accepting an action preview suggestion runs the action once through the
  installed editor action runner and marks the suggestion accepted.
- Request context descriptors take precedence over fallback descriptors.
- Unknown tool names fail closed.
- Malformed argument JSON fails closed.
- Missing actions fail closed.
- Modifying/destructive descriptors remain denied by the existing policy.
- `AI_AGENT_PANEL_MODEL::SetToolCallHandler(...)` forwards handler results into
  response tool-call records.

## Acceptance Criteria

- The handler compiles in the common AI module.
- Targeted `qa_common` tests pass for `AiActionToolCallHandler`,
  `AiAgentPanelModel`, `AiNativeRuntime`, and `AiToolExecution`.
- No API key, base URL secret, pointer address, or raw editor object pointer is
  stored in tests or traces.
- No modifying or destructive action can execute through this handler.

## Spec Self-Review

- Placeholder scan: no TODO/TBD placeholders remain.
- Scope check: this spec adds only the common bridge and panel-model setter; real
  editor runner adapters are intentionally outside this slice.
- Safety check: model output remains deny-by-default and uses the existing
  executor policy.
- Compatibility check: runtime still depends only on `AI_TOOL_CALL_HANDLER`, and
  editor integrations can attach or omit the handler.
