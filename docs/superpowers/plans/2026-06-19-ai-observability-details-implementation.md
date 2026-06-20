# AI Observability Details Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Agent observability entries carry stable, parseable model input
and output details for debugging context, panel state, anchors, visual metadata,
tool results, and handled tool calls.

**Architecture:** Keep the existing entry kinds and summaries. Replace only the
trace-derived details JSON in `ai_observability_log.cpp` with typed request,
context, and response summary helpers, while preserving redaction and bounded
metadata.

**Tech Stack:** KiSurf common C++20, nlohmann JSON via `json_common.h`,
wxString, Boost.Test `qa_common`.

---

## File Structure

- Modify: `common/kisurf/ai/ai_observability_log.cpp`
  - Replace the current `contextSummaryJson()` payload.
  - Add editor/tool-state/version helpers if needed.
  - Add model-output details JSON with handled tool call summaries.
- Modify: `qa/tests/common/test_ai_observability_log.cpp`
  - Add RED tests for typed model-input details.
  - Add RED tests for model-output tool call summaries.
  - Update no-pixel/redaction test to parse JSON instead of string scanning.
- Modify this plan with RED/GREEN/verification evidence.

## Task 1: Red Tests For Typed Model Input Details

**Files:**
- Modify: `qa/tests/common/test_ai_observability_log.cpp`

- [x] **Step 1: Add typed input details test**

Add a test that builds an `AI_TRACE_RECORD` with:

- request ID `42`
- PCB editor kind
- user text
- context summary
- context version `doc=3;sel=2;view=9`
- two selected objects
- one visible object
- one action
- one recent activity record
- one anchor
- one panel state
- one tool result
- visual metadata with a data URI

Parse `entries.at(0).m_DetailsJson` and verify:

- `details["request_id"] == 42`
- `details["editor"] == "pcb"`
- `details["context"]["selected_count"] == 2`
- `details["context"]["visible_count"] == 1`
- `details["context"]["action_count"] == 1`
- `details["context"]["recent_activity_count"] == 1`
- `details["context"]["anchor_count"] == 1`
- `details["context"]["panel_state_count"] == 1`
- `details["context"]["visual"]["has_pixels"] == true`
- no `data_uri` property exists
- `details["tool_results_count"] == 1`

- [x] **Step 2: Run test to verify RED**

Build and run:

```powershell
cmd.exe /S /C '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build out\build\x64-release --target qa_common --config Release'
.\qa_common.exe --run_test=AiAgentObservabilityLog
```

Expected: test fails because current details JSON stores serialized context
under `editor` and does not include the new typed fields.

Actual: `qa_common` built, then
`qa_common.exe --run_test=AiAgentObservabilityLog` failed in
`ModelInputDetailsExposeTypedContextSummary` because `request_id` was null in
the old details payload.

## Task 2: Red Tests For Model Output Details

**Files:**
- Modify: `qa/tests/common/test_ai_observability_log.cpp`

- [x] **Step 1: Add output tool-call summary test**

Add a test with one trace whose response has one handled tool call:

```cpp
AI_TOOL_CALL_RECORD call;
call.m_ToolCallId = wxS( "call_1" );
call.m_ToolName = wxS( "kisurf_get_workspace_view" );
call.m_Allowed = true;
call.m_Executed = false;
call.m_Message = wxS( "context returned" );
trace.m_Response.m_ToolCalls.push_back( call );
```

Parse the `ModelOutput` details and verify:

- `request_id`
- `title`
- `body_length`
- `tool_call_count == 1`
- `cancelled == false`
- `tool_calls[0].id == "call_1"`
- `tool_calls[0].name == "kisurf_get_workspace_view"`
- `tool_calls[0].allowed == true`

- [x] **Step 2: Run test to verify RED**

Run `qa_common.exe --run_test=AiAgentObservabilityLog`.

Expected: test fails because output details are currently empty.

Actual: the same RED run failed in
`ModelOutputDetailsExposeHandledToolCallSummary` because
`ModelOutput.m_DetailsJson` was empty and could not be parsed as JSON.

## Task 3: Implement Typed Details

**Files:**
- Modify: `common/kisurf/ai/ai_observability_log.cpp`

- [x] **Step 1: Add editor/version/tool-state helpers**

Add local helper functions:

- `editorKindJsonName(AI_EDITOR_KIND)`
- `toolStateKindJsonName(AI_TOOL_STATE_KIND)`
- `versionJson(const AI_CONTEXT_VERSION&)`

- [x] **Step 2: Replace input details JSON**

Change `appendTraceEntries()` so `ModelInput.m_DetailsJson` is:

```cpp
dumpJson( modelInputDetailsJson( aTrace.m_Request ) )
```

where `modelInputDetailsJson()` returns the shape from the spec.

Actual: `ModelInput.m_DetailsJson` now uses `modelInputDetailsJson(aTrace)`
and includes request metadata, redacted user text, typed context counts,
visual metadata without pixels, and tool result count.

- [x] **Step 3: Add output details JSON**

Change `appendTraceEntries()` so `ModelOutput.m_DetailsJson` is:

```cpp
dumpJson( modelOutputDetailsJson( aTrace ) )
```

where `modelOutputDetailsJson()` includes request ID, title, body length,
tool call count, cancelled flag, and a bounded array of handled tool call
summaries.

Actual: `ModelOutput.m_DetailsJson` now uses `modelOutputDetailsJson(aTrace)`
and includes handled tool call summaries without duplicating arguments or
result payloads.

## Task 4: Verify And Commit

**Files:**
- Modify this plan with actual status and verification.

- [x] **Step 1: Build and targeted tests**

Run:

```powershell
cmd.exe /S /C '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build out\build\x64-release --target qa_common --config Release'
```

Then run:

```powershell
.\qa_common.exe --run_test=AiAgentObservabilityLog,AiAgentPanelModel,AiAgentPanel
```

Expected: tests exit 0. The known schema-file warning may still appear.

Actual:

- `cmake --build out\build\x64-release --target qa_common --config Release`
  exited 0.
- `qa_common.exe --run_test=AiAgentObservabilityLog` ran 7 test cases and
  exited 0.
- `qa_common.exe --run_test=AiAgentObservabilityLog,AiAgentPanelModel,AiAgentPanel`
  ran 39 test cases and exited 0.
- The known missing schema-file warning appeared during test startup.

- [x] **Step 2: Build editor targets**

Run:

```powershell
cmd.exe /S /C '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build out\build\x64-release --target pcbnew --config Release && cmake --build out\build\x64-release --target eeschema --config Release'
```

Expected: both targets build.

Actual: `pcbnew` and `eeschema` both built successfully.

- [x] **Step 3: Static and secret checks**

Run:

```powershell
git diff --check
rg -n "sk-[0-9A-Za-z_-]{20,}|OPENAI_API_KEY\s*=|KISURF_AI_API_KEY\s*=" common\kisurf\ai\ai_observability_log.cpp qa\tests\common\test_ai_observability_log.cpp docs\superpowers\specs\2026-06-19-ai-observability-details-design.md docs\superpowers\plans\2026-06-19-ai-observability-details-implementation.md
```

Expected: whitespace check exits 0; secret scan has no matches.

Actual:

- `git diff --check` exited 0.
- Secret scan exited with no matches.

- [x] **Step 4: Commit**

Stage only files touched by this plan. Do not stage unrelated
`qa/tests/pcbnew/test_module.cpp`.

```powershell
git add common/kisurf/ai/ai_observability_log.cpp qa/tests/common/test_ai_observability_log.cpp docs/superpowers/specs/2026-06-19-ai-observability-details-design.md docs/superpowers/plans/2026-06-19-ai-observability-details-implementation.md
git commit -m "feat: enrich agent observability details"
```

Observed: committed as `237bb66e feat: enrich agent observability details`.

## Self-Review

- Spec coverage: tasks cover typed input details, output details, redaction,
  no visual pixel payload, verification, and commit.
- Open-item scan: No deferred implementation markers remain.
- Scope check: No UI, provider, IPC, or visual capture changes are included.
