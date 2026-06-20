# AI Routing Tool-State Anchor Augmentation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add transient routing anchors derived from active PCB routing tool state so the model can choose semantic route landing points from the same `AI_CONTEXT_SNAPSHOT::m_Anchors` channel as board-object anchors.

**Architecture:** Create a common helper that appends tool-state anchors to an already-built snapshot. PCB adapter remains responsible for factual board-object anchors; the new helper owns transient routing candidates after `m_ToolState` is attached. PCB editor context assembly calls the helper after `KISURF_AI_PCB_TOOL_STATE_PROVIDER::BuildToolState()`.

**Tech Stack:** KiSurf common C++20, `AI_CONTEXT_SNAPSHOT`, `AI_TOOL_STATE_SNAPSHOT`, `AI_CONTEXT_ANCHOR`, nlohmann JSON, Boost unit tests in `qa_common`, PCB editor integration in `pcbnew/pcb_edit_frame.cpp`.

---

## File Structure

- Create `include/kisurf/ai/ai_context_anchor_provider.h`
  - Declare `AppendAiToolStateAnchors( AI_CONTEXT_SNAPSHOT& )`.
- Create `common/kisurf/ai/ai_context_anchor_provider.cpp`
  - Parse active routing tool state.
  - Build route start, current-end, orthogonal, and 45-degree anchors.
  - Remove stale `tool.routing.*` anchors, append new ones, and sort.
- Create `qa/tests/common/test_ai_context_anchor_provider.cpp`
  - Test routing anchor generation, fallback target selection, inactive contexts, and preservation of factual anchors.
- Modify `common/CMakeLists.txt`
  - Add the new common source to `KICOMMON_SRCS`.
- Modify `qa/tests/common/CMakeLists.txt`
  - Add the new test file to `QA_COMMON_SRCS`.
- Modify `pcbnew/pcb_edit_frame.cpp`
  - Include the new helper and call it after assigning PCB tool state.
- Modify `docs/superpowers/plans/2026-06-19-ai-routing-tool-state-anchor-augmentation-implementation.md`
  - Check off completed steps after verification.

## Task 1: Common Tests And Build Registration

**Files:**
- Create: `qa/tests/common/test_ai_context_anchor_provider.cpp`
- Modify: `qa/tests/common/CMakeLists.txt`

- [x] **Step 1: Register the future test file**

Add `test_ai_context_anchor_provider.cpp` after `test_ai_context_index.cpp` in `qa/tests/common/CMakeLists.txt`:

```cmake
    test_ai_context_index.cpp
    test_ai_context_anchor_provider.cpp
    test_ai_visual_snapshot.cpp
```

- [x] **Step 2: Add the test file with helpers and success test**

Create `qa/tests/common/test_ai_context_anchor_provider.cpp`:

```cpp
#include <boost/test/unit_test.hpp>

#include <kisurf/ai/ai_context_anchor_provider.h>

#include <nlohmann/json.hpp>

#include <string>

namespace
{
std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


nlohmann::json detailsJson( const AI_CONTEXT_ANCHOR& aAnchor )
{
    BOOST_REQUIRE( !aAnchor.m_DetailsJson.IsEmpty() );
    return nlohmann::json::parse( toUtf8String( aAnchor.m_DetailsJson ) );
}


const AI_CONTEXT_ANCHOR* findAnchor( const AI_CONTEXT_SNAPSHOT& aSnapshot,
                                     const wxString& aId )
{
    for( const AI_CONTEXT_ANCHOR& anchor : aSnapshot.m_Anchors )
    {
        if( anchor.m_Id == aId )
            return &anchor;
    }

    return nullptr;
}


AI_CONTEXT_ANCHOR existingPadAnchor()
{
    AI_CONTEXT_ANCHOR anchor;
    anchor.m_Id = wxS( "pcb.pad.existing.center" );
    anchor.m_Kind = AI_CONTEXT_ANCHOR_KIND::RouteTarget;
    anchor.m_EditorKind = AI_EDITOR_KIND::Pcb;
    anchor.m_Label = wxS( "pad:U1.1:center" );
    anchor.m_Position = VECTOR2I( 1000, 2000 );
    anchor.m_HasPosition = true;
    anchor.m_Confidence = 1.0;
    return anchor;
}


AI_CONTEXT_SNAPSHOT routingSnapshot( const wxString& aModeContext )
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_Anchors.push_back( existingPadAnchor() );
    snapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::RoutingTrack;
    snapshot.m_ToolState.m_ModeContextJson = aModeContext;
    return snapshot;
}


wxString routingModeContextWithCursor()
{
    return wxS( "{\"net\":\"/GPIO\",\"layer\":\"F.Cu\",\"width\":150000,"
                "\"start\":{\"x\":100,\"y\":200},"
                "\"cursor\":{\"x\":500,\"y\":350}}" );
}
} // namespace


BOOST_AUTO_TEST_SUITE( AiContextAnchorProvider )


BOOST_AUTO_TEST_CASE( RoutingToolStateAddsDynamicRouteAnchors )
{
    AI_CONTEXT_SNAPSHOT snapshot = routingSnapshot( routingModeContextWithCursor() );

    AppendAiToolStateAnchors( snapshot );

    BOOST_CHECK( findAnchor( snapshot, wxS( "pcb.pad.existing.center" ) ) );

    const AI_CONTEXT_ANCHOR* start =
            findAnchor( snapshot, wxS( "tool.routing.start" ) );
    const AI_CONTEXT_ANCHOR* currentEnd =
            findAnchor( snapshot, wxS( "tool.routing.current_end" ) );
    const AI_CONTEXT_ANCHOR* horizontal =
            findAnchor( snapshot, wxS( "tool.routing.orthogonal.horizontal" ) );
    const AI_CONTEXT_ANCHOR* vertical =
            findAnchor( snapshot, wxS( "tool.routing.orthogonal.vertical" ) );
    const AI_CONTEXT_ANCHOR* fortyFive =
            findAnchor( snapshot, wxS( "tool.routing.fortyfive.horizontal" ) );

    BOOST_REQUIRE( start );
    BOOST_REQUIRE( currentEnd );
    BOOST_REQUIRE( horizontal );
    BOOST_REQUIRE( vertical );
    BOOST_REQUIRE( fortyFive );
    BOOST_CHECK( !findAnchor( snapshot, wxS( "tool.routing.fortyfive.vertical" ) ) );

    BOOST_CHECK_EQUAL( static_cast<int>( start->m_Kind ),
                       static_cast<int>( AI_CONTEXT_ANCHOR_KIND::RouteStart ) );
    BOOST_CHECK_EQUAL( static_cast<int>( currentEnd->m_Kind ),
                       static_cast<int>( AI_CONTEXT_ANCHOR_KIND::RouteCandidate ) );
    BOOST_CHECK_EQUAL( static_cast<int>( horizontal->m_Kind ),
                       static_cast<int>( AI_CONTEXT_ANCHOR_KIND::OrthogonalBreakout ) );
    BOOST_CHECK_EQUAL( static_cast<int>( fortyFive->m_Kind ),
                       static_cast<int>( AI_CONTEXT_ANCHOR_KIND::FortyFiveIntersection ) );

    BOOST_CHECK_EQUAL( start->m_Position.x, 100 );
    BOOST_CHECK_EQUAL( start->m_Position.y, 200 );
    BOOST_CHECK_EQUAL( currentEnd->m_Position.x, 500 );
    BOOST_CHECK_EQUAL( currentEnd->m_Position.y, 350 );
    BOOST_CHECK_EQUAL( horizontal->m_Position.x, 500 );
    BOOST_CHECK_EQUAL( horizontal->m_Position.y, 200 );
    BOOST_CHECK_EQUAL( vertical->m_Position.x, 100 );
    BOOST_CHECK_EQUAL( vertical->m_Position.y, 350 );
    BOOST_CHECK_EQUAL( fortyFive->m_Position.x, 350 );
    BOOST_CHECK_EQUAL( fortyFive->m_Position.y, 200 );

    nlohmann::json details = detailsJson( *fortyFive );
    BOOST_CHECK_EQUAL( details["source"].get<std::string>(), "tool_state" );
    BOOST_CHECK_EQUAL( details["mode"].get<std::string>(), "routing_track" );
    BOOST_CHECK_EQUAL( details["role"].get<std::string>(), "fortyfive_horizontal" );
    BOOST_CHECK_EQUAL( details["net"].get<std::string>(), "/GPIO" );
    BOOST_CHECK_EQUAL( details["layer"].get<std::string>(), "F.Cu" );
    BOOST_CHECK_EQUAL( details["width"].get<int>(), 150000 );
    BOOST_CHECK_EQUAL( details["start"]["x"].get<int>(), 100 );
    BOOST_CHECK_EQUAL( details["target"]["y"].get<int>(), 350 );
    BOOST_CHECK_EQUAL( details["position"]["x"].get<int>(), 350 );
}
```

- [x] **Step 3: Add fallback and inactive tests**

Append these tests before `BOOST_AUTO_TEST_SUITE_END()`:

```cpp
BOOST_AUTO_TEST_CASE( RoutingToolStateUsesCurrentEndWhenCursorMissing )
{
    AI_CONTEXT_SNAPSHOT snapshot = routingSnapshot(
            wxS( "{\"net\":\"/GPIO\",\"layer\":\"B.Cu\",\"width\":120000,"
                 "\"start\":{\"x\":100,\"y\":200},"
                 "\"current_end\":{\"x\":210,\"y\":260}}" ) );

    AppendAiToolStateAnchors( snapshot );

    const AI_CONTEXT_ANCHOR* currentEnd =
            findAnchor( snapshot, wxS( "tool.routing.current_end" ) );

    BOOST_REQUIRE( currentEnd );
    BOOST_CHECK_EQUAL( currentEnd->m_Position.x, 210 );
    BOOST_CHECK_EQUAL( currentEnd->m_Position.y, 260 );
}


BOOST_AUTO_TEST_CASE( RoutingToolStateFallsBackToToolCursorPosition )
{
    AI_CONTEXT_SNAPSHOT snapshot = routingSnapshot(
            wxS( "{\"net\":\"/GPIO\",\"layer\":\"F.Cu\",\"width\":150000,"
                 "\"start\":{\"x\":100,\"y\":200}}" ) );
    snapshot.m_ToolState.m_HasCursorBoardPosition = true;
    snapshot.m_ToolState.m_CursorBoardPosition = VECTOR2I( 300, 400 );

    AppendAiToolStateAnchors( snapshot );

    const AI_CONTEXT_ANCHOR* currentEnd =
            findAnchor( snapshot, wxS( "tool.routing.current_end" ) );

    BOOST_REQUIRE( currentEnd );
    BOOST_CHECK_EQUAL( currentEnd->m_Position.x, 300 );
    BOOST_CHECK_EQUAL( currentEnd->m_Position.y, 400 );
}


BOOST_AUTO_TEST_CASE( InactiveOrIncompleteToolStateAddsNoAnchors )
{
    std::vector<AI_CONTEXT_SNAPSHOT> snapshots;

    AI_CONTEXT_SNAPSHOT selecting = routingSnapshot( routingModeContextWithCursor() );
    selecting.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::Selecting;
    snapshots.push_back( selecting );

    AI_CONTEXT_SNAPSHOT schematic = routingSnapshot( routingModeContextWithCursor() );
    schematic.m_EditorKind = AI_EDITOR_KIND::Schematic;
    snapshots.push_back( schematic );

    snapshots.push_back( routingSnapshot( wxS( "not-json" ) ) );

    snapshots.push_back( routingSnapshot(
            wxS( "{\"net\":\"/GPIO\",\"layer\":\"F.Cu\","
                 "\"start\":{\"x\":100,\"y\":200},"
                 "\"cursor\":{\"x\":500,\"y\":350}}" ) ) );

    snapshots.push_back( routingSnapshot(
            wxS( "{\"net\":\"/GPIO\",\"layer\":\"F.Cu\",\"width\":150000,"
                 "\"start\":{\"x\":100,\"y\":200},"
                 "\"cursor\":{\"x\":100,\"y\":200}}" ) ) );

    for( AI_CONTEXT_SNAPSHOT& snapshot : snapshots )
    {
        AppendAiToolStateAnchors( snapshot );
        BOOST_REQUIRE_EQUAL( snapshot.m_Anchors.size(), 1 );
        BOOST_CHECK( findAnchor( snapshot, wxS( "pcb.pad.existing.center" ) ) );
    }
}


BOOST_AUTO_TEST_SUITE_END()
```

- [x] **Step 4: Run red**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
```

Expected: compile fails because `kisurf/ai/ai_context_anchor_provider.h` does not exist.

## Task 2: Common Routing Anchor Provider

**Files:**
- Create: `include/kisurf/ai/ai_context_anchor_provider.h`
- Create: `common/kisurf/ai/ai_context_anchor_provider.cpp`
- Modify: `common/CMakeLists.txt`

- [x] **Step 1: Add the public header**

Create `include/kisurf/ai/ai_context_anchor_provider.h`:

```cpp
#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_types.h>

KICOMMON_API void AppendAiToolStateAnchors( AI_CONTEXT_SNAPSHOT& aSnapshot );
```

- [x] **Step 2: Register the common source**

Add `kisurf/ai/ai_context_anchor_provider.cpp` after `kisurf/ai/ai_context_index.cpp` in `common/CMakeLists.txt`:

```cmake
    kisurf/ai/ai_context_index.cpp
    kisurf/ai/ai_context_anchor_provider.cpp
    kisurf/ai/ai_edit_session.cpp
```

- [x] **Step 3: Implement parser and anchor helpers**

Create `common/kisurf/ai/ai_context_anchor_provider.cpp` with:

```cpp
#include <kisurf/ai/ai_context_anchor_provider.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace
{
struct ROUTING_ANCHOR_CONTEXT
{
    wxString m_NetName;
    wxString m_LayerName;
    int      m_Width = 0;
    VECTOR2I m_Start = VECTOR2I( 0, 0 );
    VECTOR2I m_Target = VECTOR2I( 0, 0 );
};

std::string toUtf8String( const wxString& aText );
wxString fromUtf8String( const std::string& aText );
bool jsonIntegerToInt( const nlohmann::json& aValue, int& aOut );
bool jsonPositiveIntegerToInt( const nlohmann::json& aValue, int& aOut );
wxString jsonStringToWxString( const nlohmann::json& aValue );
bool jsonPointToVector2I( const nlohmann::json& aValue, VECTOR2I& aOut );
bool samePoint( const VECTOR2I& aLeft, const VECTOR2I& aRight );
int signOf( int aValue );
nlohmann::json pointJson( const VECTOR2I& aPoint );
int compareString( const wxString& aLeft, const wxString& aRight );
bool anchorLess( const AI_CONTEXT_ANCHOR& aLeft, const AI_CONTEXT_ANCHOR& aRight );
bool isRoutingToolAnchor( const AI_CONTEXT_ANCHOR& aAnchor );
std::optional<ROUTING_ANCHOR_CONTEXT> parseRoutingContext(
        const AI_CONTEXT_SNAPSHOT& aSnapshot );
AI_CONTEXT_ANCHOR makeRoutingAnchor( const ROUTING_ANCHOR_CONTEXT& aContext,
                                     const wxString& aId,
                                     AI_CONTEXT_ANCHOR_KIND aKind,
                                     const wxString& aLabel,
                                     const wxString& aRole,
                                     const VECTOR2I& aPosition,
                                     double aConfidence );
void appendIfDistinctCandidate( std::vector<AI_CONTEXT_ANCHOR>& aAnchors,
                                const ROUTING_ANCHOR_CONTEXT& aContext,
                                const wxString& aId,
                                AI_CONTEXT_ANCHOR_KIND aKind,
                                const wxString& aLabel,
                                const wxString& aRole,
                                const VECTOR2I& aPosition,
                                double aConfidence );
std::vector<AI_CONTEXT_ANCHOR> buildRoutingAnchors(
        const ROUTING_ANCHOR_CONTEXT& aContext );
} // namespace
```

Then replace the forward declarations with concrete definitions that:

- parse UTF-8 strings safely with `wxScopedCharBuffer`.
- parse integer and positive integer JSON fields with `int64_t` and `uint64_t` bounds.
- parse points from `{"x": integer, "y": integer}`.
- remove old `tool.routing.` anchors with `std::remove_if`.
- append newly built anchors and sort with `anchorLess`.

- [x] **Step 4: Implement AppendAiToolStateAnchors**

Add this exported function at the end of `common/kisurf/ai/ai_context_anchor_provider.cpp`:

```cpp
void AppendAiToolStateAnchors( AI_CONTEXT_SNAPSHOT& aSnapshot )
{
    aSnapshot.m_Anchors.erase(
            std::remove_if( aSnapshot.m_Anchors.begin(), aSnapshot.m_Anchors.end(),
                            isRoutingToolAnchor ),
            aSnapshot.m_Anchors.end() );

    std::optional<ROUTING_ANCHOR_CONTEXT> context = parseRoutingContext( aSnapshot );

    if( context )
    {
        std::vector<AI_CONTEXT_ANCHOR> anchors = buildRoutingAnchors( *context );
        aSnapshot.m_Anchors.insert( aSnapshot.m_Anchors.end(), anchors.begin(), anchors.end() );
    }

    std::sort( aSnapshot.m_Anchors.begin(), aSnapshot.m_Anchors.end(), anchorLess );
}
```

The `buildRoutingAnchors()` implementation must add:

```cpp
anchors.push_back( makeRoutingAnchor( aContext, wxS( "tool.routing.start" ),
                                      AI_CONTEXT_ANCHOR_KIND::RouteStart,
                                      wxS( "route:start" ), wxS( "route_start" ),
                                      aContext.m_Start, 1.0 ) );
anchors.push_back( makeRoutingAnchor( aContext, wxS( "tool.routing.current_end" ),
                                      AI_CONTEXT_ANCHOR_KIND::RouteCandidate,
                                      wxS( "route:current_end" ), wxS( "current_end" ),
                                      aContext.m_Target, 0.95 ) );
appendIfDistinctCandidate( anchors, aContext, wxS( "tool.routing.orthogonal.horizontal" ),
                           AI_CONTEXT_ANCHOR_KIND::OrthogonalBreakout,
                           wxS( "route:orthogonal:horizontal" ),
                           wxS( "orthogonal_horizontal" ),
                           VECTOR2I( aContext.m_Target.x, aContext.m_Start.y ), 0.8 );
appendIfDistinctCandidate( anchors, aContext, wxS( "tool.routing.orthogonal.vertical" ),
                           AI_CONTEXT_ANCHOR_KIND::OrthogonalBreakout,
                           wxS( "route:orthogonal:vertical" ),
                           wxS( "orthogonal_vertical" ),
                           VECTOR2I( aContext.m_Start.x, aContext.m_Target.y ), 0.8 );
```

and add valid 45-degree candidates using the formulas from the spec.

- [x] **Step 5: Run green for the new suite**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\pcbnew;$root\common;$root\common\gal;$root\api;$env:PATH"
& "$root\qa\tests\common\qa_common.exe" --run_test=AiContextAnchorProvider --log_level=test_suite
```

Expected: build exits 0 and `AiContextAnchorProvider` reports no errors.

## Task 3: PCB Context Integration

**Files:**
- Modify: `pcbnew/pcb_edit_frame.cpp`

- [x] **Step 1: Include the helper**

Add this include near the other KiSurf AI includes:

```cpp
#include <kisurf/ai/ai_context_anchor_provider.h>
```

- [x] **Step 2: Append tool-state anchors after tool state is built**

Replace:

```cpp
if( m_aiToolStateProvider )
    snapshot.m_ToolState = m_aiToolStateProvider->BuildToolState( snapshot.m_Version );
```

with:

```cpp
if( m_aiToolStateProvider )
{
    snapshot.m_ToolState = m_aiToolStateProvider->BuildToolState( snapshot.m_Version );
    AppendAiToolStateAnchors( snapshot );
}
```

- [x] **Step 3: Build PCB editor target**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target pcbnew -- -j 2"
```

Expected: `pcbnew` builds with exit code 0.

- [x] **Step 4: Commit implementation**

```bash
git add include/kisurf/ai/ai_context_anchor_provider.h common/kisurf/ai/ai_context_anchor_provider.cpp common/CMakeLists.txt qa/tests/common/CMakeLists.txt qa/tests/common/test_ai_context_anchor_provider.cpp pcbnew/pcb_edit_frame.cpp
git commit -m "feat: add routing tool-state anchors"
```

## Task 4: Final Verification And Plan Status

**Files:**
- Modify: `docs/superpowers/plans/2026-06-19-ai-routing-tool-state-anchor-augmentation-implementation.md`

- [x] **Step 1: Run common AI anchor and route-tool tests**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\pcbnew;$root\common;$root\common\gal;$root\api;$env:PATH"
& "$root\qa\tests\common\qa_common.exe" --run_test=AiContextAnchorProvider --log_level=nothing
& "$root\qa\tests\common\qa_common.exe" --run_test=AiNativeTypes --log_level=nothing
& "$root\qa\tests\common\qa_common.exe" --run_test=AiContextIndex --log_level=nothing
& "$root\qa\tests\common\qa_common.exe" --run_test=AiSemanticToolCallHandler --log_level=nothing
```

Expected: all commands exit 0.

- [x] **Step 2: Run editor target smoke build**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target pcbnew -- -j 2"
```

Expected: `pcbnew` builds with exit code 0.

- [x] **Step 3: Run whitespace and secret checks**

Run:

```powershell
git diff --check
rg -n "sk-[0-9A-Za-z_-]{20,}|OPENAI_API_KEY\s*=|KISURF_AI_API_KEY\s*=" common include pcbnew qa docs
```

Expected: whitespace check exits 0; secret scan has no matches.

- [x] **Step 4: Update this plan status**

Check off each completed step in this file.

- [x] **Step 5: Commit final plan status**

```bash
git add docs/superpowers/plans/2026-06-19-ai-routing-tool-state-anchor-augmentation-implementation.md
git commit -m "docs: update routing tool-state anchor plan status"
```

## Self-Review

- Spec coverage: Tasks implement the common helper, deterministic routing anchors, stale dynamic-anchor replacement, PCB context integration, tests, and verification.
- Placeholder scan: Every step names exact files, code snippets, commands, expected failures, expected passes, and commit messages.
- Type consistency: `AppendAiToolStateAnchors`, `AI_CONTEXT_SNAPSHOT`, `AI_TOOL_STATE_KIND::RoutingTrack`, `AI_CONTEXT_ANCHOR_KIND::OrthogonalBreakout`, and `AI_CONTEXT_ANCHOR_KIND::FortyFiveIntersection` match existing types.
- Scope check: Route search, visual overlays, target inference, accepted edits, and new model tools stay outside this slice.
