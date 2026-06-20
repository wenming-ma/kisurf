# AI PCB Text Context Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose board-level PCB text and text boxes to AI context and object resolution.

**Architecture:** Extend the PCB context adapter to emit text refs from `BOARD::Drawings()` for `PCB_TEXT_T` and `PCB_TEXTBOX_T`. Extend the PCB object resolver with drawing lookup for those exact types. Keep table, barcode, dimension, and footprint-local text out of this slice.

**Tech Stack:** KiCad C++, `PCB_TEXT`, `PCB_TEXTBOX`, `EDA_TEXT`, Boost.Test, existing PCB AI adapter/resolver tests.

---

## Checklist

- [x] Add the PCB text context spec and link it from the AI-native spec index.
- [x] Add RED PCB context coverage for selected board text with structured details.
- [x] Add RED PCB context coverage for selected board textbox with structured details.
- [x] Add RED PCB resolver coverage for `PCB_TEXT_T` and `PCB_TEXTBOX_T` refs.
- [x] Implement text labels/details in `KISURF_AI_PCB_CONTEXT_ADAPTER`.
- [x] Implement `PCB_TEXT_T` and `PCB_TEXTBOX_T` resolution in `KISURF_AI_PCB_OBJECT_RESOLVER`.
- [x] Run targeted PCB AI adapter tests.
- [x] Run diff hygiene and commit the slice.

## Files

- Modify: `docs/superpowers/specs/2026-06-16-kisurf-ai-native-spec-index.md`
- Modify: `pcbnew/kisurf_ai_pcb_context_adapter.cpp`
- Modify: `pcbnew/kisurf_ai_pcb_object_resolver.cpp`
- Modify: `qa/tests/pcbnew/test_ai_pcb_context_adapter.cpp`
- Modify: `qa/tests/pcbnew/test_ai_pcb_object_resolver.cpp`

## Task 1: RED Context Tests

- [x] Add `#include <pcb_text.h>` and `#include <pcb_textbox.h>` to `qa/tests/pcbnew/test_ai_pcb_context_adapter.cpp`.
- [x] Add `AdapterCollectsBoardTextAsVisibleObjects`:

```cpp
BOARD board;

PCB_TEXT* text = new PCB_TEXT( &board );
text->SetText( wxS( "JTAG HEADER" ) );
text->SetPosition( VECTOR2I( 100, 200 ) );
text->SetLayer( F_SilkS );
text->SetTextSize( VECTOR2I( 1200, 900 ) );
text->SetTextAngle( EDA_ANGLE( 90, DEGREES_T ) );
text->SetBold( true );
text->SetSelected();
board.Add( text );

KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
AI_CONTEXT_INDEX              index = adapter.BuildIndex();

const AI_OBJECT_REF* textRef =
        findRefByLabel( index.VisibleObjects(), wxS( "text:JTAG HEADER" ) );

BOOST_REQUIRE( textRef );
BOOST_CHECK( textRef->m_Uuid == text->m_Uuid );
BOOST_CHECK_EQUAL( textRef->m_Type, PCB_TEXT_T );
BOOST_CHECK( findRefByLabel( index.SelectedObjects(), wxS( "text:JTAG HEADER" ) ) );

nlohmann::json details = detailsForLabel( index.VisibleObjects(), wxS( "text:JTAG HEADER" ) );
BOOST_CHECK_EQUAL( details["kind"].get<std::string>(), "text" );
BOOST_CHECK_EQUAL( details["text"].get<std::string>(), "JTAG HEADER" );
BOOST_CHECK_EQUAL( details["shown_text"].get<std::string>(), "JTAG HEADER" );
BOOST_CHECK_EQUAL( details["position"]["x"].get<int>(), 100 );
BOOST_CHECK_EQUAL( details["size"]["x"].get<int>(), 1200 );
BOOST_CHECK_EQUAL( details["layer"].get<std::string>(), "F.Silkscreen" );
BOOST_CHECK_EQUAL( details["angle_degrees"].get<int>(), 90 );
BOOST_CHECK_EQUAL( details["visible"].get<bool>(), true );
BOOST_CHECK_EQUAL( details["bold"].get<bool>(), true );
BOOST_CHECK_EQUAL( details["italic"].get<bool>(), false );
BOOST_CHECK_EQUAL( details["h_justify"].get<std::string>(), "center" );
BOOST_CHECK_EQUAL( details["v_justify"].get<std::string>(), "center" );
```

- [x] Add `AdapterCollectsBoardTextboxesAsVisibleObjects`:

```cpp
BOARD board;

PCB_TEXTBOX* textbox = new PCB_TEXTBOX( &board );
textbox->SetText( wxS( "Assembly note" ) );
textbox->SetStart( VECTOR2I( 0, 0 ) );
textbox->SetEnd( VECTOR2I( 200000, 1000 ) );
textbox->SetLayer( Cmts_User );
textbox->SetTextSize( VECTOR2I( 1000, 500 ) );
textbox->SetHorizJustify( GR_TEXT_H_ALIGN_LEFT );
textbox->SetVertJustify( GR_TEXT_V_ALIGN_TOP );
textbox->SetBorderEnabled( true );
textbox->SetBorderWidth( 100 );
textbox->SetSelected();
board.Add( textbox );

KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
AI_CONTEXT_INDEX              index = adapter.BuildIndex();

const AI_OBJECT_REF* textboxRef =
        findRefByLabel( index.VisibleObjects(), wxS( "textbox:Assembly note" ) );

BOOST_REQUIRE( textboxRef );
BOOST_CHECK( textboxRef->m_Uuid == textbox->m_Uuid );
BOOST_CHECK_EQUAL( textboxRef->m_Type, PCB_TEXTBOX_T );
BOOST_CHECK( findRefByLabel( index.SelectedObjects(), wxS( "textbox:Assembly note" ) ) );

nlohmann::json details =
        detailsForLabel( index.VisibleObjects(), wxS( "textbox:Assembly note" ) );
BOOST_CHECK_EQUAL( details["kind"].get<std::string>(), "textbox" );
BOOST_CHECK_EQUAL( details["text"].get<std::string>(), "Assembly note" );
BOOST_CHECK_EQUAL( details["shown_text"].get<std::string>(), "Assembly\nnote" );
BOOST_CHECK_EQUAL( details["layer"].get<std::string>(), "User.Comments" );
BOOST_CHECK_EQUAL( details["start"]["x"].get<int>(), 0 );
BOOST_CHECK_EQUAL( details["end"]["x"].get<int>(), 200000 );
BOOST_CHECK_EQUAL( details["border_enabled"].get<bool>(), true );
BOOST_CHECK_EQUAL( details["border_width"].get<int>(), 100 );
BOOST_CHECK_EQUAL( details["h_justify"].get<std::string>(), "left" );
BOOST_CHECK_EQUAL( details["v_justify"].get<std::string>(), "top" );
```

- [x] Run the target verification command below and confirm these tests fail because text refs are not emitted yet.

## Task 2: RED Resolver Tests

- [x] Add `#include <pcb_text.h>` and `#include <pcb_textbox.h>` to `qa/tests/pcbnew/test_ai_pcb_object_resolver.cpp`.
- [x] Add `ResolvesBoardTextReferences`:

```cpp
BOARD board;

PCB_TEXT* text = new PCB_TEXT( &board );
text->SetText( wxS( "JTAG HEADER" ) );
board.Add( text );

PCB_TEXTBOX* textbox = new PCB_TEXTBOX( &board );
textbox->SetText( wxS( "Assembly note" ) );
board.Add( textbox );

KISURF_AI_PCB_OBJECT_RESOLVER resolver( board );

BOOST_CHECK( resolver.Resolve( AI_OBJECT_REF( text->m_Uuid, PCB_TEXT_T,
                                              wxS( "text:JTAG HEADER" ) ) ) == text );
BOOST_CHECK( resolver.Resolve( AI_OBJECT_REF( textbox->m_Uuid, PCB_TEXTBOX_T,
                                              wxS( "textbox:Assembly note" ) ) ) == textbox );
```

- [x] Run the target verification command below and confirm the resolver test fails before implementation.

## Task 3: GREEN Context Adapter

- [x] Add `#include <pcb_text.h>` and `#include <pcb_textbox.h>` to `pcbnew/kisurf_ai_pcb_context_adapter.cpp`.
- [x] Add helper functions beside drawing helpers:

```cpp
wxString hJustifyToken( GR_TEXT_H_ALIGN_T aJustify );
wxString vJustifyToken( GR_TEXT_V_ALIGN_T aJustify );
wxString textPreviewLabel( const wxString& aPrefix, const wxString& aText, const KIID& aUuid );
wxString makeTextDetailsJson( const PCB_TEXT& aText );
wxString makeTextboxDetailsJson( const PCB_TEXTBOX& aTextbox );
AI_OBJECT_REF makeTextRef( const PCB_TEXT& aText );
AI_OBJECT_REF makeTextboxRef( const PCB_TEXTBOX& aTextbox );
```

- [x] In `BuildIndex()`, extend the `BOARD::Drawings()` loop to emit refs for `PCB_TEXT_T` and `PCB_TEXTBOX_T` in addition to `PCB_SHAPE_T`.

## Task 4: GREEN Resolver

- [x] In `pcbnew/kisurf_ai_pcb_object_resolver.cpp`, treat `PCB_TEXT_T` and `PCB_TEXTBOX_T` as drawing ref types and resolve by scanning `BOARD::Drawings()`.

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
- [x] Commit with `feat: expose pcb text to ai context`.
