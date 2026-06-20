# AI Operation-Only Preview Controls Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make operation-only preview suggestions reviewable while keeping Accept disabled without edit objects.

**Architecture:** Add per-suggestion capability checks to the suggestion orchestrator, expose them through `AI_AGENT_PANEL_MODEL`, and have `AI_AGENT_PANEL` use those checks for semantic state and button enablement.  `BeginPreview()` will support recognized operation-only preview JSON without applying edits.

**Tech Stack:** C++17, wxWidgets strings, Boost unit tests.

---

Date: 2026-06-19
Spec: `docs/superpowers/specs/2026-06-19-ai-operation-only-preview-controls-design.md`

## Files

- Modify: `include/kisurf/ai/ai_suggestion_orchestrator.h`
  - Add `CanPreview()` and `CanAccept()`.
- Modify: `common/kisurf/ai/ai_suggestion_orchestrator.cpp`
  - Implement operation-only preview support.
- Modify: `include/kisurf/ai/ai_agent_panel_model.h`
  - Add model wrappers.
- Modify: `common/kisurf/ai/ai_agent_panel_model.cpp`
  - Implement model wrappers.
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`
  - Use wrappers for semantic state and button enablement.
- Modify: `qa/tests/common/test_ai_suggestion_orchestrator.cpp`
  - Add operation-only preview tests.
- Modify: `qa/tests/common/test_ai_agent_panel_model.cpp`
  - Add model capability wrapper tests.

## Task 1: Add Failing Orchestrator Tests

- [x] Add a helper that creates a suggestion with:

```cpp
record.m_Title = wxS( "Preview panel fill" );
record.m_Kind = AI_SUGGESTION_KIND::Preview;
record.m_ArgumentsJson = wxS(
        "{\"operation\":\"panel_fill_column_preview\","
        "\"panel_id\":\"board_setup.clearance\","
        "\"table_id\":\"clearance.rules\","
        "\"column_id\":\"clearance\","
        "\"value\":\"0.20 mm\","
        "\"target_row_ids\":[\"row.power\"]}" );
record.m_PreviewObjects.clear();
record.m_EditObjects.clear();
```

- [x] Add `OperationOnlySuggestionCanBeginPreview`:
  - update/add the suggestion
  - assert `CanPreview(id)` is true
  - assert `BeginPreview(id, preview)` is true
  - assert status becomes `Previewing`
  - assert the preview adapter only received `begin`
- [x] Add `OperationOnlySuggestionCannotAcceptWithoutEditObjects`:
  - assert `CanAccept(id)` is false
  - assert `Accept(id, edit)` is false
  - assert status remains active
- [x] Run `AiSuggestionOrchestrator` and expect FAIL because the new capability methods do not exist.

## Task 2: Implement Orchestrator Capabilities

- [x] Include `kisurf/ai/ai_suggestion_operations.h` in `ai_suggestion_orchestrator.cpp`.
- [x] Add helper `hasPreviewableOperation()` using `ParseAiSuggestionOperation()`.
- [x] Add `recordCanPreview()` and `recordCanAccept()` helpers.
- [x] Declare and implement:

```cpp
bool CanPreview( uint64_t aSuggestionId ) const;
bool CanAccept( uint64_t aSuggestionId ) const;
```

- [x] Update `BeginPreview()` to use `recordCanPreview()`, call `Begin()`, show objects only when present, and mark `Previewing`.
- [x] Keep `Accept()` requiring edit objects through `recordCanAccept()`.
- [x] Run `AiSuggestionOrchestrator` and expect PASS.

## Task 3: Add Failing Model Wrapper Tests

- [x] Add a model test that stores an operation-only panel-fill suggestion.
- [x] Assert `model.CanPreviewSuggestion(id)` is true.
- [x] Assert `model.CanAcceptSuggestion(id)` is false.
- [x] Assert `model.PreviewSuggestion(id, preview)` is true.
- [x] Assert `model.AcceptSuggestion(id, edit)` is false.
- [x] Run `AiAgentPanelModel` and expect FAIL because wrappers do not exist.

## Task 4: Implement Model And Panel UI Wiring

- [x] Add `CanPreviewSuggestion()` and `CanAcceptSuggestion()` to `AI_AGENT_PANEL_MODEL`.
- [x] Update `AI_AGENT_PANEL::SemanticUiTree()` to set:

```cpp
view.m_CanPreviewSuggestion = active && m_PreviewSuggestionHandler
                              && m_Model->CanPreviewSuggestion( *active );
view.m_CanAcceptSuggestion = active && m_AcceptSuggestionHandler
                             && m_Model->CanAcceptSuggestion( *active );
```

- [x] Update `AI_AGENT_PANEL::updateModeControls()` with the same capability checks.
- [x] Run `AiAgentPanelModel` and expect PASS.

## Task 5: Verify And Commit

- [x] Build `qa_common`.
- [x] Run:

```powershell
$root = Get-Location; $env:PATH = (Join-Path $root 'out\build\x64-release\api') + ';' + (Join-Path $root 'out\build\x64-release\common') + ';' + (Join-Path $root 'out\build\x64-release\common\gal') + ';' + (Join-Path $root 'out\build\x64-release\qa\tests\common') + ';' + $env:PATH; .\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiSuggestionOrchestrator,AiAgentPanelModel,AiAgentPanelSemantic --log_level=test_suite
```

- [x] Build `pcbnew`.
- [x] Build `eeschema`.
- [x] Run `git diff --check`.
- [x] Run dynamic secret scan without echoing any key.
- [x] Stage only files touched by this slice.
- [x] Commit with:

```text
feat: support operation-only AI previews
```

## Handoff Notes

- Operation-only preview is a review state, not an edit.
- Accept remains disabled until a real edit adapter supplies edit objects.
- Do not stage unrelated dirty files.
