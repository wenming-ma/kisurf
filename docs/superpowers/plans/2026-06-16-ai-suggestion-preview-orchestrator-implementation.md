# AI Suggestion Preview Orchestrator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a headless native suggestion lifecycle that turns valid context/activity triggers into bounded preview/edit candidates.

**Architecture:** Shared data contracts live in `ai_types`; the orchestrator and provider interface live in a focused `ai_suggestion_orchestrator` module in `kicommon`. Tests use deterministic fake providers plus fake preview/edit adapters, with no network provider or editor UI integration in this phase.

**Tech Stack:** C++20, Boost.Test, KiSurf AI common modules, `AI_PREVIEW_SESSION`, `AI_EDIT_SESSION`, `qa_common`.

---

## File Structure

- Modify: `include/kisurf/ai/ai_types.h`
  - Add `AI_SUGGESTION_STATUS`, `AI_SUGGESTION_TRIGGER`, and
    `AI_SUGGESTION_RECORD`.
- Create: `include/kisurf/ai/ai_suggestion_orchestrator.h`
  - Declare `AI_SUGGESTION_PROVIDER` and `AI_SUGGESTION_ORCHESTRATOR`.
- Create: `common/kisurf/ai/ai_suggestion_orchestrator.cpp`
  - Implement trigger validation, provider calls, fingerprinting, bounded queue,
    duplicate suppression, lookup, preview, accept, reject, and expiration.
- Modify: `common/CMakeLists.txt`
  - Add `kisurf/ai/ai_suggestion_orchestrator.cpp` to `KICOMMON_SRCS`.
- Create: `qa/tests/common/test_ai_suggestion_orchestrator.cpp`
  - Unit-test update, duplicate suppression, capacity eviction, preview, accept,
    validation blocking, reject, and expiration.
- Modify: `qa/tests/common/CMakeLists.txt`
  - Add `test_ai_suggestion_orchestrator.cpp`.

## Verification Command Template

Use the Visual Studio developer environment:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_common -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiSuggestionOrchestrator --log_level=test_suite"
```

Expected final result: exit code `0` and Boost reports no errors. The known
schema warning about `qa/tests/schemas/api.v1.schema.json` is acceptable when
the exit code is `0`.

## Task 1: Suggestion Data Model And Queue Update

**Files:**
- Modify: `include/kisurf/ai/ai_types.h`
- Create: `include/kisurf/ai/ai_suggestion_orchestrator.h`
- Create: `common/kisurf/ai/ai_suggestion_orchestrator.cpp`
- Modify: `common/CMakeLists.txt`
- Create: `qa/tests/common/test_ai_suggestion_orchestrator.cpp`
- Modify: `qa/tests/common/CMakeLists.txt`

- [ ] **Step 1: Write failing update tests**

Create `test_ai_suggestion_orchestrator.cpp` with these helpers:

```cpp
#include <boost/test/unit_test.hpp>

#include <kisurf/ai/ai_suggestion_orchestrator.h>

#include <wx/arrstr.h>

namespace
{
class FAKE_SUGGESTION_PROVIDER : public AI_SUGGESTION_PROVIDER
{
public:
    std::optional<AI_SUGGESTION_RECORD> Suggest(
            const AI_SUGGESTION_TRIGGER& aTrigger ) override
    {
        ++m_CallCount;
        m_LastTrigger = aTrigger;

        if( !m_NextSuggestion )
            return std::nullopt;

        AI_SUGGESTION_RECORD suggestion = *m_NextSuggestion;
        m_NextSuggestion.reset();
        return suggestion;
    }

    int                             m_CallCount = 0;
    AI_SUGGESTION_TRIGGER           m_LastTrigger;
    std::optional<AI_SUGGESTION_RECORD> m_NextSuggestion;
};

AI_SUGGESTION_TRIGGER makeTrigger( uint64_t aDocRevision = 1,
                                   uint64_t aActivitySequence = 1 )
{
    AI_SUGGESTION_TRIGGER trigger;
    trigger.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextVersion.m_DocumentRevision = aDocRevision;
    trigger.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_Version.m_DocumentRevision = aDocRevision;
    trigger.m_ContextSnapshot.m_SelectedObjects.push_back(
            AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ) ) );
    trigger.m_Activity.m_Sequence = aActivitySequence;
    trigger.m_Activity.m_ActionName = wxS( "common.Interactive.selected" );
    trigger.m_Reason = wxS( "selection changed" );
    return trigger;
}

AI_SUGGESTION_RECORD makeSuggestion( const wxString& aTitle = wxS( "Place nearby cap" ) )
{
    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_Title = aTitle;
    suggestion.m_Body = wxS( "Preview a safe next step." );
    suggestion.m_PreviewObjects.push_back( AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ) ) );
    suggestion.m_EditObjects.push_back( AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ) ) );
    return suggestion;
}
}
```

Add test cases:

```cpp
BOOST_AUTO_TEST_SUITE( AiSuggestionOrchestrator )

BOOST_AUTO_TEST_CASE( ValidTriggerStoresProviderSuggestion )
{
    FAKE_SUGGESTION_PROVIDER provider;
    provider.m_NextSuggestion = makeSuggestion();

    AI_SUGGESTION_ORCHESTRATOR orchestrator( provider );
    std::optional<AI_SUGGESTION_RECORD> suggestion =
            orchestrator.Update( makeTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK_EQUAL( provider.m_CallCount, 1 );
    BOOST_CHECK_EQUAL( suggestion->m_Id, 1 );
    BOOST_CHECK_EQUAL( suggestion->m_Sequence, 1 );
    BOOST_CHECK( suggestion->m_Status == AI_SUGGESTION_STATUS::Pending );
    BOOST_CHECK( suggestion->m_EditorKind == AI_EDITOR_KIND::Pcb );
    BOOST_CHECK_EQUAL( suggestion->m_ContextVersion.m_DocumentRevision, 1 );
    BOOST_CHECK_EQUAL( suggestion->m_TriggerActivitySequence, 1 );
    BOOST_CHECK( !suggestion->m_Fingerprint.IsEmpty() );
    BOOST_REQUIRE_EQUAL( orchestrator.Records().size(), 1 );
}

BOOST_AUTO_TEST_CASE( InvalidTriggerDoesNotCallProvider )
{
    FAKE_SUGGESTION_PROVIDER provider;
    AI_SUGGESTION_ORCHESTRATOR orchestrator( provider );

    AI_SUGGESTION_TRIGGER trigger;
    trigger.m_EditorKind = AI_EDITOR_KIND::Unknown;

    BOOST_CHECK( !orchestrator.Update( trigger ).has_value() );
    BOOST_CHECK_EQUAL( provider.m_CallCount, 0 );
}

BOOST_AUTO_TEST_CASE( DuplicateActiveFingerprintIsSuppressed )
{
    FAKE_SUGGESTION_PROVIDER provider;
    AI_SUGGESTION_RECORD first = makeSuggestion();
    first.m_Fingerprint = wxS( "same" );
    provider.m_NextSuggestion = first;

    AI_SUGGESTION_ORCHESTRATOR orchestrator( provider );
    BOOST_REQUIRE( orchestrator.Update( makeTrigger() ).has_value() );

    provider.m_NextSuggestion = first;
    BOOST_CHECK( !orchestrator.Update( makeTrigger( 1, 2 ) ).has_value() );
    BOOST_CHECK_EQUAL( provider.m_CallCount, 2 );
    BOOST_REQUIRE_EQUAL( orchestrator.Records().size(), 1 );
}

BOOST_AUTO_TEST_CASE( CapacityEvictsOldestTerminalRecordFirst )
{
    FAKE_SUGGESTION_PROVIDER provider;
    AI_SUGGESTION_ORCHESTRATOR orchestrator( provider, 2 );

    provider.m_NextSuggestion = makeSuggestion( wxS( "first" ) );
    std::optional<AI_SUGGESTION_RECORD> first = orchestrator.Update( makeTrigger( 1, 1 ) );
    BOOST_REQUIRE( first.has_value() );
    BOOST_CHECK( orchestrator.Reject( first->m_Id ) );

    provider.m_NextSuggestion = makeSuggestion( wxS( "second" ) );
    BOOST_REQUIRE( orchestrator.Update( makeTrigger( 2, 2 ) ).has_value() );

    provider.m_NextSuggestion = makeSuggestion( wxS( "third" ) );
    BOOST_REQUIRE( orchestrator.Update( makeTrigger( 3, 3 ) ).has_value() );

    std::vector<AI_SUGGESTION_RECORD> records = orchestrator.Records();
    BOOST_REQUIRE_EQUAL( records.size(), 2 );
    BOOST_CHECK_EQUAL( records.at( 0 ).m_Title, wxString( wxS( "second" ) ) );
    BOOST_CHECK_EQUAL( records.at( 1 ).m_Title, wxString( wxS( "third" ) ) );
}
```

Expected RED: compile fails because `ai_suggestion_orchestrator.h` and the new
suggestion types do not exist.

- [ ] **Step 2: Run update tests to verify RED**

Run the verification command with `--run_test=AiSuggestionOrchestrator`.

- [ ] **Step 3: Implement data model and queue update**

Add to `include/kisurf/ai/ai_types.h` after `AI_CONTEXT_SNAPSHOT`:

```cpp
enum class AI_SUGGESTION_STATUS
{
    Pending,
    Previewing,
    Accepted,
    Rejected,
    Expired
};

struct KICOMMON_API AI_SUGGESTION_TRIGGER
{
    AI_EDITOR_KIND      m_EditorKind = AI_EDITOR_KIND::Unknown;
    AI_CONTEXT_VERSION  m_ContextVersion;
    AI_CONTEXT_SNAPSHOT m_ContextSnapshot;
    AI_ACTIVITY_RECORD  m_Activity;
    wxString            m_Reason;
};

struct KICOMMON_API AI_SUGGESTION_RECORD
{
    uint64_t                      m_Id = 0;
    uint64_t                      m_Sequence = 0;
    AI_EDITOR_KIND                m_EditorKind = AI_EDITOR_KIND::Unknown;
    AI_SUGGESTION_KIND            m_Kind = AI_SUGGESTION_KIND::Preview;
    AI_SUGGESTION_STATUS          m_Status = AI_SUGGESTION_STATUS::Pending;
    AI_CONTEXT_VERSION            m_ContextVersion;
    uint64_t                      m_TriggerActivitySequence = 0;
    wxString                      m_Fingerprint;
    wxString                      m_Title;
    wxString                      m_Body;
    wxString                      m_ArgumentsJson;
    std::vector<AI_OBJECT_REF>    m_PreviewObjects;
    std::vector<AI_OBJECT_REF>    m_EditObjects;
    AI_VALIDATION_SUMMARY         m_Validation;
};
```

Create `include/kisurf/ai/ai_suggestion_orchestrator.h`:

```cpp
#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_edit_session.h>
#include <kisurf/ai/ai_preview_session.h>
#include <kisurf/ai/ai_types.h>

#include <cstddef>
#include <optional>
#include <vector>

class KICOMMON_API AI_SUGGESTION_PROVIDER
{
public:
    virtual ~AI_SUGGESTION_PROVIDER() = default;

    virtual std::optional<AI_SUGGESTION_RECORD> Suggest(
            const AI_SUGGESTION_TRIGGER& aTrigger ) = 0;
};

class KICOMMON_API AI_SUGGESTION_ORCHESTRATOR
{
public:
    explicit AI_SUGGESTION_ORCHESTRATOR( AI_SUGGESTION_PROVIDER& aProvider,
                                         size_t aCapacity = 8 );

    std::optional<AI_SUGGESTION_RECORD> Update( AI_SUGGESTION_TRIGGER aTrigger );
    std::vector<AI_SUGGESTION_RECORD> Records() const;
    std::optional<AI_SUGGESTION_RECORD> Find( uint64_t aSuggestionId ) const;

    bool BeginPreview( uint64_t aSuggestionId, AI_PREVIEW_SESSION& aPreviewSession );
    bool Accept( uint64_t aSuggestionId, AI_EDIT_SESSION& aEditSession );
    bool Reject( uint64_t aSuggestionId );
    size_t ExpireStale( const AI_CONTEXT_VERSION& aCurrentVersion );

private:
    AI_SUGGESTION_RECORD* findMutable( uint64_t aSuggestionId );
    void trimToCapacity();

    AI_SUGGESTION_PROVIDER&        m_Provider;
    size_t                         m_Capacity = 0;
    uint64_t                       m_NextId = 1;
    uint64_t                       m_NextSequence = 1;
    std::vector<AI_SUGGESTION_RECORD> m_Records;
};
```

Create `common/kisurf/ai/ai_suggestion_orchestrator.cpp` implementing:

- `validTrigger(...)`
- `validSuggestion(...)`
- `effectiveVersion(...)`
- `computeFingerprint(...)`
- `isActiveDuplicate(...)`
- `isTerminal(...)`
- constructor, `Update(...)`, `Records()`, `Find(...)`, `Reject(...)`, and
  `trimToCapacity()`

Add `kisurf/ai/ai_suggestion_orchestrator.cpp` to `KICOMMON_SRCS` in
`common/CMakeLists.txt`, near the other KiSurf AI files.

- [ ] **Step 4: Run update tests to verify GREEN**

Run the verification command with `--run_test=AiSuggestionOrchestrator`.

## Task 2: Preview, Accept, Reject, And Expiration

**Files:**
- Modify: `qa/tests/common/test_ai_suggestion_orchestrator.cpp`
- Modify: `common/kisurf/ai/ai_suggestion_orchestrator.cpp`

- [ ] **Step 1: Add failing lifecycle tests**

Append fake adapters:

```cpp
class FAKE_PREVIEW_ADAPTER : public AI_PREVIEW_ADAPTER
{
public:
    void BeginPreview( uint64_t aPreviewId ) override
    {
        m_Events.push_back( wxString::Format( wxS( "begin:%llu" ),
                                              static_cast<unsigned long long>( aPreviewId ) ) );
    }

    void ShowObject( uint64_t aPreviewId, const AI_OBJECT_REF& aObject ) override
    {
        m_Events.push_back( wxString::Format( wxS( "show:%llu:%s" ),
                                              static_cast<unsigned long long>( aPreviewId ),
                                              aObject.m_Label ) );
    }

    void ClearPreview( uint64_t aPreviewId ) override
    {
        m_Events.push_back( wxString::Format( wxS( "clear:%llu" ),
                                              static_cast<unsigned long long>( aPreviewId ) ) );
    }

    std::vector<wxString> m_Events;
};

class FAKE_EDIT_ADAPTER : public AI_EDIT_ADAPTER
{
public:
    bool ApplyObject( const AI_OBJECT_REF& aObject ) override
    {
        m_Applied.push_back( aObject.m_Label );
        return m_ShouldApply;
    }

    bool                  m_ShouldApply = true;
    std::vector<wxString> m_Applied;
};
```

Add test cases:

```cpp
BOOST_AUTO_TEST_CASE( BeginPreviewShowsPreviewObjectsAndChangesStatus )
{
    FAKE_SUGGESTION_PROVIDER provider;
    provider.m_NextSuggestion = makeSuggestion();

    AI_SUGGESTION_ORCHESTRATOR orchestrator( provider );
    std::optional<AI_SUGGESTION_RECORD> suggestion = orchestrator.Update( makeTrigger() );
    BOOST_REQUIRE( suggestion.has_value() );

    FAKE_PREVIEW_ADAPTER adapter;
    AI_PREVIEW_SESSION preview( adapter );

    BOOST_CHECK( orchestrator.BeginPreview( suggestion->m_Id, preview ) );
    std::optional<AI_SUGGESTION_RECORD> updated = orchestrator.Find( suggestion->m_Id );
    BOOST_REQUIRE( updated.has_value() );
    BOOST_CHECK( updated->m_Status == AI_SUGGESTION_STATUS::Previewing );
    BOOST_REQUIRE_EQUAL( adapter.m_Events.size(), 2 );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 0 ), wxString( wxS( "begin:1" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 1 ), wxString( wxS( "show:1:U1.1" ) ) );
}

BOOST_AUTO_TEST_CASE( AcceptAppliesEditObjectsAndChangesStatus )
{
    FAKE_SUGGESTION_PROVIDER provider;
    provider.m_NextSuggestion = makeSuggestion();

    AI_SUGGESTION_ORCHESTRATOR orchestrator( provider );
    std::optional<AI_SUGGESTION_RECORD> suggestion = orchestrator.Update( makeTrigger() );
    BOOST_REQUIRE( suggestion.has_value() );

    FAKE_EDIT_ADAPTER adapter;
    AI_EDIT_SESSION edit( adapter );

    BOOST_CHECK( orchestrator.Accept( suggestion->m_Id, edit ) );
    BOOST_REQUIRE_EQUAL( adapter.m_Applied.size(), 1 );
    BOOST_CHECK_EQUAL( adapter.m_Applied.front(), wxString( wxS( "U1.1" ) ) );
    BOOST_CHECK( orchestrator.Find( suggestion->m_Id )->m_Status
                 == AI_SUGGESTION_STATUS::Accepted );
}

BOOST_AUTO_TEST_CASE( BlockingValidationPreventsAcceptedStatus )
{
    FAKE_SUGGESTION_PROVIDER provider;
    AI_SUGGESTION_RECORD record = makeSuggestion();
    record.m_Validation.m_Issues.push_back(
            { AI_VALIDATION_SEVERITY::Error, wxS( "new short" ), true } );
    provider.m_NextSuggestion = record;

    AI_SUGGESTION_ORCHESTRATOR orchestrator( provider );
    std::optional<AI_SUGGESTION_RECORD> suggestion = orchestrator.Update( makeTrigger() );
    BOOST_REQUIRE( suggestion.has_value() );

    FAKE_EDIT_ADAPTER adapter;
    AI_EDIT_SESSION edit( adapter );

    BOOST_CHECK( !orchestrator.Accept( suggestion->m_Id, edit ) );
    BOOST_CHECK( adapter.m_Applied.empty() );
    BOOST_CHECK( orchestrator.Find( suggestion->m_Id )->m_Status
                 == AI_SUGGESTION_STATUS::Pending );
}

BOOST_AUTO_TEST_CASE( RejectChangesPendingSuggestionStatus )
{
    FAKE_SUGGESTION_PROVIDER provider;
    provider.m_NextSuggestion = makeSuggestion();

    AI_SUGGESTION_ORCHESTRATOR orchestrator( provider );
    std::optional<AI_SUGGESTION_RECORD> suggestion = orchestrator.Update( makeTrigger() );
    BOOST_REQUIRE( suggestion.has_value() );

    BOOST_CHECK( orchestrator.Reject( suggestion->m_Id ) );
    BOOST_CHECK( orchestrator.Find( suggestion->m_Id )->m_Status
                 == AI_SUGGESTION_STATUS::Rejected );
}

BOOST_AUTO_TEST_CASE( ExpireStaleChangesOnlyMismatchedActiveRecords )
{
    FAKE_SUGGESTION_PROVIDER provider;
    provider.m_NextSuggestion = makeSuggestion();

    AI_SUGGESTION_ORCHESTRATOR orchestrator( provider );
    std::optional<AI_SUGGESTION_RECORD> suggestion = orchestrator.Update( makeTrigger( 1, 1 ) );
    BOOST_REQUIRE( suggestion.has_value() );

    AI_CONTEXT_VERSION current;
    current.m_DocumentRevision = 2;

    BOOST_CHECK_EQUAL( orchestrator.ExpireStale( current ), 1 );
    BOOST_CHECK( orchestrator.Find( suggestion->m_Id )->m_Status
                 == AI_SUGGESTION_STATUS::Expired );
}

BOOST_AUTO_TEST_SUITE_END()
```

Expected RED: tests compile but fail because preview, accept, and expiration are
not implemented yet.

- [ ] **Step 2: Run lifecycle tests to verify RED**

Run the verification command with `--run_test=AiSuggestionOrchestrator`.

- [ ] **Step 3: Implement lifecycle methods**

Implement in `ai_suggestion_orchestrator.cpp`:

- `BeginPreview(...)` finds pending/previewing records, begins a preview session,
  shows every `m_PreviewObjects` entry, and changes status to `Previewing`.
- `Accept(...)` finds pending/previewing records, calls
  `AI_EDIT_SESSION::Apply( m_EditObjects, m_Validation )`, and changes status to
  `Accepted` only on success.
- `ExpireStale(...)` compares document, selection, and view revisions, expiring
  only pending/previewing records whose stored context version differs.

- [ ] **Step 4: Run lifecycle tests to verify GREEN**

Run the verification command with `--run_test=AiSuggestionOrchestrator`.

## Task 3: Final Verification And Commit

- [ ] **Step 1: Run targeted tests**

Run:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_common -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiSuggestionOrchestrator,AiPreviewSession,AiEditSession,AiNativeTypes --log_level=test_suite"
```

Expected: exit code `0`, known schema warning allowed, Boost no errors.

- [ ] **Step 2: Run diff check**

Run:

```powershell
git diff --check
git diff --cached --check
```

Expected: exit code `0`; LF/CRLF warnings are acceptable.

- [ ] **Step 3: Commit**

Run:

```powershell
git add include\kisurf\ai\ai_types.h include\kisurf\ai\ai_suggestion_orchestrator.h common\kisurf\ai\ai_suggestion_orchestrator.cpp common\CMakeLists.txt qa\tests\common\CMakeLists.txt qa\tests\common\test_ai_suggestion_orchestrator.cpp
git commit -m "feat: add ai suggestion preview orchestrator"
```

## Plan Self-Review

- Spec coverage: data model, provider boundary, queue update, duplicate
  suppression, preview, accept, reject, stale expiration, and bounded storage are
  covered.
- Placeholder scan: no placeholder or fill-in text remains.
- Type consistency: all method names and status values match the spec.
- Scope check: this plan is common-library only and does not add UI, IPC, routing,
  placement, or background model calls.
