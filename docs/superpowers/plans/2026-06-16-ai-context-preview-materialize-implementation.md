# AI Context Preview Materialize Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give KiSurf a native context index, preview ownership model, and accepted-edit session boundary that can later drive AI placement and routing suggestions safely.

**Architecture:** Context, preview, and edit sessions are common interfaces with fake adapters tested first. PCB and schematic adapters are added after the common contracts are stable, using existing listener, GAL preview, and commit mechanisms rather than bypassing KiCad editor semantics.

**Tech Stack:** C++17, KiCad listener patterns, `KIGFX::VIEW` preview group, `BOARD_COMMIT`, `SCH_COMMIT`, Boost.Test, `qa_common`, `qa_pcbnew`, `qa_eeschema`.

---

## File Structure

- Create: `include/kisurf/ai/ai_context_index.h`
  - Common context cache and observer-facing snapshot API.
- Create: `common/kisurf/ai/ai_context_index.cpp`
  - Revision bumping, selection updates, visible object snapshots.
- Create: `include/kisurf/ai/ai_preview_session.h`
  - Preview session contract and adapter interface.
- Create: `common/kisurf/ai/ai_preview_session.cpp`
  - Non-persistent preview lifecycle.
- Create: `include/kisurf/ai/ai_edit_session.h`
  - Accepted-edit session contract and adapter interface.
- Create: `common/kisurf/ai/ai_edit_session.cpp`
  - Apply/cancel boundary and validation summary storage.
- Modify: `common/CMakeLists.txt:90`
  - Add the three common implementation files.
- Create: `qa/tests/common/test_ai_context_index.cpp`
- Create: `qa/tests/common/test_ai_preview_session.cpp`
- Create: `qa/tests/common/test_ai_edit_session.cpp`
- Modify: `qa/tests/common/CMakeLists.txt:24`
- Create: `pcbnew/kisurf_ai_pcb_context_adapter.h`
- Create: `pcbnew/kisurf_ai_pcb_context_adapter.cpp`
- Modify: `pcbnew/CMakeLists.txt:519`
- Create: `qa/tests/pcbnew/test_ai_pcb_context_adapter.cpp`
- Modify: `qa/tests/pcbnew/CMakeLists.txt:22`
- Create: `eeschema/kisurf_ai_sch_context_adapter.h`
- Create: `eeschema/kisurf_ai_sch_context_adapter.cpp`
- Modify: `eeschema/CMakeLists.txt:372`
- Create: `qa/tests/eeschema/test_ai_sch_context_adapter.cpp`
- Modify: `qa/tests/eeschema/CMakeLists.txt:41`

## Task 1: Context Index Contract

**Files:**
- Create: `include/kisurf/ai/ai_context_index.h`
- Create: `common/kisurf/ai/ai_context_index.cpp`
- Test: `qa/tests/common/test_ai_context_index.cpp`
- Modify: `common/CMakeLists.txt:90`
- Modify: `qa/tests/common/CMakeLists.txt:24`

- [ ] **Step 1: Write failing context index tests**

Create `qa/tests/common/test_ai_context_index.cpp` with:

```cpp
#include <boost/test/unit_test.hpp>
#include <kisurf/ai/ai_context_index.h>

BOOST_AUTO_TEST_SUITE( AiContextIndex )

BOOST_AUTO_TEST_CASE( EmptyIndexHasInvalidVersion )
{
    AI_CONTEXT_INDEX index( AI_EDITOR_KIND::Pcb );

    BOOST_CHECK_EQUAL( index.EditorKind(), AI_EDITOR_KIND::Pcb );
    BOOST_CHECK( !index.Version().IsValid() );
    BOOST_CHECK( index.VisibleObjects().empty() );
    BOOST_CHECK( index.SelectedObjects().empty() );
}

BOOST_AUTO_TEST_CASE( DocumentChangeUpdatesVersionAndVisibleObjects )
{
    AI_CONTEXT_INDEX index( AI_EDITOR_KIND::Schematic );
    AI_OBJECT_REF symbol( KIID(), SCH_SYMBOL_T, wxS( "U1" ) );

    index.SetVisibleObjects( { symbol } );

    BOOST_CHECK_EQUAL( index.Version().m_DocumentRevision, 1 );
    BOOST_REQUIRE_EQUAL( index.VisibleObjects().size(), 1 );
    BOOST_CHECK_EQUAL( index.VisibleObjects().front().m_Label, wxS( "U1" ) );
}

BOOST_AUTO_TEST_CASE( SelectionChangeUsesSelectionRevision )
{
    AI_CONTEXT_INDEX index( AI_EDITOR_KIND::Pcb );
    AI_OBJECT_REF pad( KIID(), PCB_PAD_T, wxS( "U1.1" ) );

    index.SetSelectedObjects( { pad } );

    BOOST_CHECK_EQUAL( index.Version().m_DocumentRevision, 0 );
    BOOST_CHECK_EQUAL( index.Version().m_SelectionRevision, 1 );
    BOOST_REQUIRE_EQUAL( index.SelectedObjects().size(), 1 );
}

BOOST_AUTO_TEST_SUITE_END()
```

- [ ] **Step 2: Register failing context test**

Add to `QA_COMMON_SRCS`:

```cmake
    test_ai_context_index.cpp
```

Run:

```powershell
cmake --build --preset x64-release --target qa_common
```

Expected:

- Build fails because `<kisurf/ai/ai_context_index.h>` does not exist yet.

- [ ] **Step 3: Add context index header**

Create `include/kisurf/ai/ai_context_index.h` with:

```cpp
#pragma once

#include <import_export.h>
#include <kisurf/ai/ai_types.h>

#include <vector>

class APIEXPORT AI_CONTEXT_INDEX
{
public:
    explicit AI_CONTEXT_INDEX( AI_EDITOR_KIND aEditorKind );

    AI_EDITOR_KIND EditorKind() const { return m_EditorKind; }
    const AI_CONTEXT_VERSION& Version() const { return m_Version; }
    const std::vector<AI_OBJECT_REF>& VisibleObjects() const { return m_VisibleObjects; }
    const std::vector<AI_OBJECT_REF>& SelectedObjects() const { return m_SelectedObjects; }

    void SetVisibleObjects( std::vector<AI_OBJECT_REF> aObjects );
    void SetSelectedObjects( std::vector<AI_OBJECT_REF> aObjects );
    void BumpViewRevision();

private:
    AI_EDITOR_KIND            m_EditorKind = AI_EDITOR_KIND::Unknown;
    AI_CONTEXT_VERSION        m_Version;
    std::vector<AI_OBJECT_REF> m_VisibleObjects;
    std::vector<AI_OBJECT_REF> m_SelectedObjects;
};
```

- [ ] **Step 4: Add context index implementation**

Create `common/kisurf/ai/ai_context_index.cpp` with:

```cpp
#include <kisurf/ai/ai_context_index.h>

AI_CONTEXT_INDEX::AI_CONTEXT_INDEX( AI_EDITOR_KIND aEditorKind ) :
        m_EditorKind( aEditorKind )
{
}


void AI_CONTEXT_INDEX::SetVisibleObjects( std::vector<AI_OBJECT_REF> aObjects )
{
    m_VisibleObjects = std::move( aObjects );
    ++m_Version.m_DocumentRevision;
}


void AI_CONTEXT_INDEX::SetSelectedObjects( std::vector<AI_OBJECT_REF> aObjects )
{
    m_SelectedObjects = std::move( aObjects );
    ++m_Version.m_SelectionRevision;
}


void AI_CONTEXT_INDEX::BumpViewRevision()
{
    ++m_Version.m_ViewRevision;
}
```

- [ ] **Step 5: Register context source and test**

Add to `common/CMakeLists.txt`:

```cmake
    kisurf/ai/ai_context_index.cpp
```

Run:

```powershell
cmake --build --preset x64-release --target qa_common
$env:KICAD_RUN_FROM_BUILD_DIR='1'; ctest --test-dir out/build/x64-release -R qa_common --output-on-failure
```

Expected:

- `AiContextIndex` tests pass.

- [ ] **Step 6: Commit context index**

Run:

```powershell
git add include/kisurf/ai/ai_context_index.h common/kisurf/ai/ai_context_index.cpp common/CMakeLists.txt qa/tests/common/CMakeLists.txt qa/tests/common/test_ai_context_index.cpp
git commit -m "feat: add ai context index"
```

Expected:

- Commit succeeds.

## Task 2: Preview Session Contract

**Files:**
- Create: `include/kisurf/ai/ai_preview_session.h`
- Create: `common/kisurf/ai/ai_preview_session.cpp`
- Test: `qa/tests/common/test_ai_preview_session.cpp`
- Modify: `common/CMakeLists.txt:90`
- Modify: `qa/tests/common/CMakeLists.txt:24`

- [ ] **Step 1: Write failing preview tests**

Create `qa/tests/common/test_ai_preview_session.cpp` with:

```cpp
#include <boost/test/unit_test.hpp>
#include <kisurf/ai/ai_preview_session.h>

class FAKE_PREVIEW_ADAPTER : public AI_PREVIEW_ADAPTER
{
public:
    void BeginPreview( uint64_t aPreviewId ) override { m_Events.push_back( wxString::Format( wxS( "begin:%llu" ), static_cast<unsigned long long>( aPreviewId ) ) ); }
    void ShowObject( uint64_t aPreviewId, const AI_OBJECT_REF& aObject ) override { m_Events.push_back( wxS( "show:" ) + aObject.m_Label ); }
    void ClearPreview( uint64_t aPreviewId ) override { m_Events.push_back( wxString::Format( wxS( "clear:%llu" ), static_cast<unsigned long long>( aPreviewId ) ) ); }

    std::vector<wxString> m_Events;
};

BOOST_AUTO_TEST_SUITE( AiPreviewSession )

BOOST_AUTO_TEST_CASE( PreviewBeginShowClearUsesOnePreviewId )
{
    FAKE_PREVIEW_ADAPTER adapter;
    AI_PREVIEW_SESSION session( adapter );
    AI_OBJECT_REF pad( KIID(), PCB_PAD_T, wxS( "preview-pad" ) );

    const uint64_t id = session.Begin();
    session.Show( pad );
    session.Clear();

    BOOST_CHECK_EQUAL( id, 1 );
    BOOST_REQUIRE_EQUAL( adapter.m_Events.size(), 3 );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 0 ), wxS( "begin:1" ) );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 1 ), wxS( "show:preview-pad" ) );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 2 ), wxS( "clear:1" ) );
}

BOOST_AUTO_TEST_SUITE_END()
```

- [ ] **Step 2: Register failing preview test**

Add to `QA_COMMON_SRCS`:

```cmake
    test_ai_preview_session.cpp
```

Run:

```powershell
cmake --build --preset x64-release --target qa_common
```

Expected:

- Build fails because `<kisurf/ai/ai_preview_session.h>` does not exist yet.

- [ ] **Step 3: Add preview session header**

Create `include/kisurf/ai/ai_preview_session.h` with:

```cpp
#pragma once

#include <import_export.h>
#include <kisurf/ai/ai_types.h>

#include <cstdint>

class APIEXPORT AI_PREVIEW_ADAPTER
{
public:
    virtual ~AI_PREVIEW_ADAPTER() = default;

    virtual void BeginPreview( uint64_t aPreviewId ) = 0;
    virtual void ShowObject( uint64_t aPreviewId, const AI_OBJECT_REF& aObject ) = 0;
    virtual void ClearPreview( uint64_t aPreviewId ) = 0;
};

class APIEXPORT AI_PREVIEW_SESSION
{
public:
    explicit AI_PREVIEW_SESSION( AI_PREVIEW_ADAPTER& aAdapter );

    uint64_t Begin();
    void Show( const AI_OBJECT_REF& aObject );
    void Clear();

private:
    AI_PREVIEW_ADAPTER& m_Adapter;
    uint64_t            m_NextPreviewId = 1;
    uint64_t            m_CurrentPreviewId = 0;
};
```

- [ ] **Step 4: Add preview session implementation**

Create `common/kisurf/ai/ai_preview_session.cpp` with:

```cpp
#include <kisurf/ai/ai_preview_session.h>

AI_PREVIEW_SESSION::AI_PREVIEW_SESSION( AI_PREVIEW_ADAPTER& aAdapter ) :
        m_Adapter( aAdapter )
{
}


uint64_t AI_PREVIEW_SESSION::Begin()
{
    m_CurrentPreviewId = m_NextPreviewId++;
    m_Adapter.BeginPreview( m_CurrentPreviewId );
    return m_CurrentPreviewId;
}


void AI_PREVIEW_SESSION::Show( const AI_OBJECT_REF& aObject )
{
    if( m_CurrentPreviewId == 0 )
        Begin();

    m_Adapter.ShowObject( m_CurrentPreviewId, aObject );
}


void AI_PREVIEW_SESSION::Clear()
{
    if( m_CurrentPreviewId == 0 )
        return;

    m_Adapter.ClearPreview( m_CurrentPreviewId );
    m_CurrentPreviewId = 0;
}
```

- [ ] **Step 5: Register preview source and test**

Add to `common/CMakeLists.txt`:

```cmake
    kisurf/ai/ai_preview_session.cpp
```

Run:

```powershell
cmake --build --preset x64-release --target qa_common
$env:KICAD_RUN_FROM_BUILD_DIR='1'; ctest --test-dir out/build/x64-release -R qa_common --output-on-failure
```

Expected:

- `AiPreviewSession` tests pass.

- [ ] **Step 6: Commit preview session**

Run:

```powershell
git add include/kisurf/ai/ai_preview_session.h common/kisurf/ai/ai_preview_session.cpp common/CMakeLists.txt qa/tests/common/CMakeLists.txt qa/tests/common/test_ai_preview_session.cpp
git commit -m "feat: add ai preview session"
```

Expected:

- Commit succeeds.

## Task 3: Accepted Edit Session Contract

**Files:**
- Create: `include/kisurf/ai/ai_edit_session.h`
- Create: `common/kisurf/ai/ai_edit_session.cpp`
- Test: `qa/tests/common/test_ai_edit_session.cpp`
- Modify: `common/CMakeLists.txt:90`
- Modify: `qa/tests/common/CMakeLists.txt:24`

- [ ] **Step 1: Write failing edit session tests**

Create `qa/tests/common/test_ai_edit_session.cpp` with:

```cpp
#include <boost/test/unit_test.hpp>
#include <kisurf/ai/ai_edit_session.h>

class FAKE_EDIT_ADAPTER : public AI_EDIT_ADAPTER
{
public:
    bool ApplyObject( const AI_OBJECT_REF& aObject ) override
    {
        m_Applied.push_back( aObject.m_Label );
        return true;
    }

    std::vector<wxString> m_Applied;
};

BOOST_AUTO_TEST_SUITE( AiEditSession )

BOOST_AUTO_TEST_CASE( ApplyRecordsAcceptedObjectsAndValidation )
{
    FAKE_EDIT_ADAPTER adapter;
    AI_EDIT_SESSION session( adapter );
    AI_OBJECT_REF trace( KIID(), PCB_TRACE_T, wxS( "route-a" ) );

    AI_VALIDATION_SUMMARY validation;
    validation.m_Issues.push_back( { AI_VALIDATION_SEVERITY::Info, wxS( "accepted" ), true } );

    BOOST_CHECK( session.Apply( { trace }, validation ) );
    BOOST_REQUIRE_EQUAL( adapter.m_Applied.size(), 1 );
    BOOST_CHECK_EQUAL( adapter.m_Applied.front(), wxS( "route-a" ) );
    BOOST_CHECK_EQUAL( session.LastValidation().WorstSeverity(), AI_VALIDATION_SEVERITY::Info );
}

BOOST_AUTO_TEST_SUITE_END()
```

- [ ] **Step 2: Register failing edit test**

Add to `QA_COMMON_SRCS`:

```cmake
    test_ai_edit_session.cpp
```

Run:

```powershell
cmake --build --preset x64-release --target qa_common
```

Expected:

- Build fails because `<kisurf/ai/ai_edit_session.h>` does not exist yet.

- [ ] **Step 3: Add edit session header**

Create `include/kisurf/ai/ai_edit_session.h` with:

```cpp
#pragma once

#include <import_export.h>
#include <kisurf/ai/ai_types.h>

#include <vector>

class APIEXPORT AI_EDIT_ADAPTER
{
public:
    virtual ~AI_EDIT_ADAPTER() = default;

    virtual bool ApplyObject( const AI_OBJECT_REF& aObject ) = 0;
};

class APIEXPORT AI_EDIT_SESSION
{
public:
    explicit AI_EDIT_SESSION( AI_EDIT_ADAPTER& aAdapter );

    bool Apply( const std::vector<AI_OBJECT_REF>& aObjects,
                const AI_VALIDATION_SUMMARY& aValidation );

    const AI_VALIDATION_SUMMARY& LastValidation() const { return m_LastValidation; }

private:
    AI_EDIT_ADAPTER&     m_Adapter;
    AI_VALIDATION_SUMMARY m_LastValidation;
};
```

- [ ] **Step 4: Add edit session implementation**

Create `common/kisurf/ai/ai_edit_session.cpp` with:

```cpp
#include <kisurf/ai/ai_edit_session.h>

AI_EDIT_SESSION::AI_EDIT_SESSION( AI_EDIT_ADAPTER& aAdapter ) :
        m_Adapter( aAdapter )
{
}


bool AI_EDIT_SESSION::Apply( const std::vector<AI_OBJECT_REF>& aObjects,
                             const AI_VALIDATION_SUMMARY& aValidation )
{
    if( aValidation.HasBlockingIssue() )
        return false;

    for( const AI_OBJECT_REF& object : aObjects )
    {
        if( !m_Adapter.ApplyObject( object ) )
            return false;
    }

    m_LastValidation = aValidation;
    return true;
}
```

- [ ] **Step 5: Register edit source and test**

Add to `common/CMakeLists.txt`:

```cmake
    kisurf/ai/ai_edit_session.cpp
```

Run:

```powershell
cmake --build --preset x64-release --target qa_common
$env:KICAD_RUN_FROM_BUILD_DIR='1'; ctest --test-dir out/build/x64-release -R qa_common --output-on-failure
```

Expected:

- `AiEditSession` tests pass.

- [ ] **Step 6: Commit edit session**

Run:

```powershell
git add include/kisurf/ai/ai_edit_session.h common/kisurf/ai/ai_edit_session.cpp common/CMakeLists.txt qa/tests/common/CMakeLists.txt qa/tests/common/test_ai_edit_session.cpp
git commit -m "feat: add ai edit session"
```

Expected:

- Commit succeeds.

## Task 4: PCB Context Adapter

**Files:**
- Create: `pcbnew/kisurf_ai_pcb_context_adapter.h`
- Create: `pcbnew/kisurf_ai_pcb_context_adapter.cpp`
- Test: `qa/tests/pcbnew/test_ai_pcb_context_adapter.cpp`
- Modify: `pcbnew/CMakeLists.txt:519`
- Modify: `qa/tests/pcbnew/CMakeLists.txt:22`

- [ ] **Step 1: Write PCB adapter test**

Create `qa/tests/pcbnew/test_ai_pcb_context_adapter.cpp` with a small board fixture:

```cpp
#include <boost/test/unit_test.hpp>
#include <board.h>
#include <footprint.h>
#include <kisurf_ai_pcb_context_adapter.h>
#include <pad.h>

BOOST_AUTO_TEST_SUITE( AiPcbContextAdapter )

BOOST_AUTO_TEST_CASE( AdapterCollectsPadsAsVisibleObjects )
{
    BOARD board;
    FOOTPRINT* footprint = new FOOTPRINT( &board );
    PAD* pad = new PAD( footprint );

    pad->SetName( wxS( "1" ) );
    footprint->SetReference( wxS( "U1" ) );
    footprint->Add( pad );
    board.Add( footprint );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_INDEX index = adapter.BuildIndex();

    BOOST_CHECK_EQUAL( index.EditorKind(), AI_EDITOR_KIND::Pcb );
    BOOST_CHECK( index.Version().IsValid() );
    BOOST_CHECK( !index.VisibleObjects().empty() );
}

BOOST_AUTO_TEST_SUITE_END()
```

- [ ] **Step 2: Add PCB adapter header**

Create `pcbnew/kisurf_ai_pcb_context_adapter.h` with:

```cpp
#pragma once

#include <kisurf/ai/ai_context_index.h>

class BOARD;

class KISURF_AI_PCB_CONTEXT_ADAPTER
{
public:
    explicit KISURF_AI_PCB_CONTEXT_ADAPTER( BOARD& aBoard );

    AI_CONTEXT_INDEX BuildIndex() const;

private:
    BOARD& m_Board;
};
```

- [ ] **Step 3: Add PCB adapter implementation**

Create `pcbnew/kisurf_ai_pcb_context_adapter.cpp` with:

```cpp
#include <kisurf_ai_pcb_context_adapter.h>

#include <board.h>
#include <board_item.h>
#include <footprint.h>
#include <pad.h>

KISURF_AI_PCB_CONTEXT_ADAPTER::KISURF_AI_PCB_CONTEXT_ADAPTER( BOARD& aBoard ) :
        m_Board( aBoard )
{
}


AI_CONTEXT_INDEX KISURF_AI_PCB_CONTEXT_ADAPTER::BuildIndex() const
{
    AI_CONTEXT_INDEX index( AI_EDITOR_KIND::Pcb );
    std::vector<AI_OBJECT_REF> objects;

    for( FOOTPRINT* footprint : m_Board.Footprints() )
    {
        for( PAD* pad : footprint->Pads() )
        {
            objects.emplace_back( pad->m_Uuid, pad->Type(),
                                  footprint->GetReference() + wxS( "." ) + pad->GetName() );
        }
    }

    index.SetVisibleObjects( objects );
    return index;
}
```

- [ ] **Step 4: Register PCB adapter and test**

In `pcbnew/CMakeLists.txt`, add to `PCBNEW_SRCS`:

```cmake
    kisurf_ai_pcb_context_adapter.cpp
```

In `qa/tests/pcbnew/CMakeLists.txt`, add to `QA_PCBNEW_SRCS`:

```cmake
    test_ai_pcb_context_adapter.cpp
```

Run:

```powershell
cmake --build --preset x64-release --target qa_pcbnew
$env:KICAD_RUN_FROM_BUILD_DIR='1'; ctest --test-dir out/build/x64-release -R qa_pcbnew --output-on-failure
```

Expected:

- PCB adapter test passes.
- If the compiler rejects a method name in the snippet, stop the task, inspect the exact owning header named by the compiler, update this plan line with the exact replacement call, then rerun the same command before editing other files.

- [ ] **Step 5: Commit PCB context adapter**

Run:

```powershell
git add pcbnew/kisurf_ai_pcb_context_adapter.h pcbnew/kisurf_ai_pcb_context_adapter.cpp pcbnew/CMakeLists.txt qa/tests/pcbnew/CMakeLists.txt qa/tests/pcbnew/test_ai_pcb_context_adapter.cpp
git commit -m "feat: add pcb ai context adapter"
```

Expected:

- Commit succeeds.

## Task 5: Schematic Context Adapter

**Files:**
- Create: `eeschema/kisurf_ai_sch_context_adapter.h`
- Create: `eeschema/kisurf_ai_sch_context_adapter.cpp`
- Test: `qa/tests/eeschema/test_ai_sch_context_adapter.cpp`
- Modify: `eeschema/CMakeLists.txt:372`
- Modify: `qa/tests/eeschema/CMakeLists.txt:41`

- [ ] **Step 1: Write schematic adapter test**

Create `qa/tests/eeschema/test_ai_sch_context_adapter.cpp`:

```cpp
#include <boost/test/unit_test.hpp>
#include <kisurf_ai_sch_context_adapter.h>
#include <sch_screen.h>
#include <sch_symbol.h>

BOOST_AUTO_TEST_SUITE( AiSchContextAdapter )

BOOST_AUTO_TEST_CASE( AdapterReportsSchematicEditorKind )
{
    SCH_SCREEN screen;
    KISURF_AI_SCH_CONTEXT_ADAPTER adapter( screen );

    AI_CONTEXT_INDEX index = adapter.BuildIndex();

    BOOST_CHECK_EQUAL( index.EditorKind(), AI_EDITOR_KIND::Schematic );
}

BOOST_AUTO_TEST_SUITE_END()
```

- [ ] **Step 2: Add schematic adapter header**

Create `eeschema/kisurf_ai_sch_context_adapter.h` with:

```cpp
#pragma once

#include <kisurf/ai/ai_context_index.h>

class SCH_SCREEN;

class KISURF_AI_SCH_CONTEXT_ADAPTER
{
public:
    explicit KISURF_AI_SCH_CONTEXT_ADAPTER( SCH_SCREEN& aScreen );

    AI_CONTEXT_INDEX BuildIndex() const;

private:
    SCH_SCREEN& m_Screen;
};
```

- [ ] **Step 3: Add schematic adapter implementation**

Create `eeschema/kisurf_ai_sch_context_adapter.cpp` with:

```cpp
#include <kisurf_ai_sch_context_adapter.h>

#include <sch_item.h>
#include <sch_screen.h>

KISURF_AI_SCH_CONTEXT_ADAPTER::KISURF_AI_SCH_CONTEXT_ADAPTER( SCH_SCREEN& aScreen ) :
        m_Screen( aScreen )
{
}


AI_CONTEXT_INDEX KISURF_AI_SCH_CONTEXT_ADAPTER::BuildIndex() const
{
    AI_CONTEXT_INDEX index( AI_EDITOR_KIND::Schematic );
    std::vector<AI_OBJECT_REF> objects;

    for( SCH_ITEM* item : m_Screen.Items() )
    {
        objects.emplace_back( item->m_Uuid, item->Type(), item->GetFriendlyName() );
    }

    index.SetVisibleObjects( objects );
    return index;
}
```

- [ ] **Step 4: Register schematic adapter and test**

In `eeschema/CMakeLists.txt`, add to `EESCHEMA_SRCS`:

```cmake
    kisurf_ai_sch_context_adapter.cpp
```

In `qa/tests/eeschema/CMakeLists.txt`, add to `QA_EESCHEMA_SRCS`:

```cmake
    test_ai_sch_context_adapter.cpp
```

Run:

```powershell
cmake --build --preset x64-release --target qa_eeschema
$env:KICAD_RUN_FROM_BUILD_DIR='1'; ctest --test-dir out/build/x64-release -R qa_eeschema --output-on-failure
```

Expected:

- Schematic adapter test passes.
- If the compiler rejects a method name in the snippet, stop the task, inspect the exact owning header named by the compiler, update this plan line with the exact replacement call, then rerun the same command before editing other files.

- [ ] **Step 5: Commit schematic context adapter**

Run:

```powershell
git add eeschema/kisurf_ai_sch_context_adapter.h eeschema/kisurf_ai_sch_context_adapter.cpp eeschema/CMakeLists.txt qa/tests/eeschema/CMakeLists.txt qa/tests/eeschema/test_ai_sch_context_adapter.cpp
git commit -m "feat: add schematic ai context adapter"
```

Expected:

- Commit succeeds.

## Acceptance Criteria

- Common context, preview, and edit session tests pass in `qa_common`.
- PCB context adapter test passes in `qa_pcbnew`.
- Schematic context adapter test passes in `qa_eeschema`.
- Preview session owns non-persistent preview lifecycle through an adapter.
- Accepted edits go through `AI_EDIT_ADAPTER`, leaving real `BOARD_COMMIT` and `SCH_COMMIT` adapters as the next implementation layer after adapter tests are green.
