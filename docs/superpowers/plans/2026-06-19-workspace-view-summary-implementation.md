# Workspace View Summary Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a compact `workspace_view.summary` header to `kisurf_get_workspace_view`.

**Architecture:** Keep existing workspace-view sections unchanged. Build the summary directly from `AI_CONTEXT_SNAPSHOT` and parsed `WORKSPACE_VIEW_TOOL_OPTIONS`, then include it in every workspace-view result.

**Tech Stack:** C++20, nlohmann JSON, wxString, Boost unit tests, existing KiSurf semantic tool handler.

---

## File Structure

- Modify: `common/kisurf/ai/ai_semantic_tool_call_handler.cpp`
  - Add `workspaceViewSummaryJson()`.
  - Insert `workspaceView["summary"]` in `workspaceViewResult()`.
- Modify: `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`
  - Add a red test for summary metadata.
- Create: `docs/superpowers/specs/2026-06-19-workspace-view-summary-design.md`
- Create: `docs/superpowers/plans/2026-06-19-workspace-view-summary-implementation.md`

## Task 1: Red Test

**Files:**
- Modify: `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`

- [ ] **Step 1: Add summary test after `WorkspaceViewToolReturnsAllSectionsByDefault`**

Add:

```cpp
BOOST_AUTO_TEST_CASE( WorkspaceViewToolReturnsSummaryHeader )
{
    AI_SEMANTIC_TOOL_CALL_HANDLER handler( nullptr );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithActivityTimeline(),
            toolCall( wxS( "kisurf_get_workspace_view" ),
                      wxS( "{\"views\":[\"visual\"]}" ) ) );

    BOOST_CHECK( result.m_Allowed );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    const nlohmann::json& view = payload["workspace_view"];

    BOOST_REQUIRE( view.contains( "summary" ) );
    const nlohmann::json& summary = view["summary"];

    BOOST_REQUIRE_EQUAL( summary["included_views"].size(), 1 );
    BOOST_CHECK_EQUAL( summary["included_views"][0].get<std::string>(), "visual" );
    BOOST_CHECK_EQUAL( summary["editor"].get<std::string>(), "pcb" );
    BOOST_CHECK_EQUAL( summary["dynamic_context_kind"].get<std::string>(), "routing" );
    BOOST_CHECK_EQUAL( summary["dynamic_context_source"].get<std::string>(), "tool_state" );
    BOOST_CHECK_EQUAL( summary["tool_state_kind"].get<std::string>(), "routing_track" );
    BOOST_CHECK_EQUAL( summary["selected_object_count"].get<int>(), 2 );
    BOOST_CHECK_EQUAL( summary["visible_object_count"].get<int>(), 3 );
    BOOST_CHECK_EQUAL( summary["anchor_count"].get<int>(), 2 );
    BOOST_CHECK_EQUAL( summary["panel_state_count"].get<int>(), 2 );
    BOOST_CHECK_EQUAL( summary["recent_activity_count"].get<int>(), 4 );
    BOOST_CHECK_EQUAL( summary["visual_source"].get<std::string>(), "pcbnew.canvas" );
    BOOST_CHECK( summary["visual_has_pixels"].get<bool>() );
    BOOST_CHECK( !summary.contains( "data_uri" ) );
}
```

- [ ] **Step 2: Verify RED**

Run:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build .\out\build\x64-release --target qa_common --config Release"
$dllDirs = Get-ChildItem .\out\build\x64-release -Recurse -Filter *.dll | Select-Object -ExpandProperty DirectoryName -Unique
$env:PATH = ($dllDirs -join ';') + ';' + $env:PATH
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiSemanticToolCallHandler/WorkspaceViewToolReturnsSummaryHeader --report_level=short
```

Expected: test fails because `workspace_view.summary` is missing.

## Task 2: Implement Summary Header

**Files:**
- Modify: `common/kisurf/ai/ai_semantic_tool_call_handler.cpp`

- [ ] **Step 1: Add included-view helper**

After `parseWorkspaceViewToolOptions()`, add:

```cpp
nlohmann::json includedWorkspaceViewsJson( const WORKSPACE_VIEW_TOOL_OPTIONS& aOptions )
{
    nlohmann::json views = nlohmann::json::array();

    if( aOptions.m_Context )
        views.push_back( "context" );

    if( aOptions.m_Visual )
        views.push_back( "visual" );

    if( aOptions.m_Activity )
        views.push_back( "activity" );

    return views;
}
```

- [ ] **Step 2: Add summary helper**

Add:

```cpp
nlohmann::json workspaceViewSummaryJson(
        const AI_CONTEXT_SNAPSHOT& aSnapshot,
        const WORKSPACE_VIEW_TOOL_OPTIONS& aOptions )
{
    nlohmann::json dynamicContext =
            nlohmann::json::parse(
                    toUtf8String( AiDynamicContextDetailsJson(
                            aSnapshot, AiDynamicContextKind( aSnapshot ) ) ),
                    nullptr, false );

    nlohmann::json summary = {
        { "included_views", includedWorkspaceViewsJson( aOptions ) },
        { "editor", editorKindName( aSnapshot.m_EditorKind ) },
        { "dynamic_context_kind", toUtf8String( AiDynamicContextKind( aSnapshot ) ) },
        { "tool_state_kind", toUtf8String( AiToolStateKindName( aSnapshot.m_ToolState.m_Kind ) ) },
        { "selected_object_count", aSnapshot.m_SelectedObjects.size() },
        { "visible_object_count", aSnapshot.m_VisibleObjects.size() },
        { "anchor_count", aSnapshot.m_Anchors.size() },
        { "panel_state_count", aSnapshot.m_PanelStates.size() },
        { "recent_activity_count", aSnapshot.m_RecentActivity.size() },
        { "visual_source", toUtf8String( aSnapshot.m_Visual.m_Source ) },
        { "visual_has_pixels", aSnapshot.m_Visual.HasPixels() }
    };

    if( dynamicContext.is_object() && dynamicContext.contains( "source" )
        && dynamicContext["source"].is_string() )
    {
        summary["dynamic_context_source"] = dynamicContext["source"];
    }
    else
    {
        summary["dynamic_context_source"] = "unknown";
    }

    return summary;
}
```

Use existing helpers already visible in this file:
- `editorKindName()`
- `toUtf8String()`
- `AiDynamicContextKind()`
- `AiDynamicContextDetailsJson()`
- `AiToolStateKindName()`

- [ ] **Step 3: Insert summary into workspace view**

In `workspaceViewResult()`, immediately after:

```cpp
nlohmann::json workspaceView = nlohmann::json::object();
```

add:

```cpp
workspaceView["summary"] =
        workspaceViewSummaryJson( aRequest.m_ContextSnapshot, *options );
```

Do not modify existing context, visual, or activity insertion blocks.

## Task 3: Verification

**Files:**
- All touched files.

- [ ] **Step 1: Build `qa_common`**

Run:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build .\out\build\x64-release --target qa_common --config Release"
```

Expected: exit 0.

- [ ] **Step 2: Run targeted summary test**

Run:

```powershell
$dllDirs = Get-ChildItem .\out\build\x64-release -Recurse -Filter *.dll | Select-Object -ExpandProperty DirectoryName -Unique
$env:PATH = ($dllDirs -join ';') + ';' + $env:PATH
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiSemanticToolCallHandler/WorkspaceViewToolReturnsSummaryHeader --report_level=short
```

Expected: exit 0.

- [ ] **Step 3: Run semantic tool handler suite**

Run:

```powershell
$dllDirs = Get-ChildItem .\out\build\x64-release -Recurse -Filter *.dll | Select-Object -ExpandProperty DirectoryName -Unique
$env:PATH = ($dllDirs -join ';') + ';' + $env:PATH
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiSemanticToolCallHandler --report_level=short
```

Expected: exit 0.

- [ ] **Step 4: Run broad Agent tests**

Run:

```powershell
$dllDirs = Get-ChildItem .\out\build\x64-release -Recurse -Filter *.dll | Select-Object -ExpandProperty DirectoryName -Unique
$env:PATH = ($dllDirs -join ';') + ';' + $env:PATH
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=Ai* --report_level=short
```

Expected: exit 0.

- [ ] **Step 5: Build PCB Editor**

Run:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build .\out\build\x64-release --target pcbnew --config Release"
```

Expected: exit 0.

- [ ] **Step 6: Attempt GUI smoke**

Run:

```powershell
.\tools\run_from_build.ps1 -BuildDir .\out\build\x64-release -App pcbnew
```

Use Computer Use for screenshot/control inspection. If PCB Editor capture still
returns `Computer Use app approval timed out`, record that exact blocker.

- [ ] **Step 7: Static and secret checks**

Run:

```powershell
git diff --check -- common/kisurf/ai/ai_semantic_tool_call_handler.cpp qa/tests/common/test_ai_semantic_tool_call_handler.cpp docs/superpowers/specs/2026-06-19-workspace-view-summary-design.md docs/superpowers/plans/2026-06-19-workspace-view-summary-implementation.md
$prefix = 'sk' + '-'
$pattern = $prefix + '[A-Za-z0-9_-]{20,}|OPENAI_API_KEY=.*' + $prefix
rg -n $pattern common/kisurf/ai/ai_semantic_tool_call_handler.cpp qa/tests/common/test_ai_semantic_tool_call_handler.cpp docs/superpowers/specs/2026-06-19-workspace-view-summary-design.md docs/superpowers/plans/2026-06-19-workspace-view-summary-implementation.md
```

Expected: no whitespace errors and no secret matches.

- [ ] **Step 8: Commit**

Run:

```powershell
git add common/kisurf/ai/ai_semantic_tool_call_handler.cpp qa/tests/common/test_ai_semantic_tool_call_handler.cpp docs/superpowers/specs/2026-06-19-workspace-view-summary-design.md docs/superpowers/plans/2026-06-19-workspace-view-summary-implementation.md
git commit -m "feat: add workspace view summary"
```

Do not stage unrelated `qa/tests/pcbnew/test_module.cpp`.

## Self-Review

- Spec coverage: summary fields, included views, non-breaking behavior, and
  visual-payload safety are covered.
- Placeholder scan: no deferred tasks remain.
- Type consistency: helper names and fields match existing semantic tool
  handler types and test expectations.
