# AI Panel Table Suggestions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add deterministic next-action suggestions for focused semantic panel tables.

**Architecture:** Add a new `AI_PANEL_TABLE_NEXT_ACTION_PROVIDER` to the next-action provider module.  It reads `AI_PANEL_STATE_RECORD::m_StateJson`, detects a focused populated cell with multiple empty same-column targets, and emits a reviewable suggestion with panel context metadata.  The default panel model registers this provider before model-backed suggestions.

**Tech Stack:** C++17, wxWidgets strings, nlohmann::json, Boost unit tests.

---

Date: 2026-06-19
Spec: `docs/superpowers/specs/2026-06-19-ai-panel-table-suggestions-design.md`

## Files

- Modify: `include/kisurf/ai/ai_next_action_provider.h`
  - Declare `AI_PANEL_TABLE_NEXT_ACTION_PROVIDER`.
- Modify: `common/kisurf/ai/ai_next_action_provider.cpp`
  - Parse panel table state and build suggestions.
- Modify: `common/kisurf/ai/ai_agent_panel_model.cpp`
  - Register the panel table provider in the default controller.
- Modify: `qa/tests/common/test_ai_next_action_provider.cpp`
  - Add panel table provider unit tests.
- Modify: `qa/tests/common/test_ai_agent_panel_model.cpp`
  - Assert the default model can emit panel table suggestions.

## Task 1: Add Failing Tests

- [x] **Step 1: Provider emits column-fill suggestion**

Add a focused panel table state with a populated focused cell and two empty cells in the same column.  Assert:

```cpp
suggestion->m_Title.Contains( wxS( "Fill" ) );
suggestion->m_ContextKind == "panel";
suggestion->m_ContextDetailsJson.Contains( "panel_table_fill" );
suggestion->m_ArgumentsJson.Contains( "panel_fill_column_preview" );
suggestion->m_ArgumentsJson.Contains( "target_row_ids" );
suggestion->m_EditObjects.empty();
```

- [x] **Step 2: Provider rejects low-confidence states**

Assert no suggestion for:

- non-panel dynamic context
- malformed `m_StateJson`
- missing focused cell
- empty focused value
- fewer than two empty target rows

- [x] **Step 3: Default model includes provider**

Create an `AI_AGENT_PANEL_MODEL` with default providers and a panel table context.  Assert `UpdateSuggestions()` stores a panel fill suggestion.

- [x] **Step 4: Run tests to verify RED**

Run:

```powershell
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiNextActionProvider,AiAgentPanelModel --log_level=test_suite
```

Expected: FAIL because the provider class and default registration do not exist.

## Task 2: Implement Provider

- [x] Declare `AI_PANEL_TABLE_NEXT_ACTION_PROVIDER`.
- [x] Add panel table parsing helpers.
- [x] Detect focused table/cell.
- [x] Resolve source value and empty target row IDs.
- [x] Build operation JSON.
- [x] Populate context metadata using `AiDynamicContextKind()` and `AiDynamicContextDetailsJson()`.
- [x] Leave preview/edit object vectors empty.

## Task 3: Wire Default Controller

- [x] Register `AI_PANEL_TABLE_NEXT_ACTION_PROVIDER` after routing/layout deterministic providers and before `AI_AGENT_SUGGESTION_PROVIDER`.

## Task 4: Verify

- [x] Build `qa_common`.
- [x] Run `AiNextActionProvider`.
- [x] Run `AiAgentPanelModel`.
- [x] Run `AiAgentObservabilityLog`.
- [x] Run `AiSemanticToolCallHandler`.
- [x] Build `pcbnew`.
- [x] Build `eeschema`.
- [x] Run `git diff --check`.
- [x] Run dynamic secret scan without echoing any key.

## Task 5: Commit

- [x] Inspect `git diff`.
- [x] Stage only files touched by this slice.
- [x] Commit with:

```text
feat: add AI panel table suggestions
```

## Handoff Notes

- This provider suggests, but does not apply, panel edits.
- A future panel UI adapter should consume `panel_fill_column_preview`.
- Do not stage unrelated dirty files.
