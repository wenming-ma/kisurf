# AI Agent Observability Log Design

Date: 2026-06-18

## Purpose

The Agent cannot become a dependable chat tool or 7x24 background collaborator
until its reasoning loop is inspectable. Developers and users need to see the
important causal nodes:

- model input,
- model tool calls,
- tool results,
- model output,
- suggestion preview, accept, reject, and expiration events.

KiSurf already has useful primitives: `AI_ACTIVITY_LOG` records user actions,
model tool requests, and tool results; `AI_RUNTIME` stores `AI_TRACE_RECORD`
for provider requests and responses. The missing layer is a stable, UI-friendly
observability log that derives from those primitives and presents the Agent turn
as an ordered debug story.

## Current Source Audit

Existing sources:

- `include/kisurf/ai/ai_activity_log.h` defines a bounded, sequenced activity
  log.
- `common/kisurf/ai/ai_activity_log.cpp` assigns sequence numbers and stores
  records in memory.
- `include/kisurf/ai/ai_runtime.h` exposes `TraceRecords()` and
  `ActivityRecords()`.
- `common/kisurf/ai/ai_runtime.cpp` records provider tool calls as
  `ModelToolRequest`, records handler results as `ToolResult`, and stores the
  request/response pair as `AI_TRACE_RECORD`.
- `include/kisurf/ai/ai_agent_panel_model.h` owns the shared activity log used
  by chat, runtime tool calls, and editor activity.
- `common/kisurf/ai/ai_agent_panel.cpp` currently shows chat text and suggestion
  controls, but does not render model input, tool calls, tool results, or trace
  details as first-class UI.

Related specs:

- `2026-06-16-ai-tool-execution-activity-trace-design.md`
- `2026-06-16-ai-runtime-tool-call-loop-design.md`
- `2026-06-16-ai-unified-activity-timeline-design.md`
- `2026-06-17-ai-tool-state-next-action-preview-design.md`

## Requirements

1. Show each Agent turn as an inspectable sequence:
   user request -> model input -> tool call(s) -> tool result(s) -> model output.
2. Include background Agent suggestions in the same observability surface.
3. Do not create a second mutable log that can disagree with runtime trace and
   activity records.
4. Keep logs bounded and in memory by default.
5. Never store API keys or provider authorization headers.
6. Avoid dumping large image/base64 payloads by default; show visual snapshot
   metadata and a small prompt/context preview instead.
7. Make the surface useful in tests without requiring a live model or network.
8. Keep the design compatible with later persistence or IPC/MCP projection.

## Approaches Considered

### Approach A: Raw Trace Dump

Expose `AI_RUNTIME::TraceRecords()` directly in the Agent pane.

Pros:

- Minimal code.
- Uses existing data.

Cons:

- Too raw for users.
- Tool calls and activity records remain separate.
- UI would need to understand every runtime type.
- Hard to redact large or sensitive payloads consistently.

### Approach B: Separate Debug Journal

Add a new `AI_AGENT_DEBUG_LOG` and write debug events at each call site.

Pros:

- Easy to format specifically for the UI.
- Can include exactly the cards the panel needs.

Cons:

- Duplicates state already present in traces and activity records.
- Risks drift between the real runtime state and displayed debug state.
- Adds more write sites across provider, runtime, panel, and suggestion code.

### Approach C: Derived Observability View

Keep `AI_ACTIVITY_LOG` and `AI_TRACE_RECORD` as the fact sources. Add a
read-only formatter that merges them into bounded `AI_AGENT_OBSERVABILITY_ENTRY`
records for UI, tests, and future export.

Recommendation: Approach C. It gives locality and leverage: runtime and activity
records remain the source of truth, while the UI consumes one small, stable
interface.

## Data Contract

Add a UI-facing derived record:

```cpp
enum class AI_AGENT_OBSERVABILITY_KIND
{
    UserInput,
    ModelInput,
    ModelToolCall,
    ToolResult,
    ModelOutput,
    Suggestion,
    System
};

struct AI_AGENT_OBSERVABILITY_ENTRY
{
    uint64_t                     m_Sequence = 0;
    uint64_t                     m_RequestId = 0;
    wxString                     m_ToolCallId;
    AI_AGENT_OBSERVABILITY_KIND  m_Kind = AI_AGENT_OBSERVABILITY_KIND::System;
    AI_EDITOR_KIND               m_EditorKind = AI_EDITOR_KIND::Unknown;
    wxString                     m_Title;
    wxString                     m_Summary;
    wxString                     m_DetailsJson;
    bool                         m_Allowed = false;
    bool                         m_Executed = false;
    wxString                     m_ErrorCode;
};
```

Rules:

- `m_Sequence` is the display order. Activity sequence is used when present;
  model input/output entries are placed around the matching request ID.
- `m_DetailsJson` is redacted and bounded. It may include prompt text, context
  JSON summaries, tool arguments, tool results, suggestion operation JSON, and
  validation messages.
- Visual payloads include metadata such as source, MIME type, dimensions, byte
  size, and whether pixels exist. Raw image data is not copied into
  `m_DetailsJson` by default.
- Provider secrets, HTTP headers, and environment variable values are never
  copied into entries.

## Formatter Interface

Add a formatter module in common AI code:

```cpp
class AI_AGENT_OBSERVABILITY_LOG
{
public:
    std::vector<AI_AGENT_OBSERVABILITY_ENTRY> Build(
            const std::vector<AI_TRACE_RECORD>& aTraces,
            const std::vector<AI_ACTIVITY_RECORD>& aActivity,
            const std::vector<AI_SUGGESTION_RECORD>& aSuggestions,
            size_t aLimit = 128 ) const;
};
```

The formatter is read-only and stateless. It can be unit-tested with fake
traces, activity records, and suggestions.

## Panel Model Integration

`AI_AGENT_PANEL_MODEL` should expose:

```cpp
std::vector<AI_AGENT_OBSERVABILITY_ENTRY> ObservabilityEntries(
        size_t aLimit = 128 ) const;
```

The model builds entries from:

- `m_Runtime.TraceRecords()`,
- `m_ActivityLog.Records()`,
- `m_SuggestionOrchestrator->Records()` when available.

This keeps the Agent pane independent from runtime internals while still making
the model input/tool-call/output path visible.

## Agent Pane UI

The Agent pane should add a compact debug surface without turning the whole pane
into a developer console.

Recommended UI:

- Keep the existing chat and suggestion controls.
- Add a small segmented control or tabs: `Chat`, `Preview`, `Log`.
- The `Log` view renders chronological cards:
  - `Input`: prompt/context summary.
  - `Tool`: tool name, arguments, allowed/executed badges.
  - `Result`: result status, error code, result JSON preview.
  - `Output`: assistant response summary.
  - `Suggestion`: preview/accept/reject/expired state.
- Each card supports expand/collapse for details JSON.
- Use bounded text controls or list rows; do not render giant base64 payloads.

## Background Agent Integration

The same formatter must support background Agent events. Background suggestions
should appear as `Suggestion` entries even when no chat message was sent.

For high-frequency activity:

- Do not log every mouse move.
- Log debounced or material state transitions: tool activation, preview
  candidate created, preview displayed, candidate accepted/rejected/expired,
  tool call executed/denied.

## Privacy And Safety

- Never record `OPENAI_API_KEY`, `KISURF_AI_API_KEY`, authorization headers, or
  provider bearer tokens.
- Do not persist the log in project files in this slice.
- Keep the default buffer bounded.
- Treat raw model input as debug data: show enough to diagnose context/tool
  behavior, but avoid copying raw image data.
- Board object labels and coordinates are allowed because they are already part
  of the model-facing context, but future persistence must get a separate
  retention/privacy spec.

## Error Handling

- Malformed `m_DetailsJson` input should not break the log view. The formatter
  should either preserve it as escaped text or replace it with a small error
  object.
- Missing trace records should still allow activity records to render.
- Missing activity records should still allow model input/output trace cards to
  render.
- Tool denials should be visible with error code and message.

## Testing Requirements

Use test-first development for:

- Formatter emits model input and model output entries from one trace.
- Formatter merges model tool call and tool result activity into the same
  request turn.
- Formatter includes suggestion lifecycle entries without requiring chat.
- Formatter bounds output to the requested limit.
- Formatter redacts secret-like fields and omits raw visual base64 payloads.
- `AI_AGENT_PANEL_MODEL::ObservabilityEntries()` exposes formatter output.
- Agent pane log view can render empty, single-turn, and tool-call entries
  without crashing.

## Acceptance Criteria

- The Agent pane has a visible log/debug view.
- A chat request with a tool call shows model input, tool call, tool result, and
  model output.
- A background next-action suggestion shows a suggestion log entry.
- Existing chat and preview flows continue to work.
- Unit tests prove the formatter is bounded, redacted, and derived from runtime
  trace/activity sources.
- No secrets or raw image payloads are stored in the observability entries.

## Spec Self-Review

- Placeholder scan: no placeholder sections or unresolved TODOs remain.
- Consistency check: the design builds on existing `AI_ACTIVITY_LOG` and
  `AI_TRACE_RECORD` instead of replacing them.
- Scope check: this is a single implementation slice. Persistent logs, IPC
  streaming, and full background scheduling remain future specs.
- Ambiguity check: source-of-truth, redaction, visual payload handling, and UI
  acceptance criteria are explicit.
