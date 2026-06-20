# AI Footprint Placement Anchor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add deterministic semantic placement anchors while the PCB editor is placing a footprint.

**Architecture:** Extend the existing common AI context anchor provider. Routing anchors keep their current path, while footprint placement gets its own parser, stale-anchor prefix cleanup, and deterministic candidate builder.

**Tech Stack:** C++, wxWidgets strings, nlohmann JSON, Boost.Test common QA executable, Windows x64 Release CMake build.

---

## File Structure

- Modify: `qa/tests/common/test_ai_context_anchor_provider.cpp`
  - Adds focused regression coverage for placement anchor generation and stale-anchor cleanup.
- Modify: `common/kisurf/ai/ai_context_anchor_provider.cpp`
  - Adds placement context parsing, pitch selection, anchor creation, and `tool.placement.*` cleanup.
- Modify: `README.md`
  - Documents that active footprint placement now exposes placement candidate anchors.

## Task 1: Placement Anchor Tests

**Files:**
- Modify: `qa/tests/common/test_ai_context_anchor_provider.cpp`

- [ ] **Step 1: Add placement test helpers**

Add helpers near the existing `routingSnapshot()` helper:

```cpp
AI_CONTEXT_ANCHOR existingPlacementAnchor()
{
    AI_CONTEXT_ANCHOR anchor;
    anchor.m_Id = wxS( "tool.placement.cursor" );
    anchor.m_Kind = AI_CONTEXT_ANCHOR_KIND::PlacementCandidate;
    anchor.m_EditorKind = AI_EDITOR_KIND::Pcb;
    anchor.m_Label = wxS( "placement:stale" );
    anchor.m_Position = VECTOR2I( 10, 20 );
    anchor.m_HasPosition = true;
    anchor.m_Confidence = 0.25;
    return anchor;
}

AI_CONTEXT_SNAPSHOT placementSnapshot( const wxString& aModeContext )
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_Anchors.push_back( existingPadAnchor() );
    snapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::PlacingFootprint;
    snapshot.m_ToolState.m_ModeContextJson = aModeContext;
    return snapshot;
}
```

- [ ] **Step 2: Add explicit-pitch placement test**

Add this test before `InactiveOrIncompleteToolStateAddsNoAnchors`:

```cpp
BOOST_AUTO_TEST_CASE( FootprintPlacementAddsCandidateAnchorsAroundCursor )
{
    AI_CONTEXT_SNAPSHOT snapshot = placementSnapshot(
            wxS( "{\"mode\":\"placing_footprint\","
                 "\"cursor\":{\"x\":1000,\"y\":2000},"
                 "\"pitch\":250}" ) );

    AppendAiToolStateAnchors( snapshot );

    const AI_CONTEXT_ANCHOR* cursor =
            findAnchor( snapshot, wxS( "tool.placement.cursor" ) );
    const AI_CONTEXT_ANCHOR* east =
            findAnchor( snapshot, wxS( "tool.placement.grid.east" ) );
    const AI_CONTEXT_ANCHOR* south =
            findAnchor( snapshot, wxS( "tool.placement.grid.south" ) );
    const AI_CONTEXT_ANCHOR* diagonal =
            findAnchor( snapshot, wxS( "tool.placement.grid.diagonal" ) );

    BOOST_REQUIRE( cursor );
    BOOST_REQUIRE( east );
    BOOST_REQUIRE( south );
    BOOST_REQUIRE( diagonal );

    BOOST_CHECK_EQUAL( static_cast<int>( cursor->m_Kind ),
                       static_cast<int>( AI_CONTEXT_ANCHOR_KIND::PlacementCandidate ) );
    BOOST_CHECK_EQUAL( cursor->m_Position.x, 1000 );
    BOOST_CHECK_EQUAL( cursor->m_Position.y, 2000 );
    BOOST_CHECK_EQUAL( east->m_Position.x, 1250 );
    BOOST_CHECK_EQUAL( east->m_Position.y, 2000 );
    BOOST_CHECK_EQUAL( south->m_Position.x, 1000 );
    BOOST_CHECK_EQUAL( south->m_Position.y, 2250 );
    BOOST_CHECK_EQUAL( diagonal->m_Position.x, 1250 );
    BOOST_CHECK_EQUAL( diagonal->m_Position.y, 2250 );

    nlohmann::json details = detailsJson( *east );
    BOOST_CHECK_EQUAL( details["source"].get<std::string>(), "tool_state" );
    BOOST_CHECK_EQUAL( details["mode"].get<std::string>(), "placing_footprint" );
    BOOST_CHECK_EQUAL( details["role"].get<std::string>(), "grid_east" );
    BOOST_CHECK_EQUAL( details["pitch"].get<int>(), 250 );
    BOOST_CHECK_EQUAL( details["cursor"]["x"].get<int>(), 1000 );
    BOOST_CHECK_EQUAL( details["position"]["x"].get<int>(), 1250 );
}
```

- [ ] **Step 3: Add snapshot-cursor fallback test**

```cpp
BOOST_AUTO_TEST_CASE( FootprintPlacementUsesSnapshotCursorWhenModeCursorMissing )
{
    AI_CONTEXT_SNAPSHOT snapshot =
            placementSnapshot( wxS( "{\"mode\":\"placing_footprint\","
                                    "\"placement_pitch\":300}" ) );
    snapshot.m_ToolState.m_HasCursorBoardPosition = true;
    snapshot.m_ToolState.m_CursorBoardPosition = VECTOR2I( 700, 900 );

    AppendAiToolStateAnchors( snapshot );

    const AI_CONTEXT_ANCHOR* diagonal =
            findAnchor( snapshot, wxS( "tool.placement.grid.diagonal" ) );

    BOOST_REQUIRE( diagonal );
    BOOST_CHECK_EQUAL( diagonal->m_Position.x, 1000 );
    BOOST_CHECK_EQUAL( diagonal->m_Position.y, 1200 );
}
```

- [ ] **Step 4: Add default-pitch fallback test**

```cpp
BOOST_AUTO_TEST_CASE( FootprintPlacementUsesDefaultPitchWhenModePitchMissing )
{
    AI_CONTEXT_SNAPSHOT snapshot = placementSnapshot(
            wxS( "{\"mode\":\"placing_footprint\","
                 "\"cursor\":{\"x\":100,\"y\":200}}" ) );

    AppendAiToolStateAnchors( snapshot );

    const AI_CONTEXT_ANCHOR* east =
            findAnchor( snapshot, wxS( "tool.placement.grid.east" ) );

    BOOST_REQUIRE( east );
    BOOST_CHECK_EQUAL( east->m_Position.x, 1000100 );
    BOOST_CHECK_EQUAL( east->m_Position.y, 200 );

    nlohmann::json details = detailsJson( *east );
    BOOST_CHECK_EQUAL( details["pitch"].get<int>(), 1000000 );
}
```

- [ ] **Step 5: Add stale cleanup test**

```cpp
BOOST_AUTO_TEST_CASE( FootprintPlacementClearsStalePlacementAnchorsWithoutCursor )
{
    AI_CONTEXT_SNAPSHOT snapshot =
            placementSnapshot( wxS( "{\"mode\":\"placing_footprint\"}" ) );
    snapshot.m_Anchors.push_back( existingPlacementAnchor() );

    AppendAiToolStateAnchors( snapshot );

    BOOST_CHECK( !findAnchor( snapshot, wxS( "tool.placement.cursor" ) ) );
    BOOST_CHECK( findAnchor( snapshot, wxS( "pcb.pad.existing.center" ) ) );
}
```

- [ ] **Step 6: Run tests and verify red**

Run:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build .\out\build\x64-release --target qa_common --config Release"
$dllDirs = Get-ChildItem .\out\build\x64-release -Recurse -Filter *.dll | Select-Object -ExpandProperty DirectoryName -Unique
$env:PATH = ($dllDirs -join ';') + ';' + $env:PATH
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiContextAnchorProvider --log_level=test_suite --report_level=detailed
```

Expected: build succeeds; `AiContextAnchorProvider` fails because placement anchors are not implemented yet.

## Task 2: Placement Anchor Provider

**Files:**
- Modify: `common/kisurf/ai/ai_context_anchor_provider.cpp`

- [ ] **Step 1: Add placement context types and constants**

Add near `ROUTING_ANCHOR_CONTEXT`:

```cpp
constexpr int DEFAULT_PLACEMENT_ANCHOR_PITCH_IU = 1000000;

struct PLACEMENT_ANCHOR_CONTEXT
{
    VECTOR2I m_Cursor = VECTOR2I( 0, 0 );
    int      m_Pitch = DEFAULT_PLACEMENT_ANCHOR_PITCH_IU;
};
```

- [ ] **Step 2: Add prefix cleanup helper**

Replace `isRoutingToolAnchor()` with a prefix helper and wrappers:

```cpp
bool isToolAnchorWithPrefix( const AI_CONTEXT_ANCHOR& aAnchor,
                             const wxString& aPrefix )
{
    return aAnchor.m_Id.StartsWith( aPrefix );
}

bool isRoutingToolAnchor( const AI_CONTEXT_ANCHOR& aAnchor )
{
    return isToolAnchorWithPrefix( aAnchor, wxS( "tool.routing." ) );
}

bool isPlacementToolAnchor( const AI_CONTEXT_ANCHOR& aAnchor )
{
    return isToolAnchorWithPrefix( aAnchor, wxS( "tool.placement." ) );
}
```

- [ ] **Step 3: Add placement parsing helpers**

Add after `parseRoutingContext()`:

```cpp
int placementPitchFromModeContext( const nlohmann::json& aModeContext )
{
    static constexpr const char* PITCH_KEYS[] = {
        "pitch",
        "grid_pitch",
        "placement_pitch"
    };

    for( const char* key : PITCH_KEYS )
    {
        int pitch = 0;

        if( aModeContext.contains( key )
            && jsonPositiveIntegerToInt( aModeContext[key], pitch ) )
        {
            return pitch;
        }
    }

    return DEFAULT_PLACEMENT_ANCHOR_PITCH_IU;
}

std::optional<PLACEMENT_ANCHOR_CONTEXT> parsePlacementContext(
        const AI_CONTEXT_SNAPSHOT& aSnapshot )
{
    const AI_TOOL_STATE_SNAPSHOT& toolState = aSnapshot.m_ToolState;

    if( aSnapshot.m_EditorKind != AI_EDITOR_KIND::Pcb
        || toolState.m_EditorKind != AI_EDITOR_KIND::Pcb
        || toolState.m_Kind != AI_TOOL_STATE_KIND::PlacingFootprint )
    {
        return std::nullopt;
    }

    const nlohmann::json modeContext =
            nlohmann::json::parse( toUtf8String( toolState.m_ModeContextJson ),
                                   nullptr, false );

    if( modeContext.is_discarded() || !modeContext.is_object() )
        return std::nullopt;

    PLACEMENT_ANCHOR_CONTEXT context;
    bool hasCursor = false;

    if( modeContext.contains( "cursor" ) )
        hasCursor = jsonPointToVector2I( modeContext["cursor"], context.m_Cursor );

    if( !hasCursor && toolState.m_HasCursorBoardPosition )
    {
        context.m_Cursor = toolState.m_CursorBoardPosition;
        hasCursor = true;
    }

    if( !hasCursor )
        return std::nullopt;

    context.m_Pitch = placementPitchFromModeContext( modeContext );
    return context;
}
```

- [ ] **Step 4: Add placement anchor builders**

Add after `buildRoutingAnchors()`:

```cpp
AI_CONTEXT_ANCHOR makePlacementAnchor( const PLACEMENT_ANCHOR_CONTEXT& aContext,
                                       const wxString& aId,
                                       const wxString& aLabel,
                                       const wxString& aRole,
                                       const VECTOR2I& aPosition,
                                       double aConfidence )
{
    nlohmann::json details = {
        { "source", "tool_state" },
        { "mode", "placing_footprint" },
        { "role", toUtf8String( aRole ) },
        { "cursor", pointJson( aContext.m_Cursor ) },
        { "position", pointJson( aPosition ) },
        { "pitch", aContext.m_Pitch }
    };

    AI_CONTEXT_ANCHOR anchor;
    anchor.m_Id = aId;
    anchor.m_Kind = AI_CONTEXT_ANCHOR_KIND::PlacementCandidate;
    anchor.m_EditorKind = AI_EDITOR_KIND::Pcb;
    anchor.m_Label = aLabel;
    anchor.m_Summary = wxS( "Footprint placement tool-state anchor" );
    anchor.m_Position = aPosition;
    anchor.m_HasPosition = true;
    anchor.m_Layer = -1;
    anchor.m_DetailsJson = fromUtf8String( details.dump() );
    anchor.m_Confidence = aConfidence;
    return anchor;
}

std::vector<AI_CONTEXT_ANCHOR> buildPlacementAnchors(
        const PLACEMENT_ANCHOR_CONTEXT& aContext )
{
    std::vector<AI_CONTEXT_ANCHOR> anchors;
    const VECTOR2I cursor = aContext.m_Cursor;
    const int pitch = aContext.m_Pitch;

    anchors.push_back( makePlacementAnchor(
            aContext, wxS( "tool.placement.cursor" ),
            wxS( "placement:cursor" ), wxS( "placement_cursor" ),
            cursor, 1.0 ) );
    anchors.push_back( makePlacementAnchor(
            aContext, wxS( "tool.placement.grid.east" ),
            wxS( "placement:grid:east" ), wxS( "grid_east" ),
            VECTOR2I( cursor.x + pitch, cursor.y ), 0.75 ) );
    anchors.push_back( makePlacementAnchor(
            aContext, wxS( "tool.placement.grid.south" ),
            wxS( "placement:grid:south" ), wxS( "grid_south" ),
            VECTOR2I( cursor.x, cursor.y + pitch ), 0.75 ) );
    anchors.push_back( makePlacementAnchor(
            aContext, wxS( "tool.placement.grid.diagonal" ),
            wxS( "placement:grid:diagonal" ), wxS( "grid_diagonal" ),
            VECTOR2I( cursor.x + pitch, cursor.y + pitch ), 0.7 ) );

    return anchors;
}
```

- [ ] **Step 5: Wire placement into `AppendAiToolStateAnchors()`**

Change the function to erase both generated prefixes and append placement anchors:

```cpp
void AppendAiToolStateAnchors( AI_CONTEXT_SNAPSHOT& aSnapshot )
{
    aSnapshot.m_Anchors.erase(
            std::remove_if( aSnapshot.m_Anchors.begin(), aSnapshot.m_Anchors.end(),
                            []( const AI_CONTEXT_ANCHOR& aAnchor )
                            {
                                return isRoutingToolAnchor( aAnchor )
                                       || isPlacementToolAnchor( aAnchor );
                            } ),
            aSnapshot.m_Anchors.end() );

    std::optional<ROUTING_ANCHOR_CONTEXT> routingContext =
            parseRoutingContext( aSnapshot );

    if( routingContext )
    {
        std::vector<AI_CONTEXT_ANCHOR> anchors = buildRoutingAnchors( *routingContext );
        aSnapshot.m_Anchors.insert( aSnapshot.m_Anchors.end(), anchors.begin(), anchors.end() );
    }

    std::optional<PLACEMENT_ANCHOR_CONTEXT> placementContext =
            parsePlacementContext( aSnapshot );

    if( placementContext )
    {
        std::vector<AI_CONTEXT_ANCHOR> anchors = buildPlacementAnchors( *placementContext );
        aSnapshot.m_Anchors.insert( aSnapshot.m_Anchors.end(), anchors.begin(), anchors.end() );
    }

    std::sort( aSnapshot.m_Anchors.begin(), aSnapshot.m_Anchors.end(), anchorLess );
}
```

- [ ] **Step 6: Run target tests and verify green**

Run the same `qa_common --run_test=AiContextAnchorProvider` command. Expected:
`AiContextAnchorProvider` passes.

## Task 3: README and Regression Verification

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update current implementation list**

Add a bullet under `Currently implemented`:

```markdown
- Active footprint-placement contexts expose semantic placement candidate anchors
  around the cursor, giving the model deterministic named points for placement
  previews instead of pixel guessing.
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

- [ ] **Step 4: Inspect desktop for hidden compile/runtime popups**

Use Computer Use to list visible apps/windows and inspect any system-error,
assertion, missing-DLL, crash, or debugger modal after the build/test commands.
Expected: no unexpected error dialog remains open. If one is found, capture its
title/message and fix the underlying issue before committing.

- [ ] **Step 5: Run secret scan and diff review**

Run:

```powershell
$prefix = 'sk' + '-'
$pattern = $prefix + '[A-Za-z0-9_-]{20,}|OPENAI_API_KEY=.*' + $prefix
rg -n $pattern common docs include qa pcbnew eeschema README.md
git diff -- docs/superpowers/specs/2026-06-19-ai-footprint-placement-anchor-design.md docs/superpowers/plans/2026-06-19-ai-footprint-placement-anchor-implementation.md common/kisurf/ai/ai_context_anchor_provider.cpp qa/tests/common/test_ai_context_anchor_provider.cpp README.md
git status --short
```

Expected: secret scan has no output and exit code 1; diff contains only the
intended spec, plan, provider, test, and README changes. Unrelated dirty files
must not be staged.

- [ ] **Step 6: Commit the slice**

Run:

```powershell
git add docs/superpowers/specs/2026-06-19-ai-footprint-placement-anchor-design.md docs/superpowers/plans/2026-06-19-ai-footprint-placement-anchor-implementation.md common/kisurf/ai/ai_context_anchor_provider.cpp qa/tests/common/test_ai_context_anchor_provider.cpp README.md
git commit -m "feat: add footprint placement anchors"
```

## Self-Review

- Spec coverage: each requirement maps to tests or provider implementation.
- Placeholder scan: no unfinished marker or incomplete step remains.
- Type consistency: all enum names, helper names, ids, and JSON fields match the
  current codebase.
- Verification coverage: includes target tests, broader AI tests, editor builds,
  secret scan, diff review, and Computer Use popup inspection.
