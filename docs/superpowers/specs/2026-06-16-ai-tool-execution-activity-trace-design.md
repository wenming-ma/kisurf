# AI Tool Execution Activity Trace Design

Date: 2026-06-16

## Purpose

This spec moves KiSurf from "the model can see available actions" to "the system can
record user/editor activity and can route model-requested tool calls through a
safe, auditable gate."

This is the next native substrate layer after context snapshots and action catalog
discovery. It is deliberately conservative: model output may request tools, but no
document-changing action may bypass allowlists, preview/edit policy, validation, or
human acceptance.

## Source Research Anchors

Local source anchors:

- `include/tool/action_manager.h` owns action registration and lookup.
- `include/tool/tool_manager.h` owns `RunAction(...)`, `PostAction(...)`, and
  action dispatch into active tools.
- `common/tool/action_manager.cpp` shows `ACTION_MANAGER` can find registered
  `TOOL_ACTION` instances but does not itself execute them.
- `common/tool/tool_manager.cpp` shows synchronous action execution is mediated by
  `TOOL_MANAGER::doRunAction(...)`, which constructs `TOOL_EVENT` and processes it.
- `common/tool/tool_dispatcher.cpp` is the user-input-to-tool-event path and is the
  later hook point for deeper user activity observation.
- `common/kisurf/ai/ai_action_catalog.cpp` already classifies actions into safety
  levels for model-facing discovery.

Provider/tool-call anchors:

- OpenAI's function-calling guide describes tool calling as a loop where the
  application exposes tools, receives model tool calls, executes application-side
  code, then returns tool outputs to the model.
- The same guide notes function tools are declared with JSON-schema-like
  definitions, and that applications can restrict or disable tool choice. Because
  large tool sets consume context and tool search is model-gated, KiSurf should not
  expose every KiCad action as a separate model function in this phase.

## Goals

- Add shared data types for tool invocation requests and results.
- Add a bounded activity log that records user actions, model tool requests, policy
  decisions, and execution results.
- Add a conservative tool execution policy over `AI_ACTION_DESCRIPTOR`.
- Add a testable tool executor that can use a fake runner in unit tests and a
  `TOOL_MANAGER` runner in editor integration.
- Keep model tool execution separate from direct editor mutation.
- Prepare provider-side tool-call parsing without requiring live network tests.

## Non-Goals

- No destructive or document-modifying model action is executed directly in this
  phase.
- No autonomous placement, routing, or edit materialization is introduced here.
- No full semantic-tree UI automation implementation.
- No IPC/MCP public protocol is added in this phase.
- No per-action OpenAI tool schema is emitted for all KiCad actions.

## Data Model

### `AI_ACTIVITY_KIND`

Kinds:

- `UserAction`: a user-triggered KiCad action or editor activity.
- `ModelToolRequest`: a model-requested tool call.
- `PolicyDecision`: allow/deny decision before execution.
- `ToolResult`: result returned by the runner or policy layer.

### `AI_ACTIVITY_RECORD`

Fields:

- `uint64_t sequence`
- `uint64_t requestId`
- `wxString toolCallId`
- `AI_ACTIVITY_KIND kind`
- `AI_EDITOR_KIND editorKind`
- `wxString actionName`
- `wxString argumentsJson`
- `wxString resultJson`
- `bool allowed`
- `bool executed`
- `wxString message`

The first implementation stores activity in memory only. Persistence can be added
after privacy policy, retention settings, and project-scoped trace storage are
specified.

### `AI_TOOL_INVOCATION_REQUEST`

Fields:

- `uint64_t requestId`
- `wxString toolCallId`
- `AI_EDITOR_KIND editorKind`
- `AI_CONTEXT_VERSION contextVersion`
- `AI_ACTION_DESCRIPTOR action`
- `wxString argumentsJson`
- `bool dryRun`
- `bool userAccepted`

Rules:

- `dryRun` requests may be allowed for discovery even when execution is denied.
- `userAccepted` is required before any future modifying action can proceed.
- Requests with invalid action descriptors fail closed.

### `AI_TOOL_INVOCATION_RESULT`

Fields:

- `uint64_t requestId`
- `wxString toolCallId`
- `wxString actionName`
- `bool allowed`
- `bool executed`
- `wxString errorCode`
- `wxString message`
- `wxString resultJson`

Error codes:

- `unknown_action`
- `not_allowlisted`
- `disabled_action`
- `requires_preview`
- `requires_user_acceptance`
- `destructive_denied`
- `runner_failed`

## Policy

The default policy is deny-by-default.

Allowed in this phase:

- `ReadOnly` actions on the explicit allowlist.
- `Interactive` actions on the explicit allowlist only when they do not create a
  document edit.
- `dryRun` checks that report whether an action would be allowed.

Denied in this phase:

- `Modifying` actions, with `requires_preview`.
- `Destructive` actions, with `destructive_denied`.
- Disabled actions, with `disabled_action`.
- Actions not in the allowlist, with `not_allowlisted`.
- Requests with invalid descriptors, with `unknown_action`.

The initial allowlist should contain only safe navigation/inspection actions used
by tests and the Agent pane, such as showing the Agent panel and zoom/find-style
actions once their descriptors are available.

## Execution Boundary

Common AI code owns:

- policy evaluation
- activity recording
- invocation/result types
- fake-runner tests

Editor integration owns:

- resolving action names through the active editor's `ACTION_MANAGER`
- running actions through the active editor's `TOOL_MANAGER`
- attaching user-triggered events to activity trace
- rejecting execution when the active editor does not match the request

The first implementation should introduce an adapter interface:

```cpp
class AI_ACTION_RUNNER
{
public:
    virtual ~AI_ACTION_RUNNER() = default;
    virtual bool RunActionByName( const wxString& aActionName, wxString& aError ) = 0;
};
```

A later editor adapter can wrap `TOOL_MANAGER::RunAction( std::string )`.

## Provider Tool Strategy

The model-facing provider should eventually expose a small stable tool surface
rather than one function per KiCad action:

- `kisurf_get_context`
- `kisurf_check_action`
- `kisurf_request_preview`
- `kisurf_run_action`

The action catalog remains prompt/context data. A model may request
`kisurf_run_action` with an action name and JSON arguments, but the local policy
decides whether anything runs.

For OpenAI-compatible chat completions, the future request builder can pass a
small `tools` array and disable parallel tool calls while KiSurf's first execution
gate is single-action and synchronous. Provider parsing must be offline-testable
with fake HTTP responses containing tool-call JSON.

## User Activity Recording

Initial user activity can be recorded by explicit calls from Agent-panel and editor
integration points. Deeper automatic observation should later hook closer to
`TOOL_DISPATCHER` and `TOOL_MANAGER::ProcessEvent`, after privacy and filtering are
specified.

Privacy rules:

- Do not record secret text values.
- Store action names and high-level arguments, not raw pointer addresses.
- Keep the first trace buffer bounded.
- Do not persist traces by default.

## Testing Requirements

Use test-first development for:

- activity log assigns monotonically increasing sequence numbers
- activity log keeps only the configured capacity
- policy denies unknown, disabled, non-allowlisted, modifying, and destructive actions
- policy allows explicitly allowlisted read-only actions
- executor records model request, policy decision, and result
- executor calls fake runner only when policy allows execution
- executor does not call fake runner on denied or dry-run requests

## Acceptance Criteria

- Tool invocation and activity trace types compile in the AI module.
- Unit tests pass for bounded activity logging.
- Unit tests pass for allowlist and safety policy.
- Unit tests prove denied tool calls do not reach the runner.
- Unit tests prove allowed read-only calls reach the runner exactly once.
- Runtime or panel code can later attach these records without changing the data
  contract.
- No modifying or destructive KiCad action can be executed by model output in this
  phase.
