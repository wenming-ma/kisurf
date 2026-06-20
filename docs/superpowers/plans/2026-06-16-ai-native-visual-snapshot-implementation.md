# AI Native Visual Snapshot Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Capture native KiCad canvas pixels into `AI_CONTEXT_SNAPSHOT.m_Visual` as in-memory PNG data URIs.

**Architecture:** Keep image encoding in common AI code, add visual injection to `AI_CONTEXT_INDEX`, then integrate capture in PCB and schematic Agent panel context lambdas. Canvas capture uses existing `EDA_DRAW_PANEL_GAL::GetScreenshot(...)` first and the existing DC-blit fallback pattern second.

**Tech Stack:** C++17/20, wxImage/wxMemoryOutputStream/wxBase64Encode, KiCad GAL canvas, Boost.Test, existing `qa_common`, `pcbnew`, and `eeschema` build targets.

---

## File Structure

- Modify: `include/kisurf/ai/ai_types.h`
  - Add optional width/height/byte metadata to `AI_VISUAL_SNAPSHOT`.
- Create: `include/kisurf/ai/ai_visual_snapshot.h`
  - Declare capture options, image encoder, and canvas capture helper.
- Create: `common/kisurf/ai/ai_visual_snapshot.cpp`
  - Implement `wxImage` validation, optional downscale, PNG memory encoding, and base64 data URI creation.
- Create: `common/kisurf/ai/ai_visual_snapshot_canvas.cpp`
  - Implement canvas capture fallback in the `common` static library, where `draw_panel_gal.cpp` is link-visible.
- Modify: `include/kisurf/ai/ai_context_index.h`
  - Add `SetVisualSnapshot(...)`.
- Modify: `common/kisurf/ai/ai_context_index.cpp`
  - Store visual snapshot and copy it into `BuildSnapshot()`.
- Modify: `common/CMakeLists.txt`
  - Add `kisurf/ai/ai_visual_snapshot.cpp` to `KICOMMON_SRCS` and `kisurf/ai/ai_visual_snapshot_canvas.cpp` to `COMMON_SRCS`.
- Create: `qa/tests/common/test_ai_visual_snapshot.cpp`
  - Unit-test image encoding and scaling.
- Modify: `qa/tests/common/test_ai_context_index.cpp`
  - Unit-test visual snapshot injection and view revision bump.
- Modify: `qa/tests/common/test_ai_agent_panel_model.cpp`
  - Verify visual context reaches provider prompt text.
- Modify: `qa/tests/common/CMakeLists.txt`
  - Add `test_ai_visual_snapshot.cpp`.
- Modify: `pcbnew/pcb_edit_frame.cpp`
  - Capture and attach visual snapshot in PCB Agent panel context provider.
- Modify: `eeschema/sch_edit_frame.cpp`
  - Capture and attach visual snapshot in schematic Agent panel context provider.

## Verification Command Template

Use the Visual Studio developer environment:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_common -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiVisualSnapshot,AiContextIndex,AiAgentPanelModel,AiNativeTypes --log_level=test_suite"
```

Expected final result: exit code `0`, targeted suites run, and Boost reports no
errors. The known schema warning about `qa/tests/schemas/api.v1.schema.json` is
acceptable when exit code is `0`.

### Task 1: Visual Snapshot Encoding Tests

**Files:**
- Create: `qa/tests/common/test_ai_visual_snapshot.cpp`
- Modify: `qa/tests/common/CMakeLists.txt`
- Modify: `include/kisurf/ai/ai_types.h`
- Create: `include/kisurf/ai/ai_visual_snapshot.h`
- Create: `common/kisurf/ai/ai_visual_snapshot.cpp`
- Create: `common/kisurf/ai/ai_visual_snapshot_canvas.cpp`
- Modify: `common/CMakeLists.txt`

- [ ] **Step 1: Write failing tests**

Create `qa/tests/common/test_ai_visual_snapshot.cpp`:

```cpp
#include <boost/test/unit_test.hpp>
#include <kisurf/ai/ai_visual_snapshot.h>

#include <wx/image.h>

BOOST_AUTO_TEST_SUITE( AiVisualSnapshot )


BOOST_AUTO_TEST_CASE( ValidImageEncodesAsPngDataUri )
{
    wxImage image( 4, 2, false );
    image.SetRGB( wxRect( 0, 0, 4, 2 ), 255, 0, 0 );

    AI_VISUAL_SNAPSHOT snapshot =
            MakeAiVisualSnapshotFromImage( image, wxS( "test.image" ) );

    BOOST_CHECK( snapshot.HasPixels() );
    BOOST_CHECK_EQUAL( snapshot.m_Source, wxString( wxS( "test.image" ) ) );
    BOOST_CHECK_EQUAL( snapshot.m_MimeType, wxString( wxS( "image/png" ) ) );
    BOOST_CHECK( snapshot.m_DataUri.StartsWith( wxS( "data:image/png;base64," ) ) );
    BOOST_CHECK_EQUAL( snapshot.m_WidthPx, 4 );
    BOOST_CHECK_EQUAL( snapshot.m_HeightPx, 2 );
    BOOST_CHECK_GT( snapshot.m_ByteSize, 8 );
}


BOOST_AUTO_TEST_CASE( OversizedImageIsDownscaledToMaxEdge )
{
    wxImage image( 8, 4, false );
    image.SetRGB( wxRect( 0, 0, 8, 4 ), 0, 128, 255 );

    AI_VISUAL_CAPTURE_OPTIONS options;
    options.m_MaxEdgePixels = 4;

    AI_VISUAL_SNAPSHOT snapshot =
            MakeAiVisualSnapshotFromImage( image, wxS( "test.image" ), options );

    BOOST_CHECK( snapshot.HasPixels() );
    BOOST_CHECK_EQUAL( snapshot.m_WidthPx, 4 );
    BOOST_CHECK_EQUAL( snapshot.m_HeightPx, 2 );
}


BOOST_AUTO_TEST_CASE( InvalidImageReturnsEmptySnapshot )
{
    wxImage image;

    AI_VISUAL_SNAPSHOT snapshot =
            MakeAiVisualSnapshotFromImage( image, wxS( "test.image" ) );

    BOOST_CHECK( !snapshot.HasPixels() );
    BOOST_CHECK( snapshot.m_Source.IsEmpty() );
    BOOST_CHECK( snapshot.m_DataUri.IsEmpty() );
    BOOST_CHECK_EQUAL( snapshot.m_WidthPx, 0 );
    BOOST_CHECK_EQUAL( snapshot.m_HeightPx, 0 );
}


BOOST_AUTO_TEST_SUITE_END()
```

Add `test_ai_visual_snapshot.cpp` after `test_ai_context_index.cpp` in
`qa/tests/common/CMakeLists.txt`.

- [ ] **Step 2: Run tests to verify RED**

Run the verification command with `--run_test=AiVisualSnapshot`.

Expected: build fails because `kisurf/ai/ai_visual_snapshot.h` does not exist.

- [ ] **Step 3: Add visual snapshot types and header**

In `include/kisurf/ai/ai_types.h`, extend `AI_VISUAL_SNAPSHOT`:

```cpp
struct KICOMMON_API AI_VISUAL_SNAPSHOT
{
    wxString m_Source;
    wxString m_MimeType;
    wxString m_DataUri;
    int      m_WidthPx = 0;
    int      m_HeightPx = 0;
    size_t   m_ByteSize = 0;

    bool HasPixels() const;
};
```

Create `include/kisurf/ai/ai_visual_snapshot.h`:

```cpp
#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_types.h>

class EDA_DRAW_PANEL_GAL;
class wxImage;

struct KICOMMON_API AI_VISUAL_CAPTURE_OPTIONS
{
    int m_MaxEdgePixels = 1024;
};

KICOMMON_API AI_VISUAL_SNAPSHOT MakeAiVisualSnapshotFromImage(
        const wxImage& aImage,
        const wxString& aSource,
        const AI_VISUAL_CAPTURE_OPTIONS& aOptions = AI_VISUAL_CAPTURE_OPTIONS() );

bool CaptureAiVisualSnapshotFromCanvas(
        EDA_DRAW_PANEL_GAL& aCanvas,
        AI_VISUAL_SNAPSHOT& aSnapshot,
        const AI_VISUAL_CAPTURE_OPTIONS& aOptions = AI_VISUAL_CAPTURE_OPTIONS() );
```

- [ ] **Step 4: Add implementation**

Create `common/kisurf/ai/ai_visual_snapshot.cpp` for the exported image encoder:

```cpp
#include <kisurf/ai/ai_visual_snapshot.h>

#include <algorithm>
#include <cmath>
#include <wx/base64.h>
#include <wx/buffer.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/sstream.h>

namespace
{
wxImage scaledImage( const wxImage& aImage, int aMaxEdgePixels )
{
    if( aMaxEdgePixels <= 0 )
        return wxImage( aImage );

    const int width = aImage.GetWidth();
    const int height = aImage.GetHeight();
    const int maxEdge = std::max( width, height );

    if( maxEdge <= aMaxEdgePixels )
        return wxImage( aImage );

    const double scale = static_cast<double>( aMaxEdgePixels ) / maxEdge;
    const int    scaledWidth = std::max( 1, static_cast<int>( std::lround( width * scale ) ) );
    const int    scaledHeight = std::max( 1, static_cast<int>( std::lround( height * scale ) ) );

    return aImage.Scale( scaledWidth, scaledHeight, wxIMAGE_QUALITY_HIGH );
}


bool encodeImageToPng( const wxImage& aImage, wxMemoryBuffer& aPngData )
{
    wxImage imageCopy( aImage );
    imageCopy.SetOption( wxIMAGE_OPTION_PNG_COMPRESSION_LEVEL, 1 );
    imageCopy.SetOption( wxIMAGE_OPTION_PNG_COMPRESSION_STRATEGY, 3 );
    imageCopy.SetOption( wxIMAGE_OPTION_PNG_FILTER, 0x08 );

    wxMemoryOutputStream   memStream;
    wxBufferedOutputStream bufferedStream( memStream );

    if( !imageCopy.SaveFile( bufferedStream, wxBITMAP_TYPE_PNG ) )
        return false;

    bufferedStream.Close();

    auto* buffer = memStream.GetOutputStreamBuffer();
    const size_t byteCount = static_cast<size_t>( buffer->GetIntPosition() );

    if( byteCount == 0 )
        return false;

    aPngData.SetDataLen( 0 );
    aPngData.AppendData( buffer->GetBufferStart(), byteCount );
    return true;
}
} // namespace


AI_VISUAL_SNAPSHOT MakeAiVisualSnapshotFromImage(
        const wxImage& aImage,
        const wxString& aSource,
        const AI_VISUAL_CAPTURE_OPTIONS& aOptions )
{
    AI_VISUAL_SNAPSHOT snapshot;

    if( !aImage.IsOk() || aImage.GetWidth() <= 0 || aImage.GetHeight() <= 0 )
        return snapshot;

    wxImage image = scaledImage( aImage, aOptions.m_MaxEdgePixels );

    wxMemoryBuffer pngData;

    if( !encodeImageToPng( image, pngData ) )
        return snapshot;

    wxString encoded = wxBase64Encode( pngData.GetData(), pngData.GetDataLen() );

    if( encoded.IsEmpty() )
        return snapshot;

    snapshot.m_Source = aSource;
    snapshot.m_MimeType = wxS( "image/png" );
    snapshot.m_DataUri = wxS( "data:image/png;base64," ) + encoded;
    snapshot.m_WidthPx = image.GetWidth();
    snapshot.m_HeightPx = image.GetHeight();
    snapshot.m_ByteSize = pngData.GetDataLen();
    return snapshot;
}

```

Create `common/kisurf/ai/ai_visual_snapshot_canvas.cpp` for the canvas bridge:

```cpp
#include <kisurf/ai/ai_visual_snapshot.h>

#include <class_draw_panel_gal.h>

#include <wx/bitmap.h>
#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <wx/image.h>


bool CaptureAiVisualSnapshotFromCanvas(
        EDA_DRAW_PANEL_GAL& aCanvas,
        AI_VISUAL_SNAPSHOT& aSnapshot,
        const AI_VISUAL_CAPTURE_OPTIONS& aOptions )
{
    wxImage image;
    wxString source = wxS( "canvas.opengl" );

    if( !aCanvas.GetScreenshot( image ) )
    {
        const wxSize imageSize = aCanvas.GetClientSize();

        if( imageSize.x <= 0 || imageSize.y <= 0 )
            return false;

        wxClientDC dc( &aCanvas );
        wxBitmap   bitmap( imageSize.x, imageSize.y );
        wxMemoryDC memdc;

        memdc.SelectObject( bitmap );
        memdc.Blit( 0, 0, imageSize.x, imageSize.y, &dc, 0, 0 );
        memdc.SelectObject( wxNullBitmap );

        image = bitmap.ConvertToImage();
        source = wxS( "canvas.dc" );
    }

    aSnapshot = MakeAiVisualSnapshotFromImage( image, source, aOptions );
    return aSnapshot.HasPixels();
}
```

Add `kisurf/ai/ai_visual_snapshot.cpp` to the KiSurf AI Native `KICOMMON_SRCS`
block and `kisurf/ai/ai_visual_snapshot_canvas.cpp` to the `COMMON_SRCS` AI
block in `common/CMakeLists.txt`.

- [ ] **Step 5: Run visual snapshot tests to verify GREEN**

Run the verification command with `--run_test=AiVisualSnapshot`.

Expected: visual snapshot tests pass.

### Task 2: Context Index Visual Injection

**Files:**
- Modify: `include/kisurf/ai/ai_context_index.h`
- Modify: `common/kisurf/ai/ai_context_index.cpp`
- Modify: `qa/tests/common/test_ai_context_index.cpp`

- [ ] **Step 1: Write failing context index test**

Add before `BOOST_AUTO_TEST_SUITE_END()`:

```cpp
BOOST_AUTO_TEST_CASE( VisualSnapshotIsCarriedAndBumpsViewRevision )
{
    AI_CONTEXT_INDEX index( AI_EDITOR_KIND::Pcb );

    AI_VISUAL_SNAPSHOT visual;
    visual.m_Source = wxS( "test.image" );
    visual.m_MimeType = wxS( "image/png" );
    visual.m_DataUri = wxS( "data:image/png;base64,abc" );
    visual.m_WidthPx = 4;
    visual.m_HeightPx = 2;
    visual.m_ByteSize = 12;

    index.SetVisualSnapshot( visual );

    AI_CONTEXT_SNAPSHOT snapshot = index.BuildSnapshot();

    BOOST_CHECK_EQUAL( index.Version().m_ViewRevision, 1 );
    BOOST_CHECK_EQUAL( snapshot.m_Visual.m_Source, wxString( wxS( "test.image" ) ) );
    BOOST_CHECK_EQUAL( snapshot.m_Visual.m_WidthPx, 4 );
    BOOST_CHECK( snapshot.m_Visual.HasPixels() );
}
```

- [ ] **Step 2: Run tests to verify RED**

Run the verification command with `--run_test=AiContextIndex`.

Expected: build fails because `AI_CONTEXT_INDEX::SetVisualSnapshot(...)` does not
exist.

- [ ] **Step 3: Implement context visual storage**

Add to `include/kisurf/ai/ai_context_index.h`:

```cpp
    void SetVisualSnapshot( AI_VISUAL_SNAPSHOT aVisual );
```

Add private field:

```cpp
    AI_VISUAL_SNAPSHOT          m_Visual;
```

In `BuildSnapshot()`:

```cpp
    snapshot.m_Visual = m_Visual;
```

Add method implementation:

```cpp
void AI_CONTEXT_INDEX::SetVisualSnapshot( AI_VISUAL_SNAPSHOT aVisual )
{
    m_Visual = std::move( aVisual );

    if( m_Visual.HasPixels() || !m_Visual.m_Source.IsEmpty() )
        ++m_Version.m_ViewRevision;
}
```

- [ ] **Step 4: Run context index tests to verify GREEN**

Run the verification command with `--run_test=AiContextIndex`.

Expected: context index suite passes.

### Task 3: Agent Context Visual Prompt Test

**Files:**
- Modify: `qa/tests/common/test_ai_agent_panel_model.cpp`

- [ ] **Step 1: Write failing model test**

Add to `SendPassesContextSnapshotToProvider` after the action setup:

```cpp
    snapshot.m_Visual.m_Source = wxS( "test.image" );
    snapshot.m_Visual.m_MimeType = wxS( "image/png" );
    snapshot.m_Visual.m_DataUri = wxS( "data:image/png;base64,abc" );
    snapshot.m_Visual.m_WidthPx = 4;
    snapshot.m_Visual.m_HeightPx = 2;
    snapshot.m_Visual.m_ByteSize = 12;
```

Add assertions near the end:

```cpp
    BOOST_CHECK_EQUAL( provider->m_LastRequest.m_ContextSnapshot.m_Visual.m_Source,
                       wxString( wxS( "test.image" ) ) );
    BOOST_CHECK( response.m_Body.Contains( wxS( "visual: test.image image/png pixels=yes" ) ) );
```

- [ ] **Step 2: Run test**

Run the verification command with `--run_test=AiAgentPanelModel`.

Expected: the test should pass once Task 1 metadata and existing prompt formatting
are present. If it fails, update `AI_CONTEXT_SNAPSHOT::AsPromptText(...)` to keep
the visual line format stable.

### Task 4: Canvas Capture Integration

**Files:**
- Modify: `pcbnew/pcb_edit_frame.cpp`
- Modify: `eeschema/sch_edit_frame.cpp`

- [ ] **Step 1: Include visual snapshot helper**

Add in both frame files near the existing KiSurf AI includes:

```cpp
#include <kisurf/ai/ai_visual_snapshot.h>
```

- [ ] **Step 2: Refactor PCB Agent context provider**

Replace the current PCB Agent context lambda body with:

```cpp
                AI_CONTEXT_INDEX index( AI_EDITOR_KIND::Pcb );

                if( GetBoard() )
                    index = KISURF_AI_PCB_CONTEXT_ADAPTER( *GetBoard() ).BuildIndex();

                AI_VISUAL_SNAPSHOT visual;

                if( GetCanvas() )
                    CaptureAiVisualSnapshotFromCanvas( *GetCanvas(), visual );

                if( visual.HasPixels() )
                    index.SetVisualSnapshot( visual );

                AI_CONTEXT_SNAPSHOT snapshot = index.BuildSnapshot();

                if( GetToolManager() )
                {
                    snapshot.m_Actions = AI_ACTION_CATALOG::Build(
                            GetToolManager()->GetActionManager(), AI_EDITOR_KIND::Pcb, 128 );
                }

                return snapshot;
```

- [ ] **Step 3: Refactor schematic Agent context provider**

Replace the schematic Agent context lambda body with:

```cpp
                AI_CONTEXT_INDEX index( AI_EDITOR_KIND::Schematic );

                if( GetScreen() )
                    index = KISURF_AI_SCH_CONTEXT_ADAPTER( *GetScreen() ).BuildIndex();

                AI_VISUAL_SNAPSHOT visual;

                if( GetCanvas() )
                    CaptureAiVisualSnapshotFromCanvas( *GetCanvas(), visual );

                if( visual.HasPixels() )
                    index.SetVisualSnapshot( visual );

                AI_CONTEXT_SNAPSHOT snapshot = index.BuildSnapshot();

                if( GetToolManager() )
                {
                    snapshot.m_Actions = AI_ACTION_CATALOG::Build(
                            GetToolManager()->GetActionManager(), AI_EDITOR_KIND::Schematic, 128 );
                }

                return snapshot;
```

- [ ] **Step 4: Build editor targets**

Run:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target pcbnew eeschema -- -j 2
```

Expected: both editor targets build.

### Task 5: Final Verification And Commit

**Files:**
- Verify all files from Tasks 1-4.

- [ ] **Step 1: Run targeted common tests**

Run the verification command with:

```text
--run_test=AiVisualSnapshot,AiContextIndex,AiAgentPanelModel,AiNativeTypes
```

Expected: exit code `0` and no Boost errors.

- [ ] **Step 2: Build editor targets**

Run:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target pcbnew eeschema -- -j 2
```

Expected: exit code `0`.

- [ ] **Step 3: Run diff check**

Run:

```powershell
git diff --check
```

Expected: no whitespace errors. LF/CRLF warnings are acceptable.

- [ ] **Step 4: Commit**

Run:

```powershell
git add docs\superpowers\plans\2026-06-16-ai-native-visual-snapshot-implementation.md include\kisurf\ai\ai_types.h include\kisurf\ai\ai_visual_snapshot.h common\kisurf\ai\ai_visual_snapshot.cpp common\kisurf\ai\ai_visual_snapshot_canvas.cpp include\kisurf\ai\ai_context_index.h common\kisurf\ai\ai_context_index.cpp common\CMakeLists.txt qa\tests\common\test_ai_visual_snapshot.cpp qa\tests\common\test_ai_context_index.cpp qa\tests\common\test_ai_agent_panel_model.cpp qa\tests\common\CMakeLists.txt pcbnew\pcb_edit_frame.cpp eeschema\sch_edit_frame.cpp
git commit -m "feat: add ai native visual snapshots"
```

Expected: commit succeeds.

## Plan Self-Review

- Spec coverage: image encoding, visual context injection, canvas capture helper,
  PCB/SCH context integration, non-fatal failure behavior, and no-disk capture are
  all covered.
- Placeholder scan: no TBD/TODO/fill-in text remains.
- Type consistency: `AI_VISUAL_CAPTURE_OPTIONS`,
  `MakeAiVisualSnapshotFromImage`, `CaptureAiVisualSnapshotFromCanvas`, and
  `AI_CONTEXT_INDEX::SetVisualSnapshot` are used consistently.
- Scope check: multimodal provider formatting, crop/high-resolution offscreen
  rendering, and UI smoke screenshots remain outside this plan.
