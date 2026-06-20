# Agent Observability Readable Log Design

## Purpose

KiSurf already records structured Agent observability entries for model input,
model output, tool calls, tool results, suggestions, and user/editor activity.
The current Agent panel Log tab still reads like a thin event list: it shows the
title, summary, tool call id, and allow/execute flags, but it does not surface
the model request context or model output/tool-call counts that engineers need
while debugging Agent behavior.

This slice keeps the existing logging data model and makes the Log tab text
useful for day-to-day debugging. It is a presentation-layer improvement over
`AI_AGENT_OBSERVABILITY_ENTRY`, not a replacement for the runtime trace store.

## Requirements

1. Preserve the existing observability sources:
   - `AI_RUNTIME` trace records;
   - `AI_ACTIVITY_LOG` records;
   - `AI_SUGGESTION_ORCHESTRATOR` records.
2. Keep `AI_AGENT_OBSERVABILITY_LOG` as the single formatter that builds
   structured `AI_AGENT_OBSERVABILITY_ENTRY` records.
3. Enhance `AiAgentObservabilityEntryText()` so the Agent panel Log tab
   includes concise, human-readable details from `m_DetailsJson`.
4. For `ModelInput` entries, the text must show:
   - request id;
   - editor;
   - selected object count;
   - visible object count;
   - anchor count;
   - panel-state count;
   - tool-state kind;
   - visual source and dimensions when present;
   - tool-result count.
5. For `ModelOutput` entries, the text must show:
   - request id;
   - response body length;
   - tool-call count;
   - cancellation flag.
6. For `ModelToolCall` and `ToolResult` entries, the text must continue to show
   tool call id and allow/execute status. It may include details only when they
   are short enough to keep the log readable.
7. Invalid or legacy `m_DetailsJson` must not break logging. The formatter must
   gracefully fall back to the existing text.
8. The readable formatter must not expose raw visual data, API keys, or
   long sensitive payloads.
9. The feature must be unit-tested before production code changes.
10. After implementation, run targeted Agent panel tests and the broader common
    `Ai*` test slice, then build `pcbnew`.
11. Attempt a Computer Use smoke test after compile. If Computer Use app
    approval still blocks PCB Editor capture, record the exact blocker and do
    not claim that GUI click verification succeeded.

## Architecture

The data flow remains unchanged:

`AI_AGENT_PANEL_MODEL::ObservabilityEntries()` builds structured entries from
runtime traces, activity records, and suggestions. `AI_AGENT_PANEL::RefreshLog()`
renders those entries through `AiAgentObservabilityEntryText()`.

This slice only expands `AiAgentObservabilityEntryText()`. The formatter parses
entry details JSON with `nlohmann::json::parse(..., nullptr, false)`, extracts
known safe summary fields, and appends a compact `details:` line. Unknown or
malformed JSON is ignored.

## Log Shape

Example `ModelInput` text:

```text
#1 Model input
route selected net
details: request=42 editor=pcb selected=2 visible=1 anchors=3 panels=1 tool_state=routing_track visual=pcbnew.canvas 1280x720 tool_results=0
```

Example `ModelOutput` text:

```text
#2 Routing assistant
I can preview the next segment.
details: request=42 body_length=31 tool_calls=1 cancelled=false
```

## Non-Goals

- No provider API changes.
- No new persistent log file.
- No UI widget redesign in wxFormBuilder.
- No additional network calls.
- No changes to action execution policy.

## Verification

1. Add failing tests in `qa/tests/common/test_ai_agent_panel.cpp` for readable
   model input and model output details.
2. Verify the tests fail before implementation.
3. Implement the formatter changes.
4. Verify targeted `AiAgentPanel` tests pass.
5. Verify broad `Ai*` common tests pass.
6. Build `pcbnew`.
7. Attempt Computer Use GUI smoke; record blocker if app approval times out.

## Self-Review

- Scope check: this is one presentation-layer slice over existing structured
  observability records.
- Data-flow check: no new source of truth is introduced.
- Safety check: raw visual payloads and credentials remain excluded from the
  readable log.
- Ambiguity check: malformed JSON fallback is explicitly non-fatal.
