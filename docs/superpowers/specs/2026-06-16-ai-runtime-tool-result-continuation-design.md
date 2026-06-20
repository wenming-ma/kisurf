# AI Runtime Tool Result Continuation Design

Date: 2026-06-16

## Purpose

The current runtime can parse model tool calls, route them through the native
policy handler, and store the tool invocation result on each
`AI_TOOL_CALL_RECORD`. The model, however, does not receive those results in a
follow-up turn, so it cannot explain the outcome to the user after KiSurf has
checked or executed the requested action.

This spec closes that first native tool loop with one bounded continuation:
after the runtime handles first-round tool calls, it may ask the provider for a
final assistant message that includes the tool results.

## Source Anchors

- `include/kisurf/ai/ai_types.h` owns provider request and tool-call records.
- `common/kisurf/ai/ai_runtime.cpp` assigns request IDs, calls providers,
  invokes tool handlers, records activity, and stores traces.
- `common/kisurf/ai/ai_provider.cpp` builds OpenAI-compatible chat-completion
  messages and parses model responses.
- `qa/tests/common/test_ai_runtime.cpp` covers runtime tool-call handling.
- `qa/tests/common/test_ai_provider.cpp` covers provider request JSON.

## Goals

- Let runtime submit one provider continuation after first-round tool calls have
  native results.
- Preserve the original user request ID for the entire user-visible operation.
- Preserve executed tool result records on the final `AI_PROVIDER_RESPONSE`.
- Serialize tool results using OpenAI-compatible assistant `tool_calls` and
  `tool` messages.
- Keep model-driven editor mutation behind the existing deny-by-default policy
  and installed tool handler.

## Non-Goals

- No recursive or unbounded multi-tool loop.
- No new action allowlist entries.
- No execution of tool calls returned by the continuation response.
- No streaming provider support in this slice.
- No suggestion JSON parsing from final assistant text in this slice.

## Design

### Request Contract

Add a continuation field to `AI_PROVIDER_REQUEST`:

```cpp
std::vector<AI_TOOL_CALL_RECORD> m_ToolResults;
```

An empty vector means a normal first provider request. A non-empty vector means
the provider should reconstruct the prior assistant tool-call message and append
one tool result message per record.

The same `m_RequestId`, `m_UserText`, editor context, visual snapshot, and recent
activity remain attached to the continuation request.

### Runtime Flow

`AI_RUNTIME::Submit(...)` should:

1. Assign one request ID to the user request.
2. Call the provider for the first response.
3. If the response contains tool calls, record and handle each one exactly as it
   does today.
4. If the handled response has any tool calls, create a continuation request by
   copying the original request and assigning `m_ToolResults` to the handled tool
   calls.
5. Call the provider once more.
6. Return the continuation response body while preserving the first-round handled
   tool-call records on the returned response.
7. Store a single trace record for the user request whose response is the final
   response with preserved tool results.

If no tool handler is installed, the runtime still fills each tool call with a
denial message. The continuation may tell the model that no local handler was
available, but no editor action is executed.

### OpenAI-Compatible Messages

For normal requests, the provider keeps the existing message shape:

1. system
2. user

For continuation requests, the provider sends:

1. system
2. user
3. assistant with `content: null` and `tool_calls`
4. one `tool` message per tool result

Each assistant tool call should use the original `m_ToolCallId`, `m_ToolName`,
and `m_ArgumentsJson`:

```json
{
  "role": "assistant",
  "content": null,
  "tool_calls": [
    {
      "id": "call_runtime",
      "type": "function",
      "function": {
        "name": "kisurf_run_action",
        "arguments": "{\"action\":\"common.Control.showAgentPanel\"}"
      }
    }
  ]
}
```

Each tool message should use `m_ResultJson` when present. If no result JSON is
present, the provider must synthesize a compact JSON object containing
`allowed`, `executed`, `error_code`, and `message`.

### Safety

- Runtime executes only first-round tool calls in this slice.
- Continuation tool calls are parsed by the provider but are not executed by the
  runtime in this slice.
- Existing policy-gated tool handling remains the only path to editor actions.
- Tool result messages include result metadata only; they must not include API
  keys, raw native pointers, or full project files.

## Testing Requirements

Runtime tests must verify:

- A first response with one tool call causes one handler invocation and one
  continuation provider call.
- The final response body comes from the continuation.
- The final response keeps the handled tool-call record and result JSON.
- The trace stores the final response with preserved tool results.

Provider tests must verify:

- A continuation request emits system, user, assistant, and tool messages in the
  OpenAI-compatible order.
- The assistant message reconstructs the prior tool call ID, tool name, and
  arguments.
- The tool message uses the tool-call ID and result JSON content.

## Acceptance Criteria

- Model-requested tool calls can produce a final model-visible explanation after
  native policy handling.
- Existing first-round tool-call parsing and execution behavior remains covered.
- The implementation remains bounded to one continuation turn.
- No new editor action can execute without the existing tool handler and policy
  gates.

## Spec Self-Review

- Open-marker scan: no unresolved placeholders remain.
- Scope check: the slice adds one continuation turn only, not a full recursive
  agent loop.
- Safety check: continuation responses do not expand action execution policy.
