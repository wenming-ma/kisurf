# AI PCB Fabrication Annotation Context Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose board-level targets, barcodes, tables, table cells, and dimensions to PCB AI context and object resolution.

**Architecture:** Extend the existing `BOARD::Drawings()` loop in the PCB context adapter with annotation-specific refs. Reuse the existing text/shape JSON helpers for table cells and dimensions, and keep label/detail helpers explicit for target, barcode, table, table cell, and dimension objects. Extend the object resolver to handle these drawing refs and table-cell children.

**Tech Stack:** KiCad C++, `PCB_TARGET`, `PCB_BARCODE`, `PCB_TABLE`, `PCB_TABLECELL`, `PCB_DIMENSION_BASE`, `PCB_DIM_ALIGNED`, Boost.Test, existing PCB AI adapter/resolver tests.

---

## Checklist

- [x] Add the PCB fabrication annotation context spec and link it from the AI-native spec index.
- [x] Add RED PCB context coverage for selected targets and barcodes.
- [x] Add RED PCB context coverage for selected tables and table cells.
- [x] Add RED PCB context coverage for selected dimensions.
- [x] Add RED PCB resolver coverage for target, barcode, table, table cell, and dimension refs.
- [x] Implement annotation labels/details in `KISURF_AI_PCB_CONTEXT_ADAPTER`.
- [x] Implement annotation and table-cell resolution in `KISURF_AI_PCB_OBJECT_RESOLVER`.
- [x] Run targeted PCB AI adapter/resolver tests.
- [x] Run diff hygiene and commit the slice.

## Files

- Modify: `docs/superpowers/specs/2026-06-16-kisurf-ai-native-spec-index.md`
- Modify: `pcbnew/kisurf_ai_pcb_context_adapter.cpp`
- Modify: `pcbnew/kisurf_ai_pcb_object_resolver.cpp`
- Modify: `qa/tests/pcbnew/test_ai_pcb_context_adapter.cpp`
- Modify: `qa/tests/pcbnew/test_ai_pcb_object_resolver.cpp`

## Task 1: RED Context Tests

- [x] Add includes to `qa/tests/pcbnew/test_ai_pcb_context_adapter.cpp`:

```cpp
#include <pcb_barcode.h>
#include <pcb_dimension.h>
#include <pcb_table.h>
#include <pcb_tablecell.h>
#include <pcb_target.h>
```

- [x] Add `AdapterCollectsBoardTargetsAndBarcodesAsVisibleObjects`:

```cpp
BOARD board;

PCB_TARGET* target = new PCB_TARGET( &board, 0, Dwgs_User, VECTOR2I( 100, 200 ), 1000, 100 );
target->SetSelected();
board.Add( target );

PCB_BARCODE* barcode = new PCB_BARCODE( &board );
barcode->SetBarcodeText( wxS( "SN-001" ) );
barcode->SetKind( BARCODE_T::QR_CODE );
barcode->SetPosition( VECTOR2I( 300, 400 ) );
barcode->SetLayer( F_SilkS );
barcode->SetWidth( 2000 );
barcode->SetHeight( 2000 );
barcode->SetShowText( true );
barcode->SetSelected();
board.Add( barcode );

KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
AI_CONTEXT_INDEX              index = adapter.BuildIndex();

const AI_OBJECT_REF* targetRef =
        findRefByLabel( index.VisibleObjects(), wxS( "target:100,200" ) );
const AI_OBJECT_REF* barcodeRef =
        findRefByLabel( index.VisibleObjects(), wxS( "barcode:SN-001" ) );

BOOST_REQUIRE( targetRef );
BOOST_REQUIRE( barcodeRef );
BOOST_CHECK_EQUAL( targetRef->m_Type, PCB_TARGET_T );
BOOST_CHECK_EQUAL( barcodeRef->m_Type, PCB_BARCODE_T );
BOOST_CHECK( findRefByLabel( index.SelectedObjects(), wxS( "target:100,200" ) ) );
BOOST_CHECK( findRefByLabel( index.SelectedObjects(), wxS( "barcode:SN-001" ) ) );

nlohmann::json targetDetails =
        detailsForLabel( index.VisibleObjects(), wxS( "target:100,200" ) );
BOOST_CHECK_EQUAL( targetDetails["kind"].get<std::string>(), "target" );
BOOST_CHECK_EQUAL( targetDetails["position"]["x"].get<int>(), 100 );
BOOST_CHECK_EQUAL( targetDetails["layer"].get<std::string>(), "User.Drawings" );

nlohmann::json barcodeDetails =
        detailsForLabel( index.VisibleObjects(), wxS( "barcode:SN-001" ) );
BOOST_CHECK_EQUAL( barcodeDetails["kind"].get<std::string>(), "barcode" );
BOOST_CHECK_EQUAL( barcodeDetails["text"].get<std::string>(), "SN-001" );
BOOST_CHECK_EQUAL( barcodeDetails["barcode_kind"].get<std::string>(), "qr_code" );
BOOST_CHECK_EQUAL( barcodeDetails["layer"].get<std::string>(), "F.Silkscreen" );
```

- [x] Add `AdapterCollectsBoardTablesAndCellsAsVisibleObjects`:

```cpp
BOARD board;

PCB_TABLE* table = new PCB_TABLE( &board, 100 );
table->SetPosition( VECTOR2I( 0, 0 ) );
table->SetLayer( Cmts_User );
table->SetColCount( 2 );

PCB_TABLECELL* firstCell = new PCB_TABLECELL( table );
firstCell->SetText( wxS( "Part" ) );
firstCell->SetStart( VECTOR2I( 0, 0 ) );
firstCell->SetEnd( VECTOR2I( 1000, 500 ) );
firstCell->SetSelected();
table->AddCell( firstCell );

PCB_TABLECELL* secondCell = new PCB_TABLECELL( table );
secondCell->SetText( wxS( "Qty" ) );
secondCell->SetStart( VECTOR2I( 1000, 0 ) );
secondCell->SetEnd( VECTOR2I( 2000, 500 ) );
table->AddCell( secondCell );

table->SetSelected();
board.Add( table );

KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
AI_CONTEXT_INDEX              index = adapter.BuildIndex();

const AI_OBJECT_REF* tableRef = findRefByLabel( index.VisibleObjects(), wxS( "table:0,0" ) );
const AI_OBJECT_REF* cellRef =
        findRefByLabel( index.VisibleObjects(), wxS( "table-cell:A1" ) );

BOOST_REQUIRE( tableRef );
BOOST_REQUIRE( cellRef );
BOOST_CHECK_EQUAL( tableRef->m_Type, PCB_TABLE_T );
BOOST_CHECK_EQUAL( cellRef->m_Type, PCB_TABLECELL_T );
BOOST_CHECK( findRefByLabel( index.SelectedObjects(), wxS( "table:0,0" ) ) );
BOOST_CHECK( findRefByLabel( index.SelectedObjects(), wxS( "table-cell:A1" ) ) );

nlohmann::json tableDetails =
        detailsForLabel( index.VisibleObjects(), wxS( "table:0,0" ) );
BOOST_CHECK_EQUAL( tableDetails["kind"].get<std::string>(), "table" );
BOOST_CHECK_EQUAL( tableDetails["columns"].get<int>(), 2 );
BOOST_CHECK_EQUAL( tableDetails["rows"].get<int>(), 1 );
BOOST_CHECK_EQUAL( tableDetails["cell_count"].get<int>(), 2 );

nlohmann::json cellDetails =
        detailsForLabel( index.VisibleObjects(), wxS( "table-cell:A1" ) );
BOOST_CHECK_EQUAL( cellDetails["kind"].get<std::string>(), "table_cell" );
BOOST_CHECK_EQUAL( cellDetails["text"].get<std::string>(), "Part" );
BOOST_CHECK_EQUAL( cellDetails["address"].get<std::string>(), "A1" );
BOOST_CHECK( !cellDetails["parent_table_uuid"].get<std::string>().empty() );
```

- [x] Add `AdapterCollectsBoardDimensionsAsVisibleObjects`:

```cpp
BOARD board;

PCB_DIM_ALIGNED* dimension = new PCB_DIM_ALIGNED( &board );
dimension->SetStart( VECTOR2I( 0, 0 ) );
dimension->SetEnd( VECTOR2I( 1000, 0 ) );
dimension->SetHeight( 500 );
dimension->SetLayer( Dwgs_User );
dimension->SetLineThickness( 100 );
dimension->SetSelected();
dimension->Update();
board.Add( dimension );

KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
AI_CONTEXT_INDEX              index = adapter.BuildIndex();

const AI_OBJECT_REF* ref =
        findRefByLabel( index.VisibleObjects(), wxS( "dimension:0,0->1000,0" ) );

BOOST_REQUIRE( ref );
BOOST_CHECK( ref->m_Uuid == dimension->m_Uuid );
BOOST_CHECK_EQUAL( ref->m_Type, PCB_DIM_ALIGNED_T );
BOOST_CHECK( findRefByLabel( index.SelectedObjects(), wxS( "dimension:0,0->1000,0" ) ) );

nlohmann::json details =
        detailsForLabel( index.VisibleObjects(), wxS( "dimension:0,0->1000,0" ) );
BOOST_CHECK_EQUAL( details["kind"].get<std::string>(), "dimension" );
BOOST_CHECK_EQUAL( details["dimension_type"].get<std::string>(), "aligned" );
BOOST_CHECK_EQUAL( details["start"]["x"].get<int>(), 0 );
BOOST_CHECK_EQUAL( details["end"]["x"].get<int>(), 1000 );
BOOST_CHECK_EQUAL( details["layer"].get<std::string>(), "User.Drawings" );
```

- [x] Run the target verification command below and confirm these tests fail because annotation refs are not emitted yet.

## Task 2: RED Resolver Tests

- [x] Add the same annotation includes to `qa/tests/pcbnew/test_ai_pcb_object_resolver.cpp`.
- [x] Add `ResolvesBoardAnnotationReferences`:

```cpp
BOARD board;

PCB_TARGET* target = new PCB_TARGET( &board, 0, Dwgs_User, VECTOR2I( 100, 200 ), 1000, 100 );
board.Add( target );

PCB_BARCODE* barcode = new PCB_BARCODE( &board );
barcode->SetBarcodeText( wxS( "SN-001" ) );
board.Add( barcode );

PCB_TABLE* table = new PCB_TABLE( &board, 100 );
table->SetColCount( 1 );
PCB_TABLECELL* cell = new PCB_TABLECELL( table );
cell->SetText( wxS( "Part" ) );
table->AddCell( cell );
board.Add( table );

PCB_DIM_ALIGNED* dimension = new PCB_DIM_ALIGNED( &board );
dimension->SetStart( VECTOR2I( 0, 0 ) );
dimension->SetEnd( VECTOR2I( 1000, 0 ) );
dimension->Update();
board.Add( dimension );

KISURF_AI_PCB_OBJECT_RESOLVER resolver( board );

BOOST_CHECK( resolver.Resolve( AI_OBJECT_REF( target->m_Uuid, PCB_TARGET_T,
                                              wxS( "target:100,200" ) ) ) == target );
BOOST_CHECK( resolver.Resolve( AI_OBJECT_REF( barcode->m_Uuid, PCB_BARCODE_T,
                                              wxS( "barcode:SN-001" ) ) ) == barcode );
BOOST_CHECK( resolver.Resolve( AI_OBJECT_REF( table->m_Uuid, PCB_TABLE_T,
                                              wxS( "table:0,0" ) ) ) == table );
BOOST_CHECK( resolver.Resolve( AI_OBJECT_REF( cell->m_Uuid, PCB_TABLECELL_T,
                                              wxS( "table-cell:A1" ) ) ) == cell );
BOOST_CHECK( resolver.Resolve( AI_OBJECT_REF( dimension->m_Uuid, PCB_DIM_ALIGNED_T,
                                              wxS( "dimension:0,0->1000,0" ) ) ) == dimension );
```

- [x] Run the target verification command below and confirm the resolver test fails before implementation.

## Task 3: GREEN Context Adapter

- [x] Add annotation includes to `pcbnew/kisurf_ai_pcb_context_adapter.cpp`.
- [x] Add helper functions beside existing drawing helpers:

```cpp
wxString barcodeKindToken( BARCODE_T aKind );
wxString dimensionTypeToken( KICAD_T aType );
AI_OBJECT_REF makeTargetRef( const PCB_TARGET& aTarget );
AI_OBJECT_REF makeBarcodeRef( const PCB_BARCODE& aBarcode );
AI_OBJECT_REF makeTableRef( const PCB_TABLE& aTable );
AI_OBJECT_REF makeTableCellRef( const PCB_TABLE& aTable, const PCB_TABLECELL& aCell );
AI_OBJECT_REF makeDimensionRef( const PCB_DIMENSION_BASE& aDimension );
```

- [x] Extend `BuildIndex()` drawing handling for target, barcode, table, table cells, and `BaseType( drawing->Type() ) == PCB_DIMENSION_T`.

## Task 4: GREEN Resolver

- [x] Add annotation includes to `pcbnew/kisurf_ai_pcb_object_resolver.cpp`.
- [x] Treat `PCB_TARGET_T`, `PCB_BARCODE_T`, `PCB_TABLE_T`, and concrete dimension types as board drawing refs.
- [x] Resolve `PCB_TABLECELL_T` by scanning cells of board-level tables.
- [x] Keep exact type matching for all refs.

## Target Verification

```powershell
cmd.exe /d /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_pcbnew -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1&& set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew&& set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;%PATH%&& out\build\x64-release\qa\tests\pcbnew\qa_pcbnew.exe --run_test=AiPcbContextAdapter,AiPcbObjectResolver,AiPcbPreviewAdapter,AiPcbMoveEditAdapter --log_level=test_suite'
```

Expected GREEN result: exit code 0 with `*** No errors detected`. The existing schema-file warning may still appear.

## Hygiene

- [x] Run `git diff --check`.
- [x] Run `rg -n "OPENAI[_]API[_]KEY=|sub2[a]pi|sk-[0-9a-fA-F]{20,}"` on the modified files and expect no matches.
- [x] Stage only the implementation plan, adapter, resolver, and AI PCB tests.
- [x] Run `git diff --cached --check`.
- [x] Run the same secret scan against staged files.
- [x] Commit with `feat: expose pcb fabrication annotations to ai context`.
