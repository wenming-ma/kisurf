# AI Suggestion Context Metadata Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add optional context metadata to AI suggestions and expose it in suggestion providers, observability logs, and Agent panel summaries.

**Architecture:** Extend `AI_SUGGESTION_RECORD` with two optional strings.  Populate them near suggestion creation using shared dynamic-context projection helpers.  Serialize them only in log details when present and render a compact marker in the panel summary.

**Tech Stack:** C++17, wxWidgets strings, nlohmann::json, Boost unit tests.

---

Date: 2026-06-19
Spec: `docs/superpowers/specs/2026-06-19-ai-suggestion-context-metadata-design.md`

## Files

- Modify: `include/kisurf/ai/ai_types.h`
  - Add `m_ContextKind` and `m_ContextDetailsJson` to `AI_SUGGESTION_RECORD`.
- Modify: `common/kisurf/ai/ai_next_action_provider.cpp`
  - Populate metadata for via-pattern and routing-segment suggestions.
- Modify: `common/kisurf/ai/ai_agent_suggestion_provider.cpp`
  - Populate trigger-derived metadata for model and deterministic suggestions.
- Modify: `common/kisurf/ai/ai_semantic_tool_call_handler.cpp`
  - Populate metadata for semantic preview suggestions.
- Modify: `common/kisurf/ai/ai_observability_log.cpp`
  - Include metadata in suggestion details when present.
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`
  - Add context kind marker to suggestion summaries when present.
- Modify tests under `qa/tests/common`.

## Task 1: Add Failing Tests

- [x] **Step 1: Next-action provider metadata**

Extend `AiNextActionProvider` tests:

```cpp
BOOST_CHECK_EQUAL( suggestion->m_ContextKind, wxString( wxS( "layout" ) ) );
BOOST_CHECK( suggestion->m_ContextDetailsJson.Contains( wxS( "via_pattern" ) ) );
```

and:

```cpp
BOOST_CHECK_EQUAL( suggestion->m_ContextKind, wxString( wxS( "routing" ) ) );
BOOST_CHECK( suggestion->m_ContextDetailsJson.Contains( wxS( "route_segment" ) ) );
```

- [x] **Step 2: Agent suggestion provider metadata**

Assert deterministic and parsed model suggestions carry trigger-derived context metadata.

- [x] **Step 3: Observability metadata**

Extend suggestion log tests to parse `m_DetailsJson` and assert `context_kind` and `context_details.reason`.

- [x] **Step 4: Panel summary metadata**

Extend `AgentPanelFormatsModeTitlesAndSuggestionSummary` to assert a summary with `m_ContextKind = "routing"` contains `[routing]`.

- [x] **Step 5: Run focused tests to verify RED**

Run:

```powershell
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiNextActionProvider,AiAgentSuggestionProvider,AiAgentObservabilityLog,AiAgentPanel --log_level=test_suite
```

Expected: FAIL because suggestion records do not yet carry context metadata.

## Task 2: Implement Record Fields

- [x] Add `wxString m_ContextKind`.
- [x] Add `wxString m_ContextDetailsJson`.
- [x] Keep all validity checks unchanged.

## Task 3: Populate Metadata

- [x] Add compact JSON helper(s) in providers where needed.
- [x] Set via pattern metadata to `layout` with reason `via_pattern`.
- [x] Set routing segment metadata to `routing` with reason `route_segment`.
- [x] Set semantic route-to-anchor metadata to `routing` with reason `route_to_anchor`.
- [x] Set semantic copper-zone metadata to `layout` with reason `copper_zone`.
- [x] Set semantic move-selected metadata from trigger context with reason `move_selected`.
- [x] Set model/deterministic suggestion metadata from trigger context with reason `model_suggestion` or `deterministic_selection`.

## Task 4: Expose Metadata

- [x] Add optional `context_kind` and parsed `context_details` to observability suggestion details.
- [x] Fall back to string `context_details_json` only if parsing fails.
- [x] Add `[context_kind]` marker to `AiAgentSuggestionSummary()` when present.

## Task 5: Verify

- [x] Build `qa_common`.
- [x] Run `AiNextActionProvider`.
- [x] Run `AiAgentSuggestionProvider`.
- [x] Run `AiAgentObservabilityLog`.
- [x] Run `AiAgentPanel`.
- [x] Run `AiSemanticToolCallHandler`.
- [x] Run `AiNativeTypes`.
- [x] Run `AiNativeProvider`.
- [x] Build `pcbnew`.
- [x] Build `eeschema`.
- [x] Run `git diff --check`.
- [x] Run dynamic secret scan without echoing any key.

## Task 6: Commit

- [x] Inspect `git diff`.
- [x] Stage only files touched by this slice.
- [x] Commit with:

```text
feat: add AI suggestion context metadata
```

## Handoff Notes

- Metadata is optional and must not alter suggestion execution semantics.
- Context details are debug/provenance data; `m_ContextKind` is the UI/filter key.
- Do not stage unrelated dirty files.
