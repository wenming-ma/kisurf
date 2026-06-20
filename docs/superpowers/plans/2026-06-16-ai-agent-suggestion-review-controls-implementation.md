# AI Agent Suggestion Review Controls Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add native Agent pane controls for previewing, accepting, and rejecting the newest active AI suggestion, then wire PCB and schematic preview/accept handlers to current editor state.

**Architecture:** Keep lifecycle policy in `AI_AGENT_PANEL_MODEL`, keep UI ownership in `AI_AGENT_PANEL`, and keep editor-specific resolver/adapter/commit objects inside short-lived PCB/SCH callbacks. Accept is fail-closed and only supports an explicit `{"operation":"move","dx":int,"dy":int}` payload.

**Tech Stack:** C++17, wxWidgets, KiCad common AI module, KiCad PCB/SCH editor frames, nlohmann JSON, Boost.Test, CMake.

---

## File Structure

- Modify: `docs/superpowers/specs/2026-06-16-kisurf-ai-native-spec-index.md`
- Modify: `include/kisurf/ai/ai_agent_panel_model.h`
- Modify: `common/kisurf/ai/ai_agent_panel_model.cpp`
- Modify: `qa/tests/common/test_ai_agent_panel_model.cpp`
- Modify: `include/kisurf/ai/ai_agent_panel.h`
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`
- Modify: `qa/tests/common/test_ai_agent_panel.cpp`
- Modify: `pcbnew/pcb_edit_frame.cpp`
- Modify: `eeschema/sch_edit_frame.cpp`

## Verification Commands

Common targeted verification:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_common -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiAgentPanel,AiAgentPanelModel,AiActionToolCallHandler,AiNativeRuntime,AiToolExecution --log_level=test_suite
```

PCB targeted verification:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_pcbnew -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\pcbnew\qa_pcbnew.exe --run_test=AiPcbMoveEditAdapter,AiPcbObjectResolver,AiPcbContextAdapter --log_level=test_suite
```

Schematic targeted verification:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_eeschema -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\eeschema\qa_eeschema.exe --run_test=AiSchMoveEditAdapter,AiSchObjectResolver,AiSchContextAdapter --log_level=test_suite
```

The known missing schema warning is acceptable only when Boost reports no errors.

## Task 1: Model Active-Suggestion Selector

**Files:**
- Modify: `include/kisurf/ai/ai_agent_panel_model.h`
- Modify: `common/kisurf/ai/ai_agent_panel_model.cpp`
- Modify: `qa/tests/common/test_ai_agent_panel_model.cpp`

- [ ] **Step 1: Write the failing test**

Add this test to `qa/tests/common/test_ai_agent_panel_model.cpp`:

```cpp
BOOST_AUTO_TEST_CASE( LatestActiveSuggestionReturnsNewestPendingOrPreviewingRecord )
{
    auto* suggestionProvider = new FAKE_SUGGESTION_PROVIDER();
    AI_AGENT_PANEL_MODEL model(
            std::make_unique<AI_STUB_PROVIDER>(),
            std::unique_ptr<AI_SUGGESTION_PROVIDER>( suggestionProvider ) );

    BOOST_CHECK( !model.LatestActiveSuggestionId().has_value() );

    suggestionProvider->m_NextSuggestion = makeModelSuggestion( wxS( "First" ) );
    std::optional<AI_SUGGESTION_RECORD> first = model.UpdateSuggestions(
            makeSuggestionContext( 1 ), makeSuggestionActivity( 1 ), wxS( "activity" ) );
    BOOST_REQUIRE( first.has_value() );

    suggestionProvider->m_NextSuggestion = makeModelSuggestion( wxS( "Second" ) );
    std::optional<AI_SUGGESTION_RECORD> second = model.UpdateSuggestions(
            makeSuggestionContext( 1 ), makeSuggestionActivity( 2 ), wxS( "activity" ) );
    BOOST_REQUIRE( second.has_value() );

    BOOST_REQUIRE( model.LatestActiveSuggestionId().has_value() );
    BOOST_CHECK_EQUAL( *model.LatestActiveSuggestionId(), second->m_Id );

    BOOST_CHECK( model.RejectSuggestion( second->m_Id ) );
    BOOST_REQUIRE( model.LatestActiveSuggestionId().has_value() );
    BOOST_CHECK_EQUAL( *model.LatestActiveSuggestionId(), first->m_Id );

    BOOST_CHECK( model.RejectSuggestion( first->m_Id ) );
    BOOST_CHECK( !model.LatestActiveSuggestionId().has_value() );
}
```

- [ ] **Step 2: Run test to verify RED**

Run the common targeted verification command with:

```bat
--run_test=AiAgentPanelModel/LatestActiveSuggestionReturnsNewestPendingOrPreviewingRecord
```

Expected: compile failure because `LatestActiveSuggestionId` is not declared.

- [ ] **Step 3: Add the model method**

In `include/kisurf/ai/ai_agent_panel_model.h`, add:

```cpp
std::optional<uint64_t> LatestActiveSuggestionId() const;
```

In `common/kisurf/ai/ai_agent_panel_model.cpp`, add:

```cpp
std::optional<uint64_t> AI_AGENT_PANEL_MODEL::LatestActiveSuggestionId() const
{
    std::vector<AI_SUGGESTION_RECORD> records = Suggestions();

    for( auto it = records.rbegin(); it != records.rend(); ++it )
    {
        if( it->m_Status == AI_SUGGESTION_STATUS::Pending
            || it->m_Status == AI_SUGGESTION_STATUS::Previewing )
        {
            return it->m_Id;
        }
    }

    return std::nullopt;
}
```

- [ ] **Step 4: Run test to verify GREEN**

Run:

```bat
--run_test=AiAgentPanelModel/LatestActiveSuggestionReturnsNewestPendingOrPreviewingRecord
```

Expected: test passes.

## Task 2: Panel Review API And Buttons

**Files:**
- Modify: `include/kisurf/ai/ai_agent_panel.h`
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`
- Modify: `qa/tests/common/test_ai_agent_panel.cpp`

- [ ] **Step 1: Write failing panel API tests**

Add these tests to `qa/tests/common/test_ai_agent_panel.cpp`:

```cpp
BOOST_AUTO_TEST_CASE( AgentPanelExposesSuggestionReviewConfigurationSurface )
{
    BOOST_CHECK( ( std::is_member_function_pointer_v<
            decltype( &AI_AGENT_PANEL::ConfigureSuggestionReview )> ) );
}

BOOST_AUTO_TEST_CASE( AgentPanelExposesSuggestionReviewCommands )
{
    BOOST_CHECK( ( std::is_member_function_pointer_v<
            decltype( &AI_AGENT_PANEL::PreviewLatestSuggestion )> ) );
    BOOST_CHECK( ( std::is_member_function_pointer_v<
            decltype( &AI_AGENT_PANEL::AcceptLatestSuggestion )> ) );
    BOOST_CHECK( ( std::is_member_function_pointer_v<
            decltype( &AI_AGENT_PANEL::RejectLatestSuggestion )> ) );
}
```

- [ ] **Step 2: Run tests to verify RED**

Run:

```bat
--run_test=AiAgentPanel
```

Expected: compile failure because the panel review methods are not declared.

- [ ] **Step 3: Declare panel handler types and methods**

In `include/kisurf/ai/ai_agent_panel.h`, add public aliases after
`CONTEXT_PROVIDER`:

```cpp
using SUGGESTION_PREVIEW_HANDLER =
        std::function<bool( AI_AGENT_PANEL_MODEL& aModel, uint64_t aSuggestionId )>;
using SUGGESTION_ACCEPT_HANDLER =
        std::function<bool( AI_AGENT_PANEL_MODEL& aModel, uint64_t aSuggestionId )>;
```

Add public methods:

```cpp
void ConfigureSuggestionReview( SUGGESTION_PREVIEW_HANDLER aPreviewHandler,
                                SUGGESTION_ACCEPT_HANDLER aAcceptHandler );
bool PreviewLatestSuggestion();
bool AcceptLatestSuggestion();
bool RejectLatestSuggestion();
```

Add private members:

```cpp
SUGGESTION_PREVIEW_HANDLER m_PreviewSuggestionHandler;
SUGGESTION_ACCEPT_HANDLER  m_AcceptSuggestionHandler;
wxButton*                  m_PreviewButton = nullptr;
wxButton*                  m_AcceptButton = nullptr;
wxButton*                  m_RejectButton = nullptr;
```

- [ ] **Step 4: Add buttons and command implementations**

In the panel constructor, create buttons:

```cpp
m_PreviewButton = new wxButton( this, wxID_ANY, _( "Preview" ) );
m_AcceptButton = new wxButton( this, wxID_ANY, _( "Accept" ) );
m_RejectButton = new wxButton( this, wxID_ANY, _( "Reject" ) );

wxBoxSizer* suggestionButtons = new wxBoxSizer( wxHORIZONTAL );
suggestionButtons->Add( m_PreviewButton, 0, wxRIGHT, FromDIP( 4 ) );
suggestionButtons->Add( m_AcceptButton, 0, wxRIGHT, FromDIP( 4 ) );
suggestionButtons->Add( m_RejectButton, 0 );
```

Add `suggestionButtons` between `m_Suggestions` and `m_Input`:

```cpp
root->Add( suggestionButtons, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM,
           FromDIP( 6 ) );
```

Bind the buttons:

```cpp
m_PreviewButton->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& )
{
    PreviewLatestSuggestion();
} );

m_AcceptButton->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& )
{
    AcceptLatestSuggestion();
} );

m_RejectButton->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& )
{
    RejectLatestSuggestion();
} );
```

Add implementations:

```cpp
void AI_AGENT_PANEL::ConfigureSuggestionReview(
        SUGGESTION_PREVIEW_HANDLER aPreviewHandler,
        SUGGESTION_ACCEPT_HANDLER aAcceptHandler )
{
    m_PreviewSuggestionHandler = std::move( aPreviewHandler );
    m_AcceptSuggestionHandler = std::move( aAcceptHandler );
}

bool AI_AGENT_PANEL::PreviewLatestSuggestion()
{
    std::optional<uint64_t> suggestionId = m_Model->LatestActiveSuggestionId();

    if( !suggestionId || !m_PreviewSuggestionHandler )
        return false;

    bool handled = m_PreviewSuggestionHandler( *m_Model, *suggestionId );
    RefreshSuggestions();
    return handled;
}

bool AI_AGENT_PANEL::AcceptLatestSuggestion()
{
    std::optional<uint64_t> suggestionId = m_Model->LatestActiveSuggestionId();

    if( !suggestionId || !m_AcceptSuggestionHandler )
        return false;

    bool handled = m_AcceptSuggestionHandler( *m_Model, *suggestionId );
    RefreshSuggestions();
    return handled;
}

bool AI_AGENT_PANEL::RejectLatestSuggestion()
{
    std::optional<uint64_t> suggestionId = m_Model->LatestActiveSuggestionId();

    if( !suggestionId )
        return false;

    bool handled = m_Model->RejectSuggestion( *suggestionId );
    RefreshSuggestions();
    return handled;
}
```

- [ ] **Step 5: Run panel tests**

Run:

```bat
--run_test=AiAgentPanel,AiAgentPanelModel
```

Expected: panel and model tests pass.

## Task 3: PCB Review Handler Installation

**Files:**
- Modify: `pcbnew/pcb_edit_frame.cpp`

- [ ] **Step 1: Add required includes**

Add:

```cpp
#include <board_commit.h>
#include <kisurf_ai_pcb_move_edit_adapter.h>
#include <kisurf_ai_pcb_object_resolver.h>
#include <kisurf_ai_pcb_preview_adapter.h>
#include <nlohmann/json.hpp>
```

- [ ] **Step 2: Add a move payload parser in the anonymous namespace**

Add:

```cpp
std::optional<VECTOR2I> parseAiMoveDelta( const wxString& aArgumentsJson )
{
    if( aArgumentsJson.IsEmpty() )
        return std::nullopt;

    try
    {
        wxScopedCharBuffer buffer = aArgumentsJson.ToUTF8();
        nlohmann::json args = nlohmann::json::parse(
                buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string() );

        if( !args.is_object() || !args.contains( "operation" )
            || !args["operation"].is_string()
            || args["operation"].get<std::string>() != "move"
            || !args.contains( "dx" ) || !args["dx"].is_number_integer()
            || !args.contains( "dy" ) || !args["dy"].is_number_integer() )
        {
            return std::nullopt;
        }

        return VECTOR2I( args["dx"].get<int>(), args["dy"].get<int>() );
    }
    catch( const std::exception& )
    {
        return std::nullopt;
    }
}
```

- [ ] **Step 3: Configure PCB review handlers**

After action tool-call configuration, add:

```cpp
m_agentPanel->ConfigureSuggestionReview(
        [this]( AI_AGENT_PANEL_MODEL& aModel, uint64_t aSuggestionId )
        {
            if( !GetBoard() || !GetCanvas() || !GetCanvas()->GetView() )
                return false;

            KISURF_AI_PCB_OBJECT_RESOLVER resolver( *GetBoard() );
            KISURF_AI_PCB_PREVIEW_ADAPTER adapter( resolver, *GetCanvas()->GetView() );
            AI_PREVIEW_SESSION            session( adapter );
            return aModel.PreviewSuggestion( aSuggestionId, session );
        },
        [this]( AI_AGENT_PANEL_MODEL& aModel, uint64_t aSuggestionId )
        {
            std::optional<AI_SUGGESTION_RECORD> suggestion =
                    aModel.FindSuggestion( aSuggestionId );

            if( !suggestion || !GetBoard() || !GetToolManager() )
                return false;

            std::optional<VECTOR2I> delta = parseAiMoveDelta( suggestion->m_ArgumentsJson );

            if( !delta )
                return false;

            BOARD_COMMIT commit( GetToolManager(), true, false );
            KISURF_AI_PCB_OBJECT_RESOLVER resolver( *GetBoard() );
            KISURF_AI_PCB_MOVE_EDIT_ADAPTER adapter( resolver, commit, *delta );
            AI_EDIT_SESSION                  session( adapter );
            return aModel.AcceptSuggestion( aSuggestionId, session );
        } );
```

- [ ] **Step 4: Build PCB target**

Run the PCB targeted verification command.

Expected: target builds and AI PCB suites pass.

## Task 4: Schematic Review Handler Installation

**Files:**
- Modify: `eeschema/sch_edit_frame.cpp`

- [ ] **Step 1: Add required includes**

Add:

```cpp
#include <kisurf_ai_sch_move_edit_adapter.h>
#include <kisurf_ai_sch_object_resolver.h>
#include <kisurf_ai_sch_preview_adapter.h>
#include <nlohmann/json.hpp>
#include <sch_commit.h>
```

- [ ] **Step 2: Add the same move payload parser**

Add the same `parseAiMoveDelta(...)` helper used in the PCB frame anonymous
namespace. The helper returns `std::optional<VECTOR2I>` and fails closed.

- [ ] **Step 3: Configure schematic review handlers**

After action tool-call configuration, add:

```cpp
m_agentPanel->ConfigureSuggestionReview(
        [this]( AI_AGENT_PANEL_MODEL& aModel, uint64_t aSuggestionId )
        {
            if( !GetScreen() || !GetCanvas() || !GetCanvas()->GetView() )
                return false;

            KISURF_AI_SCH_OBJECT_RESOLVER resolver( *GetScreen() );
            KISURF_AI_SCH_PREVIEW_ADAPTER adapter( resolver, *GetCanvas()->GetView() );
            AI_PREVIEW_SESSION            session( adapter );
            return aModel.PreviewSuggestion( aSuggestionId, session );
        },
        [this]( AI_AGENT_PANEL_MODEL& aModel, uint64_t aSuggestionId )
        {
            std::optional<AI_SUGGESTION_RECORD> suggestion =
                    aModel.FindSuggestion( aSuggestionId );

            if( !suggestion || !GetScreen() || !GetToolManager() )
                return false;

            std::optional<VECTOR2I> delta = parseAiMoveDelta( suggestion->m_ArgumentsJson );

            if( !delta )
                return false;

            SCH_COMMIT commit( GetToolManager() );
            KISURF_AI_SCH_OBJECT_RESOLVER resolver( *GetScreen() );
            KISURF_AI_SCH_MOVE_EDIT_ADAPTER adapter( resolver, commit, *GetScreen(), *delta );
            AI_EDIT_SESSION                  session( adapter );
            return aModel.AcceptSuggestion( aSuggestionId, session );
        } );
```

- [ ] **Step 4: Build schematic target**

Run the schematic targeted verification command.

Expected: target builds and AI schematic suites pass.

## Task 5: Spec Index, Full Verification, And Commit

**Files:**
- All files above.

- [ ] **Step 1: Update the spec index**

Add a new spec entry:

```markdown
17. [AI Agent Suggestion Review Controls](./2026-06-16-ai-agent-suggestion-review-controls-design.md)
    - Defines Agent pane Preview/Accept/Reject controls and editor callback wiring for current-state preview and bounded move edits.
```

Add a new implementation-order item:

```markdown
21. Phase 14 Agent suggestion review controls that let users preview, accept, or reject the newest active suggestion through native editor callbacks.
```

- [ ] **Step 2: Run common verification**

Run the common targeted verification command.

Expected: exit code `0`.

- [ ] **Step 3: Run editor verification**

Run the PCB and schematic targeted verification commands.

Expected: both exit `0`.

- [ ] **Step 4: Check diff hygiene**

Run:

```powershell
git diff --check
git status --short
git diff --stat
```

Expected: no whitespace errors and only files from this plan changed.

- [ ] **Step 5: Commit**

```powershell
git add docs\superpowers\specs\2026-06-16-ai-agent-suggestion-review-controls-design.md docs\superpowers\specs\2026-06-16-kisurf-ai-native-spec-index.md docs\superpowers\plans\2026-06-16-ai-agent-suggestion-review-controls-implementation.md include\kisurf\ai\ai_agent_panel_model.h common\kisurf\ai\ai_agent_panel_model.cpp qa\tests\common\test_ai_agent_panel_model.cpp include\kisurf\ai\ai_agent_panel.h common\kisurf\ai\ai_agent_panel.cpp qa\tests\common\test_ai_agent_panel.cpp pcbnew\pcb_edit_frame.cpp eeschema\sch_edit_frame.cpp
git commit -m "feat: add ai suggestion review controls"
```

## Plan Self-Review

- Spec coverage: tasks cover latest active selection, panel controls, editor
  preview handlers, bounded move accept handlers, fail-closed behavior, spec
  index updates, and verification.
- Open-marker scan: no task depends on an undefined class or method; all new
  methods are declared before use.
- Type consistency: handler aliases, method names, and editor adapter class
  names match existing source and this plan.
