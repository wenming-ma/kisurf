# AI Native Semantic Self-Test Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a native, test-facing semantic UI surface for the Agent pane so KiSurf can verify core Agent controls without relying on external desktop automation.

**Architecture:** First add common semantic UI types and redaction. Then add a pure Agent-pane semantic tree builder that can be unit-tested without constructing wx windows. Finally wire `AI_AGENT_PANEL` to expose a tree and execute allowlisted high-level semantic actions.

**Tech Stack:** KiSurf common C++20, wxString/wxRegEx, Boost.Test `qa_common`, existing `AI_AGENT_PANEL` and `AI_AGENT_PANEL_MODEL`.

---

## File Structure

- Create: `include/kisurf/ai/ai_semantic_ui.h`
  - Semantic node/tree/action data model and redaction helper declarations.
- Create: `common/kisurf/ai/ai_semantic_ui.cpp`
  - `FindNode` and redaction implementation.
- Create: `include/kisurf/ai/ai_agent_panel_semantic.h`
  - Pure Agent-pane semantic view state and tree builder declaration.
- Create: `common/kisurf/ai/ai_agent_panel_semantic.cpp`
  - Stable Agent-pane node IDs and enabled-state mapping.
- Modify: `include/kisurf/ai/ai_agent_panel.h`
  - Add semantic tree and action methods.
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`
  - Build semantic view from live panel state and invoke allowlisted actions.
- Modify: `common/CMakeLists.txt`
  - Register `ai_semantic_ui.cpp` and `ai_agent_panel_semantic.cpp`.
- Create: `qa/tests/common/test_ai_semantic_ui.cpp`
  - Unit tests for redaction and node lookup.
- Create: `qa/tests/common/test_ai_agent_panel_semantic.cpp`
  - Unit tests for deterministic Agent-pane nodes and enabled state.
- Modify: `qa/tests/common/test_ai_agent_panel.cpp`
  - Add API surface test for `AI_AGENT_PANEL`.
- Modify: `qa/tests/common/CMakeLists.txt`
  - Register the new common tests.
- Modify: `docs/superpowers/plans/2026-06-16-agent-pane-computer-use-smoke-test.md`
  - Already corrected stale GUI smoke evidence.
- Modify: `docs/superpowers/plans/2026-06-19-ai-native-semantic-self-test-implementation.md`
  - Check off completed steps and record verification.

## Task 1: Common Semantic UI Model

**Files:**
- Create: `include/kisurf/ai/ai_semantic_ui.h`
- Create: `common/kisurf/ai/ai_semantic_ui.cpp`
- Create: `qa/tests/common/test_ai_semantic_ui.cpp`
- Modify: `common/CMakeLists.txt`
- Modify: `qa/tests/common/CMakeLists.txt`

- [x] **Step 1: Write failing semantic UI tests**

Create `qa/tests/common/test_ai_semantic_ui.cpp`:

```cpp
#include <boost/test/unit_test.hpp>
#include <kisurf/ai/ai_semantic_ui.h>

BOOST_AUTO_TEST_SUITE( AiSemanticUi )

BOOST_AUTO_TEST_CASE( RedactsSensitiveText )
{
    const wxString secret = wxString( wxS( "token: " ) ) + wxS( "abc123" )
                            + wxS( " " ) + wxS( "sk-" )
                            + wxS( "12345678901234567890" )
                            + wxS( " " ) + wxS( "OPENAI_API_KEY" )
                            + wxS( "=" ) + wxS( "secret-value" );

    const wxString redacted = RedactSemanticUiText( secret );

    BOOST_CHECK( !redacted.Contains( wxS( "12345678901234567890" ) ) );
    BOOST_CHECK( !redacted.Contains( wxS( "secret-value" ) ) );
    BOOST_CHECK( redacted.Contains( wxS( "redacted" ) ) );
}

BOOST_AUTO_TEST_CASE( TreeFindsNodeById )
{
    AI_SEMANTIC_UI_TREE tree;
    tree.m_FrameId = wxS( "agent" );
    tree.m_Nodes.push_back( { wxS( "agent.root" ), wxEmptyString, wxS( "panel" ),
                              wxS( "Agent" ) } );
    tree.m_Nodes.push_back( { wxS( "agent.send" ), wxS( "agent.root" ),
                              wxS( "button" ), wxS( "Send" ) } );

    const AI_SEMANTIC_UI_NODE* send = tree.FindNode( wxS( "agent.send" ) );

    BOOST_REQUIRE( send );
    BOOST_CHECK_EQUAL( send->m_Label, wxString( wxS( "Send" ) ) );
    BOOST_CHECK( tree.FindNode( wxS( "missing" ) ) == nullptr );
}

BOOST_AUTO_TEST_SUITE_END()
```

Add `test_ai_semantic_ui.cpp` to `QA_COMMON_SRCS` in
`qa/tests/common/CMakeLists.txt` after `test_ai_semantic_tool_call_handler.cpp`.

- [x] **Step 2: Run tests to verify RED**

Run:

```powershell
cmd.exe /S /C '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build out\build\x64-release --target qa_common --config Release'
```

Expected: build fails because `<kisurf/ai/ai_semantic_ui.h>` does not exist.

Actual: build failed with `fatal error C1083: Cannot open include file:
'kisurf/ai/ai_semantic_ui.h': No such file or directory`.

- [x] **Step 3: Add semantic UI header**

Create `include/kisurf/ai/ai_semantic_ui.h`:

```cpp
#pragma once

#include <kicommon.h>

#include <optional>
#include <vector>
#include <wx/string.h>

enum class AI_SEMANTIC_UI_TEXT_POLICY
{
    None,
    Plain,
    Redacted
};

struct KICOMMON_API AI_SEMANTIC_UI_BOUNDS
{
    bool m_Available = false;
    int  m_X = 0;
    int  m_Y = 0;
    int  m_Width = 0;
    int  m_Height = 0;
};

struct KICOMMON_API AI_SEMANTIC_UI_NODE
{
    wxString                    m_NodeId;
    wxString                    m_ParentNodeId;
    wxString                    m_Role;
    wxString                    m_Label;
    bool                        m_Enabled = true;
    bool                        m_Visible = true;
    bool                        m_Focused = false;
    wxString                    m_ActionName;
    wxString                    m_ToolActionId;
    AI_SEMANTIC_UI_TEXT_POLICY  m_TextPolicy = AI_SEMANTIC_UI_TEXT_POLICY::None;
    wxString                    m_TextValue;
    AI_SEMANTIC_UI_BOUNDS       m_Bounds;
};

struct KICOMMON_API AI_SEMANTIC_UI_TREE
{
    wxString                         m_FrameId;
    wxString                         m_Title;
    bool                             m_ScreenshotAvailable = false;
    wxString                         m_ScreenshotUnavailableReason;
    std::vector<AI_SEMANTIC_UI_NODE> m_Nodes;

    const AI_SEMANTIC_UI_NODE* FindNode( const wxString& aNodeId ) const;
};

struct KICOMMON_API AI_SEMANTIC_UI_ACTION_REQUEST
{
    wxString            m_NodeId;
    wxString            m_Action;
    bool                m_HasText = false;
    wxString            m_Text;
    std::optional<bool> m_Checked;
};

struct KICOMMON_API AI_SEMANTIC_UI_ACTION_RESULT
{
    bool     m_Success = false;
    wxString m_ErrorCode;
    wxString m_Message;
    wxString m_FocusedNodeId;
};

KICOMMON_API wxString RedactSemanticUiText( const wxString& aText );
```

- [x] **Step 4: Add semantic UI implementation**

Create `common/kisurf/ai/ai_semantic_ui.cpp`:

```cpp
#include <kisurf/ai/ai_semantic_ui.h>

#include <wx/regex.h>

const AI_SEMANTIC_UI_NODE* AI_SEMANTIC_UI_TREE::FindNode(
        const wxString& aNodeId ) const
{
    for( const AI_SEMANTIC_UI_NODE& node : m_Nodes )
    {
        if( node.m_NodeId == aNodeId )
            return &node;
    }

    return nullptr;
}


wxString RedactSemanticUiText( const wxString& aText )
{
    wxString text = aText;

    wxRegEx keyPattern( wxS( "sk-[A-Za-z0-9_-]{12,}" ) );
    keyPattern.ReplaceAll( &text, wxS( "sk-[redacted]" ) );

    wxRegEx envPattern( wxS( "(OPENAI_API_KEY|KISURF_AI_API_KEY)[[:space:]]*=[^[:space:]\\\"']+" ) );
    envPattern.ReplaceAll( &text, wxS( "\\1=[redacted]" ) );

    wxRegEx credentialPattern(
            wxS( "(credential|token|api key|api_key)[[:space:]]*:[^\\n\\r,}\\\"']+" ),
            wxRE_ADVANCED | wxRE_ICASE );
    credentialPattern.ReplaceAll( &text, wxS( "\\1: [redacted]" ) );

    if( text.length() > 4000 )
        text = text.Left( 4000 ) + wxS( "...[truncated]" );

    return text;
}
```

Add `kisurf/ai/ai_semantic_ui.cpp` to `common/CMakeLists.txt` after
`kisurf/ai/ai_runtime.cpp`.

- [x] **Step 5: Run semantic UI tests to verify GREEN**

Run:

```powershell
cmd.exe /S /C '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build out\build\x64-release --target qa_common --config Release'
$env:KICAD_RUN_FROM_BUILD_DIR='1'
$env:KICAD_BUILD_PATHS='C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad;C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common;C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api;C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal;C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew;C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema;C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb'
$env:PATH='D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;'+$env:PATH
.\qa_common.exe --run_test=AiSemanticUi
```

Workdir for the test executable:
`C:\Users\wenming\source\repos\kisurf\out\build\x64-release\qa\tests\common`.

Expected: `AiSemanticUi` exits 0.

Actual: after fixing the test string construction to start from `wxString`,
`qa_common.exe --run_test=AiSemanticUi` exited 0 with `No errors detected`.

## Task 2: Agent Pane Semantic Tree Builder

**Files:**
- Create: `include/kisurf/ai/ai_agent_panel_semantic.h`
- Create: `common/kisurf/ai/ai_agent_panel_semantic.cpp`
- Create: `qa/tests/common/test_ai_agent_panel_semantic.cpp`
- Modify: `common/CMakeLists.txt`
- Modify: `qa/tests/common/CMakeLists.txt`

- [x] **Step 1: Write failing Agent-pane semantic tests**

Create `qa/tests/common/test_ai_agent_panel_semantic.cpp`:

```cpp
#include <boost/test/unit_test.hpp>
#include <kisurf/ai/ai_agent_panel_semantic.h>

BOOST_AUTO_TEST_SUITE( AiAgentPanelSemantic )

BOOST_AUTO_TEST_CASE( EmitsStableAgentPaneNodeIds )
{
    AI_AGENT_PANEL_SEMANTIC_VIEW view;

    AI_SEMANTIC_UI_TREE tree = AiAgentPanelSemanticTree( view );

    const wxString ids[] = {
        wxS( "agent.root" ),
        wxS( "agent.background.toggle" ),
        wxS( "agent.tabs.chat" ),
        wxS( "agent.tabs.log" ),
        wxS( "agent.chat.transcript" ),
        wxS( "agent.log.entries" ),
        wxS( "agent.input" ),
        wxS( "agent.send" ),
        wxS( "agent.stop" ),
        wxS( "agent.preview.invoke" ),
        wxS( "agent.accept" ),
        wxS( "agent.reject" )
    };

    for( const wxString& id : ids )
        BOOST_CHECK_MESSAGE( tree.FindNode( id ), id.ToStdString() );
}

BOOST_AUTO_TEST_CASE( SendNodeReflectsInputState )
{
    AI_AGENT_PANEL_SEMANTIC_VIEW view;
    view.m_InputHasText = false;

    AI_SEMANTIC_UI_TREE tree = AiAgentPanelSemanticTree( view );
    BOOST_REQUIRE( tree.FindNode( wxS( "agent.send" ) ) );
    BOOST_CHECK( !tree.FindNode( wxS( "agent.send" ) )->m_Enabled );

    view.m_InputHasText = true;
    tree = AiAgentPanelSemanticTree( view );
    BOOST_REQUIRE( tree.FindNode( wxS( "agent.send" ) ) );
    BOOST_CHECK( tree.FindNode( wxS( "agent.send" ) )->m_Enabled );
}

BOOST_AUTO_TEST_CASE( SuggestionControlsReflectHandlersAndActiveSuggestion )
{
    AI_AGENT_PANEL_SEMANTIC_VIEW view;
    view.m_HasActiveSuggestion = false;
    view.m_CanPreviewSuggestion = true;
    view.m_CanAcceptSuggestion = true;

    AI_SEMANTIC_UI_TREE tree = AiAgentPanelSemanticTree( view );
    BOOST_CHECK( !tree.FindNode( wxS( "agent.preview.invoke" ) )->m_Enabled );
    BOOST_CHECK( !tree.FindNode( wxS( "agent.accept" ) )->m_Enabled );
    BOOST_CHECK( !tree.FindNode( wxS( "agent.reject" ) )->m_Enabled );

    view.m_HasActiveSuggestion = true;
    tree = AiAgentPanelSemanticTree( view );
    BOOST_CHECK( tree.FindNode( wxS( "agent.preview.invoke" ) )->m_Enabled );
    BOOST_CHECK( tree.FindNode( wxS( "agent.accept" ) )->m_Enabled );
    BOOST_CHECK( tree.FindNode( wxS( "agent.reject" ) )->m_Enabled );
}

BOOST_AUTO_TEST_SUITE_END()
```

Add `test_ai_agent_panel_semantic.cpp` to `QA_COMMON_SRCS` after
`test_ai_agent_panel.cpp`.

- [x] **Step 2: Run tests to verify RED**

Run the same `qa_common` build command from Task 1.

Expected: build fails because `<kisurf/ai/ai_agent_panel_semantic.h>` does not
exist.

Actual: build failed with `fatal error C1083: Cannot open include file:
'kisurf/ai/ai_agent_panel_semantic.h': No such file or directory`.

- [x] **Step 3: Add Agent-pane semantic header**

Create `include/kisurf/ai/ai_agent_panel_semantic.h`:

```cpp
#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_semantic_ui.h>
#include <kisurf/ai/ai_types.h>

#include <cstddef>

struct KICOMMON_API AI_AGENT_PANEL_SEMANTIC_VIEW
{
    bool                 m_BackgroundAgentEnabled = false;
    bool                 m_InputHasText = false;
    bool                 m_HasActiveSuggestion = false;
    bool                 m_CanPreviewSuggestion = false;
    bool                 m_CanAcceptSuggestion = false;
    size_t               m_MessageCount = 0;
    size_t               m_SuggestionCount = 0;
    size_t               m_LogEntryCount = 0;
};

KICOMMON_API AI_SEMANTIC_UI_TREE AiAgentPanelSemanticTree(
        const AI_AGENT_PANEL_SEMANTIC_VIEW& aView );
```

- [x] **Step 4: Add Agent-pane semantic implementation**

Create `common/kisurf/ai/ai_agent_panel_semantic.cpp`:

```cpp
#include <kisurf/ai/ai_agent_panel_semantic.h>

namespace
{
void addNode( AI_SEMANTIC_UI_TREE& aTree, wxString aId, wxString aParent,
              wxString aRole, wxString aLabel, bool aEnabled = true,
              wxString aAction = wxEmptyString,
              AI_SEMANTIC_UI_TEXT_POLICY aTextPolicy = AI_SEMANTIC_UI_TEXT_POLICY::None,
              wxString aTextValue = wxEmptyString )
{
    AI_SEMANTIC_UI_NODE node;
    node.m_NodeId = std::move( aId );
    node.m_ParentNodeId = std::move( aParent );
    node.m_Role = std::move( aRole );
    node.m_Label = std::move( aLabel );
    node.m_Enabled = aEnabled;
    node.m_ActionName = std::move( aAction );
    node.m_TextPolicy = aTextPolicy;
    node.m_TextValue = RedactSemanticUiText( aTextValue );
    aTree.m_Nodes.push_back( std::move( node ) );
}
}


AI_SEMANTIC_UI_TREE AiAgentPanelSemanticTree(
        const AI_AGENT_PANEL_SEMANTIC_VIEW& aView )
{
    AI_SEMANTIC_UI_TREE tree;
    tree.m_FrameId = wxS( "agent" );
    tree.m_Title = wxS( "Agent" );
    tree.m_ScreenshotAvailable = false;
    tree.m_ScreenshotUnavailableReason =
            wxS( "Agent pane semantic self-test exposes controls; canvas pixels come from visual frame tools." );

    addNode( tree, wxS( "agent.root" ), wxEmptyString, wxS( "panel" ),
             wxS( "Agent" ) );
    addNode( tree, wxS( "agent.background.toggle" ), wxS( "agent.root" ),
             wxS( "checkbox" ),
             aView.m_BackgroundAgentEnabled ? wxS( "Background Agent on" )
                                            : wxS( "Background Agent off" ),
             true, wxS( "toggle" ) );

    addNode( tree, wxS( "agent.tabs.chat" ), wxS( "agent.root" ), wxS( "tab" ),
             wxS( "Chat" ), true, wxS( "select" ) );
    addNode( tree, wxS( "agent.tabs.log" ), wxS( "agent.root" ), wxS( "tab" ),
             wxS( "Log" ), true, wxS( "select" ) );

    addNode( tree, wxS( "agent.chat.transcript" ), wxS( "agent.tabs.chat" ),
             wxS( "text" ), wxS( "Transcript" ), true, wxEmptyString,
             AI_SEMANTIC_UI_TEXT_POLICY::Plain,
             wxString::Format( wxS( "%zu messages" ), aView.m_MessageCount ) );
    addNode( tree, wxS( "agent.log.entries" ), wxS( "agent.tabs.log" ),
             wxS( "text" ), wxS( "Log entries" ), true, wxEmptyString,
             AI_SEMANTIC_UI_TEXT_POLICY::Plain,
             wxString::Format( wxS( "%zu entries" ), aView.m_LogEntryCount ) );

    addNode( tree, wxS( "agent.input" ), wxS( "agent.root" ), wxS( "textbox" ),
             wxS( "Input" ), true, wxS( "set_text" ),
             AI_SEMANTIC_UI_TEXT_POLICY::Redacted );
    addNode( tree, wxS( "agent.send" ), wxS( "agent.root" ), wxS( "button" ),
             wxS( "Send" ), aView.m_InputHasText, wxS( "invoke" ) );
    addNode( tree, wxS( "agent.stop" ), wxS( "agent.root" ), wxS( "button" ),
             wxS( "Stop" ), true, wxS( "invoke" ) );
    addNode( tree, wxS( "agent.preview.invoke" ), wxS( "agent.tabs.chat" ),
             wxS( "button" ), wxS( "Preview" ),
             aView.m_HasActiveSuggestion && aView.m_CanPreviewSuggestion,
             wxS( "invoke" ) );
    addNode( tree, wxS( "agent.accept" ), wxS( "agent.tabs.chat" ),
             wxS( "button" ), wxS( "Accept" ),
             aView.m_HasActiveSuggestion && aView.m_CanAcceptSuggestion,
             wxS( "invoke" ) );
    addNode( tree, wxS( "agent.reject" ), wxS( "agent.tabs.chat" ),
             wxS( "button" ), wxS( "Reject" ), aView.m_HasActiveSuggestion,
             wxS( "invoke" ) );

    return tree;
}
```

Add `kisurf/ai/ai_agent_panel_semantic.cpp` to `common/CMakeLists.txt` after
`kisurf/ai/ai_agent_panel_model.cpp`.

- [x] **Step 5: Run Agent-pane semantic tests to verify GREEN**

Run the same build and test environment commands from Task 1, then:

```powershell
.\qa_common.exe --run_test=AiAgentPanelSemantic
```

Expected: `AiAgentPanelSemantic` exits 0.

Actual: `qa_common.exe --run_test=AiAgentPanelSemantic` exited 0 with
`No errors detected`.

## Task 3: Wire AI_AGENT_PANEL Semantic API

**Files:**
- Modify: `include/kisurf/ai/ai_agent_panel.h`
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`
- Modify: `qa/tests/common/test_ai_agent_panel.cpp`

- [x] **Step 1: Add failing panel API surface test**

Add to `qa/tests/common/test_ai_agent_panel.cpp` after
`AgentPanelExposesBackgroundAgentToggleSurface`:

```cpp
BOOST_AUTO_TEST_CASE( AgentPanelExposesSemanticSelfTestSurface )
{
    BOOST_CHECK( ( std::is_member_function_pointer_v<
            decltype( &AI_AGENT_PANEL::SemanticUiTree )> ) );
    BOOST_CHECK( ( std::is_member_function_pointer_v<
            decltype( &AI_AGENT_PANEL::InvokeSemanticUiAction )> ) );
}
```

Run the same `qa_common` build command from Task 1.

Expected: build fails because the two methods do not exist.

Actual: build failed because `SemanticUiTree` and `InvokeSemanticUiAction`
were not members of `AI_AGENT_PANEL`.

- [x] **Step 2: Add panel method declarations**

In `include/kisurf/ai/ai_agent_panel.h`, add:

```cpp
#include <kisurf/ai/ai_semantic_ui.h>
```

Add public methods after `BackgroundAgentEnabled()`:

```cpp
AI_SEMANTIC_UI_TREE SemanticUiTree() const;
AI_SEMANTIC_UI_ACTION_RESULT InvokeSemanticUiAction(
        const AI_SEMANTIC_UI_ACTION_REQUEST& aRequest );
```

- [x] **Step 3: Implement panel semantic tree**

In `common/kisurf/ai/ai_agent_panel.cpp`, add:

```cpp
#include <kisurf/ai/ai_agent_panel_semantic.h>
```

Add this method after `BackgroundAgentEnabled()`:

```cpp
AI_SEMANTIC_UI_TREE AI_AGENT_PANEL::SemanticUiTree() const
{
    AI_AGENT_PANEL_SEMANTIC_VIEW view;
    view.m_BackgroundAgentEnabled = m_Model->BackgroundAgentEnabled();
    view.m_InputHasText = m_Input && m_Model->CanSend( m_Input->GetValue() );
    view.m_HasActiveSuggestion = m_Model->LatestActiveSuggestionId().has_value();
    view.m_CanPreviewSuggestion = static_cast<bool>( m_PreviewSuggestionHandler );
    view.m_CanAcceptSuggestion = static_cast<bool>( m_AcceptSuggestionHandler );
    view.m_MessageCount = m_Model->Messages().size();
    view.m_SuggestionCount = m_Model->Suggestions().size();
    view.m_LogEntryCount = m_Model->ObservabilityEntries( 128 ).size();
    return AiAgentPanelSemanticTree( view );
}
```

- [x] **Step 4: Implement panel semantic action invocation**

Add helper functions in the anonymous namespace of
`common/kisurf/ai/ai_agent_panel.cpp`:

```cpp
AI_SEMANTIC_UI_ACTION_RESULT semanticActionError( wxString aCode, wxString aMessage )
{
    AI_SEMANTIC_UI_ACTION_RESULT result;
    result.m_Success = false;
    result.m_ErrorCode = std::move( aCode );
    result.m_Message = RedactSemanticUiText( aMessage );
    return result;
}


AI_SEMANTIC_UI_ACTION_RESULT semanticActionOk( wxString aFocusedNode = wxEmptyString )
{
    AI_SEMANTIC_UI_ACTION_RESULT result;
    result.m_Success = true;
    result.m_FocusedNodeId = std::move( aFocusedNode );
    return result;
}
```

Add this method after `SemanticUiTree()`:

```cpp
AI_SEMANTIC_UI_ACTION_RESULT AI_AGENT_PANEL::InvokeSemanticUiAction(
        const AI_SEMANTIC_UI_ACTION_REQUEST& aRequest )
{
    const AI_SEMANTIC_UI_TREE tree = SemanticUiTree();
    const AI_SEMANTIC_UI_NODE* node = tree.FindNode( aRequest.m_NodeId );

    if( !node )
        return semanticActionError( wxS( "unknown_node" ), aRequest.m_NodeId );

    if( !node->m_Enabled )
        return semanticActionError( wxS( "disabled_node" ), aRequest.m_NodeId );

    if( aRequest.m_NodeId == wxS( "agent.input" ) && aRequest.m_Action == wxS( "set_text" ) )
    {
        if( !aRequest.m_HasText )
            return semanticActionError( wxS( "missing_text" ), wxS( "set_text requires text" ) );

        if( !m_Input )
            return semanticActionError( wxS( "unavailable_node" ), aRequest.m_NodeId );

        m_Input->SetValue( aRequest.m_Text );
        m_Input->SetFocus();
        return semanticActionOk( wxS( "agent.input" ) );
    }

    if( aRequest.m_NodeId == wxS( "agent.background.toggle" )
        && aRequest.m_Action == wxS( "toggle" ) )
    {
        const bool enabled = aRequest.m_Checked.value_or( !BackgroundAgentEnabled() );
        SetBackgroundAgentEnabled( enabled );
        return semanticActionOk( wxS( "agent.background.toggle" ) );
    }

    if( aRequest.m_Action != wxS( "invoke" ) )
        return semanticActionError( wxS( "unsupported_action" ), aRequest.m_Action );

    if( aRequest.m_NodeId == wxS( "agent.send" ) )
    {
        SendCurrentText();
        return semanticActionOk( wxS( "agent.input" ) );
    }

    if( aRequest.m_NodeId == wxS( "agent.stop" ) )
    {
        m_Model->CancelLastRequest();
        RefreshLog();
        return semanticActionOk();
    }

    if( aRequest.m_NodeId == wxS( "agent.preview.invoke" ) )
        return PreviewLatestSuggestion() ? semanticActionOk()
                                         : semanticActionError( wxS( "action_failed" ),
                                                                aRequest.m_NodeId );

    if( aRequest.m_NodeId == wxS( "agent.accept" ) )
        return AcceptLatestSuggestion() ? semanticActionOk()
                                        : semanticActionError( wxS( "action_failed" ),
                                                               aRequest.m_NodeId );

    if( aRequest.m_NodeId == wxS( "agent.reject" ) )
        return RejectLatestSuggestion() ? semanticActionOk()
                                        : semanticActionError( wxS( "action_failed" ),
                                                               aRequest.m_NodeId );

    return semanticActionError( wxS( "unsupported_action" ), aRequest.m_NodeId );
}
```

- [x] **Step 5: Run panel tests to verify GREEN**

Run the same build and test environment commands from Task 1, then:

```powershell
.\qa_common.exe --run_test=AiAgentPanel
```

Expected: `AiAgentPanel` exits 0.

Actual: `qa_common.exe --run_test=AiAgentPanel` exited 0 with
`No errors detected`.

## Task 4: Verification, Plan Status, And Commit

**Files:**
- Modify: `docs/superpowers/plans/2026-06-19-ai-native-semantic-self-test-implementation.md`

- [x] **Step 1: Run targeted common tests**

Run:

```powershell
cmd.exe /S /C '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build out\build\x64-release --target qa_common --config Release'
$env:KICAD_RUN_FROM_BUILD_DIR='1'
$env:KICAD_BUILD_PATHS='C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad;C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common;C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api;C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal;C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew;C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema;C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb'
$env:PATH='D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;'+$env:PATH
.\qa_common.exe --run_test=AiSemanticUi,AiAgentPanelSemantic,AiAgentPanel
```

Expected: build exits 0; targeted tests exit 0.

Actual:
- `cmake --build out\build\x64-release --target qa_common --config Release`
  exited 0.
- `qa_common.exe --run_test=AiSemanticUi,AiAgentPanelSemantic,AiAgentPanel`
  exited 0 with `No errors detected`.

- [x] **Step 2: Build editor targets**

Run:

```powershell
cmd.exe /S /C '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build out\build\x64-release --target pcbnew --config Release && cmake --build out\build\x64-release --target eeschema --config Release'
```

Expected: both editor targets build.

Actual: `pcbnew` and `eeschema` both built successfully. The build emitted the
pre-existing `length_delay_calculation_item.h` C5266 warning while building
`pcbnew`.

- [x] **Step 3: Run diff and secret checks**

Run:

```powershell
git diff --check
rg -n "sk-[0-9A-Za-z_-]{20,}|OPENAI_API_KEY\s*=|KISURF_AI_API_KEY\s*=" include\kisurf\ai common\kisurf\ai qa\tests\common docs\superpowers\specs\2026-06-19-ai-native-semantic-self-test-design.md docs\superpowers\plans\2026-06-16-agent-pane-computer-use-smoke-test.md docs\superpowers\plans\2026-06-19-ai-native-semantic-self-test-implementation.md
```

Expected: whitespace check exits 0; secret scan has no matches.

Actual:
- `git diff --check` exited 0.
- Secret scan exited with no matches.

- [x] **Step 4: Update this plan status**

Check off completed steps and add actual verification output summaries under
the relevant verification steps.

- [x] **Step 5: Commit**

Stage only files from this plan. Do not stage unrelated
`qa/tests/pcbnew/test_module.cpp`.

```powershell
git add include/kisurf/ai/ai_semantic_ui.h common/kisurf/ai/ai_semantic_ui.cpp include/kisurf/ai/ai_agent_panel_semantic.h common/kisurf/ai/ai_agent_panel_semantic.cpp include/kisurf/ai/ai_agent_panel.h common/kisurf/ai/ai_agent_panel.cpp common/CMakeLists.txt qa/tests/common/CMakeLists.txt qa/tests/common/test_ai_semantic_ui.cpp qa/tests/common/test_ai_agent_panel_semantic.cpp qa/tests/common/test_ai_agent_panel.cpp docs/superpowers/specs/2026-06-19-ai-native-semantic-self-test-design.md docs/superpowers/plans/2026-06-16-agent-pane-computer-use-smoke-test.md docs/superpowers/plans/2026-06-19-ai-native-semantic-self-test-implementation.md
git commit -m "feat: add agent pane semantic self-test surface"
```

Expected: commit succeeds.

## Self-Review

- Spec coverage: Tasks implement common semantic data model, stable Agent-pane
  nodes, enabled-state mapping, panel APIs, safe action invocation, tests, and
  stale smoke evidence correction.
- Placeholder scan: The plan names exact files, methods, commands, expected
  red/green outcomes, and commit command.
- Type consistency: `AI_SEMANTIC_UI_TREE`, `AI_SEMANTIC_UI_NODE`,
  `AI_SEMANTIC_UI_ACTION_REQUEST`, `AI_AGENT_PANEL_SEMANTIC_VIEW`,
  `SemanticUiTree`, and `InvokeSemanticUiAction` are used consistently.
- Scope check: The implementation stays limited to Agent pane self-test
  semantics and does not attempt full KiCad UI automation.
