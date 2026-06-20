# AI Unified Context Anchors And Panel State Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add semantic anchors and live panel-state records to the shared AI context snapshot so chat, background Agent, and future IPC projections consume one bounded context interface.

**Architecture:** Extend existing common-layer AI types instead of creating a parallel context API. `AI_CONTEXT_SNAPSHOT` remains the model-facing carrier, while `AI_CONTEXT_INDEX` remains the editor-side incremental source for snapshots. The first implementation is common-layer only; PCB geometry anchor generation, panel walkers, visual overlays, and `go_to_anchor` tools remain follow-up work.

**Tech Stack:** C++17/C++20 in KiCad common AI modules, wxWidgets strings, nlohmann JSON through `json_common.h`, Boost unit tests, CMake/Ninja build.

---

## File Structure

- Modify `include/kisurf/ai/ai_types.h`
  - Add `AI_CONTEXT_ANCHOR_KIND`, `AI_CONTEXT_ANCHOR`, and `AI_PANEL_STATE_RECORD`.
  - Extend `AI_CONTEXT_SNAPSHOT` with `m_Anchors` and `m_PanelStates`.
  - Extend prompt/JSON serializer signatures with max anchor and max panel-state bounds.
- Modify `common/kisurf/ai/ai_types.cpp`
  - Add string conversion and validity helpers.
  - Serialize anchors and panel states into prompt text and structured JSON.
- Modify `include/kisurf/ai/ai_context_index.h`
  - Add accessors and setters for anchors and panel states.
- Modify `common/kisurf/ai/ai_context_index.cpp`
  - Sort anchor and panel-state records and include them in snapshots.
  - Bump view revision when anchors or panel states change.
- Modify `qa/tests/common/test_ai_types.cpp`
  - Add tests for anchor/panel validity and context serialization.
- Modify `qa/tests/common/test_ai_context_index.cpp`
  - Add tests for index sorting and view revision.
- Modify `docs/superpowers/plans/2026-06-18-ai-unified-context-anchors-panel-state-implementation.md`
  - Check off completed steps after verification.

## Task 1: Anchor And Panel-State Type Contracts

**Files:**
- Modify: `include/kisurf/ai/ai_types.h`
- Modify: `common/kisurf/ai/ai_types.cpp`
- Modify: `qa/tests/common/test_ai_types.cpp`

- [x] **Step 1: Write failing tests for anchor and panel record helpers**

Append these tests before `BOOST_AUTO_TEST_SUITE_END()` in `qa/tests/common/test_ai_types.cpp`:

```cpp
BOOST_AUTO_TEST_CASE( ContextAnchorValidityAndKindNames )
{
    AI_CONTEXT_ANCHOR anchor;
    BOOST_CHECK( !anchor.IsValid() );
    BOOST_CHECK_EQUAL( anchor.KindAsString(), wxString( wxS( "unknown" ) ) );

    anchor.m_Id = wxS( "route.candidate.1" );
    anchor.m_Kind = AI_CONTEXT_ANCHOR_KIND::FortyFiveIntersection;
    anchor.m_EditorKind = AI_EDITOR_KIND::Pcb;
    anchor.m_Label = wxS( "45-degree bend" );
    anchor.m_Position = VECTOR2I( 1000, 2000 );
    anchor.m_HasPosition = true;
    anchor.m_Layer = 0;
    anchor.m_Confidence = 0.75;

    BOOST_CHECK( anchor.IsValid() );
    BOOST_CHECK_EQUAL( anchor.KindAsString(),
                       wxString( wxS( "forty_five_intersection" ) ) );
}


BOOST_AUTO_TEST_CASE( PanelStateRecordDetectsVisibleState )
{
    AI_PANEL_STATE_RECORD panel;
    BOOST_CHECK( !panel.HasState() );

    panel.m_Id = wxS( "pcb.board_setup.clearance" );
    panel.m_Title = wxS( "Board Setup" );
    panel.m_FocusedControlId = wxS( "clearance.grid.row3.col2" );
    panel.m_FocusedControlLabel = wxS( "Minimum clearance" );
    panel.m_SelectedText = wxS( "0.20 mm" );
    panel.m_StateJson = wxS( "{\"row\":3,\"column\":\"clearance\"}" );

    BOOST_CHECK( panel.HasState() );
}
```

- [x] **Step 2: Run red**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
```

Expected: compile fails because `AI_CONTEXT_ANCHOR`, `AI_CONTEXT_ANCHOR_KIND`, and `AI_PANEL_STATE_RECORD` do not exist.

- [x] **Step 3: Add public type declarations**

Modify `include/kisurf/ai/ai_types.h` after `AI_AGENT_WORKSPACE_CONTEXT_STATE`:

```cpp
enum class AI_CONTEXT_ANCHOR_KIND
{
    Unknown,
    RouteStart,
    RouteTarget,
    RouteCandidate,
    OrthogonalBreakout,
    FortyFiveIntersection,
    PlacementCandidate,
    PatternContinuation,
    ShapeCorner,
    ZoneVertex,
    PanelCell,
    General
};

struct KICOMMON_API AI_CONTEXT_ANCHOR
{
    wxString               m_Id;
    AI_CONTEXT_ANCHOR_KIND m_Kind = AI_CONTEXT_ANCHOR_KIND::Unknown;
    AI_EDITOR_KIND         m_EditorKind = AI_EDITOR_KIND::Unknown;
    wxString               m_Label;
    wxString               m_Summary;
    VECTOR2I               m_Position = VECTOR2I( 0, 0 );
    bool                   m_HasPosition = false;
    int                    m_Layer = -1;
    wxString               m_DetailsJson;
    double                 m_Confidence = 0.0;

    bool IsValid() const;
    wxString KindAsString() const;
};

struct KICOMMON_API AI_PANEL_STATE_RECORD
{
    wxString m_Id;
    wxString m_Title;
    wxString m_FocusedControlId;
    wxString m_FocusedControlLabel;
    wxString m_SelectedText;
    wxString m_Summary;
    wxString m_StateJson;

    bool HasState() const;
};
```

- [x] **Step 4: Add helper implementations**

Modify `common/kisurf/ai/ai_types.cpp` inside the anonymous namespace:

```cpp
std::string contextAnchorKindJsonName( AI_CONTEXT_ANCHOR_KIND aKind )
{
    switch( aKind )
    {
    case AI_CONTEXT_ANCHOR_KIND::RouteStart: return "route_start";
    case AI_CONTEXT_ANCHOR_KIND::RouteTarget: return "route_target";
    case AI_CONTEXT_ANCHOR_KIND::RouteCandidate: return "route_candidate";
    case AI_CONTEXT_ANCHOR_KIND::OrthogonalBreakout: return "orthogonal_breakout";
    case AI_CONTEXT_ANCHOR_KIND::FortyFiveIntersection: return "forty_five_intersection";
    case AI_CONTEXT_ANCHOR_KIND::PlacementCandidate: return "placement_candidate";
    case AI_CONTEXT_ANCHOR_KIND::PatternContinuation: return "pattern_continuation";
    case AI_CONTEXT_ANCHOR_KIND::ShapeCorner: return "shape_corner";
    case AI_CONTEXT_ANCHOR_KIND::ZoneVertex: return "zone_vertex";
    case AI_CONTEXT_ANCHOR_KIND::PanelCell: return "panel_cell";
    case AI_CONTEXT_ANCHOR_KIND::General: return "general";
    case AI_CONTEXT_ANCHOR_KIND::Unknown: return "unknown";
    }

    return "unknown";
}
```

Add near the other type member implementations:

```cpp
bool AI_CONTEXT_ANCHOR::IsValid() const
{
    return !m_Id.IsEmpty()
           && ( m_Kind != AI_CONTEXT_ANCHOR_KIND::Unknown
                || !m_Label.IsEmpty()
                || !m_Summary.IsEmpty()
                || m_HasPosition
                || !m_DetailsJson.IsEmpty() );
}


wxString AI_CONTEXT_ANCHOR::KindAsString() const
{
    return wxString::FromUTF8( contextAnchorKindJsonName( m_Kind ).c_str() );
}


bool AI_PANEL_STATE_RECORD::HasState() const
{
    return !m_Id.IsEmpty()
           || !m_Title.IsEmpty()
           || !m_FocusedControlId.IsEmpty()
           || !m_FocusedControlLabel.IsEmpty()
           || !m_SelectedText.IsEmpty()
           || !m_Summary.IsEmpty()
           || !m_StateJson.IsEmpty();
}
```

- [x] **Step 5: Run green**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\common;$root\common\gal;$root\api;$env:PATH"
& "$root\qa\tests\common\qa_common.exe" --run_test=AiNativeTypes/ContextAnchorValidityAndKindNames,AiNativeTypes/PanelStateRecordDetectsVisibleState --log_level=test_suite
```

Expected: both new tests pass.

- [x] **Step 6: Commit**

```bash
git add include/kisurf/ai/ai_types.h common/kisurf/ai/ai_types.cpp qa/tests/common/test_ai_types.cpp
git commit -m "feat: add ai context anchor record types"
```

## Task 2: Context Snapshot Prompt And JSON Serialization

**Files:**
- Modify: `include/kisurf/ai/ai_types.h`
- Modify: `common/kisurf/ai/ai_types.cpp`
- Modify: `qa/tests/common/test_ai_types.cpp`

- [x] **Step 1: Write failing context serialization tests**

Append these tests before `BOOST_AUTO_TEST_SUITE_END()` in `qa/tests/common/test_ai_types.cpp`:

```cpp
BOOST_AUTO_TEST_CASE( ContextSnapshotFormatsAnchorsAndPanelState )
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;

    AI_CONTEXT_ANCHOR anchor;
    anchor.m_Id = wxS( "route.candidate.2" );
    anchor.m_Kind = AI_CONTEXT_ANCHOR_KIND::OrthogonalBreakout;
    anchor.m_Label = wxS( "Horizontal breakout" );
    anchor.m_Summary = wxS( "First horizontal segment from current cursor" );
    anchor.m_Position = VECTOR2I( 1200, 3400 );
    anchor.m_HasPosition = true;
    anchor.m_Layer = 0;
    anchor.m_DetailsJson = wxS( "{\"net\":\"GND\",\"width\":150000}" );
    anchor.m_Confidence = 0.9;
    snapshot.m_Anchors.push_back( anchor );

    AI_PANEL_STATE_RECORD panel;
    panel.m_Id = wxS( "pcb.board_setup.clearance" );
    panel.m_Title = wxS( "Board Setup" );
    panel.m_FocusedControlLabel = wxS( "Minimum clearance" );
    panel.m_SelectedText = wxS( "0.20 mm" );
    panel.m_StateJson = wxS( "{\"row\":3,\"column\":\"clearance\"}" );
    snapshot.m_PanelStates.push_back( panel );

    BOOST_CHECK( snapshot.HasContext() );

    const wxString prompt = snapshot.AsPromptText( 10, 10, 10, 10 );

    BOOST_CHECK( prompt.Contains( wxS( "semantic anchors: 1" ) ) );
    BOOST_CHECK( prompt.Contains( wxS( "route.candidate.2" ) ) );
    BOOST_CHECK( prompt.Contains( wxS( "orthogonal_breakout" ) ) );
    BOOST_CHECK( prompt.Contains( wxS( "x=1200 y=3400" ) ) );
    BOOST_CHECK( prompt.Contains( wxS( "panel states: 1" ) ) );
    BOOST_CHECK( prompt.Contains( wxS( "pcb.board_setup.clearance" ) ) );
    BOOST_CHECK( prompt.Contains( wxS( "Minimum clearance" ) ) );
}


BOOST_AUTO_TEST_CASE( ContextSnapshotJsonIncludesAnchorsAndPanelState )
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;

    AI_CONTEXT_ANCHOR anchor;
    anchor.m_Id = wxS( "route.target.pad" );
    anchor.m_Kind = AI_CONTEXT_ANCHOR_KIND::RouteTarget;
    anchor.m_Label = wxS( "U1.1 pad" );
    anchor.m_DetailsJson = wxS( "{\"pad\":\"U1.1\"}" );
    snapshot.m_Anchors.push_back( anchor );

    AI_CONTEXT_ANCHOR rawAnchor;
    rawAnchor.m_Id = wxS( "route.raw" );
    rawAnchor.m_Kind = AI_CONTEXT_ANCHOR_KIND::General;
    rawAnchor.m_DetailsJson = wxS( "not-json" );
    snapshot.m_Anchors.push_back( rawAnchor );

    AI_PANEL_STATE_RECORD panel;
    panel.m_Id = wxS( "pcb.panel" );
    panel.m_StateJson = wxS( "{\"focused\":\"cell\"}" );
    snapshot.m_PanelStates.push_back( panel );

    const wxString jsonText = snapshot.AsJsonText( 10, 10, 10, 10, 10 );
    nlohmann::json parsed = nlohmann::json::parse( jsonText.ToStdString() );
    const nlohmann::json& context = parsed["kisurf_context"];

    BOOST_CHECK_EQUAL( context["anchor_count"].get<int>(), 2 );
    BOOST_CHECK_EQUAL( context["anchors"].at( 0 )["id"].get<std::string>(),
                       "route.target.pad" );
    BOOST_CHECK_EQUAL( context["anchors"].at( 0 )["kind"].get<std::string>(),
                       "route_target" );
    BOOST_CHECK_EQUAL( context["anchors"].at( 0 )["details"]["pad"].get<std::string>(),
                       "U1.1" );
    BOOST_CHECK_EQUAL( context["anchors"].at( 1 )["details_raw"].get<std::string>(),
                       "not-json" );
    BOOST_CHECK_EQUAL( context["panel_state_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( context["panel_states"].at( 0 )["state"]["focused"].get<std::string>(),
                       "cell" );
}
```

- [x] **Step 2: Run red**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
```

Expected: compile fails because `AI_CONTEXT_SNAPSHOT` has no `m_Anchors` or `m_PanelStates`, and serializer signatures do not accept anchor/panel bounds.

- [x] **Step 3: Extend snapshot fields and serializer declarations**

Modify `AI_CONTEXT_SNAPSHOT` in `include/kisurf/ai/ai_types.h`:

```cpp
std::vector<AI_CONTEXT_ANCHOR>      m_Anchors;
std::vector<AI_PANEL_STATE_RECORD>  m_PanelStates;
```

Update method declarations:

```cpp
wxString AsPromptText( size_t aMaxObjects = 25, size_t aMaxActions = 25,
                       size_t aMaxAnchors = 25,
                       size_t aMaxPanelStates = 10 ) const;
wxString AsJsonText( size_t aMaxObjects = 64, size_t aMaxActions = 128,
                     size_t aMaxActivity = 64, size_t aMaxAnchors = 64,
                     size_t aMaxPanelStates = 16 ) const;
```

- [x] **Step 4: Add JSON helper functions**

Modify `common/kisurf/ai/ai_types.cpp` inside the anonymous namespace:

```cpp
nlohmann::json parseDetailsJsonOrRawText( const wxString& aText )
{
    if( aText.IsEmpty() )
        return nlohmann::json::object();

    try
    {
        return nlohmann::json::parse( toUtf8String( aText ) );
    }
    catch( const std::exception& )
    {
        return nlohmann::json{ { "raw", toUtf8String( aText ) } };
    }
}


nlohmann::json anchorsJson( const std::vector<AI_CONTEXT_ANCHOR>& aAnchors,
                            size_t aMaxAnchors )
{
    nlohmann::json anchors = nlohmann::json::array();
    const size_t count = std::min( aAnchors.size(), aMaxAnchors );

    for( size_t i = 0; i < count; ++i )
    {
        const AI_CONTEXT_ANCHOR& anchor = aAnchors[i];
        nlohmann::json record = {
            { "id", toUtf8String( anchor.m_Id ) },
            { "kind", contextAnchorKindJsonName( anchor.m_Kind ) },
            { "editor", editorKindJsonName( anchor.m_EditorKind ) },
            { "label", toUtf8String( anchor.m_Label ) },
            { "summary", toUtf8String( anchor.m_Summary ) },
            { "has_position", anchor.m_HasPosition },
            { "position", { { "x", anchor.m_Position.x },
                            { "y", anchor.m_Position.y } } },
            { "layer", anchor.m_Layer },
            { "confidence", anchor.m_Confidence }
        };

        nlohmann::json details = parseDetailsJsonOrRawText( anchor.m_DetailsJson );

        if( details.contains( "raw" ) )
            record["details_raw"] = details["raw"];
        else
            record["details"] = details;

        anchors.push_back( std::move( record ) );
    }

    return anchors;
}


nlohmann::json panelStatesJson( const std::vector<AI_PANEL_STATE_RECORD>& aPanels,
                                size_t aMaxPanelStates )
{
    nlohmann::json panels = nlohmann::json::array();
    const size_t count = std::min( aPanels.size(), aMaxPanelStates );

    for( size_t i = 0; i < count; ++i )
    {
        const AI_PANEL_STATE_RECORD& panel = aPanels[i];
        nlohmann::json record = {
            { "id", toUtf8String( panel.m_Id ) },
            { "title", toUtf8String( panel.m_Title ) },
            { "focused_control_id", toUtf8String( panel.m_FocusedControlId ) },
            { "focused_control_label", toUtf8String( panel.m_FocusedControlLabel ) },
            { "selected_text", toUtf8String( panel.m_SelectedText ) },
            { "summary", toUtf8String( panel.m_Summary ) }
        };

        nlohmann::json state = parseDetailsJsonOrRawText( panel.m_StateJson );

        if( state.contains( "raw" ) )
            record["state_raw"] = state["raw"];
        else
            record["state"] = state;

        panels.push_back( std::move( record ) );
    }

    return panels;
}
```

- [x] **Step 5: Extend prompt serialization**

Update `AI_CONTEXT_SNAPSHOT::HasContext()`:

```cpp
return m_Version.IsValid() || !m_VisibleObjects.empty() || !m_SelectedObjects.empty()
       || !m_Actions.empty() || !m_RecentActivity.empty() || !m_Summary.IsEmpty()
       || m_ToolState.HasToolState() || !m_Visual.m_Source.IsEmpty()
       || !m_Anchors.empty() || !m_PanelStates.empty();
```

Update the `AI_CONTEXT_SNAPSHOT::AsPromptText(...)` signature and append:

```cpp
if( !m_Anchors.empty() )
{
    text << wxS( "semantic anchors: " ) << m_Anchors.size() << wxS( "\n" );
    const size_t anchorCount = std::min( m_Anchors.size(), aMaxAnchors );

    for( size_t i = 0; i < anchorCount; ++i )
    {
        const AI_CONTEXT_ANCHOR& anchor = m_Anchors[i];

        text << wxS( "- " ) << anchor.m_Id << wxS( " | " )
             << anchor.KindAsString();

        if( !anchor.m_Label.IsEmpty() )
            text << wxS( " | " ) << anchor.m_Label;

        if( anchor.m_HasPosition )
            text << wxS( " | x=" ) << anchor.m_Position.x
                 << wxS( " y=" ) << anchor.m_Position.y;

        if( anchor.m_Layer >= 0 )
            text << wxS( " | layer=" ) << anchor.m_Layer;

        if( !anchor.m_Summary.IsEmpty() )
            text << wxS( " | " ) << anchor.m_Summary;

        text << wxS( "\n" );
    }

    if( m_Anchors.size() > anchorCount )
        text << wxS( "- ... " ) << ( m_Anchors.size() - anchorCount )
             << wxS( " more\n" );
}

if( !m_PanelStates.empty() )
{
    text << wxS( "panel states: " ) << m_PanelStates.size() << wxS( "\n" );
    const size_t panelCount = std::min( m_PanelStates.size(), aMaxPanelStates );

    for( size_t i = 0; i < panelCount; ++i )
    {
        const AI_PANEL_STATE_RECORD& panel = m_PanelStates[i];

        text << wxS( "- " ) << panel.m_Id;

        if( !panel.m_Title.IsEmpty() )
            text << wxS( " | " ) << panel.m_Title;

        if( !panel.m_FocusedControlLabel.IsEmpty() )
            text << wxS( " | focused=" ) << panel.m_FocusedControlLabel;

        if( !panel.m_SelectedText.IsEmpty() )
            text << wxS( " | selected=" ) << panel.m_SelectedText;

        if( !panel.m_Summary.IsEmpty() )
            text << wxS( " | " ) << panel.m_Summary;

        text << wxS( "\n" );
    }

    if( m_PanelStates.size() > panelCount )
        text << wxS( "- ... " ) << ( m_PanelStates.size() - panelCount )
             << wxS( " more\n" );
}
```

- [x] **Step 6: Extend JSON serialization**

Update the `AI_CONTEXT_SNAPSHOT::AsJsonText(...)` signature and add:

```cpp
context["anchor_count"] = m_Anchors.size();
context["anchors"] = anchorsJson( m_Anchors, aMaxAnchors );
context["panel_state_count"] = m_PanelStates.size();
context["panel_states"] = panelStatesJson( m_PanelStates, aMaxPanelStates );
```

- [x] **Step 7: Run green**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\common;$root\common\gal;$root\api;$env:PATH"
& "$root\qa\tests\common\qa_common.exe" --run_test=AiNativeTypes/ContextSnapshotFormatsAnchorsAndPanelState,AiNativeTypes/ContextSnapshotJsonIncludesAnchorsAndPanelState,AiNativeTypes/ContextSnapshotFormatsPromptText,AiNativeTypes/ContextSnapshotFormatsStructuredJson --log_level=test_suite
```

Expected: new and existing context serialization tests pass.

- [x] **Step 8: Commit**

```bash
git add include/kisurf/ai/ai_types.h common/kisurf/ai/ai_types.cpp qa/tests/common/test_ai_types.cpp
git commit -m "feat: serialize ai context anchors and panel state"
```

## Task 3: Context Index Support For Anchors And Panel State

**Files:**
- Modify: `include/kisurf/ai/ai_context_index.h`
- Modify: `common/kisurf/ai/ai_context_index.cpp`
- Modify: `qa/tests/common/test_ai_context_index.cpp`

- [x] **Step 1: Write failing index tests**

Append these tests before `BOOST_AUTO_TEST_SUITE_END()` in `qa/tests/common/test_ai_context_index.cpp`:

```cpp
BOOST_AUTO_TEST_CASE( AnchorsAreSortedAndCarriedInSnapshot )
{
    AI_CONTEXT_INDEX index( AI_EDITOR_KIND::Pcb );

    AI_CONTEXT_ANCHOR second;
    second.m_Id = wxS( "route.candidate.2" );
    second.m_Kind = AI_CONTEXT_ANCHOR_KIND::RouteCandidate;

    AI_CONTEXT_ANCHOR first;
    first.m_Id = wxS( "route.candidate.1" );
    first.m_Kind = AI_CONTEXT_ANCHOR_KIND::RouteCandidate;

    index.SetAnchors( { second, first } );

    BOOST_CHECK_EQUAL( index.Version().m_ViewRevision, 1 );
    BOOST_REQUIRE_EQUAL( index.Anchors().size(), 2 );
    BOOST_CHECK_EQUAL( index.Anchors().at( 0 ).m_Id,
                       wxString( wxS( "route.candidate.1" ) ) );

    AI_CONTEXT_SNAPSHOT snapshot = index.BuildSnapshot();

    BOOST_REQUIRE_EQUAL( snapshot.m_Anchors.size(), 2 );
    BOOST_CHECK_EQUAL( snapshot.m_Anchors.at( 1 ).m_Id,
                       wxString( wxS( "route.candidate.2" ) ) );
}


BOOST_AUTO_TEST_CASE( PanelStatesAreSortedAndCarriedInSnapshot )
{
    AI_CONTEXT_INDEX index( AI_EDITOR_KIND::Pcb );

    AI_PANEL_STATE_RECORD later;
    later.m_Id = wxS( "panel.z" );
    later.m_Title = wxS( "Z panel" );

    AI_PANEL_STATE_RECORD earlier;
    earlier.m_Id = wxS( "panel.a" );
    earlier.m_Title = wxS( "A panel" );

    index.SetPanelStates( { later, earlier } );

    BOOST_CHECK_EQUAL( index.Version().m_ViewRevision, 1 );
    BOOST_REQUIRE_EQUAL( index.PanelStates().size(), 2 );
    BOOST_CHECK_EQUAL( index.PanelStates().front().m_Id,
                       wxString( wxS( "panel.a" ) ) );

    AI_CONTEXT_SNAPSHOT snapshot = index.BuildSnapshot();

    BOOST_REQUIRE_EQUAL( snapshot.m_PanelStates.size(), 2 );
    BOOST_CHECK_EQUAL( snapshot.m_PanelStates.back().m_Id,
                       wxString( wxS( "panel.z" ) ) );
}
```

- [x] **Step 2: Run red**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
```

Expected: compile fails because `AI_CONTEXT_INDEX` has no anchor or panel-state accessors/setters.

- [x] **Step 3: Add index declarations**

Modify `include/kisurf/ai/ai_context_index.h`:

```cpp
const std::vector<AI_CONTEXT_ANCHOR>& Anchors() const { return m_Anchors; }
const std::vector<AI_PANEL_STATE_RECORD>& PanelStates() const { return m_PanelStates; }
void SetAnchors( std::vector<AI_CONTEXT_ANCHOR> aAnchors );
void SetPanelStates( std::vector<AI_PANEL_STATE_RECORD> aPanelStates );
```

Add private fields:

```cpp
std::vector<AI_CONTEXT_ANCHOR>     m_Anchors;
std::vector<AI_PANEL_STATE_RECORD> m_PanelStates;
```

- [x] **Step 4: Add sorting helpers**

Modify `common/kisurf/ai/ai_context_index.cpp` inside the anonymous namespace:

```cpp
bool anchorLess( const AI_CONTEXT_ANCHOR& aLeft, const AI_CONTEXT_ANCHOR& aRight )
{
    const int idCompare = compareString( aLeft.m_Id, aRight.m_Id );

    if( idCompare != 0 )
        return idCompare < 0;

    if( aLeft.m_Kind != aRight.m_Kind )
        return static_cast<int>( aLeft.m_Kind ) < static_cast<int>( aRight.m_Kind );

    return compareString( aLeft.m_Label, aRight.m_Label ) < 0;
}


bool panelStateLess( const AI_PANEL_STATE_RECORD& aLeft,
                     const AI_PANEL_STATE_RECORD& aRight )
{
    const int idCompare = compareString( aLeft.m_Id, aRight.m_Id );

    if( idCompare != 0 )
        return idCompare < 0;

    return compareString( aLeft.m_Title, aRight.m_Title ) < 0;
}
```

- [x] **Step 5: Carry fields into snapshots and setters**

Update `BuildSnapshot()`:

```cpp
snapshot.m_Anchors = m_Anchors;
snapshot.m_PanelStates = m_PanelStates;
```

Add:

```cpp
void AI_CONTEXT_INDEX::SetAnchors( std::vector<AI_CONTEXT_ANCHOR> aAnchors )
{
    std::sort( aAnchors.begin(), aAnchors.end(), anchorLess );
    m_Anchors = std::move( aAnchors );
    ++m_Version.m_ViewRevision;
}


void AI_CONTEXT_INDEX::SetPanelStates( std::vector<AI_PANEL_STATE_RECORD> aPanelStates )
{
    std::sort( aPanelStates.begin(), aPanelStates.end(), panelStateLess );
    m_PanelStates = std::move( aPanelStates );
    ++m_Version.m_ViewRevision;
}
```

- [x] **Step 6: Run green**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\common;$root\common\gal;$root\api;$env:PATH"
& "$root\qa\tests\common\qa_common.exe" --run_test=AiContextIndex,AiNativeTypes --log_level=nothing
```

Expected: context index and native type suites pass.

- [x] **Step 7: Commit**

```bash
git add include/kisurf/ai/ai_context_index.h common/kisurf/ai/ai_context_index.cpp qa/tests/common/test_ai_context_index.cpp
git commit -m "feat: carry ai anchors through context index"
```

## Task 4: Final Verification And Plan Status

**Files:**
- Modify: `docs/superpowers/plans/2026-06-18-ai-unified-context-anchors-panel-state-implementation.md`

- [x] **Step 1: Run common AI build and tests**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\common;$root\common\gal;$root\api;$env:PATH"
& "$root\qa\tests\common\qa_common.exe" --run_test=AiNativeTypes,AiContextIndex,AiAgentPanelModel,AiAgentObservabilityLog --log_level=nothing
```

Expected: build exit code 0 and test exit code 0.

- [x] **Step 2: Run editor target smoke build**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target pcbnew -- -j 2 && cmake --build out/build/x64-release --target eeschema -- -j 2"
```

Expected: both editor targets build with exit code 0.

- [x] **Step 3: Run whitespace and secret checks**

Run:

```powershell
git diff --check
git grep -n -E "sk-[0-9A-Za-z_-]{20,}|OPENAI_API_KEY\s*=|KISURF_AI_API_KEY\s*=" -- common include qa docs
```

Expected: whitespace check exit code 0; secret scan has no matches.

- [x] **Step 4: Update this plan status**

Check off each completed step in this file. Leave no completed implementation step unchecked.

- [x] **Step 5: Commit final plan status**

```bash
git add docs/superpowers/plans/2026-06-18-ai-unified-context-anchors-panel-state-implementation.md
git commit -m "docs: update ai context anchor plan status"
```

## Self-Review

- Spec coverage: Tasks implement common-layer anchor records, panel-state records, prompt output, JSON output, index carriage, bounds, stable ordering, and verification.
- Placeholder scan: The plan contains concrete file paths, test names, code snippets, commands, expected failures, expected passes, and commit messages.
- Type consistency: `AI_CONTEXT_ANCHOR_KIND`, `AI_CONTEXT_ANCHOR`, `AI_PANEL_STATE_RECORD`, `m_Anchors`, `m_PanelStates`, `SetAnchors`, and `SetPanelStates` are consistently named across tasks.
- Scope check: PCB-specific anchor generation, panel walkers, visual overlays, and `go_to_anchor` tools are deferred as follow-up implementation slices.
