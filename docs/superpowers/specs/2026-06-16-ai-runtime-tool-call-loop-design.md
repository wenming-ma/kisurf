# AI Runtime Tool Call Loop Design

Date: 2026-06-16

## Purpose

This spec connects the OpenAI-compatible provider to the native KiSurf tool
execution substrate. Phase 3 created the activity log, invocation request/result
types, and deny-by-default executor. Phase 4 makes model tool calls first-class
runtime data: provider responses can carry parsed tool calls, runtime can record
them, and editor integrations can later choose whether a vetted executor should
run them.

The first implementation remains conservative. It does not create autonomous
editing behavior, does not run modifying actions, and does not add live-network
tests. It makes the model's requested action visible, typed, auditable, and ready
for later preview/materialize work.

## Source Research Anchors

Local source anchors:

- `include/kisurf/ai/ai_types.h` already defines `AI_PROVIDER_REQUEST`,
  `AI_PROVIDER_RESPONSE`, `AI_TRACE_RECORD`, `AI_TOOL_CALL_RECORD`, and
  `AI_TOOL_INVOCATION_REQUEST`.
- `common/kisurf/ai/ai_provider.cpp` currently builds a chat-completions request
  and parses only `choices[0].message.content`.
- `common/kisurf/ai/ai_runtime.cpp` assigns request IDs and stores provider
  request/response traces, but does not inspect provider tool calls.
- `include/kisurf/ai/ai_tool_execution.h` and
  `common/kisurf/ai/ai_tool_execution.cpp` provide the deny-by-default execution
  policy and activity-recording executor.
- `common/kisurf/ai/ai_action_catalog.cpp` is the native source for
  action descriptors and safety classification; it should remain outside the
  generic provider parser.

External practice anchors:

- OpenAI's function-calling guide describes tool calling as an application loop:
  send tool definitions, receive a model tool call, execute application-side
  code, return tool output, then receive a final response or more tool calls.
- The OpenAI chat-completions reference represents assistant tool calls under
  `choices[0].message.tool_calls[]`, with each function call carrying an `id`,
  `function.name`, and JSON-string `function.arguments`.
- The same OpenAI guide recommends constraining tool choice when needed and notes
  that `parallel_tool_calls` can be disabled to force at most one tool call in a
  turn. KiSurf should start single-call/synchronous until preview ownership and
  edit commit semantics are wired.

References:

- https://developers.openai.com/api/docs/guides/function-calling
- https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create

## Goals

- Preserve assistant text parsing while also parsing OpenAI-compatible
  `tool_calls`.
- Extend provider/runtimes types so tool calls are stored with the matching
  request ID and trace record.
- Record provider-originated tool calls into the existing bounded activity log.
- Add an optional runtime executor boundary that can convert a parsed tool call
  into an `AI_TOOL_INVOCATION_REQUEST` only when editor code supplies an action
  descriptor resolver.
- Keep provider parsing independent from KiCad editor/action-manager classes.
- Add unit tests with fake HTTP/provider/runner objects only; no real API key or
  network dependency is allowed.

## Non-Goals

- No full multi-turn "send tool result back to the model" loop in this phase.
  This phase records and optionally executes one provider response's tool calls;
  the follow-up provider request is a later conversation-state phase.
- No provider-side dynamic emission of every KiCad action as a separate OpenAI
  function.
- No direct execution of `Modifying` or `Destructive` KiCad actions.
- No UI affordance for accepting previews.
- No persistent activity trace storage.
- No IPC/MCP public protocol changes.

## Design Decision

Three approaches were considered:

1. Parse tool calls only and leave execution entirely to later phases.
2. Parse, record, and optionally execute through the existing Phase 3 executor.
3. Implement the complete OpenAI tool loop, including tool-result follow-up
   calls, conversation memory, and UI preview acceptance.

The recommended implementation is option 2. Option 1 is too passive because it
does not prove the Phase 3 executor can be used by runtime clients. Option 3 is
too large because follow-up calls require conversation-state storage, user
acceptance UI, preview ownership, and edit materialization. Option 2 produces a
small complete slice: the provider parses tool calls, runtime records them, and
tests prove that optional execution remains policy-gated.

## Data Model Changes

### `AI_TOOL_CALL_RECORD`

Extend the existing record so it can represent raw provider tool calls and local
execution state:

- `uint64_t requestId`
- `wxString toolCallId`
- `wxString toolName`
- `wxString argumentsJson`
- `wxString resultJson`
- `bool allowed`
- `bool executed`
- `wxString errorCode`
- `wxString message`

`toolName` is provider-level and may be a stable KiSurf tool such as
`kisurf_run_action`. It is not necessarily a KiCad `TOOL_ACTION` name.

### `AI_PROVIDER_RESPONSE`

Add:

- `std::vector<AI_TOOL_CALL_RECORD> toolCalls`

The response may have both text and tool calls. If content is empty but tool
calls are present, this is a valid provider response with title `AI Provider` and
body `Tool call requested.`. Empty content with no tool calls remains an error.

### `AI_TRACE_RECORD`

No new field is required because it stores the full response, but tests must
verify that response tool calls are preserved in traces.

## Runtime Interfaces

Add a runtime-side optional tool-call handler rather than making `AI_RUNTIME`
depend on `ACTION_MANAGER`, `TOOL_MANAGER`, or `AI_ACTION_CATALOG`.

```cpp
class AI_TOOL_CALL_HANDLER
{
public:
    virtual ~AI_TOOL_CALL_HANDLER() = default;

    virtual AI_TOOL_INVOCATION_RESULT HandleToolCall(
            const AI_PROVIDER_REQUEST& aRequest,
            const AI_TOOL_CALL_RECORD& aToolCall ) = 0;
};
```

`AI_RUNTIME` gains:

- `void SetToolCallHandler( AI_TOOL_CALL_HANDLER* aHandler )`
- `std::vector<AI_ACTIVITY_RECORD> ActivityRecords() const`

Runtime owns a bounded `AI_ACTIVITY_LOG`. During `Submit(...)`, it records each
provider tool call as `ModelToolRequest`. If a handler is installed, runtime
passes the call to the handler and records the handler result as `ToolResult`.
If no handler is installed, runtime records only the model request and leaves
`allowed=false`, `executed=false`, and `message="No tool handler installed."` on
the response copy stored in trace.

The handler pointer is non-owning because editor frames own action resolution and
tool-manager lifetimes. Runtime must tolerate a null handler and must not retain
per-call references after `Submit(...)` returns.

## Provider Parsing

`AI_OPENAI_COMPAT_PROVIDER::Generate(...)` should parse:

```json
{
  "choices": [
    {
      "message": {
        "content": null,
        "tool_calls": [
          {
            "id": "call_abc123",
            "type": "function",
            "function": {
              "name": "kisurf_run_action",
              "arguments": "{\"action\":\"zoomFit\"}"
            }
          }
        ]
      },
      "finish_reason": "tool_calls"
    }
  ]
}
```

Rules:

- Ignore non-function tool calls in this phase.
- Preserve `arguments` exactly as returned if it is a string.
- If `arguments` is an object, serialize it to compact JSON.
- If the provider returns malformed `tool_calls`, return a provider error
  instead of fabricating a partial call.
- Do not parse the action name from arguments inside provider code. That belongs
  to an editor/runtime handler.

## Request Tool Declarations

The first implementation may include a minimal `tools` array with one stable
function:

- `kisurf_run_action`

Schema:

- `action`: string, required. Native action name from the current context's
  action catalog.
- `arguments`: object, optional. Tool-specific structured arguments.
- `dry_run`: boolean, optional. Defaults to true for uncertain requests.

Set `parallel_tool_calls` to false in provider requests. This does not make the
system safe by itself; the local policy remains authoritative. It only keeps the
first runtime implementation synchronous and easier to audit.

## Tool Call Handler Strategy

The first concrete handler can be test-only:

- Resolve `kisurf_run_action` arguments into an `AI_ACTION_DESCRIPTOR` supplied
  by the fake test handler.
- Call `AI_TOOL_EXECUTOR::Invoke(...)`.
- Copy `AI_TOOL_INVOCATION_RESULT` fields back into the provider response's
  `AI_TOOL_CALL_RECORD`.

Later editor handlers will:

- Resolve action descriptors from `AI_CONTEXT_SNAPSHOT.m_Actions` first.
- Fall back to the active editor action catalog when needed.
- Reject calls whose request context version no longer matches the active editor.
- Use preview/materialize services for modifying actions instead of direct
  `TOOL_MANAGER` execution.

## Error Handling And Safety

- Provider/network errors remain chat responses with an explanatory body.
- Tool-call parsing errors also become provider errors and do not reach runtime
  execution.
- Missing runtime handler is not an error; it records a blocked tool request.
- Handler denial must preserve the policy error code.
- Runtime must record tool-call activity even when the handler denies execution.
- No plaintext API key may be stored in traces, specs, test fixtures, or logs.

## Testing Requirements

Use test-first development for:

- OpenAI-compatible provider parses `tool_calls` with id, function name, and
  arguments.
- OpenAI-compatible provider accepts tool-call-only responses with null content.
- OpenAI-compatible provider includes the minimal KiSurf tool declaration and
  `parallel_tool_calls:false` in the fake HTTP request body.
- Malformed tool calls produce a provider error and no parsed calls.
- Runtime stores provider tool calls inside `AI_TRACE_RECORD`.
- Runtime records model tool calls into `AI_ACTIVITY_LOG` without a handler.
- Runtime with a fake handler copies allowed/executed/error/result fields back
  to response tool-call records.
- Runtime does not execute anything when no handler is installed.

## Acceptance Criteria

- `AI_PROVIDER_RESPONSE` can carry parsed tool calls.
- Provider unit tests pass without network access or real credentials.
- Runtime unit tests prove tool calls are traced and activity-recorded.
- Optional handler execution is tested with a fake handler and remains
  deny-by-default through Phase 3 policy.
- Existing Agent panel and stub-provider tests continue passing.
- No modifying or destructive action can be executed by model output in this
  phase.

## Spec Self-Review

- Placeholder scan: no placeholders, TODOs, or deferred implementation holes are
  present in the Phase 4 scope.
- Internal consistency: provider parsing remains editor-independent, runtime
  owns tracing/activity recording, and editor-specific action resolution remains
  behind `AI_TOOL_CALL_HANDLER`.
- Scope check: the spec is a single testable subsystem. Multi-turn conversation
  state, preview UI, native visual snapshots, and editor action adapters are
  intentionally left for later specs.
- Ambiguity check: missing handler behavior, null-content tool-call responses,
  malformed tool-call handling, and no-live-key testing requirements are explicit.
