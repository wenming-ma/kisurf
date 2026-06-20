# Workspace View Panels Section Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a first-class `panels` section to `kisurf_get_workspace_view`.

**Architecture:** Keep panel state in `AI_PANEL_STATE_RECORD`; add a workspace-view parser branch and a local JSON projector for filtered panel records. The provider schema exposes the same options so models can discover the section.

**Tech Stack:** C++20, nlohmann::json, Boost.Test, KiSurf common AI tool-call layer.

---

## File Structure

- `common/kisurf/ai/ai_semantic_tool_call_handler.cpp`: parse `panels` workspace options and emit `workspace_view.panels`.
- `common/kisurf/ai/ai_provider.cpp`: add `panels` to the function schema.
- `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`: add red/green coverage for the section.
- `docs/superpowers/specs/2026-06-19-workspace-view-panels-section-design.md`: design contract.

### Task 1: RED Tests

**Files:**
- Modify: `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`

- [ ] **Step 1: Add panels-only workspace test**

Add `WorkspaceViewToolReturnsPanelsAsFirstClassSection`:

```cpp
AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
        requestWithActivityTimeline(),
        toolCall( wxS( "kisurf_get_workspace_view" ),
                  wxS( "{\"views\":[\"panels\"],\"panels\":{\"max_panels\":1}}" ) ) );
```

Assert that `workspace_view` contains `summary` and `panels`, omits `context`, `visual`, and `activity`, reports total `panel_state_count` of 2, reports `matched_panel_count` of 2, returns one bounded record, and includes the parsed `state`.

- [ ] **Step 2: Add filter/omit-state test**

Add `WorkspaceViewToolFiltersPanelsAndCanOmitState`:

```cpp
AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
        requestWithActivityTimeline(),
        toolCall( wxS( "kisurf_get_workspace_view" ),
                  wxS( "{\"views\":[\"panels\"],"
                       "\"panels\":{\"panel_id\":\"properties.selection\","
                       "\"include_state\":false}}" ) ) );
```

Assert that only `properties.selection` is returned and that neither `state` nor `state_raw` exists. Then call with `{"focused_only":true}` and assert that only the panel with `focused_control_id` is returned.

- [ ] **Step 3: Add malformed panels options test**

Extend `WorkspaceViewToolRejectsMalformedArguments` with non-object `panels`, bad `max_panels`, bad `panel_id`, bad `focused_only`, and bad `include_state`.

- [ ] **Step 4: Verify RED**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
```

Expected: compile or assertion failure because `panels` is not a supported workspace view.

### Task 2: GREEN Implementation

**Files:**
- Modify: `common/kisurf/ai/ai_semantic_tool_call_handler.cpp`
- Modify: `common/kisurf/ai/ai_provider.cpp`

- [ ] **Step 1: Add panels options**

Add:

```cpp
struct PANEL_STATE_TOOL_OPTIONS
{
    size_t   m_MaxPanels = 16;
    wxString m_PanelId;
    bool     m_FocusedOnly = false;
    bool     m_IncludeState = true;
};
```

Parse `max_panels`, `panel_id`, `focused_only`, and `include_state`.

- [ ] **Step 2: Add JSON projection**

Emit:

```json
{
  "panel_state_count": 2,
  "matched_panel_count": 1,
  "records": []
}
```

Each record includes id, title, focused control, selected text, summary, and optional parsed `state` or `state_raw`.

- [ ] **Step 3: Wire workspace view**

Add `m_Panels` to `WORKSPACE_VIEW_TOOL_OPTIONS`, allow `panels` in `views`, accept nested `panels` options, include it in `included_views`, and emit `workspace_view["panels"]` when requested.

- [ ] **Step 4: Update provider schema**

Add `panelStateToolParameters()`, add `panels` to the `views` enum, and add nested `panels` property to `workspaceViewToolParameters()`.

- [ ] **Step 5: Verify GREEN**

Run targeted:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiSemanticToolCallHandler --report_level=short"
```

Expected: semantic tool-call tests pass.

### Task 3: Verification and Commit

**Files:**
- Verify all files touched in Tasks 1 and 2.

- [ ] **Step 1: Run full AI common suite**

Run `qa_common.exe --run_test=Ai* --report_level=short`.

- [ ] **Step 2: Build `pcbnew`**

Run `cmake --build out/build/x64-release --target pcbnew -- -j 2`.

- [ ] **Step 3: Launch GUI and inspect**

Launch `.\tools\run_from_build.ps1 -BuildDir .\out\build\x64-release -App pcbnew` and inspect with Computer Use for blocking popups. If Computer Use cannot capture the window, report the exact helper limitation and process evidence.

- [ ] **Step 4: Hygiene and commit**

Run `git diff --check`, scan staged content for `sk-` style secrets, stage only this slice's files, and commit:

```powershell
git commit -m "ai: expose workspace panels section"
```

## Self-Review

- Spec coverage: each response option has a corresponding test or implementation step.
- Placeholder scan: no marker text or postponed behavior remains.
- Type consistency: `PANEL_STATE_TOOL_OPTIONS`, `m_Panels`, and `workspace_view.panels` are used consistently.
