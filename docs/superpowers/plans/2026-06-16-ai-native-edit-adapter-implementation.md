# AI Native Edit Adapter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add transaction-safe, commit-backed PCB and schematic AI edit adapters that materialize accepted move suggestions through native KiCad undo/redo seams.

**Architecture:** Extend the common `AI_EDIT_SESSION` lifecycle with begin/end/abort hooks, then implement editor-local move adapters that resolve `AI_OBJECT_REF` values, stage native commits before mutation, push one commit on success, and revert on failure.

**Tech Stack:** C++20, KiCad `COMMIT`, `BOARD_ITEM`, `SCH_ITEM`, `VECTOR2I`, Boost.Test, `qa_common`, `qa_pcbnew`, `qa_eeschema`.

---

## File Structure

- Modify: `include/kisurf/ai/ai_edit_session.h`
  - Add default transaction lifecycle hooks to `AI_EDIT_ADAPTER`.
- Modify: `common/kisurf/ai/ai_edit_session.cpp`
  - Sequence validation, begin, per-object apply, end, and abort.
- Modify: `qa/tests/common/test_ai_edit_session.cpp`
  - Add lifecycle and abort coverage while preserving existing tests.
- Create: `pcbnew/kisurf_ai_pcb_move_edit_adapter.h`
  - PCB move-by-delta adapter implementing `AI_EDIT_ADAPTER`.
- Create: `pcbnew/kisurf_ai_pcb_move_edit_adapter.cpp`
  - Resolve refs to `BOARD_ITEM*`, stage `COMMIT::Modify`, move, push/revert.
- Modify: `pcbnew/CMakeLists.txt`
  - Add `kisurf_ai_pcb_move_edit_adapter.cpp` next to the object/preview adapters.
- Create: `qa/tests/pcbnew/test_ai_pcb_move_edit_adapter.cpp`
  - PCB commit-backed move adapter tests.
- Modify: `qa/tests/pcbnew/CMakeLists.txt`
  - Add the PCB move adapter test.
- Create: `eeschema/kisurf_ai_sch_move_edit_adapter.h`
  - Schematic move-by-delta adapter implementing `AI_EDIT_ADAPTER`.
- Create: `eeschema/kisurf_ai_sch_move_edit_adapter.cpp`
  - Resolve refs to `SCH_ITEM*`, stage with `SCH_SCREEN`, move, push/revert.
- Modify: `eeschema/CMakeLists.txt`
  - Add `kisurf_ai_sch_move_edit_adapter.cpp` next to the object/preview adapters.
- Create: `qa/tests/eeschema/test_ai_sch_move_edit_adapter.cpp`
  - Schematic commit-backed move adapter tests.
- Modify: `qa/tests/eeschema/CMakeLists.txt`
  - Add the schematic move adapter test.

## Verification Command Templates

Common targeted regression:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_common -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiEditSession,AiSuggestionOrchestrator,AiAgentPanelModel --log_level=test_suite"
```

PCB targeted test:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_pcbnew -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\pcbnew\qa_pcbnew.exe --run_test=AiPcbMoveEditAdapter,AiPcbObjectResolver,AiPcbContextAdapter --log_level=test_suite"
```

Schematic targeted test:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_eeschema -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\eeschema\qa_eeschema.exe --run_test=AiSchMoveEditAdapter,AiSchObjectResolver,AiSchContextAdapter --log_level=test_suite"
```

Expected final result for each command: exit code `0` and Boost reports no
errors. The known schema warning about `qa/tests/schemas/api.v1.schema.json` is
acceptable when the exit code is `0`.

## Task 1: Common Edit Session Lifecycle

**Files:**
- Modify: `include/kisurf/ai/ai_edit_session.h`
- Modify: `common/kisurf/ai/ai_edit_session.cpp`
- Modify: `qa/tests/common/test_ai_edit_session.cpp`

- [ ] **Step 1: Write failing lifecycle tests**

In `qa/tests/common/test_ai_edit_session.cpp`, add a new adapter and three test
cases below the existing fake adapter:

```cpp
class LIFECYCLE_EDIT_ADAPTER : public AI_EDIT_ADAPTER
{
public:
    bool BeginApply( const AI_VALIDATION_SUMMARY&, size_t aObjectCount ) override
    {
        m_Events.push_back( wxString::Format( wxS( "begin:%zu" ), aObjectCount ) );
        return m_BeginResult;
    }

    bool ApplyObject( const AI_OBJECT_REF& aObject ) override
    {
        m_Events.push_back( wxS( "apply:" ) + aObject.m_Label );
        return m_ApplyResult;
    }

    bool EndApply() override
    {
        m_Events.push_back( wxS( "end" ) );
        return m_EndResult;
    }

    void AbortApply() override
    {
        m_Events.push_back( wxS( "abort" ) );
    }

    bool                  m_BeginResult = true;
    bool                  m_ApplyResult = true;
    bool                  m_EndResult = true;
    std::vector<wxString> m_Events;
};

BOOST_AUTO_TEST_CASE( LifecycleWrapsSuccessfulApply )
{
    LIFECYCLE_EDIT_ADAPTER adapter;
    AI_EDIT_SESSION        session( adapter );
    AI_OBJECT_REF          first( KIID(), PCB_PAD_T, wxS( "first" ) );
    AI_OBJECT_REF          second( KIID(), PCB_PAD_T, wxS( "second" ) );

    BOOST_CHECK( session.Apply( { first, second }, AI_VALIDATION_SUMMARY() ) );
    BOOST_REQUIRE_EQUAL( adapter.m_Events.size(), 4 );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 0 ), wxString( wxS( "begin:2" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 1 ), wxString( wxS( "apply:first" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 2 ), wxString( wxS( "apply:second" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 3 ), wxString( wxS( "end" ) ) );
}

BOOST_AUTO_TEST_CASE( ApplyFailureAbortsAndDoesNotStoreValidation )
{
    LIFECYCLE_EDIT_ADAPTER adapter;
    adapter.m_ApplyResult = false;

    AI_EDIT_SESSION session( adapter );
    AI_VALIDATION_SUMMARY validation;
    validation.m_Issues.push_back( { AI_VALIDATION_SEVERITY::Info, wxS( "candidate" ), false } );

    BOOST_CHECK( !session.Apply( { AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "bad" ) ) },
                                 validation ) );
    BOOST_REQUIRE_EQUAL( adapter.m_Events.size(), 3 );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 0 ), wxString( wxS( "begin:1" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 1 ), wxString( wxS( "apply:bad" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 2 ), wxString( wxS( "abort" ) ) );
    BOOST_CHECK( session.LastValidation().WorstSeverity() == AI_VALIDATION_SEVERITY::None );
}

BOOST_AUTO_TEST_CASE( EndFailureAborts )
{
    LIFECYCLE_EDIT_ADAPTER adapter;
    adapter.m_EndResult = false;

    AI_EDIT_SESSION session( adapter );

    BOOST_CHECK( !session.Apply( { AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "late" ) ) },
                                 AI_VALIDATION_SUMMARY() ) );
    BOOST_REQUIRE_EQUAL( adapter.m_Events.size(), 4 );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 0 ), wxString( wxS( "begin:1" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 1 ), wxString( wxS( "apply:late" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 2 ), wxString( wxS( "end" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 3 ), wxString( wxS( "abort" ) ) );
}
```

- [ ] **Step 2: Run common test to verify RED**

Run the common verification command with `--run_test=AiEditSession`.

Expected RED: compile fails because `AI_EDIT_ADAPTER` has no `BeginApply`,
`EndApply`, or `AbortApply` methods to override.

- [ ] **Step 3: Add default lifecycle hooks**

In `include/kisurf/ai/ai_edit_session.h`:

```cpp
#include <cstddef>
```

Add the lifecycle methods before `ApplyObject(...)`:

```cpp
virtual bool BeginApply( const AI_VALIDATION_SUMMARY& aValidation, size_t aObjectCount )
{
    wxUnusedVar( aValidation );
    wxUnusedVar( aObjectCount );
    return true;
}

virtual bool ApplyObject( const AI_OBJECT_REF& aObject ) = 0;

virtual bool EndApply() { return true; }
virtual void AbortApply() {}
```

- [ ] **Step 4: Sequence lifecycle in edit session**

Replace `AI_EDIT_SESSION::Apply(...)` in
`common/kisurf/ai/ai_edit_session.cpp` with:

```cpp
bool AI_EDIT_SESSION::Apply( const std::vector<AI_OBJECT_REF>& aObjects,
                             const AI_VALIDATION_SUMMARY& aValidation )
{
    if( aValidation.HasBlockingIssue() )
        return false;

    if( !m_Adapter.BeginApply( aValidation, aObjects.size() ) )
        return false;

    for( const AI_OBJECT_REF& object : aObjects )
    {
        if( !m_Adapter.ApplyObject( object ) )
        {
            m_Adapter.AbortApply();
            return false;
        }
    }

    if( !m_Adapter.EndApply() )
    {
        m_Adapter.AbortApply();
        return false;
    }

    m_LastValidation = aValidation;
    return true;
}
```

- [ ] **Step 5: Run common test to verify GREEN**

Run the common verification command with:
`--run_test=AiEditSession,AiSuggestionOrchestrator,AiAgentPanelModel`.

Expected: exit code `0`.

- [ ] **Step 6: Commit common lifecycle**

```powershell
git add include/kisurf/ai/ai_edit_session.h common/kisurf/ai/ai_edit_session.cpp qa/tests/common/test_ai_edit_session.cpp
git commit -m "feat: add ai edit session lifecycle"
```

## Task 2: PCB Move Edit Adapter

**Files:**
- Create: `pcbnew/kisurf_ai_pcb_move_edit_adapter.h`
- Create: `pcbnew/kisurf_ai_pcb_move_edit_adapter.cpp`
- Modify: `pcbnew/CMakeLists.txt`
- Create: `qa/tests/pcbnew/test_ai_pcb_move_edit_adapter.cpp`
- Modify: `qa/tests/pcbnew/CMakeLists.txt`

- [ ] **Step 1: Write failing PCB move adapter tests**

Create `qa/tests/pcbnew/test_ai_pcb_move_edit_adapter.cpp` with:

- includes for `boost/test/unit_test.hpp`, `board.h`, `board_item.h`,
  `commit.h`, `footprint.h`, `kisurf_ai_pcb_context_adapter.h`,
  `kisurf_ai_pcb_move_edit_adapter.h`, `kisurf_ai_pcb_object_resolver.h`,
  `pad.h`, `<algorithm>`, `<functional>`, `<vector>`, and `<wx/string.h>`.
- `PCB_SPY_COMMIT : public COMMIT` whose `Stage(...)` records modified
  `BOARD_ITEM*`, original positions, screens, and change types; `Push(...)`
  increments `m_PushCount`; `Revert()` restores recorded positions in reverse
  order and increments `m_RevertCount`.
- a `PCB_MOVE_FIXTURE` with one `BOARD`, one `FOOTPRINT`, and two `PAD`s at
  stable positions.
- a helper that finds an `AI_OBJECT_REF` by label from
  `KISURF_AI_PCB_CONTEXT_ADAPTER`.
- test `SessionMovesResolvedPadThroughOneCommit`:
  - create resolver, spy commit, adapter delta `VECTOR2I( 100, -25 )`, and
    `AI_EDIT_SESSION`;
  - apply one pad ref;
  - assert apply returns true;
  - assert spy commit saw one modify and one push;
  - assert no revert;
  - assert the pad position changed by the delta;
  - assert adapter `WasCommitted()` is true and `MovedItems().size() == 1`.
- test `UnknownReferenceRevertsAndDoesNotPush`:
  - apply an unknown UUID ref;
  - assert apply returns false;
  - assert push count is zero and revert count is one;
  - assert adapter `FailedObjects().size() == 1`.
- test `SecondObjectFailureRevertsFirstMove`:
  - apply a valid pad ref followed by an unknown ref;
  - assert apply returns false;
  - assert first pad position is restored to its original position;
  - assert push count is zero and revert count is one.

- [ ] **Step 2: Register failing PCB test**

Add to `qa/tests/pcbnew/CMakeLists.txt` near the object/preview adapter tests:

```cmake
    test_ai_pcb_move_edit_adapter.cpp
```

- [ ] **Step 3: Run PCB test to verify RED**

Run the PCB verification command with `--run_test=AiPcbMoveEditAdapter`.

Expected RED: compile fails because `kisurf_ai_pcb_move_edit_adapter.h` does not
exist.

- [ ] **Step 4: Add PCB move adapter header**

Create `pcbnew/kisurf_ai_pcb_move_edit_adapter.h`:

```cpp
#pragma once

#include <kisurf/ai/ai_edit_session.h>
#include <math/vector2d.h>

#include <vector>
#include <wx/string.h>

class BOARD_ITEM;
class COMMIT;
class KISURF_AI_PCB_OBJECT_RESOLVER;

class KISURF_AI_PCB_MOVE_EDIT_ADAPTER : public AI_EDIT_ADAPTER
{
public:
    KISURF_AI_PCB_MOVE_EDIT_ADAPTER( KISURF_AI_PCB_OBJECT_RESOLVER& aResolver,
                                     COMMIT& aCommit, const VECTOR2I& aDelta,
                                     wxString aCommitMessage = wxS( "Apply AI PCB edit" ) );

    bool BeginApply( const AI_VALIDATION_SUMMARY& aValidation, size_t aObjectCount ) override;
    bool ApplyObject( const AI_OBJECT_REF& aObject ) override;
    bool EndApply() override;
    void AbortApply() override;

    const std::vector<BOARD_ITEM*>& MovedItems() const { return m_MovedItems; }
    const std::vector<AI_OBJECT_REF>& FailedObjects() const { return m_FailedObjects; }
    bool WasCommitted() const { return m_WasCommitted; }
    bool WasReverted() const { return m_WasReverted; }

private:
    KISURF_AI_PCB_OBJECT_RESOLVER& m_Resolver;
    COMMIT&                        m_Commit;
    VECTOR2I                       m_Delta;
    wxString                       m_CommitMessage;
    std::vector<BOARD_ITEM*>        m_MovedItems;
    std::vector<AI_OBJECT_REF>      m_FailedObjects;
    bool                           m_WasCommitted = false;
    bool                           m_WasReverted = false;
};
```

- [ ] **Step 5: Add PCB move adapter implementation**

Create `pcbnew/kisurf_ai_pcb_move_edit_adapter.cpp`:

```cpp
#include <kisurf_ai_pcb_move_edit_adapter.h>

#include <board_item.h>
#include <commit.h>
#include <kisurf_ai_pcb_object_resolver.h>

KISURF_AI_PCB_MOVE_EDIT_ADAPTER::KISURF_AI_PCB_MOVE_EDIT_ADAPTER(
        KISURF_AI_PCB_OBJECT_RESOLVER& aResolver, COMMIT& aCommit,
        const VECTOR2I& aDelta, wxString aCommitMessage ) :
        m_Resolver( aResolver ),
        m_Commit( aCommit ),
        m_Delta( aDelta ),
        m_CommitMessage( std::move( aCommitMessage ) )
{
}

bool KISURF_AI_PCB_MOVE_EDIT_ADAPTER::BeginApply( const AI_VALIDATION_SUMMARY&,
                                                  size_t aObjectCount )
{
    m_MovedItems.clear();
    m_FailedObjects.clear();
    m_WasCommitted = false;
    m_WasReverted = false;
    return aObjectCount > 0;
}

bool KISURF_AI_PCB_MOVE_EDIT_ADAPTER::ApplyObject( const AI_OBJECT_REF& aObject )
{
    BOARD_ITEM* item = m_Resolver.Resolve( aObject );

    if( !item )
    {
        m_FailedObjects.push_back( aObject );
        return false;
    }

    m_Commit.Modify( item );
    item->Move( m_Delta );
    m_MovedItems.push_back( item );
    return true;
}

bool KISURF_AI_PCB_MOVE_EDIT_ADAPTER::EndApply()
{
    if( m_MovedItems.empty() )
        return false;

    m_Commit.Push( m_CommitMessage );
    m_WasCommitted = true;
    return true;
}

void KISURF_AI_PCB_MOVE_EDIT_ADAPTER::AbortApply()
{
    m_Commit.Revert();
    m_WasReverted = true;
    m_MovedItems.clear();
}
```

Add to `pcbnew/CMakeLists.txt` next to the object/preview adapters:

```cmake
    kisurf_ai_pcb_move_edit_adapter.cpp
```

- [ ] **Step 6: Run PCB test to verify GREEN**

Run the PCB verification command with:
`--run_test=AiPcbMoveEditAdapter,AiPcbObjectResolver,AiPcbContextAdapter`.

Expected: exit code `0`.

- [ ] **Step 7: Commit PCB adapter**

```powershell
git add pcbnew/kisurf_ai_pcb_move_edit_adapter.h pcbnew/kisurf_ai_pcb_move_edit_adapter.cpp pcbnew/CMakeLists.txt qa/tests/pcbnew/test_ai_pcb_move_edit_adapter.cpp qa/tests/pcbnew/CMakeLists.txt
git commit -m "feat: add pcb ai move edit adapter"
```

## Task 3: Schematic Move Edit Adapter

**Files:**
- Create: `eeschema/kisurf_ai_sch_move_edit_adapter.h`
- Create: `eeschema/kisurf_ai_sch_move_edit_adapter.cpp`
- Modify: `eeschema/CMakeLists.txt`
- Create: `qa/tests/eeschema/test_ai_sch_move_edit_adapter.cpp`
- Modify: `qa/tests/eeschema/CMakeLists.txt`

- [ ] **Step 1: Write failing schematic move adapter tests**

Create `qa/tests/eeschema/test_ai_sch_move_edit_adapter.cpp` with:

- includes for `boost/test/unit_test.hpp`, `commit.h`,
  `kisurf_ai_sch_context_adapter.h`, `kisurf_ai_sch_move_edit_adapter.h`,
  `kisurf_ai_sch_object_resolver.h`, `sch_item.h`, `sch_screen.h`,
  `sch_symbol.h`, `<algorithm>`, `<vector>`, and `<wx/string.h>`.
- `SCH_SPY_COMMIT : public COMMIT` whose `Stage(...)` records modified
  `SCH_ITEM*`, original positions, screen pointers, and change types; `Push(...)`
  increments `m_PushCount`; `Revert()` restores positions and increments
  `m_RevertCount`.
- a `SCH_MOVE_FIXTURE` with one `SCH_SCREEN` and two `SCH_SYMBOL`s at stable
  positions.
- a helper that finds refs by label from `KISURF_AI_SCH_CONTEXT_ADAPTER`.
- test `SessionMovesResolvedSymbolThroughOneCommit`:
  - apply one symbol ref with delta `VECTOR2I( -50, 75 )`;
  - assert one modify, one push, no revert;
  - assert modified screen pointer equals the fixture screen;
  - assert symbol position changed by the delta.
- test `UnknownReferenceRevertsAndDoesNotPush`.
- test `SecondObjectFailureRevertsFirstMove`.

- [ ] **Step 2: Register failing schematic test**

Add to `qa/tests/eeschema/CMakeLists.txt` near object/preview adapter tests:

```cmake
    test_ai_sch_move_edit_adapter.cpp
```

- [ ] **Step 3: Run schematic test to verify RED**

Run the schematic verification command with `--run_test=AiSchMoveEditAdapter`.

Expected RED: compile fails because `kisurf_ai_sch_move_edit_adapter.h` does not
exist.

- [ ] **Step 4: Add schematic move adapter header**

Create `eeschema/kisurf_ai_sch_move_edit_adapter.h`:

```cpp
#pragma once

#include <kisurf/ai/ai_edit_session.h>
#include <math/vector2d.h>

#include <vector>
#include <wx/string.h>

class COMMIT;
class KISURF_AI_SCH_OBJECT_RESOLVER;
class SCH_ITEM;
class SCH_SCREEN;

class KISURF_AI_SCH_MOVE_EDIT_ADAPTER : public AI_EDIT_ADAPTER
{
public:
    KISURF_AI_SCH_MOVE_EDIT_ADAPTER( KISURF_AI_SCH_OBJECT_RESOLVER& aResolver,
                                     SCH_SCREEN& aScreen, COMMIT& aCommit,
                                     const VECTOR2I& aDelta,
                                     wxString aCommitMessage =
                                             wxS( "Apply AI schematic edit" ) );

    bool BeginApply( const AI_VALIDATION_SUMMARY& aValidation, size_t aObjectCount ) override;
    bool ApplyObject( const AI_OBJECT_REF& aObject ) override;
    bool EndApply() override;
    void AbortApply() override;

    const std::vector<SCH_ITEM*>& MovedItems() const { return m_MovedItems; }
    const std::vector<AI_OBJECT_REF>& FailedObjects() const { return m_FailedObjects; }
    bool WasCommitted() const { return m_WasCommitted; }
    bool WasReverted() const { return m_WasReverted; }

private:
    KISURF_AI_SCH_OBJECT_RESOLVER& m_Resolver;
    SCH_SCREEN&                    m_Screen;
    COMMIT&                        m_Commit;
    VECTOR2I                       m_Delta;
    wxString                       m_CommitMessage;
    std::vector<SCH_ITEM*>          m_MovedItems;
    std::vector<AI_OBJECT_REF>      m_FailedObjects;
    bool                           m_WasCommitted = false;
    bool                           m_WasReverted = false;
};
```

- [ ] **Step 5: Add schematic move adapter implementation**

Create `eeschema/kisurf_ai_sch_move_edit_adapter.cpp`:

```cpp
#include <kisurf_ai_sch_move_edit_adapter.h>

#include <commit.h>
#include <kisurf_ai_sch_object_resolver.h>
#include <sch_item.h>
#include <sch_screen.h>

KISURF_AI_SCH_MOVE_EDIT_ADAPTER::KISURF_AI_SCH_MOVE_EDIT_ADAPTER(
        KISURF_AI_SCH_OBJECT_RESOLVER& aResolver, SCH_SCREEN& aScreen,
        COMMIT& aCommit, const VECTOR2I& aDelta, wxString aCommitMessage ) :
        m_Resolver( aResolver ),
        m_Screen( aScreen ),
        m_Commit( aCommit ),
        m_Delta( aDelta ),
        m_CommitMessage( std::move( aCommitMessage ) )
{
}

bool KISURF_AI_SCH_MOVE_EDIT_ADAPTER::BeginApply( const AI_VALIDATION_SUMMARY&,
                                                  size_t aObjectCount )
{
    m_MovedItems.clear();
    m_FailedObjects.clear();
    m_WasCommitted = false;
    m_WasReverted = false;
    return aObjectCount > 0;
}

bool KISURF_AI_SCH_MOVE_EDIT_ADAPTER::ApplyObject( const AI_OBJECT_REF& aObject )
{
    SCH_ITEM* item = m_Resolver.Resolve( aObject );

    if( !item )
    {
        m_FailedObjects.push_back( aObject );
        return false;
    }

    m_Commit.Modify( item, &m_Screen );
    item->Move( m_Delta );
    m_MovedItems.push_back( item );
    return true;
}

bool KISURF_AI_SCH_MOVE_EDIT_ADAPTER::EndApply()
{
    if( m_MovedItems.empty() )
        return false;

    m_Commit.Push( m_CommitMessage );
    m_WasCommitted = true;
    return true;
}

void KISURF_AI_SCH_MOVE_EDIT_ADAPTER::AbortApply()
{
    m_Commit.Revert();
    m_WasReverted = true;
    m_MovedItems.clear();
}
```

Add to `eeschema/CMakeLists.txt` next to the object/preview adapters:

```cmake
    kisurf_ai_sch_move_edit_adapter.cpp
```

- [ ] **Step 6: Run schematic test to verify GREEN**

Run the schematic verification command with:
`--run_test=AiSchMoveEditAdapter,AiSchObjectResolver,AiSchContextAdapter`.

Expected: exit code `0`.

- [ ] **Step 7: Commit schematic adapter**

```powershell
git add eeschema/kisurf_ai_sch_move_edit_adapter.h eeschema/kisurf_ai_sch_move_edit_adapter.cpp eeschema/CMakeLists.txt qa/tests/eeschema/test_ai_sch_move_edit_adapter.cpp qa/tests/eeschema/CMakeLists.txt
git commit -m "feat: add schematic ai move edit adapter"
```

## Task 4: Final Verification

- [ ] **Step 1: Run common targeted regression**

Run the common verification command with:
`--run_test=AiEditSession,AiSuggestionOrchestrator,AiAgentPanelModel`.

- [ ] **Step 2: Run PCB targeted regression**

Run the PCB verification command with:
`--run_test=AiPcbMoveEditAdapter,AiPcbObjectResolver,AiPcbContextAdapter`.

- [ ] **Step 3: Run schematic targeted regression**

Run the schematic verification command with:
`--run_test=AiSchMoveEditAdapter,AiSchObjectResolver,AiSchContextAdapter`.

- [ ] **Step 4: Run diff checks**

```powershell
git diff --check
git diff --cached --check
git status --short
```

Expected:

- diff checks exit `0` except acceptable LF/CRLF warnings;
- status is clean after the task commits.

## Plan Self-Review

- Spec coverage: the plan implements the common lifecycle, PCB move adapter,
  schematic move adapter, native commit staging, single push, and rollback on
  failure.
- Placeholder scan: no placeholder sections remain.
- Type consistency: PCB uses `BOARD_ITEM*` and `COMMIT::Modify( item )`;
  schematic uses `SCH_ITEM*` and `COMMIT::Modify( item, &screen )`.
- Scope check: the plan does not add Agent pane buttons, JSON edit parsing,
  routing, placement, validation runners, or IPC transport changes.
