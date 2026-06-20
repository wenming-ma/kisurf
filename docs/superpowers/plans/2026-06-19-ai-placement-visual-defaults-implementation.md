# AI Placement Visual Defaults Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Automatically highlight footprint-placement candidate anchors in model-facing visual reads.

**Architecture:** Reuse the existing visual-frame render-directive pipeline. Add placement-specific default selection next to the existing routing default selection, and call the combined defaulting path from both visual-frame and workspace-view tools.

**Tech Stack:** C++, nlohmann JSON, Boost.Test common QA executable, Windows x64 Release CMake build.

---

## File Structure

- Modify: `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`
  - Adds placement visual default tests.
- Modify: `common/kisurf/ai/ai_semantic_tool_call_handler.cpp`
  - Adds placement-anchor default directive logic and wires it into visual tools.
- Modify: `README.md`
  - Documents placement visual reads highlighting semantic placement anchors.

## Task 1: Placement Visual Default Tests

**Files:**
- Modify: `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`

- [ ] **Step 1: Add placement request helper**

Add after `requestWithUnifiedContext()`:

```cpp
AI_PROVIDER_REQUEST requestWithPlacementContext()
{
    AI_PROVIDER_REQUEST request = requestWithUnifiedContext();
    request.m_ContextSnapshot.m_ToolState.m_Kind =
            AI_TOOL_STATE_KIND::PlacingFootprint;
    request.m_ContextSnapshot.m_ToolState.m_ActiveActionName =
            wxS( "pcbnew.placeFootprint" );
    request.m_ContextSnapshot.m_ToolState.m_ModeContextJson =
            wxS( "{\"mode\":\"placing_footprint\","
                 "\"cursor\":{\"x\":1000,\"y\":2000}}" );
    request.m_ContextSnapshot.m_Anchors.clear();
    request.m_ContextSnapshot.m_Anchors.push_back(
            positionedAnchor( wxS( "tool.placement.cursor" ),
                              AI_CONTEXT_ANCHOR_KIND::PlacementCandidate,
                              VECTOR2I( 1000, 2000 ) ) );
    request.m_ContextSnapshot.m_Anchors.push_back(
            positionedAnchor( wxS( "tool.placement.grid.east" ),
                              AI_CONTEXT_ANCHOR_KIND::PlacementCandidate,
                              VECTOR2I( 2000, 2000 ) ) );
    request.m_ContextSnapshot.m_Anchors.push_back(
            positionedAnchor( wxS( "pcb.pad.target" ),
                              AI_CONTEXT_ANCHOR_KIND::RouteTarget,
                              VECTOR2I( 5000, 6000 ) ) );
    return request;
}
```

- [ ] **Step 2: Add `kisurf_get_visual_frame` placement default test**

Add near the existing visual-frame tests:

```cpp
BOOST_AUTO_TEST_CASE( VisualFrameToolHighlightsPlacementAnchorsByDefault )
{
    AI_SEMANTIC_TOOL_CALL_HANDLER handler( nullptr );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithPlacementContext(),
            toolCall( wxS( "kisurf_get_visual_frame" ), wxS( "{}" ) ) );

    BOOST_CHECK( result.m_Allowed );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    const nlohmann::json& directives = payload["visual"]["render_directives"];

    BOOST_CHECK( !directives.contains( "focus_layer" ) );
    BOOST_CHECK( !directives.contains( "focus_net" ) );
    BOOST_CHECK( !directives.contains( "dim_unfocused_layers" ) );
    BOOST_REQUIRE( directives.contains( "highlight_anchor_ids" ) );
    BOOST_REQUIRE_EQUAL( directives["highlight_anchor_ids"].size(), 2 );
    BOOST_CHECK_EQUAL( directives["highlight_anchor_ids"][0].get<std::string>(),
                       "tool.placement.cursor" );
    BOOST_CHECK_EQUAL( directives["highlight_anchor_ids"][1].get<std::string>(),
                       "tool.placement.grid.east" );
}
```

- [ ] **Step 3: Add explicit-override test**

```cpp
BOOST_AUTO_TEST_CASE( VisualFrameToolPreservesExplicitPlacementHighlights )
{
    AI_SEMANTIC_TOOL_CALL_HANDLER handler( nullptr );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithPlacementContext(),
            toolCall( wxS( "kisurf_get_visual_frame" ),
                      wxS( "{\"highlight_anchor_ids\":[\"pcb.pad.target\"]}" ) ) );

    BOOST_CHECK( result.m_Allowed );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    const nlohmann::json& directives = payload["visual"]["render_directives"];
    BOOST_REQUIRE_EQUAL( directives["highlight_anchor_ids"].size(), 1 );
    BOOST_CHECK_EQUAL( directives["highlight_anchor_ids"][0].get<std::string>(),
                       "pcb.pad.target" );
}
```

- [ ] **Step 4: Add workspace-view placement default test**

```cpp
BOOST_AUTO_TEST_CASE( WorkspaceViewToolHighlightsPlacementAnchorsByDefault )
{
    AI_SEMANTIC_TOOL_CALL_HANDLER handler( nullptr );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithPlacementContext(),
            toolCall( wxS( "kisurf_get_workspace_view" ),
                      wxS( "{\"views\":[\"visual\"]}" ) ) );

    BOOST_CHECK( result.m_Allowed );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    const nlohmann::json& directives =
            payload["workspace_view"]["visual"]["render_directives"];
    BOOST_REQUIRE_EQUAL( directives["highlight_anchor_ids"].size(), 2 );
    BOOST_CHECK_EQUAL( directives["highlight_anchor_ids"][0].get<std::string>(),
                       "tool.placement.cursor" );
    BOOST_CHECK_EQUAL( directives["highlight_anchor_ids"][1].get<std::string>(),
                       "tool.placement.grid.east" );
}
```

- [ ] **Step 5: Build and verify red**

Run:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build .\out\build\x64-release --target qa_common --config Release"
$dllDirs = Get-ChildItem .\out\build\x64-release -Recurse -Filter *.dll | Select-Object -ExpandProperty DirectoryName -Unique
$env:PATH = ($dllDirs -join ';') + ';' + $env:PATH
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiSemanticToolCallHandler --log_level=test_suite --report_level=detailed
```

Expected: build succeeds; the new placement visual default tests fail because no placement defaulting exists yet.

## Task 2: Placement Visual Default Implementation

**Files:**
- Modify: `common/kisurf/ai/ai_semantic_tool_call_handler.cpp`

- [ ] **Step 1: Add placement tool-state predicate**

Add near `routingModeContextJson()`:

```cpp
bool placementToolStateActive( const AI_PROVIDER_REQUEST& aRequest )
{
    const AI_TOOL_STATE_SNAPSHOT& toolState = aRequest.m_ContextSnapshot.m_ToolState;

    return effectiveEditorKind( aRequest ) == AI_EDITOR_KIND::Pcb
           && toolState.m_EditorKind == AI_EDITOR_KIND::Pcb
           && toolState.m_Kind == AI_TOOL_STATE_KIND::PlacingFootprint;
}
```

- [ ] **Step 2: Add placement anchor filter**

Add after `isRoutingVisualAnchorKind()`:

```cpp
bool isPlacementVisualAnchorKind( AI_CONTEXT_ANCHOR_KIND aKind )
{
    return aKind == AI_CONTEXT_ANCHOR_KIND::PlacementCandidate;
}
```

- [ ] **Step 3: Add shared helper for default anchor ids**

Add after `isPlacementVisualAnchorKind()`:

```cpp
void appendDefaultHighlightAnchors(
        const AI_CONTEXT_SNAPSHOT& aSnapshot,
        bool ( *aKindPredicate )( AI_CONTEXT_ANCHOR_KIND ),
        std::vector<wxString>& aHighlightAnchorIds )
{
    for( const AI_CONTEXT_ANCHOR& anchor : aSnapshot.m_Anchors )
    {
        if( aHighlightAnchorIds.size() >= 32 )
            break;

        if( anchor.m_HasPosition && aKindPredicate( anchor.m_Kind ) )
            aHighlightAnchorIds.push_back( anchor.m_Id );
    }
}
```

- [ ] **Step 4: Refactor routing defaults to use shared helper**

Replace the routing default anchor loop with:

```cpp
if( !aOptions.m_HasExplicitHighlightAnchorIds )
{
    appendDefaultHighlightAnchors( aRequest.m_ContextSnapshot,
                                   isRoutingVisualAnchorKind,
                                   aOptions.m_HighlightAnchorIds );
}
```

- [ ] **Step 5: Add placement defaults and combined dispatcher**

Add after `applyRoutingVisualDefaults()`:

```cpp
void applyPlacementVisualDefaults( const AI_PROVIDER_REQUEST& aRequest,
                                   VISUAL_FRAME_TOOL_OPTIONS& aOptions )
{
    if( !placementToolStateActive( aRequest ) )
        return;

    if( !aOptions.m_HasExplicitHighlightAnchorIds )
    {
        appendDefaultHighlightAnchors( aRequest.m_ContextSnapshot,
                                       isPlacementVisualAnchorKind,
                                       aOptions.m_HighlightAnchorIds );
    }
}

void applyVisualDefaults( const AI_PROVIDER_REQUEST& aRequest,
                          VISUAL_FRAME_TOOL_OPTIONS& aOptions )
{
    applyRoutingVisualDefaults( aRequest, aOptions );
    applyPlacementVisualDefaults( aRequest, aOptions );
}
```

- [ ] **Step 6: Use combined defaulting path**

Replace both calls to `applyRoutingVisualDefaults()` with:

```cpp
applyVisualDefaults( aRequest, *options );
```

and for workspace-view nested visual options:

```cpp
applyVisualDefaults( aRequest, options->m_VisualOptions );
```

- [ ] **Step 7: Run target tests and verify green**

Run the same `AiSemanticToolCallHandler` command. Expected: all tests pass.

## Task 3: README and Regression Verification

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update capability list**

Add under the placement anchor bullet:

```markdown
- Visual reads in active footprint-placement contexts automatically highlight
  placement candidate anchors, so the model sees layout choices without
  repeating visual directives.
```

- [ ] **Step 2: Run broader tests**

Run:

```powershell
$dllDirs = Get-ChildItem .\out\build\x64-release -Recurse -Filter *.dll | Select-Object -ExpandProperty DirectoryName -Unique
$env:PATH = ($dllDirs -join ';') + ';' + $env:PATH
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiContextAnchorProvider,AiSemanticToolCallHandler --report_level=short
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=Ai* --report_level=short
```

Expected: exit code 0 for both commands.

- [ ] **Step 3: Build editor targets**

Run:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build .\out\build\x64-release --target pcbnew --config Release"
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build .\out\build\x64-release --target eeschema --config Release"
```

Expected: both builds exit 0.

- [ ] **Step 4: Launch PCB editor and perform desktop smoke check**

Use Computer Use to launch `out\build\x64-release\pcbnew\pcbnew.exe`, wait for
the main window, and inspect the visible UI. Confirm:

- No system-error, missing-DLL, assertion, crash, debugger, exception, or failure
  dialog is blocking startup.
- The PCB editor main window is targetable.
- The AI menu or Agent entry is visible/reachable through the UI surface.

If startup opens a project prompt or first-run modal, record the modal title and
use safe dismissal only when it does not change project data.

- [ ] **Step 5: Inspect desktop for hidden compile/runtime popups**

Use Computer Use after the final build to list visible windows and check for
system-error, missing-DLL, assertion, crash, debugger, exception, or failure
dialogs. Expected: no suspicious window.

- [ ] **Step 6: Run secret scan and diff review**

Run:

```powershell
$prefix = 'sk' + '-'
$pattern = $prefix + '[A-Za-z0-9_-]{20,}|OPENAI_API_KEY=.*' + $prefix
rg -n $pattern common docs include qa pcbnew eeschema README.md
git diff --check -- docs/superpowers/specs/2026-06-19-ai-placement-visual-defaults-design.md docs/superpowers/plans/2026-06-19-ai-placement-visual-defaults-implementation.md common/kisurf/ai/ai_semantic_tool_call_handler.cpp qa/tests/common/test_ai_semantic_tool_call_handler.cpp README.md
git status --short
```

Expected: secret scan has no output and exit code 1; diff check has no whitespace
errors; unrelated dirty files remain unstaged.

- [ ] **Step 7: Commit**

Run:

```powershell
git add docs/superpowers/specs/2026-06-19-ai-placement-visual-defaults-design.md docs/superpowers/plans/2026-06-19-ai-placement-visual-defaults-implementation.md common/kisurf/ai/ai_semantic_tool_call_handler.cpp qa/tests/common/test_ai_semantic_tool_call_handler.cpp README.md
git commit -m "feat: highlight placement anchors in visual reads"
```

## Self-Review

- Spec coverage: each requirement maps to a test or implementation task.
- Placeholder scan: no unfinished marker remains.
- Type consistency: tool-state enum, anchor kind, field names, and tool names
  match current code.
- Verification coverage: includes red/green tests, broader AI tests, editor
  target builds, actual PCB editor launch smoke, secret scan, and Computer Use
  popup inspection.
