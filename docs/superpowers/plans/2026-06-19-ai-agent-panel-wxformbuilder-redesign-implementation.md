# AI Agent Panel wxFormBuilder Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the Agent panel layout into wxFormBuilder-generated base files and make the chat pane more polished while preserving existing Agent behavior.

**Architecture:** Add `AI_AGENT_PANEL_BASE` generated from wxFormBuilder under `common/kisurf/ai`. Change `AI_AGENT_PANEL` to inherit from that base and keep all model, semantic UI, and event behavior in the derived class. Render chat transcript with `HTML_WINDOW` through a testable escaping/rendering helper.

**Tech Stack:** C++20, wxWidgets, wxFormBuilder 4.2.1, KiCad `HTML_WINDOW`, Boost unit tests, Computer Use GUI smoke.

---

## File Structure

- Create: `common/kisurf/ai/ai_agent_panel_base.fbp`
  - wxFormBuilder project for the generated Agent panel layout.
- Create: `common/kisurf/ai/ai_agent_panel_base.h`
  - Generated base class and protected widget members.
- Create: `common/kisurf/ai/ai_agent_panel_base.cpp`
  - Generated widget construction and virtual event hook connections.
- Modify: `common/CMakeLists.txt`
  - Adds `kisurf/ai/ai_agent_panel_base.cpp` to `KICOMMON_SRCS`.
- Modify: `include/kisurf/ai/ai_agent_panel.h`
  - `AI_AGENT_PANEL` derives from `AI_AGENT_PANEL_BASE`.
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`
  - Removes manual layout creation, uses generated members, overrides event handlers, and renders HTML transcript.
- Modify: `qa/tests/common/test_ai_agent_panel.cpp`
  - Adds TDD coverage for base inheritance, widget member surface, and transcript HTML rendering.
- Create: `docs/superpowers/specs/2026-06-19-ai-agent-panel-wxformbuilder-redesign-design.md`
- Create: `docs/superpowers/plans/2026-06-19-ai-agent-panel-wxformbuilder-redesign-implementation.md`

## Task 1: Generate wxFormBuilder Base Layout

**Files:**
- Create: `common/kisurf/ai/ai_agent_panel_base.fbp`
- Create: `common/kisurf/ai/ai_agent_panel_base.h`
- Create: `common/kisurf/ai/ai_agent_panel_base.cpp`

- [ ] **Step 1: Create the panel in wxFormBuilder**

Use wxFormBuilder to create a project with:

- project file name `ai_agent_panel_base`;
- class `AI_AGENT_PANEL_BASE`;
- base object `Panel`;
- generated language `C++`;
- class decoration `KICOMMON_API`;
- relative path enabled.

The layout must contain these protected members:

```cpp
wxButton*      m_ModelSettingsButton;
wxCheckBox*    m_BackgroundAgentToggle;
wxNotebook*    m_Notebook;
HTML_WINDOW*   m_Transcript;
wxTextCtrl*    m_Log;
wxTextCtrl*    m_Input;
wxButton*      m_PreviewButton;
wxButton*      m_AcceptButton;
wxButton*      m_RejectButton;
wxButton*      m_SendButton;
wxButton*      m_StopButton;
```

Use a vertical root sizer:

- top horizontal toolbar row;
- notebook with Chat and Log pages only;
- bottom composer row with a multi-line prompt input and Send/Stop buttons.
- no Routing, Place, Via, Zone, or Preview mode selector in the Chat Panel.
  Those are background workspace contexts, not user-visible chat categories.

- [ ] **Step 2: Configure virtual event hooks**

The generated base class should expose virtual handlers:

```cpp
virtual void OnModelSettings( wxCommandEvent& event ) { event.Skip(); }
virtual void OnBackgroundAgentToggled( wxCommandEvent& event ) { event.Skip(); }
virtual void OnPromptEnter( wxCommandEvent& event ) { event.Skip(); }
virtual void OnSend( wxCommandEvent& event ) { event.Skip(); }
virtual void OnStop( wxCommandEvent& event ) { event.Skip(); }
virtual void OnPreviewSuggestion( wxCommandEvent& event ) { event.Skip(); }
virtual void OnAcceptSuggestion( wxCommandEvent& event ) { event.Skip(); }
virtual void OnRejectSuggestion( wxCommandEvent& event ) { event.Skip(); }
```

- [ ] **Step 3: Generate code from wxFormBuilder**

Run:

```powershell
& 'C:\Program Files\wxFormBuilder\wxFormBuilder.exe' --generate .\common\kisurf\ai\ai_agent_panel_base.fbp
```

Expected: generated `.cpp` and `.h` are created or updated.

- [ ] **Step 4: Inspect with Computer Use**

Open the `.fbp` in wxFormBuilder and use Computer Use to verify that the design
tree shows `AI_AGENT_PANEL_BASE`, the top controls, Chat/Log pages, and the
bottom composer. It must not show a mode choice, Routing/Place categories, or a
Preview page; all previews render in the editor workspace.

## Task 2: Red Tests

**Files:**
- Modify: `qa/tests/common/test_ai_agent_panel.cpp`

- [ ] **Step 1: Add base inheritance and transcript rendering tests**

Add includes:

```cpp
#include <kisurf/ai/ai_agent_panel_base.h>
#include <kisurf/ai/ai_agent_panel_model.h>
```

Add tests:

```cpp
class AI_AGENT_PANEL_BASE_SURFACE_TEST : public AI_AGENT_PANEL_BASE
{
public:
    explicit AI_AGENT_PANEL_BASE_SURFACE_TEST( wxWindow* aParent ) :
            AI_AGENT_PANEL_BASE( aParent )
    {
    }

    bool HasExpectedControls() const
    {
        return m_ModelSettingsButton && m_BackgroundAgentToggle
               && m_Notebook && m_Transcript && m_Log && m_Input
               && m_PreviewButton && m_AcceptButton
               && m_RejectButton && m_SendButton && m_StopButton;
    }
};

BOOST_AUTO_TEST_CASE( AgentPanelInheritsGeneratedWxFormBuilderBase )
{
    BOOST_CHECK( ( std::is_base_of_v<AI_AGENT_PANEL_BASE, AI_AGENT_PANEL> ) );
}

BOOST_AUTO_TEST_CASE( AgentPanelBaseExposesExpectedControlSurface )
{
    BOOST_CHECK( ( std::is_member_object_pointer_v<
            decltype( &AI_AGENT_PANEL_BASE_SURFACE_TEST::m_Transcript )> ) );
    BOOST_CHECK( ( std::is_member_object_pointer_v<
            decltype( &AI_AGENT_PANEL_BASE_SURFACE_TEST::m_Input )> ) );
    BOOST_CHECK( ( std::is_member_object_pointer_v<
            decltype( &AI_AGENT_PANEL_BASE_SURFACE_TEST::m_SendButton )> ) );
}

BOOST_AUTO_TEST_CASE( AgentPanelTranscriptHtmlEscapesMessageText )
{
    std::vector<AI_AGENT_MESSAGE> messages;
    AI_AGENT_MESSAGE user;
    user.m_Role = wxS( "user" );
    user.m_Text = wxS( "<route & preview>" );
    messages.push_back( user );

    AI_AGENT_MESSAGE assistant;
    assistant.m_Role = wxS( "assistant" );
    assistant.m_Text = wxS( "Use anchor A1" );
    messages.push_back( assistant );

    wxString html = AiAgentTranscriptHtml( messages );

    BOOST_CHECK( html.Contains( wxS( "You" ) ) );
    BOOST_CHECK( html.Contains( wxS( "Agent" ) ) );
    BOOST_CHECK( html.Contains( wxS( "&lt;route &amp; preview&gt;" ) ) );
    BOOST_CHECK( !html.Contains( wxS( "<route & preview>" ) ) );
}
```

- [ ] **Step 2: Verify RED**

Run:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build .\out\build\x64-release --target qa_common --config Release"
```

Expected before implementation: build fails because `AI_AGENT_PANEL_BASE` and
`AiAgentTranscriptHtml` are not wired into the project yet.

## Task 3: Wire Generated Base Into Agent Panel

**Files:**
- Modify: `common/CMakeLists.txt`
- Modify: `include/kisurf/ai/ai_agent_panel.h`
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`

- [ ] **Step 1: Add base source to CMake**

In `KICOMMON_SRCS`, add:

```cmake
kisurf/ai/ai_agent_panel_base.cpp
```

next to `kisurf/ai/ai_agent_panel.cpp`.

- [ ] **Step 2: Change inheritance**

In `include/kisurf/ai/ai_agent_panel.h`, include the base header:

```cpp
#include <kisurf/ai/ai_agent_panel_base.h>
```

Change:

```cpp
class KICOMMON_API AI_AGENT_PANEL : public wxPanel
```

to:

```cpp
class KICOMMON_API AI_AGENT_PANEL : public AI_AGENT_PANEL_BASE
```

Add private overrides for the generated virtual handlers:

```cpp
void OnModelSettings( wxCommandEvent& aEvent ) override;
void OnBackgroundAgentToggled( wxCommandEvent& aEvent ) override;
void OnPromptEnter( wxCommandEvent& aEvent ) override;
void OnSend( wxCommandEvent& aEvent ) override;
void OnStop( wxCommandEvent& aEvent ) override;
void OnPreviewSuggestion( wxCommandEvent& aEvent ) override;
void OnAcceptSuggestion( wxCommandEvent& aEvent ) override;
void OnRejectSuggestion( wxCommandEvent& aEvent ) override;
```

- [ ] **Step 3: Use generated controls in constructor**

In `AI_AGENT_PANEL::AI_AGENT_PANEL`, call `AI_AGENT_PANEL_BASE( aParent )`
instead of `wxPanel( aParent, wxID_ANY )`, remove manual sizer/control creation,
and keep only:

```cpp
m_BackgroundAgentToggle->SetValue( m_Model->BackgroundAgentEnabled() );
```

Do not add a mode-order array, a mode choice control, a public panel context
setter, or any visible Routing/Place context selector. The panel is only a chat
command surface.

- [ ] **Step 4: Implement generated event overrides**

Add:

```cpp
void AI_AGENT_PANEL::OnModelSettings( wxCommandEvent& )
{
    ShowModelSettingsDialog();
}

void AI_AGENT_PANEL::OnBackgroundAgentToggled( wxCommandEvent& )
{
    SetBackgroundAgentEnabled( m_BackgroundAgentToggle->GetValue() );
}

void AI_AGENT_PANEL::OnPromptEnter( wxCommandEvent& )
{
    SendCurrentText();
}

void AI_AGENT_PANEL::OnSend( wxCommandEvent& )
{
    SendCurrentText();
}

void AI_AGENT_PANEL::OnStop( wxCommandEvent& )
{
    m_Model->CancelLastRequest();
    RefreshLog();
}

void AI_AGENT_PANEL::OnPreviewSuggestion( wxCommandEvent& )
{
    PreviewLatestSuggestion();
}

void AI_AGENT_PANEL::OnAcceptSuggestion( wxCommandEvent& )
{
    AcceptLatestSuggestion();
}

void AI_AGENT_PANEL::OnRejectSuggestion( wxCommandEvent& )
{
    RejectLatestSuggestion();
}
```

## Task 4: HTML Transcript Rendering

**Files:**
- Modify: `include/kisurf/ai/ai_agent_panel.h`
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`
- Modify: `qa/tests/common/test_ai_agent_panel.cpp`

- [ ] **Step 1: Declare the helper**

Add:

```cpp
KICOMMON_API wxString AiAgentTranscriptHtml(
        const std::vector<AI_AGENT_MESSAGE>& aMessages );
```

- [ ] **Step 2: Implement escaping and message cards**

Implement:

```cpp
wxString AiAgentTranscriptHtml( const std::vector<AI_AGENT_MESSAGE>& aMessages )
{
    wxString html;
    html << wxS( "<html><body bgcolor=\"#ffffff\">" );

    if( aMessages.empty() )
    {
        html << wxS( "<font color=\"#666666\">Ask the Agent to inspect, route, place, or explain the current board.</font>" );
    }

    for( const AI_AGENT_MESSAGE& message : aMessages )
    {
        const bool isUser = message.m_Role.CmpNoCase( wxS( "user" ) ) == 0;
        const wxString label = isUser ? wxString( wxS( "You" ) )
                                      : wxString( wxS( "Agent" ) );
        const wxString color = isUser ? wxString( wxS( "#f3f6fb" ) )
                                      : wxString( wxS( "#eef7f1" ) );
        html << wxS( "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"4\"><tr><td bgcolor=\"" )
             << color << wxS( "\"><b>" ) << label << wxS( "</b><br>" )
             << wxHtmlEscape( message.m_Text ) << wxS( "</td></tr></table><br>" );
    }

    html << wxS( "</body></html>" );
    return html;
}
```

Include the wx header that provides `wxHtmlEscape`.

- [ ] **Step 3: Render transcript HTML**

Change `RefreshTranscript()` to call:

```cpp
m_Transcript->SetPage( AiAgentTranscriptHtml( m_Model->Messages() ) );
```

## Task 5: Verification And Commit

**Files:**
- All touched files above.

- [ ] **Step 1: Run targeted and broad tests**

Run:

```powershell
$dllDirs = Get-ChildItem .\out\build\x64-release -Recurse -Filter *.dll | Select-Object -ExpandProperty DirectoryName -Unique
$env:PATH = ($dllDirs -join ';') + ';' + $env:PATH
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiAgentPanel --report_level=short
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=Ai* --report_level=short
```

Expected: both exit 0.

- [ ] **Step 2: Build PCB Editor**

Run:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build .\out\build\x64-release --target pcbnew --config Release"
```

Expected: exits 0.

- [ ] **Step 3: GUI smoke with Computer Use**

Run:

```powershell
.\tools\run_from_build.ps1 -BuildDir .\out\build\x64-release -App pcbnew
```

Use Computer Use to verify:

- no missing-DLL/system-error modal;
- `AI -> Agent` opens;
- Agent panel shows Chat/Preview/Log, Model, Background Agent, Send, and Stop;
- composer accepts typed text;
- after sending a short prompt, the transcript area shows the user message and
  Agent response/diagnostic in the redesigned chat surface.

- [ ] **Step 4: Static and secret checks**

Run:

```powershell
git diff --check -- common/CMakeLists.txt include/kisurf/ai/ai_agent_panel.h common/kisurf/ai/ai_agent_panel.cpp common/kisurf/ai/ai_agent_panel_base.cpp common/kisurf/ai/ai_agent_panel_base.h common/kisurf/ai/ai_agent_panel_base.fbp qa/tests/common/test_ai_agent_panel.cpp docs/superpowers/specs/2026-06-19-ai-agent-panel-wxformbuilder-redesign-design.md docs/superpowers/plans/2026-06-19-ai-agent-panel-wxformbuilder-redesign-implementation.md
$prefix = 'sk' + '-'
$pattern = $prefix + '[A-Za-z0-9_-]{20,}|OPENAI_API_KEY=.*' + $prefix
rg -n $pattern common/CMakeLists.txt include/kisurf/ai/ai_agent_panel.h common/kisurf/ai/ai_agent_panel.cpp common/kisurf/ai/ai_agent_panel_base.cpp common/kisurf/ai/ai_agent_panel_base.h common/kisurf/ai/ai_agent_panel_base.fbp qa/tests/common/test_ai_agent_panel.cpp docs/superpowers/specs/2026-06-19-ai-agent-panel-wxformbuilder-redesign-design.md docs/superpowers/plans/2026-06-19-ai-agent-panel-wxformbuilder-redesign-implementation.md
```

Expected: no whitespace errors and no secret matches.

- [ ] **Step 5: Commit**

Run:

```powershell
git add common/CMakeLists.txt include/kisurf/ai/ai_agent_panel.h common/kisurf/ai/ai_agent_panel.cpp common/kisurf/ai/ai_agent_panel_base.cpp common/kisurf/ai/ai_agent_panel_base.h common/kisurf/ai/ai_agent_panel_base.fbp qa/tests/common/test_ai_agent_panel.cpp docs/superpowers/specs/2026-06-19-ai-agent-panel-wxformbuilder-redesign-design.md docs/superpowers/plans/2026-06-19-ai-agent-panel-wxformbuilder-redesign-implementation.md
git commit -m "ui: redesign agent panel with wxformbuilder"
```

Do not stage unrelated `qa/tests/pcbnew/test_module.cpp`.

## Self-Review

- Spec coverage: generated base, derived behavior, chat UI, tests, build, and
  GUI smoke all map to tasks.
- Placeholder scan: no TBD/TODO/implement-later placeholders remain.
- Type consistency: generated control names match existing derived-code member
  names where possible.
