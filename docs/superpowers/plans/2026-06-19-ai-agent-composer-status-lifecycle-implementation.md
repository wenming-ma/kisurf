# AI Agent Composer Status Lifecycle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the wxFormBuilder composer footer show and expose a derived Agent status lifecycle.

**Architecture:** A pure status formatter derives text from a small view struct, while the panel only gathers current model/UI state and assigns the wx label. Model accessors expose the latest request id and cancellation state from the runtime trace, and semantic UI consumes the same text.

**Tech Stack:** C++17, wxWidgets, Boost.Test, KiSurf/KiCad common library, existing `qa_common` target.

---

## File Structure

- `include/kisurf/ai/ai_agent_panel.h`: add `AI_AGENT_COMPOSER_STATUS_VIEW`, `AiAgentComposerStatusText()`, and `updateComposerStatus()`.
- `common/kisurf/ai/ai_agent_panel.cpp`: implement the formatter and call status refresh from lifecycle handlers.
- `include/kisurf/ai/ai_agent_panel_model.h`: add `LastRequestId()` and `LastRequestCancelled()`.
- `common/kisurf/ai/ai_agent_panel_model.cpp`: inspect trace records to determine cancellation.
- `include/kisurf/ai/ai_agent_panel_semantic.h`: add `m_ComposerStatusText`.
- `common/kisurf/ai/ai_agent_panel_semantic.cpp`: emit `agent.composer.status` and include status in summary.
- `qa/tests/common/test_ai_agent_panel.cpp`: add pure status precedence tests.
- `qa/tests/common/test_ai_agent_panel_model.cpp`: extend latest request cancellation coverage.
- `qa/tests/common/test_ai_agent_panel_semantic.cpp`: add semantic projection coverage.

### Task 1: RED Tests

**Files:**
- Modify: `qa/tests/common/test_ai_agent_panel.cpp`
- Modify: `qa/tests/common/test_ai_agent_panel_model.cpp`
- Modify: `qa/tests/common/test_ai_agent_panel_semantic.cpp`

- [ ] **Step 1: Add pure composer status tests**

Add a test named `AgentPanelFormatsComposerStatusLifecycle` that constructs `AI_AGENT_COMPOSER_STATUS_VIEW` values and checks each precedence case:

```cpp
BOOST_AUTO_TEST_CASE( AgentPanelFormatsComposerStatusLifecycle )
{
    AI_AGENT_COMPOSER_STATUS_VIEW view;
    BOOST_CHECK_EQUAL( AiAgentComposerStatusText( view ), "Ask about the current board" );

    view.m_InputHasText = true;
    BOOST_CHECK_EQUAL( AiAgentComposerStatusText( view ), "Ready to send" );

    view.m_InputHasText = false;
    view.m_BackgroundAgentEnabled = true;
    BOOST_CHECK_EQUAL( AiAgentComposerStatusText( view ), "Background Agent on" );

    view.m_HasActiveSuggestion = true;
    BOOST_CHECK_EQUAL( AiAgentComposerStatusText( view ), "Preview ready" );

    view.m_LatestRequestId = 7;
    BOOST_CHECK_EQUAL( AiAgentComposerStatusText( view ), "Last response #7" );

    view.m_LastRequestCancelled = true;
    BOOST_CHECK_EQUAL( AiAgentComposerStatusText( view ), "Stopped request #7" );
}
```

- [ ] **Step 2: Add latest request state expectations**

Extend `StopMarksLastRequestCancelled`:

```cpp
BOOST_CHECK_EQUAL( model.LastRequestId(), 1 );
BOOST_CHECK( !model.LastRequestCancelled() );
BOOST_CHECK( model.CancelLastRequest() );
BOOST_CHECK( model.LastRequestCancelled() );
BOOST_CHECK( !model.CancelRequest( 999 ) );
```

- [ ] **Step 3: Add semantic status projection tests**

Update the stable node-id list to include `agent.composer.status`, and add:

```cpp
BOOST_AUTO_TEST_CASE( ComposerStatusIsProjectedIntoSemanticTreeAndPanelState )
{
    AI_AGENT_PANEL_SEMANTIC_VIEW view;
    view.m_ComposerStatusText = "Background Agent on";

    AI_SEMANTIC_UI_TREE tree = AiAgentPanelSemanticTree( view );
    const AI_SEMANTIC_UI_NODE* statusNode = findNode( tree, "agent.composer.status" );

    BOOST_REQUIRE( statusNode );
    BOOST_CHECK_EQUAL( statusNode->m_Role, "text" );
    BOOST_CHECK_EQUAL( statusNode->m_Text, "Background Agent on" );

    AI_PANEL_STATE_RECORD record = AiAgentPanelSemanticStateRecord( tree );
    BOOST_CHECK( record.m_Summary.Contains( "composer_status=Background Agent on" ) );
}
```

- [ ] **Step 4: Verify RED**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
```

Expected: build fails because `AI_AGENT_COMPOSER_STATUS_VIEW`, `AiAgentComposerStatusText`, `LastRequestId`, `LastRequestCancelled`, or `m_ComposerStatusText` do not exist yet.

### Task 2: GREEN Implementation

**Files:**
- Modify: `include/kisurf/ai/ai_agent_panel.h`
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`
- Modify: `include/kisurf/ai/ai_agent_panel_model.h`
- Modify: `common/kisurf/ai/ai_agent_panel_model.cpp`
- Modify: `include/kisurf/ai/ai_agent_panel_semantic.h`
- Modify: `common/kisurf/ai/ai_agent_panel_semantic.cpp`

- [ ] **Step 1: Add public model accessors**

Declare:

```cpp
uint64_t LastRequestId() const { return m_LastRequestId; }
bool LastRequestCancelled() const;
```

Implement `LastRequestCancelled()` by scanning `m_Runtime.TraceRecords()` for `m_LastRequestId` and returning that record's `m_Cancelled` value.

- [ ] **Step 2: Add pure status formatter**

Declare `AI_AGENT_COMPOSER_STATUS_VIEW` with fields for active mode, background enabled, input text, active suggestion, latest request id, and cancelled state. Implement `AiAgentComposerStatusText()` using the precedence in the spec.

- [ ] **Step 3: Synchronize the wx label**

Add `AI_AGENT_PANEL::updateComposerStatus()` and call it after construction, sending, stopping, background toggle, suggestion refresh/preview/accept/reject, model settings changes, mode changes, and activity recording.

- [ ] **Step 4: Expose semantic status**

Add `m_ComposerStatusText` to `AI_AGENT_PANEL_SEMANTIC_VIEW`, emit `agent.composer.status`, and include `composer_status=<text>` in the panel-state summary.

- [ ] **Step 5: Verify GREEN**

Run the targeted common tests:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiAgentPanel,AiAgentPanelModel,AiAgentPanelSemantic --report_level=short"
```

Expected: all selected test cases pass.

### Task 3: Verification and Commit

**Files:**
- Verify all files touched in Tasks 1 and 2.

- [ ] **Step 1: Run full AI common suite**

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\common\qa_common.exe --run_test=Ai* --report_level=short"
```

Expected: all `Ai*` test cases pass. The known schema warning can appear on stderr if it is unrelated to these assertions.

- [ ] **Step 2: Build PCB editor**

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target pcbnew -- -j 2"
```

Expected: build exits 0. Existing C5266 warnings are acceptable if unchanged.

- [ ] **Step 3: Launch and inspect with Computer Use**

Launch the build-tree PCB editor:

```powershell
.\tools\run_from_build.ps1 -BuildDir .\out\build\x64-release -App pcbnew
```

Use Computer Use to confirm the window opens and whether any blocking modal/system-error popup appears.

- [ ] **Step 4: Run hygiene checks**

Run `git diff --check` on touched files and scan changed/staged content for embedded API keys with the established `sk-` pattern.

- [ ] **Step 5: Stage and commit**

Stage only the spec, plan, AI panel/model/semantic files, and common tests. Do not stage `qa/tests/pcbnew/test_module.cpp`.

Commit:

```powershell
git commit -m "ui: sync agent composer status"
```

## Self-Review

- Spec coverage: each design goal maps to a test and implementation step.
- Placeholder scan: no marker text, deferred implementation, or vague test step remains.
- Type consistency: `AI_AGENT_COMPOSER_STATUS_VIEW`, `AiAgentComposerStatusText`, `LastRequestId`, `LastRequestCancelled`, and `m_ComposerStatusText` are named consistently across tasks.
