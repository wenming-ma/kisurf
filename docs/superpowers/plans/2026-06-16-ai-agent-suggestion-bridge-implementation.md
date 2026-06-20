# AI Agent Suggestion Bridge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Surface bounded AI suggestion records through the Agent model and pane without adding real canvas or document mutation adapters.

**Architecture:** Add a deterministic common-layer suggestion provider, then wire `AI_AGENT_PANEL_MODEL` to own an `AI_SUGGESTION_ORCHESTRATOR`. The Agent pane renders suggestions as safe read-only text and records editor activity into the model, while real preview overlays and commit-backed edits remain deferred.

**Tech Stack:** C++20, wxWidgets, Boost.Test, KiSurf AI common modules, `qa_common`.

---

## File Structure

- Create: `include/kisurf/ai/ai_agent_suggestion_provider.h`
  - Deterministic local provider that implements `AI_SUGGESTION_PROVIDER`.
- Create: `common/kisurf/ai/ai_agent_suggestion_provider.cpp`
  - Selected-object suggestion generation.
- Modify: `common/CMakeLists.txt`
  - Add `kisurf/ai/ai_agent_suggestion_provider.cpp`.
- Create: `qa/tests/common/test_ai_agent_suggestion_provider.cpp`
  - Unit tests for deterministic suggestion generation.
- Modify: `qa/tests/common/CMakeLists.txt`
  - Add the new provider test file.
- Modify: `include/kisurf/ai/ai_agent_panel_model.h`
  - Add suggestion constructor overload and lifecycle/query methods.
- Modify: `common/kisurf/ai/ai_agent_panel_model.cpp`
  - Own suggestion provider/orchestrator and delegate lifecycle operations.
- Modify: `qa/tests/common/test_ai_agent_panel_model.cpp`
  - Add model suggestion bridge tests while preserving chat tests.
- Modify: `include/kisurf/ai/ai_agent_panel.h`
  - Add suggestion refresh API and text control member.
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`
  - Render suggestions below the transcript and update them from recorded activity.

## Verification Command Template

Use the Visual Studio developer environment:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_common -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiAgentSuggestionProvider,AiAgentPanelModel,AiSuggestionOrchestrator,AiAgentPanel --log_level=test_suite"
```

Expected final result: exit code `0` and Boost reports no errors. The known
schema warning about `qa/tests/schemas/api.v1.schema.json` is acceptable when
the exit code is `0`.

## Task 1: Deterministic Agent Suggestion Provider

**Files:**
- Create: `include/kisurf/ai/ai_agent_suggestion_provider.h`
- Create: `common/kisurf/ai/ai_agent_suggestion_provider.cpp`
- Modify: `common/CMakeLists.txt`
- Create: `qa/tests/common/test_ai_agent_suggestion_provider.cpp`
- Modify: `qa/tests/common/CMakeLists.txt`

- [ ] **Step 1: Write failing provider tests**

Create `qa/tests/common/test_ai_agent_suggestion_provider.cpp` with:

```cpp
#include <boost/test/unit_test.hpp>

#include <kisurf/ai/ai_agent_suggestion_provider.h>

namespace
{
AI_SUGGESTION_TRIGGER makeSelectedTrigger()
{
    AI_SUGGESTION_TRIGGER trigger;
    trigger.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_Version.m_DocumentRevision = 7;
    trigger.m_ContextSnapshot.m_SelectedObjects.push_back(
            AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ) ) );
    trigger.m_Activity.m_Sequence = 3;
    trigger.m_Activity.m_ActionName = wxS( "common.Interactive.selected" );
    trigger.m_Activity.m_Message = wxS( "selection changed" );
    return trigger;
}
} // namespace

BOOST_AUTO_TEST_SUITE( AiAgentSuggestionProvider )

BOOST_AUTO_TEST_CASE( SelectedContextCreatesPreviewableSuggestion )
{
    AI_AGENT_SUGGESTION_PROVIDER provider;

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            provider.Suggest( makeSelectedTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK( suggestion->m_EditorKind == AI_EDITOR_KIND::Pcb );
    BOOST_CHECK( suggestion->m_Kind == AI_SUGGESTION_KIND::Preview );
    BOOST_CHECK( suggestion->m_Title.Contains( wxS( "U1.1" ) ) );
    BOOST_CHECK( suggestion->m_Body.Contains( wxS( "Preview" ) ) );
    BOOST_REQUIRE_EQUAL( suggestion->m_PreviewObjects.size(), 1 );
    BOOST_REQUIRE_EQUAL( suggestion->m_EditObjects.size(), 1 );
    BOOST_CHECK_EQUAL( suggestion->m_PreviewObjects.front().m_Label,
                       wxString( wxS( "U1.1" ) ) );
    BOOST_CHECK_EQUAL( suggestion->m_ContextVersion.m_DocumentRevision, 7 );
    BOOST_CHECK_EQUAL( suggestion->m_TriggerActivitySequence, 3 );
}

BOOST_AUTO_TEST_CASE( MissingSelectionCreatesNoSuggestion )
{
    AI_AGENT_SUGGESTION_PROVIDER provider;
    AI_SUGGESTION_TRIGGER        trigger = makeSelectedTrigger();
    trigger.m_ContextSnapshot.m_SelectedObjects.clear();

    BOOST_CHECK( !provider.Suggest( trigger ).has_value() );
}

BOOST_AUTO_TEST_CASE( MissingActivityCreatesNoSuggestion )
{
    AI_AGENT_SUGGESTION_PROVIDER provider;
    AI_SUGGESTION_TRIGGER        trigger = makeSelectedTrigger();
    trigger.m_Activity = AI_ACTIVITY_RECORD();

    BOOST_CHECK( !provider.Suggest( trigger ).has_value() );
}

BOOST_AUTO_TEST_SUITE_END()
```

- [ ] **Step 2: Register the failing provider test**

Add `test_ai_agent_suggestion_provider.cpp` to `QA_COMMON_SRCS` in
`qa/tests/common/CMakeLists.txt`.

- [ ] **Step 3: Run provider tests to verify RED**

Run the verification command with `--run_test=AiAgentSuggestionProvider`.

Expected RED: compile fails because `<kisurf/ai/ai_agent_suggestion_provider.h>`
does not exist.

- [ ] **Step 4: Add provider header**

Create `include/kisurf/ai/ai_agent_suggestion_provider.h`:

```cpp
#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_suggestion_orchestrator.h>

class KICOMMON_API AI_AGENT_SUGGESTION_PROVIDER : public AI_SUGGESTION_PROVIDER
{
public:
    std::optional<AI_SUGGESTION_RECORD> Suggest(
            const AI_SUGGESTION_TRIGGER& aTrigger ) override;
};
```

- [ ] **Step 5: Add provider implementation**

Create `common/kisurf/ai/ai_agent_suggestion_provider.cpp`:

```cpp
#include <kisurf/ai/ai_agent_suggestion_provider.h>

namespace
{
bool hasActivity( const AI_ACTIVITY_RECORD& aActivity )
{
    return aActivity.m_Sequence != 0 || !aActivity.m_ActionName.IsEmpty()
           || !aActivity.m_Message.IsEmpty();
}

AI_CONTEXT_VERSION effectiveVersion( const AI_SUGGESTION_TRIGGER& aTrigger )
{
    if( aTrigger.m_ContextVersion.IsValid() )
        return aTrigger.m_ContextVersion;

    return aTrigger.m_ContextSnapshot.m_Version;
}
} // namespace

std::optional<AI_SUGGESTION_RECORD> AI_AGENT_SUGGESTION_PROVIDER::Suggest(
        const AI_SUGGESTION_TRIGGER& aTrigger )
{
    if( aTrigger.m_EditorKind == AI_EDITOR_KIND::Unknown )
        return std::nullopt;

    if( !hasActivity( aTrigger.m_Activity ) )
        return std::nullopt;

    if( aTrigger.m_ContextSnapshot.m_SelectedObjects.empty() )
        return std::nullopt;

    const AI_OBJECT_REF& first = aTrigger.m_ContextSnapshot.m_SelectedObjects.front();
    const wxString       label = first.m_Label.IsEmpty() ? wxString( wxS( "selected item" ) )
                                                         : first.m_Label;

    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_EditorKind = aTrigger.m_EditorKind;
    suggestion.m_Kind = AI_SUGGESTION_KIND::Preview;
    suggestion.m_ContextVersion = effectiveVersion( aTrigger );
    suggestion.m_TriggerActivitySequence = aTrigger.m_Activity.m_Sequence;
    suggestion.m_Title = wxString::Format( wxS( "Review %s" ), label );
    suggestion.m_Body = wxS( "Preview this suggestion before applying any edit." );
    suggestion.m_PreviewObjects = aTrigger.m_ContextSnapshot.m_SelectedObjects;
    suggestion.m_EditObjects = aTrigger.m_ContextSnapshot.m_SelectedObjects;
    return suggestion;
}
```

Add `kisurf/ai/ai_agent_suggestion_provider.cpp` to `KICOMMON_SRCS` in
`common/CMakeLists.txt`, near the other KiSurf AI files.

- [ ] **Step 6: Run provider tests to verify GREEN**

Run the verification command with `--run_test=AiAgentSuggestionProvider`.

## Task 2: Agent Panel Model Suggestion Bridge

**Files:**
- Modify: `include/kisurf/ai/ai_agent_panel_model.h`
- Modify: `common/kisurf/ai/ai_agent_panel_model.cpp`
- Modify: `qa/tests/common/test_ai_agent_panel_model.cpp`

- [ ] **Step 1: Add failing model bridge tests**

Append these helpers to the anonymous namespace in
`qa/tests/common/test_ai_agent_panel_model.cpp`:

```cpp
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

    int                                m_CallCount = 0;
    AI_SUGGESTION_TRIGGER              m_LastTrigger;
    std::optional<AI_SUGGESTION_RECORD> m_NextSuggestion;
};

class FAKE_PREVIEW_ADAPTER : public AI_PREVIEW_ADAPTER
{
public:
    void BeginPreview( uint64_t aPreviewId ) override { m_BeginId = aPreviewId; }
    void ShowObject( uint64_t, const AI_OBJECT_REF& aObject ) override
    {
        m_Shown.push_back( aObject.m_Label );
    }
    void ClearPreview( uint64_t ) override {}

    uint64_t              m_BeginId = 0;
    std::vector<wxString> m_Shown;
};

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

AI_CONTEXT_SNAPSHOT makeSuggestionContext( uint64_t aDocRevision = 1 )
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_Version.m_DocumentRevision = aDocRevision;
    snapshot.m_SelectedObjects.push_back( AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ) ) );
    return snapshot;
}

AI_ACTIVITY_RECORD makeSuggestionActivity( uint64_t aSequence = 1 )
{
    AI_ACTIVITY_RECORD activity;
    activity.m_Sequence = aSequence;
    activity.m_ActionName = wxS( "common.Interactive.selected" );
    return activity;
}

AI_SUGGESTION_RECORD makeModelSuggestion( const wxString& aTitle = wxS( "Review U1.1" ) )
{
    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_Title = aTitle;
    suggestion.m_Body = wxS( "Preview before edit." );
    suggestion.m_PreviewObjects.push_back( AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ) ) );
    suggestion.m_EditObjects.push_back( AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ) ) );
    return suggestion;
}
```

Add tests:

```cpp
BOOST_AUTO_TEST_CASE( UpdateSuggestionsStoresProviderSuggestion )
{
    auto* suggestionProvider = new FAKE_SUGGESTION_PROVIDER();
    suggestionProvider->m_NextSuggestion = makeModelSuggestion();

    AI_AGENT_PANEL_MODEL model(
            std::make_unique<AI_STUB_PROVIDER>(),
            std::unique_ptr<AI_SUGGESTION_PROVIDER>( suggestionProvider ) );

    std::optional<AI_SUGGESTION_RECORD> suggestion = model.UpdateSuggestions(
            makeSuggestionContext(), makeSuggestionActivity(), wxS( "activity" ) );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK_EQUAL( suggestionProvider->m_CallCount, 1 );
    BOOST_CHECK_EQUAL( suggestion->m_Id, 1 );
    BOOST_REQUIRE_EQUAL( model.Suggestions().size(), 1 );
    BOOST_CHECK_EQUAL( suggestionProvider->m_LastTrigger.m_Reason,
                       wxString( wxS( "activity" ) ) );
}

BOOST_AUTO_TEST_CASE( DuplicateActiveSuggestionsAreSuppressed )
{
    auto* suggestionProvider = new FAKE_SUGGESTION_PROVIDER();
    AI_SUGGESTION_RECORD record = makeModelSuggestion();
    record.m_Fingerprint = wxS( "same" );
    suggestionProvider->m_NextSuggestion = record;

    AI_AGENT_PANEL_MODEL model(
            std::make_unique<AI_STUB_PROVIDER>(),
            std::unique_ptr<AI_SUGGESTION_PROVIDER>( suggestionProvider ) );

    BOOST_REQUIRE( model.UpdateSuggestions( makeSuggestionContext(), makeSuggestionActivity(),
                                            wxS( "activity" ) )
                           .has_value() );

    suggestionProvider->m_NextSuggestion = record;
    BOOST_CHECK( !model.UpdateSuggestions( makeSuggestionContext(), makeSuggestionActivity( 2 ),
                                           wxS( "activity" ) )
                          .has_value() );
    BOOST_REQUIRE_EQUAL( model.Suggestions().size(), 1 );
}

BOOST_AUTO_TEST_CASE( SuggestionLifecycleDelegatesToOrchestrator )
{
    auto* suggestionProvider = new FAKE_SUGGESTION_PROVIDER();
    suggestionProvider->m_NextSuggestion = makeModelSuggestion();

    AI_AGENT_PANEL_MODEL model(
            std::make_unique<AI_STUB_PROVIDER>(),
            std::unique_ptr<AI_SUGGESTION_PROVIDER>( suggestionProvider ) );

    std::optional<AI_SUGGESTION_RECORD> suggestion = model.UpdateSuggestions(
            makeSuggestionContext(), makeSuggestionActivity(), wxS( "activity" ) );
    BOOST_REQUIRE( suggestion.has_value() );

    FAKE_PREVIEW_ADAPTER previewAdapter;
    AI_PREVIEW_SESSION   preview( previewAdapter );
    BOOST_CHECK( model.PreviewSuggestion( suggestion->m_Id, preview ) );
    BOOST_CHECK_EQUAL( previewAdapter.m_BeginId, 1 );
    BOOST_REQUIRE_EQUAL( previewAdapter.m_Shown.size(), 1 );
    BOOST_CHECK( model.FindSuggestion( suggestion->m_Id )->m_Status
                 == AI_SUGGESTION_STATUS::Previewing );

    FAKE_EDIT_ADAPTER editAdapter;
    AI_EDIT_SESSION   edit( editAdapter );
    BOOST_CHECK( model.AcceptSuggestion( suggestion->m_Id, edit ) );
    BOOST_REQUIRE_EQUAL( editAdapter.m_Applied.size(), 1 );
    BOOST_CHECK( model.FindSuggestion( suggestion->m_Id )->m_Status
                 == AI_SUGGESTION_STATUS::Accepted );
}

BOOST_AUTO_TEST_CASE( ExpireSuggestionsMarksOnlyStaleActiveRecords )
{
    auto* suggestionProvider = new FAKE_SUGGESTION_PROVIDER();
    suggestionProvider->m_NextSuggestion = makeModelSuggestion();

    AI_AGENT_PANEL_MODEL model(
            std::make_unique<AI_STUB_PROVIDER>(),
            std::unique_ptr<AI_SUGGESTION_PROVIDER>( suggestionProvider ) );

    std::optional<AI_SUGGESTION_RECORD> suggestion = model.UpdateSuggestions(
            makeSuggestionContext( 1 ), makeSuggestionActivity(), wxS( "activity" ) );
    BOOST_REQUIRE( suggestion.has_value() );

    AI_CONTEXT_VERSION current;
    current.m_DocumentRevision = 2;
    BOOST_CHECK_EQUAL( model.ExpireSuggestions( current ), 1 );
    BOOST_CHECK( model.FindSuggestion( suggestion->m_Id )->m_Status
                 == AI_SUGGESTION_STATUS::Expired );
}
```

- [ ] **Step 2: Run model tests to verify RED**

Run the verification command with `--run_test=AiAgentPanelModel`.

Expected RED: compile fails because the model constructor overload and suggestion
methods do not exist.

- [ ] **Step 3: Add model APIs and members**

Update `include/kisurf/ai/ai_agent_panel_model.h`:

```cpp
#include <kisurf/ai/ai_suggestion_orchestrator.h>
```

Add constructor and methods:

```cpp
    AI_AGENT_PANEL_MODEL( std::unique_ptr<AI_PROVIDER> aProvider,
                          std::unique_ptr<AI_SUGGESTION_PROVIDER> aSuggestionProvider );

    std::optional<AI_SUGGESTION_RECORD> UpdateSuggestions(
            AI_CONTEXT_SNAPSHOT aContextSnapshot, AI_ACTIVITY_RECORD aActivity,
            const wxString& aReason );
    std::vector<AI_SUGGESTION_RECORD> Suggestions() const;
    std::optional<AI_SUGGESTION_RECORD> FindSuggestion( uint64_t aSuggestionId ) const;
    bool PreviewSuggestion( uint64_t aSuggestionId, AI_PREVIEW_SESSION& aPreviewSession );
    bool AcceptSuggestion( uint64_t aSuggestionId, AI_EDIT_SESSION& aEditSession );
    bool RejectSuggestion( uint64_t aSuggestionId );
    size_t ExpireSuggestions( const AI_CONTEXT_VERSION& aCurrentVersion );
```

Add private members after `m_Runtime`:

```cpp
    std::unique_ptr<AI_SUGGESTION_PROVIDER>      m_SuggestionProvider;
    std::unique_ptr<AI_SUGGESTION_ORCHESTRATOR>  m_SuggestionOrchestrator;
```

- [ ] **Step 4: Implement model bridge**

Update `common/kisurf/ai/ai_agent_panel_model.cpp`:

```cpp
#include <kisurf/ai/ai_agent_suggestion_provider.h>
```

Replace the constructor with delegating constructors:

```cpp
AI_AGENT_PANEL_MODEL::AI_AGENT_PANEL_MODEL( std::unique_ptr<AI_PROVIDER> aProvider ) :
        AI_AGENT_PANEL_MODEL( std::move( aProvider ),
                              std::make_unique<AI_AGENT_SUGGESTION_PROVIDER>() )
{
}

AI_AGENT_PANEL_MODEL::AI_AGENT_PANEL_MODEL(
        std::unique_ptr<AI_PROVIDER> aProvider,
        std::unique_ptr<AI_SUGGESTION_PROVIDER> aSuggestionProvider ) :
        m_Runtime( std::move( aProvider ) ),
        m_SuggestionProvider( std::move( aSuggestionProvider ) )
{
    if( m_SuggestionProvider )
    {
        m_SuggestionOrchestrator =
                std::make_unique<AI_SUGGESTION_ORCHESTRATOR>( *m_SuggestionProvider );
    }
}
```

Add methods:

```cpp
std::optional<AI_SUGGESTION_RECORD> AI_AGENT_PANEL_MODEL::UpdateSuggestions(
        AI_CONTEXT_SNAPSHOT aContextSnapshot, AI_ACTIVITY_RECORD aActivity,
        const wxString& aReason )
{
    if( !m_SuggestionOrchestrator )
        return std::nullopt;

    AI_SUGGESTION_TRIGGER trigger;
    trigger.m_EditorKind = aContextSnapshot.m_EditorKind;
    trigger.m_ContextVersion = aContextSnapshot.m_Version;
    trigger.m_ContextSnapshot = std::move( aContextSnapshot );
    trigger.m_Activity = std::move( aActivity );
    trigger.m_Reason = aReason;
    return m_SuggestionOrchestrator->Update( std::move( trigger ) );
}

std::vector<AI_SUGGESTION_RECORD> AI_AGENT_PANEL_MODEL::Suggestions() const
{
    return m_SuggestionOrchestrator ? m_SuggestionOrchestrator->Records()
                                    : std::vector<AI_SUGGESTION_RECORD>();
}

std::optional<AI_SUGGESTION_RECORD> AI_AGENT_PANEL_MODEL::FindSuggestion(
        uint64_t aSuggestionId ) const
{
    return m_SuggestionOrchestrator ? m_SuggestionOrchestrator->Find( aSuggestionId )
                                    : std::nullopt;
}

bool AI_AGENT_PANEL_MODEL::PreviewSuggestion( uint64_t aSuggestionId,
                                              AI_PREVIEW_SESSION& aPreviewSession )
{
    return m_SuggestionOrchestrator
           && m_SuggestionOrchestrator->BeginPreview( aSuggestionId, aPreviewSession );
}

bool AI_AGENT_PANEL_MODEL::AcceptSuggestion( uint64_t aSuggestionId,
                                             AI_EDIT_SESSION& aEditSession )
{
    return m_SuggestionOrchestrator
           && m_SuggestionOrchestrator->Accept( aSuggestionId, aEditSession );
}

bool AI_AGENT_PANEL_MODEL::RejectSuggestion( uint64_t aSuggestionId )
{
    return m_SuggestionOrchestrator && m_SuggestionOrchestrator->Reject( aSuggestionId );
}

size_t AI_AGENT_PANEL_MODEL::ExpireSuggestions( const AI_CONTEXT_VERSION& aCurrentVersion )
{
    return m_SuggestionOrchestrator ? m_SuggestionOrchestrator->ExpireStale( aCurrentVersion ) : 0;
}
```

- [ ] **Step 5: Run model tests to verify GREEN**

Run the verification command with `--run_test=AiAgentPanelModel`.

## Task 3: Agent Pane Suggestion Rendering

**Files:**
- Modify: `include/kisurf/ai/ai_agent_panel.h`
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`
- Modify: `qa/tests/common/test_ai_agent_panel.cpp`

- [ ] **Step 1: Add failing panel surface test**

Update `qa/tests/common/test_ai_agent_panel.cpp`:

```cpp
BOOST_AUTO_TEST_CASE( AgentPanelExposesSuggestionRefreshSurface )
{
    BOOST_CHECK( ( std::is_member_function_pointer_v<decltype( &AI_AGENT_PANEL::RefreshSuggestions )> ) );
}
```

Expected RED: compile fails because `RefreshSuggestions` does not exist.

- [ ] **Step 2: Run panel tests to verify RED**

Run the verification command with `--run_test=AiAgentPanel`.

- [ ] **Step 3: Add panel suggestion surface**

Update `include/kisurf/ai/ai_agent_panel.h`:

```cpp
    void RefreshSuggestions();
```

Add private member:

```cpp
    wxTextCtrl*                           m_Suggestions = nullptr;
```

- [ ] **Step 4: Implement panel rendering**

Update `common/kisurf/ai/ai_agent_panel.cpp`.

Add helper near the top:

```cpp
namespace
{
wxString suggestionStatusText( AI_SUGGESTION_STATUS aStatus )
{
    switch( aStatus )
    {
    case AI_SUGGESTION_STATUS::Pending:
        return wxS( "Pending" );
    case AI_SUGGESTION_STATUS::Previewing:
        return wxS( "Previewing" );
    case AI_SUGGESTION_STATUS::Accepted:
        return wxS( "Accepted" );
    case AI_SUGGESTION_STATUS::Rejected:
        return wxS( "Rejected" );
    case AI_SUGGESTION_STATUS::Expired:
        return wxS( "Expired" );
    }

    return wxS( "Unknown" );
}
} // namespace
```

Create the suggestion control in the constructor after `m_Transcript`:

```cpp
    m_Suggestions = new wxTextCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                    wxDefaultSize,
                                    wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE );
    m_Suggestions->SetMinSize( wxSize( -1, FromDIP( 72 ) ) );
```

Add it to the layout after the transcript:

```cpp
    root->Add( m_Suggestions, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP( 6 ) );
```

Add `RefreshSuggestions()`:

```cpp
void AI_AGENT_PANEL::RefreshSuggestions()
{
    wxString text;

    for( const AI_SUGGESTION_RECORD& suggestion : m_Model->Suggestions() )
    {
        text << wxS( "#" ) << suggestion.m_Id << wxS( " [" )
             << suggestionStatusText( suggestion.m_Status ) << wxS( "] " )
             << suggestion.m_Title;

        if( !suggestion.m_Body.IsEmpty() )
            text << wxS( "\n" ) << suggestion.m_Body;

        text << wxS( "\n\n" );
    }

    m_Suggestions->SetValue( text );
    m_Suggestions->SetInsertionPointEnd();
}
```

Update `RecordActivity(...)`:

```cpp
void AI_AGENT_PANEL::RecordActivity( AI_ACTIVITY_RECORD aRecord )
{
    AI_ACTIVITY_RECORD activity = aRecord;
    m_Model->RecordActivity( std::move( aRecord ) );

    if( m_ContextProvider )
    {
        AI_CONTEXT_SNAPSHOT snapshot = m_ContextProvider();

        if( snapshot.m_EditorKind == AI_EDITOR_KIND::Unknown )
            snapshot.m_EditorKind = m_EditorKind;

        m_Model->ExpireSuggestions( snapshot.m_Version );
        m_Model->UpdateSuggestions( std::move( snapshot ), std::move( activity ),
                                    wxS( "activity" ) );
        RefreshSuggestions();
    }
}
```

- [ ] **Step 5: Run panel tests to verify GREEN**

Run the verification command with `--run_test=AiAgentPanel`.

## Task 4: Final Verification And Commit

- [ ] **Step 1: Run targeted tests**

Run the full verification command from the template with:

```bat
--run_test=AiAgentSuggestionProvider,AiAgentPanelModel,AiSuggestionOrchestrator,AiAgentPanel --log_level=test_suite
```

Expected: exit code `0`, known schema warning allowed, Boost no errors.

- [ ] **Step 2: Run diff checks**

Run:

```powershell
git diff --check
git diff --cached --check
```

Expected: exit code `0`; LF/CRLF warnings are acceptable.

- [ ] **Step 3: Commit**

Run:

```powershell
git add include\kisurf\ai\ai_agent_suggestion_provider.h common\kisurf\ai\ai_agent_suggestion_provider.cpp common\CMakeLists.txt qa\tests\common\CMakeLists.txt qa\tests\common\test_ai_agent_suggestion_provider.cpp include\kisurf\ai\ai_agent_panel_model.h common\kisurf\ai\ai_agent_panel_model.cpp qa\tests\common\test_ai_agent_panel_model.cpp include\kisurf\ai\ai_agent_panel.h common\kisurf\ai\ai_agent_panel.cpp qa\tests\common\test_ai_agent_panel.cpp
git commit -m "feat: bridge ai suggestions into agent panel"
```

## Plan Self-Review

- Spec coverage: deterministic provider, model bridge, pane rendering, lifecycle
  delegation, and no real editor mutation are covered.
- Placeholder scan: no placeholder or fill-in text remains.
- Type consistency: all names match the Phase 7 orchestrator and existing Agent
  panel/model types.
- Scope check: real canvas preview and real commit-backed accept remain deferred
  and are not implied by this implementation.
