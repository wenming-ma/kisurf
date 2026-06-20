# AI Panel Fill Semantic Tool Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a chat-accessible semantic tool that creates grounded preview suggestions for panel table column fills.

**Architecture:** Extend the common suggestion operation parser with a `PanelFillColumnPreview` operation.  Add `kisurf_preview_panel_fill_column` to the semantic tool handler with current-panel validation, then declare the tool in the OpenAI-compatible provider schema.

**Tech Stack:** C++17, wxWidgets strings, nlohmann::json, Boost unit tests.

---

Date: 2026-06-19
Spec: `docs/superpowers/specs/2026-06-19-ai-panel-fill-semantic-tool-design.md`

## Files

- Modify: `include/kisurf/ai/ai_suggestion_operations.h`
  - Add `PanelFillColumnPreview` kind, parsed panel fields, and `IsPanelFillColumnPreview()`.
- Modify: `common/kisurf/ai/ai_suggestion_operations.cpp`
  - Parse `panel_fill_column_preview`.
- Modify: `common/kisurf/ai/ai_semantic_tool_call_handler.cpp`
  - Support `kisurf_preview_panel_fill_column` and validate against current panel state.
- Modify: `common/kisurf/ai/ai_provider.cpp`
  - Add OpenAI-compatible function schema for the new tool.
- Modify: `qa/tests/common/test_ai_suggestion_operations.cpp`
  - Add parser acceptance and rejection tests.
- Modify: `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`
  - Add handler acceptance and denial tests.
- Modify: `qa/tests/common/test_ai_provider.cpp`
  - Assert the provider declares the new tool schema.

## Task 1: Add Failing Parser Tests

- [x] **Step 1: Add panel operation parse test**

Add this test to `qa/tests/common/test_ai_suggestion_operations.cpp`:

```cpp
BOOST_AUTO_TEST_CASE( ParsesPanelFillColumnPreviewOperation )
{
    const wxString payload = wxS(
            "{\"operation\":\"panel_fill_column_preview\","
            "\"panel_id\":\"board_setup.clearance\","
            "\"table_id\":\"clearance.rules\","
            "\"column_id\":\"clearance\","
            "\"value\":\"0.20 mm\","
            "\"target_row_ids\":[\"row.power\",\"row.signal\"]}" );

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( payload );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsPanelFillColumnPreview() );
    BOOST_CHECK_EQUAL( operation->m_PanelId,
                       wxString( wxS( "board_setup.clearance" ) ) );
    BOOST_CHECK_EQUAL( operation->m_TableId,
                       wxString( wxS( "clearance.rules" ) ) );
    BOOST_CHECK_EQUAL( operation->m_ColumnId, wxString( wxS( "clearance" ) ) );
    BOOST_CHECK_EQUAL( operation->m_Value, wxString( wxS( "0.20 mm" ) ) );
    BOOST_REQUIRE_EQUAL( operation->m_TargetRowIds.size(), 2 );
    BOOST_CHECK_EQUAL( operation->m_TargetRowIds[1],
                       wxString( wxS( "row.signal" ) ) );
}
```

- [x] **Step 2: Add malformed payload checks**

Extend `RejectsMalformedOrUnsupportedPayloads` with:

```cpp
BOOST_CHECK( !ParseAiSuggestionOperation(
                       wxS( "{\"operation\":\"panel_fill_column_preview\","
                            "\"panel_id\":\"board_setup.clearance\","
                            "\"table_id\":\"clearance.rules\","
                            "\"column_id\":\"clearance\","
                            "\"value\":\"0.20 mm\","
                            "\"target_row_ids\":[]}" ) )
                      .has_value() );
BOOST_CHECK( !ParseAiSuggestionOperation(
                       wxS( "{\"operation\":\"panel_fill_column_preview\","
                            "\"panel_id\":\"\","
                            "\"table_id\":\"clearance.rules\","
                            "\"column_id\":\"clearance\","
                            "\"value\":\"0.20 mm\","
                            "\"target_row_ids\":[\"row.power\"]}" ) )
                      .has_value() );
BOOST_CHECK( !ParseAiSuggestionOperation(
                       wxS( "{\"operation\":\"panel_fill_column_preview\","
                            "\"panel_id\":\"board_setup.clearance\","
                            "\"table_id\":\"clearance.rules\","
                            "\"column_id\":\"clearance\","
                            "\"value\":\"0.20 mm\","
                            "\"target_row_ids\":[\"\"]}" ) )
                      .has_value() );
```

- [x] **Step 3: Verify RED**

Run:

```powershell
$root = Get-Location; $env:PATH = (Join-Path $root 'out\build\x64-release\api') + ';' + (Join-Path $root 'out\build\x64-release\common') + ';' + (Join-Path $root 'out\build\x64-release\common\gal') + ';' + (Join-Path $root 'out\build\x64-release\qa\tests\common') + ';' + $env:PATH; .\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiSuggestionOperations --log_level=test_suite
```

Expected: FAIL because `IsPanelFillColumnPreview` and parsed panel fields do not exist.

## Task 2: Implement Operation Parser

- [x] Add these members to `AI_SUGGESTION_OPERATION`:

```cpp
wxString              m_PanelId;
wxString              m_TableId;
wxString              m_ColumnId;
wxString              m_Value;
std::vector<wxString> m_TargetRowIds;
```

- [x] Add `PanelFillColumnPreview` to `AI_SUGGESTION_OPERATION_KIND`.
- [x] Add `bool IsPanelFillColumnPreview() const;`.
- [x] Add parser helper logic that requires non-empty strings and at least one non-empty target row ID.
- [x] Return `PanelFillColumnPreview` when `operation == "panel_fill_column_preview"`.
- [x] Run `AiSuggestionOperations` and expect PASS.

## Task 3: Add Failing Semantic Handler Tests

- [x] **Step 1: Add a panel-table request helper**

Add a helper in `qa/tests/common/test_ai_semantic_tool_call_handler.cpp` that creates an `AI_PROVIDER_REQUEST` with:

- `m_EditorKind = AI_EDITOR_KIND::Pcb`
- one focused `AI_PANEL_STATE_RECORD` with id `board_setup.clearance`
- `m_StateJson` containing table `clearance.rules`, column `clearance`, and rows `row.power` plus `row.signal`

- [x] **Step 2: Add accepted tool-call test**

Assert `kisurf_preview_panel_fill_column`:

- returns `m_Allowed == true`
- returns `status == "preview_ready"`
- stores one suggestion
- stores `m_ContextKind == "panel"`
- stores `m_ContextDetailsJson` containing `panel_fill_column`
- stores operation JSON containing `panel_fill_column_preview`
- leaves preview/edit objects empty

- [x] **Step 3: Add denial tests**

Assert the tool denies and stores no suggestion for:

- unknown `panel_id`
- unknown `table_id`
- unknown `column_id`
- unknown target row ID
- malformed JSON arguments

- [x] **Step 4: Verify RED**

Run:

```powershell
$root = Get-Location; $env:PATH = (Join-Path $root 'out\build\x64-release\api') + ';' + (Join-Path $root 'out\build\x64-release\common') + ';' + (Join-Path $root 'out\build\x64-release\common\gal') + ';' + (Join-Path $root 'out\build\x64-release\qa\tests\common') + ';' + $env:PATH; .\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiSemanticToolCallHandler --log_level=test_suite
```

Expected: FAIL because the tool is not supported.

## Task 4: Implement Semantic Tool Handler

- [x] Add `kisurf_preview_panel_fill_column` to `supportedTool()`.
- [x] Add panel state validation helpers:
  - find panel by id
  - parse panel `m_StateJson`
  - find table by id
  - validate column id when columns are present
  - validate target row ids against rows
- [x] Add `panelFillColumnSuggestion()` that creates a preview suggestion with panel context metadata.
- [x] Add a build branch in `buildSuggestion()` that sets `operation = "panel_fill_column_preview"`, validates `ParseAiSuggestionOperation()`, validates current panel state, then returns the suggestion.
- [x] Run `AiSemanticToolCallHandler` and expect PASS.

## Task 5: Add Provider Tool Schema

- [x] Add `panelFillColumnToolParameters()` in `common/kisurf/ai/ai_provider.cpp`.
- [x] Add `functionTool( "kisurf_preview_panel_fill_column", ... )` to the `tools` array.
- [x] Update `OpenAiProviderDeclaresKiSurfTools` to expect 10 tools and assert:
  - tool name exists
  - required fields include `panel_id`, `table_id`, `column_id`, `value`, `target_row_ids`
  - `additionalProperties` is false
- [x] Run `AiNativeProvider` and expect PASS.

## Task 6: Verify And Commit

- [x] Build `qa_common`.
- [x] Run:

```powershell
$root = Get-Location; $env:PATH = (Join-Path $root 'out\build\x64-release\api') + ';' + (Join-Path $root 'out\build\x64-release\common') + ';' + (Join-Path $root 'out\build\x64-release\common\gal') + ';' + (Join-Path $root 'out\build\x64-release\qa\tests\common') + ';' + $env:PATH; .\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiSuggestionOperations,AiSemanticToolCallHandler,AiNativeProvider,AiAgentObservabilityLog --log_level=test_suite
```

- [x] Build `pcbnew`.
- [x] Build `eeschema`.
- [x] Run `git diff --check`.
- [x] Run dynamic secret scan without echoing any key.
- [x] Stage only files touched by this slice.
- [x] Commit with:

```text
feat: add AI panel fill semantic tool
```

## Handoff Notes

- This tool creates a suggestion but still does not apply panel edits.
- A future panel adapter should consume `panel_fill_column_preview`.
- Do not stage unrelated dirty files.
