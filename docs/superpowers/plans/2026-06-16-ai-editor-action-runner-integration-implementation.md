# AI Editor Action Runner Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire the policy-gated AI action tool-call handler into live PCB and schematic Agent panes through a reusable callback runner.

**Architecture:** Keep provider/runtime code editor-agnostic by adding a small common runner over `std::function`, then let `AI_AGENT_PANEL` own the runner, policy, activity log, and handler lifetime. PCB and schematic frames install callbacks that preflight `ACTION_MANAGER::FindAction(...)` before calling `TOOL_MANAGER::RunAction(...)`.

**Tech Stack:** C++17, wxWidgets, KiCad `TOOL_MANAGER`, Boost.Test, CMake.

---

## File Structure

- Create: `include/kisurf/ai/ai_callback_action_runner.h`
- Create: `common/kisurf/ai/ai_callback_action_runner.cpp`
- Modify: `common/CMakeLists.txt`
- Create: `qa/tests/common/test_ai_callback_action_runner.cpp`
- Modify: `qa/tests/common/CMakeLists.txt`
- Modify: `include/kisurf/ai/ai_agent_panel.h`
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`
- Modify: `qa/tests/common/test_ai_agent_panel.cpp`
- Modify: `pcbnew/pcb_edit_frame.cpp`
- Modify: `eeschema/sch_edit_frame.cpp`
- Modify: `docs/superpowers/specs/2026-06-16-kisurf-ai-native-spec-index.md`

## Verification Commands

Common targeted verification:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_common -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiCallbackActionRunner,AiAgentPanel,AiActionToolCallHandler,AiAgentPanelModel,AiNativeRuntime,AiToolExecution --log_level=test_suite
```

PCB targeted verification:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_pcbnew -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\pcbnew\qa_pcbnew.exe --run_test=AiPcbMoveEditAdapter,AiPcbObjectResolver,AiPcbContextAdapter --log_level=test_suite
```

Schematic targeted verification:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_eeschema -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\eeschema\qa_eeschema.exe --run_test=AiSchMoveEditAdapter,AiSchObjectResolver,AiSchContextAdapter --log_level=test_suite
```

The known missing schema warning is acceptable when Boost reports no errors.

## Task 1: Callback Runner Tests

**Files:**
- Create: `qa/tests/common/test_ai_callback_action_runner.cpp`
- Modify: `qa/tests/common/CMakeLists.txt`

- [ ] **Step 1: Write the failing runner tests**

```cpp
#include <boost/test/unit_test.hpp>

#include <kisurf/ai/ai_callback_action_runner.h>

BOOST_AUTO_TEST_SUITE( AiCallbackActionRunner )

BOOST_AUTO_TEST_CASE( EmptyCallbackFailsCleanly )
{
    AI_CALLBACK_ACTION_RUNNER runner{ AI_CALLBACK_ACTION_RUNNER::ACTION_CALLBACK() };
    wxString                  error;

    BOOST_CHECK( !runner.RunActionByName( wxS( "common.Control.showAgentPanel" ), error ) );
    BOOST_CHECK( error.Contains( wxS( "callback" ) ) );
}

BOOST_AUTO_TEST_CASE( CallbackReceivesActionNameAndCanSucceed )
{
    wxString receivedAction;
    AI_CALLBACK_ACTION_RUNNER runner(
            [&]( const wxString& aActionName, wxString& )
            {
                receivedAction = aActionName;
                return true;
            } );
    wxString error;

    BOOST_CHECK( runner.RunActionByName( wxS( "common.Control.showAgentPanel" ), error ) );
    BOOST_CHECK_EQUAL( receivedAction, wxString( wxS( "common.Control.showAgentPanel" ) ) );
}

BOOST_AUTO_TEST_SUITE_END()
```

Register the test file in `QA_COMMON_SRCS`:

```cmake
    test_ai_callback_action_runner.cpp
```

- [ ] **Step 2: Run tests to verify RED**

Run the common targeted verification command with:

```bat
--run_test=AiCallbackActionRunner
```

Expected: compile failure because `kisurf/ai/ai_callback_action_runner.h`
does not exist.

## Task 2: Callback Runner

**Files:**
- Create: `include/kisurf/ai/ai_callback_action_runner.h`
- Create: `common/kisurf/ai/ai_callback_action_runner.cpp`
- Modify: `common/CMakeLists.txt`

- [ ] **Step 1: Add the runner header**

```cpp
#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_tool_execution.h>

class KICOMMON_API AI_CALLBACK_ACTION_RUNNER : public AI_ACTION_RUNNER
{
public:
    using ACTION_CALLBACK = std::function<bool( const wxString& aActionName,
                                                wxString& aError )>;

    explicit AI_CALLBACK_ACTION_RUNNER( ACTION_CALLBACK aCallback );

    bool RunActionByName( const wxString& aActionName, wxString& aError ) override;

private:
    ACTION_CALLBACK m_Callback;
};
```

- [ ] **Step 2: Add the runner implementation**

```cpp
#include <kisurf/ai/ai_callback_action_runner.h>

#include <utility>

AI_CALLBACK_ACTION_RUNNER::AI_CALLBACK_ACTION_RUNNER( ACTION_CALLBACK aCallback ) :
        m_Callback( std::move( aCallback ) )
{
}

bool AI_CALLBACK_ACTION_RUNNER::RunActionByName( const wxString& aActionName,
                                                 wxString& aError )
{
    if( !m_Callback )
    {
        aError = wxS( "Action callback is not available." );
        return false;
    }

    return m_Callback( aActionName, aError );
}
```

Register the implementation in `KICOMMON_SRCS`:

```cmake
    kisurf/ai/ai_callback_action_runner.cpp
```

- [ ] **Step 3: Run runner tests to verify GREEN**

Run:

```bat
--run_test=AiCallbackActionRunner
```

Expected: both runner tests pass.

## Task 3: Agent Panel Configuration Surface

**Files:**
- Modify: `include/kisurf/ai/ai_agent_panel.h`
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`
- Modify: `qa/tests/common/test_ai_agent_panel.cpp`

- [ ] **Step 1: Add a failing panel API test**

Add to `qa/tests/common/test_ai_agent_panel.cpp`:

```cpp
BOOST_AUTO_TEST_CASE( AgentPanelExposesActionToolCallConfigurationSurface )
{
    BOOST_CHECK( ( std::is_member_function_pointer_v<
            decltype( &AI_AGENT_PANEL::ConfigureActionToolCalls )> ) );
}
```

Run:

```bat
--run_test=AiAgentPanel
```

Expected: compile failure because `ConfigureActionToolCalls` is not defined.

- [ ] **Step 2: Add panel includes, method, and owned members**

Update `include/kisurf/ai/ai_agent_panel.h` with:

```cpp
#include <kisurf/ai/ai_action_tool_call_handler.h>
#include <kisurf/ai/ai_activity_log.h>
#include <kisurf/ai/ai_tool_execution.h>
```

Add the public method:

```cpp
void ConfigureActionToolCalls(
        std::unique_ptr<AI_ACTION_RUNNER> aRunner,
        const std::vector<wxString>& aAllowlistedActions,
        std::vector<AI_ACTION_DESCRIPTOR> aFallbackActions = {} );
```

Add private members after `m_Model`:

```cpp
AI_TOOL_EXECUTION_POLICY                 m_ToolExecutionPolicy;
AI_ACTIVITY_LOG                          m_ToolActivityLog;
std::unique_ptr<AI_ACTION_RUNNER>        m_ActionRunner;
std::unique_ptr<AI_ACTION_TOOL_CALL_HANDLER> m_ToolCallHandler;
```

- [ ] **Step 3: Implement panel configuration**

Add to `common/kisurf/ai/ai_agent_panel.cpp`:

```cpp
void AI_AGENT_PANEL::ConfigureActionToolCalls(
        std::unique_ptr<AI_ACTION_RUNNER> aRunner,
        const std::vector<wxString>& aAllowlistedActions,
        std::vector<AI_ACTION_DESCRIPTOR> aFallbackActions )
{
    m_Model->SetToolCallHandler( nullptr );
    m_ToolCallHandler.reset();
    m_ActionRunner = std::move( aRunner );
    m_ToolExecutionPolicy = AI_TOOL_EXECUTION_POLICY();

    if( !m_ActionRunner )
        return;

    for( const wxString& actionName : aAllowlistedActions )
        m_ToolExecutionPolicy.AllowAction( actionName );

    m_ToolCallHandler = std::make_unique<AI_ACTION_TOOL_CALL_HANDLER>(
            m_ToolExecutionPolicy, *m_ActionRunner, m_ToolActivityLog );
    m_ToolCallHandler->SetFallbackActions( std::move( aFallbackActions ) );
    m_Model->SetToolCallHandler( m_ToolCallHandler.get() );
}
```

- [ ] **Step 4: Run panel tests**

Run:

```bat
--run_test=AiAgentPanel,AiAgentPanelModel,AiActionToolCallHandler
```

Expected: panel and model tests pass.

## Task 4: Editor Installation

**Files:**
- Modify: `pcbnew/pcb_edit_frame.cpp`
- Modify: `eeschema/sch_edit_frame.cpp`

- [ ] **Step 1: Include the callback runner and action-manager preflight types**

Add:

```cpp
#include <kisurf/ai/ai_callback_action_runner.h>
#include <tool/action_manager.h>

#include <string>
```

- [ ] **Step 2: Add a local action callback helper to both editor files**

Add an anonymous-namespace helper before the event table in each file:

```cpp
std::string toAiUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}

bool runAiToolManagerAction( TOOL_MANAGER* aToolManager, const wxString& aActionName,
                             wxString& aError )
{
    if( !aToolManager )
    {
        aError = wxS( "Tool manager is not available." );
        return false;
    }

    if( aActionName.IsEmpty() )
    {
        aError = wxS( "Action name is empty." );
        return false;
    }

    ACTION_MANAGER* actionManager = aToolManager->GetActionManager();

    if( !actionManager )
    {
        aError = wxS( "Action manager is not available." );
        return false;
    }

    const std::string actionName = toAiUtf8String( aActionName );

    if( actionName.empty() )
    {
        aError = wxS( "Action name cannot be encoded as UTF-8." );
        return false;
    }

    if( !actionManager->FindAction( actionName ) )
    {
        aError = wxString::Format( wxS( "Action '%s' is not registered." ), aActionName );
        return false;
    }

    if( !aToolManager->RunAction( actionName ) )
    {
        aError = wxString::Format( wxS( "Action '%s' was not handled." ), aActionName );
        return false;
    }

    return true;
}
```

- [ ] **Step 3: Configure PCB Agent panel**

After `m_agentPanel = new AI_AGENT_PANEL(...)`, add:

```cpp
    if( GetToolManager() )
    {
        TOOL_MANAGER* toolManager = GetToolManager();

        m_agentPanel->ConfigureActionToolCalls(
                std::make_unique<AI_CALLBACK_ACTION_RUNNER>(
                        [toolManager]( const wxString& aActionName, wxString& aError )
                        {
                            return runAiToolManagerAction( toolManager, aActionName, aError );
                        } ),
                { wxS( "common.Control.showAgentPanel" ) },
                AI_ACTION_CATALOG::Build( toolManager->GetActionManager(),
                                          AI_EDITOR_KIND::Pcb, 128 ) );
    }
```

- [ ] **Step 4: Configure schematic Agent panel**

After `m_agentPanel = new AI_AGENT_PANEL(...)`, add:

```cpp
    if( GetToolManager() )
    {
        TOOL_MANAGER* toolManager = GetToolManager();

        m_agentPanel->ConfigureActionToolCalls(
                std::make_unique<AI_CALLBACK_ACTION_RUNNER>(
                        [toolManager]( const wxString& aActionName, wxString& aError )
                        {
                            return runAiToolManagerAction( toolManager, aActionName, aError );
                        } ),
                { wxS( "common.Control.showAgentPanel" ) },
                AI_ACTION_CATALOG::Build( toolManager->GetActionManager(),
                                          AI_EDITOR_KIND::Schematic, 128 ) );
    }
```

- [ ] **Step 5: Build editor test targets**

Run the PCB and schematic targeted verification commands.

Expected: both targets build and their AI suites pass.

## Task 5: Full Verification And Commit

**Files:**
- All files above.

- [ ] **Step 1: Run common targeted verification**

Run the common targeted verification command.

Expected: exit code `0`.

- [ ] **Step 2: Run diff checks**

```powershell
git diff --check
git status --short
git diff --stat
```

Expected: no whitespace errors and only files from this plan changed.

- [ ] **Step 3: Commit**

```powershell
git add docs\superpowers\specs\2026-06-16-ai-editor-action-runner-integration-design.md docs\superpowers\specs\2026-06-16-kisurf-ai-native-spec-index.md docs\superpowers\plans\2026-06-16-ai-editor-action-runner-integration-implementation.md include\kisurf\ai\ai_callback_action_runner.h common\kisurf\ai\ai_callback_action_runner.cpp common\CMakeLists.txt qa\tests\common\test_ai_callback_action_runner.cpp qa\tests\common\CMakeLists.txt include\kisurf\ai\ai_agent_panel.h common\kisurf\ai\ai_agent_panel.cpp qa\tests\common\test_ai_agent_panel.cpp pcbnew\pcb_edit_frame.cpp eeschema\sch_edit_frame.cpp
git commit -m "feat: wire ai action runner into editors"
```

## Plan Self-Review

- Spec coverage: tasks cover the common runner, panel-owned handler lifetime,
  editor installation, conservative allowlist, fallback action catalog, and
  verification in common/PCB/schematic targets.
- Open-marker scan: no task depends on an undefined function, missing file, or
  unbounded instruction.
- Type consistency: method and class names match the spec:
  `AI_CALLBACK_ACTION_RUNNER` and
  `AI_AGENT_PANEL::ConfigureActionToolCalls(...)`.
