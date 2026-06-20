# AI Agent Window Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a native dockable Agent pane to PCB and schematic editors, backed by the deterministic AI runtime.

**Architecture:** The pane uses a shared model in `kicommon` and a wxWidgets panel that can be hosted by both editors. PCB and schematic frames attach the panel through existing AUI pane patterns, while show/hide is exposed through the common action system and each editor control tool.

**Tech Stack:** C++17, wxWidgets, wxAUI, KiCad `TOOL_ACTION`, `ACTION_MANAGER`, Boost.Test, `qa_common`, PCB/SCH kiface build targets.

---

## File Structure

- Create: `include/kisurf/ai/ai_agent_panel_model.h`
  - Non-GUI state model for transcript entries, send readiness, busy state, and runtime submission.
- Create: `common/kisurf/ai/ai_agent_panel_model.cpp`
  - Model implementation using `AI_RUNTIME`.
- Create: `include/kisurf/ai/ai_agent_panel.h`
  - wxPanel declaration for shared Agent UI.
- Create: `common/kisurf/ai/ai_agent_panel.cpp`
  - wxWidgets controls: transcript, input, send, stop.
- Modify: `include/tool/actions.h:255`
  - Add `ACTIONS::showAgentPanel`.
- Modify: `common/tool/actions.cpp:1317`
  - Define the shared action.
- Modify: `common/CMakeLists.txt:90`
  - Add the shared model and panel source files.
- Modify: `pcbnew/pcb_edit_frame.h`
  - Add `AI_AGENT_PANEL* m_agentPanel` and `ToggleAgentPanel()`.
- Modify: `pcbnew/pcb_edit_frame.cpp:300`
  - Instantiate and dock the Agent pane.
- Modify: `pcbnew/tools/board_editor_control.h:96`
  - Add `ToggleAgentPanel`.
- Modify: `pcbnew/tools/board_editor_control.cpp:989` and `pcbnew/tools/board_editor_control.cpp:2363`
  - Add control method and action transition.
- Modify: `pcbnew/menubar_pcb_editor.cpp:219`
  - Add Agent pane entry under Show/Hide Panels.
- Modify: `pcbnew/pcbnew_settings.h` and `pcbnew/pcbnew_settings.cpp`
  - Persist Agent pane visibility and size.
- Modify: `eeschema/sch_edit_frame.h`
  - Add `AI_AGENT_PANEL* m_agentPanel` and `ToggleAgentPanel()`.
- Modify: `eeschema/sch_edit_frame.cpp:250`
  - Instantiate and dock the Agent pane.
- Modify: `eeschema/tools/sch_editor_control.h:143`
  - Add `ToggleAgentPanel`.
- Modify: `eeschema/tools/sch_editor_control.cpp:3543` and `eeschema/tools/sch_editor_control.cpp:3988`
  - Add control method and action transition.
- Modify: `eeschema/menubar.cpp:180`
  - Add Agent pane entry under Show/Hide Panels.
- Modify: `eeschema/eeschema_settings.h` and `eeschema/eeschema_settings.cpp`
  - Persist Agent pane visibility and size.
- Create: `qa/tests/common/test_ai_agent_panel_model.cpp`
  - Unit tests for send/readiness/transcript behavior.

## Task 1: Agent Panel Model

**Files:**
- Create: `include/kisurf/ai/ai_agent_panel_model.h`
- Create: `common/kisurf/ai/ai_agent_panel_model.cpp`
- Test: `qa/tests/common/test_ai_agent_panel_model.cpp`
- Modify: `common/CMakeLists.txt:90`
- Modify: `qa/tests/common/CMakeLists.txt:24`

- [ ] **Step 1: Write the failing model tests**

Create `qa/tests/common/test_ai_agent_panel_model.cpp` with:

```cpp
#include <boost/test/unit_test.hpp>
#include <kisurf/ai/ai_agent_panel_model.h>
#include <kisurf/ai/ai_provider.h>

BOOST_AUTO_TEST_SUITE( AiAgentPanelModel )

BOOST_AUTO_TEST_CASE( EmptyInputCannotSend )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    BOOST_CHECK( !model.CanSend( wxEmptyString ) );
    BOOST_CHECK( !model.CanSend( wxS( "   " ) ) );
    BOOST_CHECK( model.CanSend( wxS( "inspect board" ) ) );
}

BOOST_AUTO_TEST_CASE( SendAppendsUserAndAgentMessages )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    AI_PROVIDER_RESPONSE response = model.SendUserText( wxS( "inspect board" ), AI_EDITOR_KIND::Pcb );

    BOOST_CHECK_EQUAL( response.m_RequestId, 1 );
    BOOST_REQUIRE_EQUAL( model.Messages().size(), 2 );
    BOOST_CHECK_EQUAL( model.Messages().at( 0 ).m_Role, wxS( "user" ) );
    BOOST_CHECK_EQUAL( model.Messages().at( 0 ).m_Text, wxS( "inspect board" ) );
    BOOST_CHECK_EQUAL( model.Messages().at( 1 ).m_Role, wxS( "assistant" ) );
    BOOST_CHECK( model.Messages().at( 1 ).m_Text.Contains( wxS( "inspect board" ) ) );
}

BOOST_AUTO_TEST_CASE( StopMarksLastRequestCancelled )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    model.SendUserText( wxS( "cancel after response" ), AI_EDITOR_KIND::Schematic );

    BOOST_CHECK( model.CancelLastRequest() );
    BOOST_CHECK( !model.CancelRequest( 999 ) );
}

BOOST_AUTO_TEST_SUITE_END()
```

- [ ] **Step 2: Register the failing model test**

Add to `QA_COMMON_SRCS`:

```cmake
    test_ai_agent_panel_model.cpp
```

Run:

```powershell
cmake --build --preset x64-release --target qa_common
```

Expected:

- Build fails because `<kisurf/ai/ai_agent_panel_model.h>` does not exist yet.

- [ ] **Step 3: Add model header**

Create `include/kisurf/ai/ai_agent_panel_model.h` with:

```cpp
#pragma once

#include <import_export.h>
#include <kisurf/ai/ai_runtime.h>
#include <kisurf/ai/ai_types.h>

#include <memory>
#include <vector>
#include <wx/string.h>

struct APIEXPORT AI_AGENT_MESSAGE
{
    wxString m_Role;
    wxString m_Text;
};

class APIEXPORT AI_AGENT_PANEL_MODEL
{
public:
    explicit AI_AGENT_PANEL_MODEL( std::unique_ptr<AI_PROVIDER> aProvider );

    bool CanSend( const wxString& aText ) const;
    AI_PROVIDER_RESPONSE SendUserText( const wxString& aText, AI_EDITOR_KIND aEditorKind );

    bool CancelLastRequest();
    bool CancelRequest( uint64_t aRequestId );

    const std::vector<AI_AGENT_MESSAGE>& Messages() const { return m_Messages; }

private:
    AI_RUNTIME                   m_Runtime;
    std::vector<AI_AGENT_MESSAGE> m_Messages;
    uint64_t                     m_LastRequestId = 0;
};
```

- [ ] **Step 4: Add model implementation**

Create `common/kisurf/ai/ai_agent_panel_model.cpp` with:

```cpp
#include <kisurf/ai/ai_agent_panel_model.h>

AI_AGENT_PANEL_MODEL::AI_AGENT_PANEL_MODEL( std::unique_ptr<AI_PROVIDER> aProvider ) :
        m_Runtime( std::move( aProvider ) )
{
}


bool AI_AGENT_PANEL_MODEL::CanSend( const wxString& aText ) const
{
    wxString trimmed = aText;
    trimmed.Trim( true ).Trim( false );
    return !trimmed.IsEmpty();
}


AI_PROVIDER_RESPONSE AI_AGENT_PANEL_MODEL::SendUserText( const wxString& aText,
                                                         AI_EDITOR_KIND aEditorKind )
{
    AI_PROVIDER_REQUEST request;
    request.m_EditorKind = aEditorKind;
    request.m_UserText = aText;

    m_Messages.push_back( { wxS( "user" ), aText } );

    AI_PROVIDER_RESPONSE response = m_Runtime.Submit( request );
    m_LastRequestId = response.m_RequestId;

    m_Messages.push_back( { wxS( "assistant" ), response.m_Body } );

    return response;
}


bool AI_AGENT_PANEL_MODEL::CancelLastRequest()
{
    return m_LastRequestId != 0 && CancelRequest( m_LastRequestId );
}


bool AI_AGENT_PANEL_MODEL::CancelRequest( uint64_t aRequestId )
{
    return m_Runtime.Cancel( aRequestId );
}
```

- [ ] **Step 5: Register model source and run tests**

Add to `common/CMakeLists.txt` in the KiSurf AI Native source block:

```cmake
    kisurf/ai/ai_agent_panel_model.cpp
```

Run:

```powershell
cmake --build --preset x64-release --target qa_common
$env:KICAD_RUN_FROM_BUILD_DIR='1'; ctest --test-dir out/build/x64-release -R qa_common --output-on-failure
```

Expected:

- `AiAgentPanelModel` tests pass.

- [ ] **Step 6: Commit the panel model**

Run:

```powershell
git add include/kisurf/ai/ai_agent_panel_model.h common/kisurf/ai/ai_agent_panel_model.cpp common/CMakeLists.txt qa/tests/common/CMakeLists.txt qa/tests/common/test_ai_agent_panel_model.cpp
git commit -m "feat: add ai agent panel model"
```

Expected:

- Commit succeeds.

## Task 2: Shared wxWidgets Agent Panel

**Files:**
- Create: `include/kisurf/ai/ai_agent_panel.h`
- Create: `common/kisurf/ai/ai_agent_panel.cpp`
- Modify: `common/CMakeLists.txt:90`

- [ ] **Step 1: Add the panel header**

Create `include/kisurf/ai/ai_agent_panel.h` with:

```cpp
#pragma once

#include <kisurf/ai/ai_agent_panel_model.h>

#include <memory>
#include <wx/panel.h>

class wxButton;
class wxTextCtrl;

class AI_AGENT_PANEL : public wxPanel
{
public:
    AI_AGENT_PANEL( wxWindow* aParent, AI_EDITOR_KIND aEditorKind );

    void SendCurrentText();
    void RefreshTranscript();

private:
    AI_EDITOR_KIND                         m_EditorKind;
    std::unique_ptr<AI_AGENT_PANEL_MODEL>  m_Model;
    wxTextCtrl*                            m_Transcript = nullptr;
    wxTextCtrl*                            m_Input = nullptr;
    wxButton*                              m_SendButton = nullptr;
    wxButton*                              m_StopButton = nullptr;
};
```

- [ ] **Step 2: Add the panel implementation**

Create `common/kisurf/ai/ai_agent_panel.cpp` with:

```cpp
#include <kisurf/ai/ai_agent_panel.h>
#include <kisurf/ai/ai_provider.h>

#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>

AI_AGENT_PANEL::AI_AGENT_PANEL( wxWindow* aParent, AI_EDITOR_KIND aEditorKind ) :
        wxPanel( aParent, wxID_ANY ),
        m_EditorKind( aEditorKind ),
        m_Model( std::make_unique<AI_AGENT_PANEL_MODEL>( std::make_unique<AI_STUB_PROVIDER>() ) )
{
    wxBoxSizer* root = new wxBoxSizer( wxVERTICAL );

    m_Transcript = new wxTextCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                   wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE );
    m_Input = new wxTextCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                              wxDefaultSize, wxTE_PROCESS_ENTER );
    m_SendButton = new wxButton( this, wxID_ANY, _( "Send" ) );
    m_StopButton = new wxButton( this, wxID_ANY, _( "Stop" ) );

    wxBoxSizer* buttons = new wxBoxSizer( wxHORIZONTAL );
    buttons->Add( m_SendButton, 0, wxRIGHT, FromDIP( 4 ) );
    buttons->Add( m_StopButton, 0 );

    root->Add( m_Transcript, 1, wxEXPAND | wxALL, FromDIP( 6 ) );
    root->Add( m_Input, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP( 6 ) );
    root->Add( buttons, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP( 6 ) );

    SetSizer( root );

    m_SendButton->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& )
    {
        SendCurrentText();
    } );

    m_Input->Bind( wxEVT_TEXT_ENTER, [this]( wxCommandEvent& )
    {
        SendCurrentText();
    } );

    m_StopButton->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& )
    {
        m_Model->CancelLastRequest();
    } );
}


void AI_AGENT_PANEL::SendCurrentText()
{
    const wxString text = m_Input->GetValue();

    if( !m_Model->CanSend( text ) )
        return;

    m_Model->SendUserText( text, m_EditorKind );
    m_Input->Clear();
    RefreshTranscript();
}


void AI_AGENT_PANEL::RefreshTranscript()
{
    wxString transcript;

    for( const AI_AGENT_MESSAGE& message : m_Model->Messages() )
    {
        transcript << message.m_Role << wxS( ": " ) << message.m_Text << wxS( "\n\n" );
    }

    m_Transcript->SetValue( transcript );
    m_Transcript->SetInsertionPointEnd();
}
```

- [ ] **Step 3: Register the panel source and build common tests**

Add to `common/CMakeLists.txt`:

```cmake
    kisurf/ai/ai_agent_panel.cpp
```

Run:

```powershell
cmake --build --preset x64-release --target qa_common
```

Expected:

- `qa_common` builds.

- [ ] **Step 4: Commit shared Agent panel**

Run:

```powershell
git add include/kisurf/ai/ai_agent_panel.h common/kisurf/ai/ai_agent_panel.cpp common/CMakeLists.txt
git commit -m "feat: add shared ai agent panel"
```

Expected:

- Commit succeeds.

## Task 3: Shared Show Agent Action

**Files:**
- Modify: `include/tool/actions.h:255`
- Modify: `common/tool/actions.cpp:1317`

- [ ] **Step 1: Add action declaration**

In `include/tool/actions.h`, add beside `showProperties`:

```cpp
    static TOOL_ACTION showAgentPanel;
```

- [ ] **Step 2: Add action definition**

In `common/tool/actions.cpp`, add after `ACTIONS::showProperties`:

```cpp
TOOL_ACTION ACTIONS::showAgentPanel( TOOL_ACTION_ARGS()
        .Name( "common.Control.showAgentPanel" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Agent" ) )
        .Tooltip( _( "Show/hide the Agent panel" ) )
        .ToolbarState( TOOLBAR_STATE::TOGGLE )
        .Icon( BITMAPS::tools ) );
```

- [ ] **Step 3: Build common target**

Run:

```powershell
cmake --build --preset x64-release --target qa_common
```

Expected:

- `qa_common` builds and links against the new action symbol.

- [ ] **Step 4: Commit shared action**

Run:

```powershell
git add include/tool/actions.h common/tool/actions.cpp
git commit -m "feat: add shared agent panel action"
```

Expected:

- Commit succeeds.

## Task 4: PCB Editor Agent Pane

**Files:**
- Modify: `pcbnew/pcb_edit_frame.h`
- Modify: `pcbnew/pcb_edit_frame.cpp`
- Modify: `pcbnew/tools/board_editor_control.h`
- Modify: `pcbnew/tools/board_editor_control.cpp`
- Modify: `pcbnew/menubar_pcb_editor.cpp`
- Modify: `pcbnew/pcbnew_settings.h`
- Modify: `pcbnew/pcbnew_settings.cpp`

- [ ] **Step 1: Add PCB frame members**

In `pcbnew/pcb_edit_frame.h`, forward declare `AI_AGENT_PANEL` with the other panel classes and add:

```cpp
    bool IsAgentPaneShown() { return m_auimgr.GetPane( wxS( "AgentPanel" ) ).IsShown(); }
    void ToggleAgentPanel();
```

Add to the private panel members:

```cpp
    AI_AGENT_PANEL*       m_agentPanel = nullptr;
```

- [ ] **Step 2: Instantiate and dock the PCB pane**

In `pcbnew/pcb_edit_frame.cpp`, include:

```cpp
#include <kisurf/ai/ai_agent_panel.h>
```

Near the existing panel construction around `m_designBlocksPane`, add:

```cpp
    m_agentPanel = new AI_AGENT_PANEL( this, AI_EDITOR_KIND::Pcb );
```

Near the existing `m_auimgr.AddPane` calls, add:

```cpp
    m_auimgr.AddPane( m_agentPanel, EDA_PANE().Name( wxS( "AgentPanel" ) )
                      .Right().Layer( 5 ).Position( 1 )
                      .Caption( _( "Agent" ) )
                      .CaptionVisible( true )
                      .PaneBorder( true )
                      .TopDockable( false )
                      .BottomDockable( false )
                      .CloseButton( true )
                      .MinSize( FromDIP( wxSize( 260, 120 ) ) )
                      .BestSize( FromDIP( wxSize( 360, 360 ) ) )
                      .FloatingSize( FromDIP( wxSize( 520, 640 ) ) )
                      .Show( false ) );
```

After `RestoreAuiLayout()`, add:

```cpp
    m_auimgr.GetPane( wxS( "AgentPanel" ) ).Show( GetPcbNewSettings()->m_AuiPanels.show_agent_panel );
```

Add the toggle method near other pane toggles:

```cpp
void PCB_EDIT_FRAME::ToggleAgentPanel()
{
    wxAuiPaneInfo& pane = m_auimgr.GetPane( wxS( "AgentPanel" ) );
    pane.Show( !pane.IsShown() );
    m_auimgr.Update();
}
```

- [ ] **Step 3: Persist PCB Agent pane visibility**

In `pcbnew/pcbnew_settings.h`, add to `AUI_PANELS`:

```cpp
        bool  show_agent_panel;
        int   agent_panel_width;
```

In `pcbnew/pcbnew_settings.cpp`, add parameters after existing AUI panel booleans:

```cpp
    m_params.emplace_back( new PARAM<bool>( "aui.show_agent_panel",
            &m_AuiPanels.show_agent_panel, false ) );

    m_params.emplace_back( new PARAM<int>( "aui.agent_panel_width",
            &m_AuiPanels.agent_panel_width, -1 ) );
```

In `pcbnew/pcb_edit_frame.cpp`, where AUI settings are saved, add:

```cpp
        wxAuiPaneInfo& agentPane = m_auimgr.GetPane( wxS( "AgentPanel" ) );
        cfg->m_AuiPanels.show_agent_panel = agentPane.IsShown();

        if( agentPane.IsDocked() && m_agentPanel )
            cfg->m_AuiPanels.agent_panel_width = m_agentPanel->GetSize().x;
```

- [ ] **Step 4: Add PCB control transition and condition**

In `pcbnew/tools/board_editor_control.h`, add:

```cpp
    int ToggleAgentPanel( const TOOL_EVENT& aEvent );
```

In `pcbnew/tools/board_editor_control.cpp`, add near other panel toggles:

```cpp
int BOARD_EDITOR_CONTROL::ToggleAgentPanel( const TOOL_EVENT& aEvent )
{
    getEditFrame<PCB_EDIT_FRAME>()->ToggleAgentPanel();
    return 0;
}
```

In `BOARD_EDITOR_CONTROL::setTransitions()`, add:

```cpp
    Go( &BOARD_EDITOR_CONTROL::ToggleAgentPanel,      ACTIONS::showAgentPanel.MakeEvent() );
```

In `PCB_EDIT_FRAME::setupTools()`, add condition near `propertiesCond`:

```cpp
    auto agentPanelCond =
            [this] ( const SELECTION& )
            {
                return m_auimgr.GetPane( wxS( "AgentPanel" ) ).IsShown();
            };
```

Then add:

```cpp
    mgr->SetConditions( ACTIONS::showAgentPanel,          CHECK( agentPanelCond ) );
```

- [ ] **Step 5: Add PCB menu entry**

In `pcbnew/menubar_pcb_editor.cpp`, add under Show/Hide Panels:

```cpp
    showHidePanels->Add( ACTIONS::showAgentPanel,              ACTION_MENU::CHECK );
```

- [ ] **Step 6: Build PCB editor target**

Run:

```powershell
cmake --build --preset x64-release --target pcbnew_kiface
```

Expected:

- `pcbnew_kiface` builds without compile or link errors.

- [ ] **Step 7: Commit PCB Agent pane**

Run:

```powershell
git add pcbnew/pcb_edit_frame.h pcbnew/pcb_edit_frame.cpp pcbnew/tools/board_editor_control.h pcbnew/tools/board_editor_control.cpp pcbnew/menubar_pcb_editor.cpp pcbnew/pcbnew_settings.h pcbnew/pcbnew_settings.cpp
git commit -m "feat: add pcb agent pane"
```

Expected:

- Commit succeeds.

## Task 5: Schematic Editor Agent Pane

**Files:**
- Modify: `eeschema/sch_edit_frame.h`
- Modify: `eeschema/sch_edit_frame.cpp`
- Modify: `eeschema/tools/sch_editor_control.h`
- Modify: `eeschema/tools/sch_editor_control.cpp`
- Modify: `eeschema/menubar.cpp`
- Modify: `eeschema/eeschema_settings.h`
- Modify: `eeschema/eeschema_settings.cpp`

- [ ] **Step 1: Add schematic frame members**

In `eeschema/sch_edit_frame.h`, forward declare `AI_AGENT_PANEL` and add:

```cpp
    bool IsAgentPaneShown() { return m_auimgr.GetPane( wxS( "AgentPanel" ) ).IsShown(); }
    void ToggleAgentPanel();
```

Add to panel members:

```cpp
    AI_AGENT_PANEL*             m_agentPanel = nullptr;
```

- [ ] **Step 2: Instantiate and dock the schematic pane**

In `eeschema/sch_edit_frame.cpp`, include:

```cpp
#include <kisurf/ai/ai_agent_panel.h>
```

Near existing panel construction, add:

```cpp
    m_agentPanel = new AI_AGENT_PANEL( this, AI_EDITOR_KIND::Schematic );
```

Near existing pane additions, add:

```cpp
    m_auimgr.AddPane( m_agentPanel, EDA_PANE()
                      .Name( wxS( "AgentPanel" ) )
                      .Right().Layer( 5 ).Position( 1 )
                      .Caption( _( "Agent" ) )
                      .CaptionVisible( true )
                      .PaneBorder( true )
                      .TopDockable( false )
                      .BottomDockable( false )
                      .CloseButton( true )
                      .MinSize( FromDIP( wxSize( 260, 120 ) ) )
                      .BestSize( FromDIP( wxSize( 360, 360 ) ) )
                      .FloatingSize( FromDIP( wxSize( 520, 640 ) ) )
                      .Show( false ) );
```

After `RestoreAuiLayout()`, add:

```cpp
    m_auimgr.GetPane( wxS( "AgentPanel" ) ).Show( aui_cfg.show_agent_panel );
```

Add:

```cpp
void SCH_EDIT_FRAME::ToggleAgentPanel()
{
    wxAuiPaneInfo& pane = m_auimgr.GetPane( wxS( "AgentPanel" ) );
    pane.Show( !pane.IsShown() );
    m_auimgr.Update();
}
```

- [ ] **Step 3: Persist schematic Agent pane visibility**

In `eeschema/eeschema_settings.h`, add to `AUI_PANELS`:

```cpp
        bool show_agent_panel;
        int  agent_panel_width;
```

In `eeschema/eeschema_settings.cpp`, add:

```cpp
    m_params.emplace_back( new PARAM<bool>( "aui.show_agent_panel",
            &m_AuiPanels.show_agent_panel, false ) );

    m_params.emplace_back( new PARAM<int>( "aui.agent_panel_width",
            &m_AuiPanels.agent_panel_width, -1 ) );
```

In `eeschema/eeschema_config.cpp`, add to save:

```cpp
        wxAuiPaneInfo& agentPane = m_auimgr.GetPane( wxS( "AgentPanel" ) );
        cfg->m_AuiPanels.show_agent_panel = agentPane.IsShown();

        if( agentPane.IsDocked() && m_agentPanel )
            cfg->m_AuiPanels.agent_panel_width = m_agentPanel->GetSize().x;
```

- [ ] **Step 4: Add schematic control transition and condition**

In `eeschema/tools/sch_editor_control.h`, add:

```cpp
    int ToggleAgentPanel( const TOOL_EVENT& aEvent );
```

In `eeschema/tools/sch_editor_control.cpp`, add:

```cpp
int SCH_EDITOR_CONTROL::ToggleAgentPanel( const TOOL_EVENT& aEvent )
{
    getEditFrame<SCH_EDIT_FRAME>()->ToggleAgentPanel();
    return 0;
}
```

In `SCH_EDITOR_CONTROL::setTransitions()`, add:

```cpp
    Go( &SCH_EDITOR_CONTROL::ToggleAgentPanel,        ACTIONS::showAgentPanel.MakeEvent() );
```

In `SCH_EDIT_FRAME::setupTools()`, add:

```cpp
    auto agentPanelCond =
            [this] ( const SELECTION& )
            {
                return m_auimgr.GetPane( wxS( "AgentPanel" ) ).IsShown();
            };
```

Then add:

```cpp
    mgr->SetConditions( ACTIONS::showAgentPanel,           CHECK( agentPanelCond ) );
```

- [ ] **Step 5: Add schematic menu entry**

In `eeschema/menubar.cpp`, add under Show/Hide Panels:

```cpp
    showHidePanels->Add( ACTIONS::showAgentPanel,    ACTION_MENU::CHECK );
```

- [ ] **Step 6: Build schematic editor target**

Run:

```powershell
cmake --build --preset x64-release --target eeschema_kiface
```

Expected:

- `eeschema_kiface` builds without compile or link errors.

- [ ] **Step 7: Commit schematic Agent pane**

Run:

```powershell
git add eeschema/sch_edit_frame.h eeschema/sch_edit_frame.cpp eeschema/tools/sch_editor_control.h eeschema/tools/sch_editor_control.cpp eeschema/menubar.cpp eeschema/eeschema_settings.h eeschema/eeschema_settings.cpp eeschema/eeschema_config.cpp
git commit -m "feat: add schematic agent pane"
```

Expected:

- Commit succeeds.

## Task 6: Manual Smoke Test

**Files:**
- No source edits

- [ ] **Step 1: Build application shells**

Run:

```powershell
cmake --build --preset x64-release --target pcbnew_kiface eeschema_kiface kicad
```

Expected:

- Targets build successfully.

- [ ] **Step 2: Open KiCad and verify pane toggles**

Run the built KiCad executable from `out/build/x64-release`. Use the View menu in PCB and schematic editors.

Expected:

- View menu contains `Agent`.
- Agent pane is hidden by default.
- Selecting `Agent` shows the pane.
- Sending `hello` appends a user row and a deterministic `Stub Agent` response.
- Closing and reopening the editor preserves pane visibility according to settings.

## Acceptance Criteria

- Shared Agent model tests pass in `qa_common`.
- `pcbnew_kiface` and `eeschema_kiface` build.
- PCB and schematic View menus can show/hide the Agent pane.
- Agent pane sends messages through the stub provider and records deterministic transcript entries.
