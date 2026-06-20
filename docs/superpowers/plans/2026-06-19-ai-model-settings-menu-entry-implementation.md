# AI Model Settings Menu Entry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a dedicated `AI -> Model Settings...` command in PCB and schematic editors while reusing the existing Model Settings dialog and secret-backed configuration store.

**Architecture:** Define one common action, add it to both AI menus, and bind each editor control to a small frame wrapper that delegates to `AI_AGENT_PANEL::ShowModelSettingsDialog()`. Keep all provider and persistence behavior in the existing model-config layer.

**Tech Stack:** C++17, wxWidgets menu/action framework, KiCad `TOOL_ACTION`, Boost `qa_common` source-discoverability tests.

---

### Task 1: Lock Menu And Action Discoverability With A Failing Test

**Files:**
- Modify: `qa/tests/common/test_ai_agent_menu_discoverability.cpp`

- [ ] **Step 1: Add regression checks**

Add checks that read the existing source files and require:

```cpp
BOOST_CHECK( containsIgnoringWhitespace(
        actionsHeader, "static TOOL_ACTION showAiModelSettings;" ) );
BOOST_CHECK( containsIgnoringWhitespace(
        actionsSource,
        "TOOL_ACTION ACTIONS::showAiModelSettings( TOOL_ACTION_ARGS()" ) );
BOOST_CHECK( actionsSource.find( "common.Control.showAiModelSettings" )
             != std::string::npos );
BOOST_CHECK( actionsSource.find( "Model Settings..." ) != std::string::npos );
BOOST_CHECK( containsIgnoringWhitespace(
        pcbMenu, "aiMenu->Add( ACTIONS::showAiModelSettings );" ) );
BOOST_CHECK( containsIgnoringWhitespace(
        schMenu, "aiMenu->Add( ACTIONS::showAiModelSettings );" ) );
BOOST_CHECK( containsIgnoringWhitespace(
        pcbControl,
        "Go( &BOARD_EDITOR_CONTROL::ShowAiModelSettings, ACTIONS::showAiModelSettings.MakeEvent() );" ) );
BOOST_CHECK( containsIgnoringWhitespace(
        schControl,
        "Go( &SCH_EDITOR_CONTROL::ShowAiModelSettings, ACTIONS::showAiModelSettings.MakeEvent() );" ) );
```

- [ ] **Step 2: Run the targeted test and verify RED**

Run:

```powershell
$dllDirs = Get-ChildItem .\out\build\x64-release -Recurse -Filter *.dll | Select-Object -ExpandProperty DirectoryName -Unique
$env:PATH = ($dllDirs -join ';') + ';' + $env:PATH
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiAgentMenuDiscoverability
```

Expected: fail because `showAiModelSettings` does not exist yet.

### Task 2: Add The Common Action

**Files:**
- Modify: `include/tool/actions.h`
- Modify: `common/tool/actions.cpp`

- [ ] **Step 1: Declare the action**

Add:

```cpp
static TOOL_ACTION showAiModelSettings;
```

next to `showAgentPanel`.

- [ ] **Step 2: Define the action**

Add:

```cpp
TOOL_ACTION ACTIONS::showAiModelSettings( TOOL_ACTION_ARGS()
        .Name( "common.Control.showAiModelSettings" )
        .Scope( AS_GLOBAL )
        .FriendlyName( _( "Model Settings..." ) )
        .Tooltip( _( "Configure the Agent model provider" ) )
        .Icon( BITMAPS::tools ) );
```

next to `ACTIONS::showAgentPanel`.

- [ ] **Step 3: Run the targeted test**

Run:

```powershell
$dllDirs = Get-ChildItem .\out\build\x64-release -Recurse -Filter *.dll | Select-Object -ExpandProperty DirectoryName -Unique
$env:PATH = ($dllDirs -join ';') + ';' + $env:PATH
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiAgentMenuDiscoverability
```

Expected: still fail because menus and bindings are not wired.

### Task 3: Wire Editor Menus And Controllers

**Files:**
- Modify: `pcbnew/menubar_pcb_editor.cpp`
- Modify: `eeschema/menubar.cpp`
- Modify: `pcbnew/pcb_edit_frame.h`
- Modify: `eeschema/sch_edit_frame.h`
- Modify: `pcbnew/toolbars_pcb_editor.cpp`
- Modify: `eeschema/sch_edit_frame.cpp`
- Modify: `pcbnew/tools/board_editor_control.h`
- Modify: `eeschema/tools/sch_editor_control.h`
- Modify: `pcbnew/tools/board_editor_control.cpp`
- Modify: `eeschema/tools/sch_editor_control.cpp`

- [ ] **Step 1: Add the menu entries**

Add after `showAgentPanel` in both AI menus:

```cpp
aiMenu->AppendSeparator();
aiMenu->Add( ACTIONS::showAiModelSettings );
```

- [ ] **Step 2: Add frame wrappers**

Declare and implement:

```cpp
void ShowAgentModelSettings();
```

with:

```cpp
void PCB_EDIT_FRAME::ShowAgentModelSettings()
{
    if( m_agentPanel )
        m_agentPanel->ShowModelSettingsDialog();
}
```

and the schematic equivalent.

- [ ] **Step 3: Add controller handlers and bindings**

Declare and implement:

```cpp
int ShowAiModelSettings( const TOOL_EVENT& aEvent );
```

with:

```cpp
int BOARD_EDITOR_CONTROL::ShowAiModelSettings( const TOOL_EVENT& aEvent )
{
    getEditFrame<PCB_EDIT_FRAME>()->ShowAgentModelSettings();
    return 0;
}
```

and the schematic equivalent. Bind:

```cpp
Go( &BOARD_EDITOR_CONTROL::ShowAiModelSettings,
    ACTIONS::showAiModelSettings.MakeEvent() );
```

and the schematic equivalent.

- [ ] **Step 4: Run the targeted test and verify GREEN**

Run:

```powershell
$dllDirs = Get-ChildItem .\out\build\x64-release -Recurse -Filter *.dll | Select-Object -ExpandProperty DirectoryName -Unique
$env:PATH = ($dllDirs -join ';') + ';' + $env:PATH
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiAgentMenuDiscoverability
```

Expected: pass.

### Task 4: Documentation, Local Configuration, And Verification

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update README**

Describe `AI -> Model Settings...` as the primary route and Agent panel
`Model...` as the secondary route. Keep the rule that real keys are stored in
the platform secret store, not source files.

- [ ] **Step 2: Preconfigure this machine outside the repo**

Write the local user settings JSON with the OpenAI-compatible provider, the
configured base URL, the default model, and `api_key_ref`. Store the API key in
Windows Credential Manager target
`org.kicad.kisurf.ai_model:openai-compatible.default_api_key`. Do not echo the
key and do not write it into repository files.

- [ ] **Step 3: Run verification**

Run:

```powershell
$dllDirs = Get-ChildItem .\out\build\x64-release -Recurse -Filter *.dll | Select-Object -ExpandProperty DirectoryName -Unique
$env:PATH = ($dllDirs -join ';') + ';' + $env:PATH
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiAgentMenuDiscoverability,AiModelConfig,AiAgentPanel,AiAgentPanelModel
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=Ai*
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build .\out\build\x64-release --target pcbnew --config Release"
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build .\out\build\x64-release --target eeschema --config Release"
```

Expected: all commands exit 0.

- [ ] **Step 4: Run secret scan and commit**

Run the project secret scan pattern and ensure it emits no matches. Stage only
the spec, plan, tests, source, and README changes. Leave unrelated dirty files
unstaged.
