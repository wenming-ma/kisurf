# AI PCB Zone And Keepout Context Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose board-level copper zones, rule areas, and keepouts to AI context and object resolution.

**Architecture:** Extend the existing PCB context adapter with one `AI_OBJECT_REF` per `ZONE` from `BOARD::Zones()`. Extend the PCB object resolver with UUID/type lookup for `PCB_ZONE_T`. Keep preview and edit adapters unchanged because they operate on resolved `BOARD_ITEM` instances.

**Tech Stack:** KiCad C++, `ZONE`, `BOARD::Zones()`, Boost.Test, existing PCB AI adapter/resolver tests.

---

## Checklist

- [x] Add the PCB zone/keepout context spec and link it from the AI-native spec index.
- [x] Add RED PCB context coverage for a named selected copper zone with structured details.
- [x] Add RED PCB context coverage for a named selected keepout rule area with keepout details.
- [x] Add RED PCB resolver coverage for `PCB_ZONE_T` refs.
- [x] Implement zone labels/details in `KISURF_AI_PCB_CONTEXT_ADAPTER`.
- [x] Implement `PCB_ZONE_T` resolution in `KISURF_AI_PCB_OBJECT_RESOLVER`.
- [x] Run targeted PCB AI adapter tests.
- [x] Run diff hygiene and commit the slice.

## Files

- Modify: `docs/superpowers/specs/2026-06-16-kisurf-ai-native-spec-index.md`
- Modify: `pcbnew/kisurf_ai_pcb_context_adapter.cpp`
- Modify: `pcbnew/kisurf_ai_pcb_object_resolver.cpp`
- Modify: `qa/tests/pcbnew/test_ai_pcb_context_adapter.cpp`
- Modify: `qa/tests/pcbnew/test_ai_pcb_object_resolver.cpp`

## Task 1: RED Context Tests

- [ ] Add `#include <zone.h>` to `qa/tests/pcbnew/test_ai_pcb_context_adapter.cpp`.
- [ ] Add a helper inside the test file to create a rectangular zone:

```cpp
void appendRectangle( ZONE& aZone )
{
    aZone.AppendCorner( VECTOR2I( 0, 0 ), -1 );
    aZone.AppendCorner( VECTOR2I( 1000, 0 ), -1 );
    aZone.AppendCorner( VECTOR2I( 1000, 500 ), -1 );
    aZone.AppendCorner( VECTOR2I( 0, 500 ), -1 );
}
```

- [ ] Add `AdapterCollectsZonesAsVisibleObjects`:

```cpp
BOARD board;
board.Add( new NETINFO_ITEM( &board, wxS( "/GND" ), 1 ) );

ZONE* zone = new ZONE( &board );
zone->SetZoneName( wxS( "GND_POUR" ) );
zone->SetLayerSet( LSET( { F_Cu } ) );
zone->SetNetCode( 1 );
zone->SetAssignedPriority( 2 );
zone->SetSelected();
appendRectangle( *zone );
board.Add( zone );

KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
AI_CONTEXT_INDEX              index = adapter.BuildIndex();

const AI_OBJECT_REF* zoneRef =
        findRefByLabel( index.VisibleObjects(), wxS( "zone:GND_POUR" ) );

BOOST_REQUIRE( zoneRef );
BOOST_CHECK( zoneRef->m_Uuid == zone->m_Uuid );
BOOST_CHECK_EQUAL( zoneRef->m_Type, PCB_ZONE_T );
BOOST_CHECK( findRefByLabel( index.SelectedObjects(), wxS( "zone:GND_POUR" ) ) );

nlohmann::json details = detailsForLabel( index.VisibleObjects(), wxS( "zone:GND_POUR" ) );
BOOST_CHECK_EQUAL( details["kind"].get<std::string>(), "zone" );
BOOST_CHECK_EQUAL( details["zone_kind"].get<std::string>(), "copper" );
BOOST_CHECK_EQUAL( details["name"].get<std::string>(), "GND_POUR" );
BOOST_CHECK_EQUAL( details["layers"][0].get<std::string>(), "F.Cu" );
BOOST_CHECK_EQUAL( details["corner_count"].get<int>(), 4 );
BOOST_CHECK_EQUAL( details["net_code"].get<int>(), 1 );
BOOST_CHECK_EQUAL( details["net_name"].get<std::string>(), "/GND" );
BOOST_CHECK_EQUAL( details["priority"].get<int>(), 2 );
BOOST_CHECK_EQUAL( details["is_rule_area"].get<bool>(), false );
BOOST_CHECK_EQUAL( details["has_keepout"].get<bool>(), false );
```

- [ ] Add `AdapterCollectsKeepoutRuleAreasAsVisibleObjects`:

```cpp
BOARD board;

ZONE* keepout = new ZONE( &board );
keepout->SetZoneName( wxS( "NO_ROUTING" ) );
keepout->SetIsRuleArea( true );
keepout->SetLayerSet( LSET( { F_Cu, B_Cu } ) );
keepout->SetDoNotAllowTracks( true );
keepout->SetDoNotAllowVias( true );
keepout->SetSelected();
appendRectangle( *keepout );
board.Add( keepout );

KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
AI_CONTEXT_INDEX              index = adapter.BuildIndex();

const AI_OBJECT_REF* keepoutRef =
        findRefByLabel( index.VisibleObjects(), wxS( "keepout:NO_ROUTING" ) );

BOOST_REQUIRE( keepoutRef );
BOOST_CHECK( keepoutRef->m_Uuid == keepout->m_Uuid );
BOOST_CHECK_EQUAL( keepoutRef->m_Type, PCB_ZONE_T );
BOOST_CHECK( findRefByLabel( index.SelectedObjects(), wxS( "keepout:NO_ROUTING" ) ) );

nlohmann::json details =
        detailsForLabel( index.VisibleObjects(), wxS( "keepout:NO_ROUTING" ) );
BOOST_CHECK_EQUAL( details["zone_kind"].get<std::string>(), "keepout" );
BOOST_CHECK_EQUAL( details["is_rule_area"].get<bool>(), true );
BOOST_CHECK_EQUAL( details["has_keepout"].get<bool>(), true );
BOOST_CHECK_EQUAL( details["keepout"]["tracks"].get<bool>(), true );
BOOST_CHECK_EQUAL( details["keepout"]["vias"].get<bool>(), true );
BOOST_CHECK_EQUAL( details["keepout"]["pads"].get<bool>(), false );
```

- [ ] Run the target verification command below and confirm these tests fail because zone refs are not emitted yet.

## Task 2: RED Resolver Test

- [ ] Add `#include <zone.h>` to `qa/tests/pcbnew/test_ai_pcb_object_resolver.cpp`.
- [ ] Add `ResolvesZoneReference`:

```cpp
BOARD board;

ZONE* zone = new ZONE( &board );
zone->SetZoneName( wxS( "GND_POUR" ) );
zone->SetLayerSet( LSET( { F_Cu } ) );
zone->AppendCorner( VECTOR2I( 0, 0 ), -1 );
zone->AppendCorner( VECTOR2I( 1000, 0 ), -1 );
zone->AppendCorner( VECTOR2I( 1000, 500 ), -1 );
zone->AppendCorner( VECTOR2I( 0, 500 ), -1 );
board.Add( zone );

AI_OBJECT_REF ref( zone->m_Uuid, PCB_ZONE_T, wxS( "zone:GND_POUR" ) );
KISURF_AI_PCB_OBJECT_RESOLVER resolver( board );

BOOST_CHECK( resolver.Resolve( ref ) == zone );
```

- [ ] Run the target verification command below and confirm the resolver test fails before implementation.

## Task 3: GREEN Context Adapter

- [ ] Add `#include <zone.h>` to `pcbnew/kisurf_ai_pcb_context_adapter.cpp`.
- [ ] Add helper functions beside the existing PCB shape helpers:

```cpp
wxString boolJson( bool aValue );
wxString layerSetDetailsJson( const BOARD_ITEM& aItem, const LSET& aLayers );
wxString zoneKindToken( const ZONE& aZone );
wxString makeZoneDetailsJson( const ZONE& aZone );
AI_OBJECT_REF makeZoneRef( const ZONE& aZone );
```

- [ ] `layerSetDetailsJson()` must iterate `aLayers.UIOrder()` and emit board layer names through `boardLayerName()`.
- [ ] `zoneKindToken()` must return `copper`, `keepout`, or `rule_area` following the spec.
- [ ] `makeZoneRef()` must choose labels following the spec.
- [ ] In `BuildIndex()`, iterate `m_Board.Zones()`, push each zone ref to `visibleObjects`, and push selected zones to `selectedObjects`.

## Task 4: GREEN Resolver

- [ ] Add `#include <zone.h>` to `pcbnew/kisurf_ai_pcb_object_resolver.cpp`.
- [ ] Add a `PCB_ZONE_T` branch in `Resolve()` before the footprint/pad branch:

```cpp
if( aRef.m_Type == PCB_ZONE_T )
{
    for( ZONE* zone : m_Board.Zones() )
    {
        if( zone->m_Uuid == aRef.m_Uuid && zone->Type() == aRef.m_Type )
            return zone;
    }

    return nullptr;
}
```

## Target Verification

```powershell
cmd.exe /d /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_pcbnew -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1&& set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew&& set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;%PATH%&& out\build\x64-release\qa\tests\pcbnew\qa_pcbnew.exe --run_test=AiPcbContextAdapter,AiPcbObjectResolver,AiPcbPreviewAdapter,AiPcbMoveEditAdapter --log_level=test_suite'
```

Expected GREEN result: exit code 0 with `*** No errors detected`. The existing schema-file warning may still appear.

## Hygiene

- [ ] Run `git diff --check`.
- [ ] Run `rg -n "OPENAI[_]API[_]KEY=|sub2[a]pi|sk-[0-9a-fA-F]{20,}"` on the modified files and expect no matches.
- [ ] Stage only the spec, plan, index, adapter, resolver, and AI PCB tests.
- [ ] Run `git diff --cached --check`.
- [ ] Run the same secret scan against staged files.
- [ ] Commit with `feat: expose pcb zones to ai context`.
