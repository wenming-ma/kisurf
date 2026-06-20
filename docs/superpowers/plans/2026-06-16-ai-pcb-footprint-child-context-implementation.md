# AI PCB Footprint Child Context Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose footprint-owned fields, text, text boxes, and graphics to PCB AI context and object resolution.

**Architecture:** Extend the PCB context adapter inside the existing footprint loop. Emit parent-qualified refs for `PCB_FIELD_T` from `FOOTPRINT::GetFields()` and for `PCB_SHAPE_T`, `PCB_TEXT_T`, and `PCB_TEXTBOX_T` from `FOOTPRINT::GraphicalItems()`. Extend the resolver to scan footprint fields and graphical items after board-level drawing lookup.

**Tech Stack:** KiCad C++, `FOOTPRINT`, `PCB_FIELD`, `PCB_SHAPE`, `PCB_TEXT`, `PCB_TEXTBOX`, Boost.Test, existing PCB AI adapter/resolver tests.

---

## Checklist

- [x] Add the PCB footprint child context spec and link it from the AI-native spec index.
- [x] Add RED PCB context coverage for selected footprint fields with structured details.
- [x] Add RED PCB context coverage for selected footprint-local graphics/text with structured details.
- [x] Add RED PCB resolver coverage for `PCB_FIELD_T`, footprint-local `PCB_SHAPE_T`, and footprint-local `PCB_TEXT_T`.
- [x] Implement parent-qualified footprint child labels and details in `KISURF_AI_PCB_CONTEXT_ADAPTER`.
- [x] Implement footprint field/graphic resolution in `KISURF_AI_PCB_OBJECT_RESOLVER`.
- [x] Run targeted PCB AI adapter/resolver tests.
- [x] Run diff hygiene and commit the slice.

## Files

- Modify: `docs/superpowers/specs/2026-06-16-kisurf-ai-native-spec-index.md`
- Modify: `pcbnew/kisurf_ai_pcb_context_adapter.cpp`
- Modify: `pcbnew/kisurf_ai_pcb_object_resolver.cpp`
- Modify: `qa/tests/pcbnew/test_ai_pcb_context_adapter.cpp`
- Modify: `qa/tests/pcbnew/test_ai_pcb_object_resolver.cpp`

## Task 1: RED Context Tests

- [x] Add `#include <pcb_field.h>` to `qa/tests/pcbnew/test_ai_pcb_context_adapter.cpp`.
- [x] Add `AdapterCollectsFootprintFieldsAsVisibleObjects`:

```cpp
BOARD board;

FOOTPRINT* footprint = new FOOTPRINT( &board );
footprint->SetReference( wxS( "U1" ) );
footprint->SetValue( wxS( "MCU" ) );

PCB_FIELD* reference = footprint->GetField( FIELD_T::REFERENCE );
reference->SetPosition( VECTOR2I( 100, 200 ) );
reference->SetLayer( F_SilkS );
reference->SetTextSize( VECTOR2I( 1200, 900 ) );
reference->SetSelected();

board.Add( footprint );

KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
AI_CONTEXT_INDEX              index = adapter.BuildIndex();

const AI_OBJECT_REF* ref =
        findRefByLabel( index.VisibleObjects(), wxS( "fp:U1/field:Reference" ) );

BOOST_REQUIRE( ref );
BOOST_CHECK( ref->m_Uuid == reference->m_Uuid );
BOOST_CHECK_EQUAL( ref->m_Type, PCB_FIELD_T );
BOOST_CHECK( findRefByLabel( index.SelectedObjects(), wxS( "fp:U1/field:Reference" ) ) );

nlohmann::json details =
        detailsForLabel( index.VisibleObjects(), wxS( "fp:U1/field:Reference" ) );
BOOST_CHECK_EQUAL( details["kind"].get<std::string>(), "field" );
BOOST_CHECK_EQUAL( details["text"].get<std::string>(), "U1" );
BOOST_CHECK_EQUAL( details["shown_text"].get<std::string>(), "U1" );
BOOST_CHECK_EQUAL( details["parent_footprint_reference"].get<std::string>(), "U1" );
BOOST_CHECK( !details["parent_footprint_uuid"].get<std::string>().empty() );
BOOST_CHECK_EQUAL( details["field_name"].get<std::string>(), "Reference" );
BOOST_CHECK_EQUAL( details["field_canonical_name"].get<std::string>(), "Reference" );
BOOST_CHECK_EQUAL( details["is_reference"].get<bool>(), true );
BOOST_CHECK_EQUAL( details["is_value"].get<bool>(), false );
BOOST_CHECK_EQUAL( details["position"]["x"].get<int>(), 100 );
BOOST_CHECK_EQUAL( details["layer"].get<std::string>(), "F.Silkscreen" );
```

- [x] Add `AdapterCollectsFootprintGraphicalItemsAsVisibleObjects`:

```cpp
BOARD board;

FOOTPRINT* footprint = new FOOTPRINT( &board );
footprint->SetReference( wxS( "U1" ) );

PCB_SHAPE* silkLine = new PCB_SHAPE( footprint );
silkLine->SetShape( SHAPE_T::SEGMENT );
silkLine->SetStart( VECTOR2I( 0, 0 ) );
silkLine->SetEnd( VECTOR2I( 1000, 0 ) );
silkLine->SetLayer( F_SilkS );
silkLine->SetWidth( 100 );
silkLine->SetSelected();
footprint->Add( silkLine );

PCB_TEXT* pinOne = new PCB_TEXT( footprint );
pinOne->SetText( wxS( "PIN 1" ) );
pinOne->SetPosition( VECTOR2I( 50, 75 ) );
pinOne->SetLayer( F_Fab );
pinOne->SetTextSize( VECTOR2I( 500, 500 ) );
pinOne->SetSelected();
footprint->Add( pinOne );

board.Add( footprint );

KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
AI_CONTEXT_INDEX              index = adapter.BuildIndex();

const AI_OBJECT_REF* shapeRef =
        findRefByLabel( index.VisibleObjects(), wxS( "fp:U1/shape:segment:0,0->1000,0" ) );
const AI_OBJECT_REF* textRef =
        findRefByLabel( index.VisibleObjects(), wxS( "fp:U1/text:PIN 1" ) );

BOOST_REQUIRE( shapeRef );
BOOST_REQUIRE( textRef );
BOOST_CHECK_EQUAL( shapeRef->m_Type, PCB_SHAPE_T );
BOOST_CHECK_EQUAL( textRef->m_Type, PCB_TEXT_T );
BOOST_CHECK( findRefByLabel( index.SelectedObjects(), wxS( "fp:U1/shape:segment:0,0->1000,0" ) ) );
BOOST_CHECK( findRefByLabel( index.SelectedObjects(), wxS( "fp:U1/text:PIN 1" ) ) );

nlohmann::json shapeDetails =
        detailsForLabel( index.VisibleObjects(), wxS( "fp:U1/shape:segment:0,0->1000,0" ) );
BOOST_CHECK_EQUAL( shapeDetails["kind"].get<std::string>(), "shape" );
BOOST_CHECK_EQUAL( shapeDetails["parent_footprint_reference"].get<std::string>(), "U1" );
BOOST_CHECK_EQUAL( shapeDetails["layer"].get<std::string>(), "F.Silkscreen" );

nlohmann::json textDetails =
        detailsForLabel( index.VisibleObjects(), wxS( "fp:U1/text:PIN 1" ) );
BOOST_CHECK_EQUAL( textDetails["kind"].get<std::string>(), "text" );
BOOST_CHECK_EQUAL( textDetails["parent_footprint_reference"].get<std::string>(), "U1" );
BOOST_CHECK_EQUAL( textDetails["text"].get<std::string>(), "PIN 1" );
BOOST_CHECK_EQUAL( textDetails["layer"].get<std::string>(), "F.Fab" );
```

- [x] Run the target verification command below and confirm these tests fail because footprint child refs are not emitted yet.

## Task 2: RED Resolver Tests

- [x] Add `#include <pcb_field.h>` to `qa/tests/pcbnew/test_ai_pcb_object_resolver.cpp`.
- [x] Add `ResolvesFootprintChildReferences`:

```cpp
BOARD board;

FOOTPRINT* footprint = new FOOTPRINT( &board );
footprint->SetReference( wxS( "U1" ) );

PCB_FIELD* reference = footprint->GetField( FIELD_T::REFERENCE );

PCB_SHAPE* silkLine = new PCB_SHAPE( footprint );
silkLine->SetShape( SHAPE_T::SEGMENT );
footprint->Add( silkLine );

PCB_TEXT* pinOne = new PCB_TEXT( footprint );
pinOne->SetText( wxS( "PIN 1" ) );
footprint->Add( pinOne );

board.Add( footprint );

KISURF_AI_PCB_OBJECT_RESOLVER resolver( board );

BOOST_CHECK( resolver.Resolve( AI_OBJECT_REF( reference->m_Uuid, PCB_FIELD_T,
                                              wxS( "fp:U1/field:Reference" ) ) ) == reference );
BOOST_CHECK( resolver.Resolve( AI_OBJECT_REF( silkLine->m_Uuid, PCB_SHAPE_T,
                                              wxS( "fp:U1/shape:segment" ) ) ) == silkLine );
BOOST_CHECK( resolver.Resolve( AI_OBJECT_REF( pinOne->m_Uuid, PCB_TEXT_T,
                                              wxS( "fp:U1/text:PIN 1" ) ) ) == pinOne );
```

- [x] Run the target verification command below and confirm the resolver test fails before implementation.

## Task 3: GREEN Context Adapter

- [x] Add `#include <pcb_field.h>` to `pcbnew/kisurf_ai_pcb_context_adapter.cpp`.
- [x] Add helpers beside existing shape/text helpers:

```cpp
wxString footprintChildLabelPrefix( const FOOTPRINT& aFootprint );
wxString parentFootprintDetailsJson( const FOOTPRINT* aFootprint );
AI_OBJECT_REF makeFieldRef( const FOOTPRINT& aFootprint, const PCB_FIELD& aField );
AI_OBJECT_REF makeFootprintShapeRef( const FOOTPRINT& aFootprint, const PCB_SHAPE& aShape );
AI_OBJECT_REF makeFootprintTextRef( const FOOTPRINT& aFootprint, const PCB_TEXT& aText );
AI_OBJECT_REF makeFootprintTextboxRef( const FOOTPRINT& aFootprint, const PCB_TEXTBOX& aTextbox );
```

- [x] Extend the footprint loop in `BuildIndex()`:

```cpp
for( PCB_FIELD* field : footprint->GetFields() )
{
    AI_OBJECT_REF ref = makeFieldRef( *footprint, *field );
    visibleObjects.push_back( ref );

    if( field->IsSelected() )
        selectedObjects.push_back( ref );
}
```

- [x] After pads, iterate `footprint->GraphicalItems()` and emit `PCB_SHAPE_T`, `PCB_TEXT_T`, and `PCB_TEXTBOX_T` refs with parent-qualified labels.

## Task 4: GREEN Resolver

- [x] Add `#include <pcb_field.h>` to `pcbnew/kisurf_ai_pcb_object_resolver.cpp`.
- [x] Preserve the existing board-level drawing lookup.
- [x] Extend the footprint scan to resolve:

```cpp
for( PCB_FIELD* field : footprint->GetFields() )
{
    if( field->m_Uuid == aRef.m_Uuid && field->Type() == aRef.m_Type )
        return field;
}

for( BOARD_ITEM* item : footprint->GraphicalItems() )
{
    if( item->m_Uuid == aRef.m_Uuid && item->Type() == aRef.m_Type )
        return item;
}
```

## Target Verification

```powershell
cmd.exe /d /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_pcbnew -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1&& set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew&& set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;%PATH%&& out\build\x64-release\qa\tests\pcbnew\qa_pcbnew.exe --run_test=AiPcbContextAdapter,AiPcbObjectResolver,AiPcbPreviewAdapter,AiPcbMoveEditAdapter --log_level=test_suite'
```

Expected GREEN result: exit code 0 with `*** No errors detected`. The existing schema-file warning may still appear.

## Hygiene

- [x] Run `git diff --check`.
- [x] Run `rg -n "OPENAI[_]API[_]KEY=|sub2[a]pi|sk-[0-9a-fA-F]{20,}"` on the modified files and expect no matches.
- [x] Stage only the spec, plan, index, adapter, resolver, and AI PCB tests.
- [x] Run `git diff --cached --check`.
- [x] Run the same secret scan against staged files.
- [x] Commit with `feat: expose footprint child pcb objects to ai context`.
