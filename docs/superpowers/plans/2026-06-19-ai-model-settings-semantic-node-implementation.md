# AI Model Settings Semantic Node Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose the Agent panel's model-settings button through semantic UI state and the semantic UI action bridge.

**Architecture:** Extend `AiAgentPanelSemanticTree()` with a stable `agent.model.settings` button node. Extend `AI_AGENT_PANEL::InvokeSemanticUiAction()` so the same node can invoke the existing `ShowModelSettingsDialog()` path.

**Tech Stack:** C++20, wxWidgets, Boost unit tests, existing `qa_common` target, Computer Use GUI smoke.

---

## File Structure

- Modify `qa/tests/common/test_ai_agent_panel_semantic.cpp`
  - Adds red semantic-tree and serialized panel-state assertions.
- Modify `common/kisurf/ai/ai_agent_panel_semantic.cpp`
  - Adds the `agent.model.settings` semantic node.
- Modify `common/kisurf/ai/ai_agent_panel.cpp`
  - Handles semantic invocation for the model-settings node.
- Create `docs/superpowers/specs/2026-06-19-ai-model-settings-semantic-node-design.md`
- Create `docs/superpowers/plans/2026-06-19-ai-model-settings-semantic-node-implementation.md`

## Task 1: Red Tests

**Files:**
- Modify: `qa/tests/common/test_ai_agent_panel_semantic.cpp`

- [x] **Step 1: Add `agent.model.settings` to the stable id list**

In `EmitsStableAgentPaneNodeIds`, add:

```cpp
wxS( "agent.model.settings" ),
```

immediately after `agent.background.toggle`.

- [x] **Step 2: Add a semantic-node contract test**

Add:

```cpp
BOOST_AUTO_TEST_CASE( ModelSettingsNodeIsInvokableButton )
{
    AI_AGENT_PANEL_SEMANTIC_VIEW view;

    AI_SEMANTIC_UI_TREE tree = AiAgentPanelSemanticTree( view );
    const AI_SEMANTIC_UI_NODE* modelSettings =
            tree.FindNode( wxS( "agent.model.settings" ) );

    BOOST_REQUIRE( modelSettings );
    BOOST_CHECK_EQUAL( modelSettings->m_ParentNodeId,
                       wxString( wxS( "agent.root" ) ) );
    BOOST_CHECK_EQUAL( modelSettings->m_Role, wxString( wxS( "button" ) ) );
    BOOST_CHECK_EQUAL( modelSettings->m_Label,
                       wxString( wxS( "Model Settings" ) ) );
    BOOST_CHECK( modelSettings->m_Enabled );
    BOOST_CHECK_EQUAL( modelSettings->m_ActionName,
                       wxString( wxS( "invoke" ) ) );
    BOOST_CHECK( !modelSettings->m_RequiresUserConfirmation );
    BOOST_CHECK( modelSettings->m_TextPolicy == AI_SEMANTIC_UI_TEXT_POLICY::None );
    BOOST_CHECK( modelSettings->m_TextValue.IsEmpty() );
}
```

- [x] **Step 3: Extend serialized state assertions**

In `PanelStateRecordProjectsSemanticTree`, add:

```cpp
const nlohmann::json* modelSettings =
        findNodeJson( nodes, "agent.model.settings" );
BOOST_REQUIRE( modelSettings );
BOOST_CHECK_EQUAL( ( *modelSettings )["role"].get<std::string>(), "button" );
BOOST_CHECK_EQUAL( ( *modelSettings )["action"].get<std::string>(), "invoke" );
BOOST_CHECK( !( *modelSettings )["requires_user_confirmation"].get<bool>() );
BOOST_CHECK( !( *modelSettings ).contains( "text_value" ) );
```

- [x] **Step 4: Verify RED**

Run:

```powershell
$dllDirs = Get-ChildItem .\out\build\x64-release -Recurse -Filter *.dll | Select-Object -ExpandProperty DirectoryName -Unique
$env:PATH = ($dllDirs -join ';') + ';' + $env:PATH
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiAgentPanelSemantic --report_level=short
```

Expected before implementation: fails because `agent.model.settings` is absent.

Actual: `AiAgentPanelSemantic` failed because `agent.model.settings` was absent
from the stable id list, the invokable-button contract test, and serialized
panel state.

## Task 2: Implement Semantic Node

**Files:**
- Modify: `common/kisurf/ai/ai_agent_panel_semantic.cpp`

- [x] **Step 1: Add the semantic node**

After `agent.background.toggle`, add:

```cpp
addNode( tree, wxS( "agent.model.settings" ), wxS( "agent.root" ),
         wxS( "button" ), wxS( "Model Settings" ), true, wxS( "invoke" ) );
```

- [x] **Step 2: Verify targeted tests pass**

Run the `AiAgentPanelSemantic` command from Task 1 again.

Expected: exits 0.

Actual: `AiAgentPanelSemantic` passed with 13 test cases and 91 assertions.

## Task 3: Implement Semantic Invocation

**Files:**
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`

- [x] **Step 1: Handle `agent.model.settings`**

In `AI_AGENT_PANEL::InvokeSemanticUiAction()`, after the `agent.stop` branch,
add:

```cpp
if( aRequest.m_NodeId == wxS( "agent.model.settings" ) )
{
    ShowModelSettingsDialog();
    return semanticActionOk( wxS( "agent.model.settings" ) );
}
```

- [x] **Step 2: Run common AI tests**

Run:

```powershell
$dllDirs = Get-ChildItem .\out\build\x64-release -Recurse -Filter *.dll | Select-Object -ExpandProperty DirectoryName -Unique
$env:PATH = ($dllDirs -join ';') + ';' + $env:PATH
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=Ai* --report_level=short
```

Expected: exits 0.

Actual: `Ai*` passed with 267 test cases and 1784 assertions.

## Task 4: Build, GUI Smoke, And Commit

**Files:**
- Modified source and test files above.
- Spec and plan docs.

- [x] **Step 1: Build PCB Editor**

Run:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build .\out\build\x64-release --target pcbnew --config Release"
```

Expected: exits 0.

Actual: `pcbnew` built successfully.

- [x] **Step 2: Launch and inspect with Computer Use**

Run:

```powershell
.\tools\run_from_build.ps1 -BuildDir .\out\build\x64-release -App pcbnew
```

Then use Computer Use to verify:

- no missing-DLL/system-error modal appears;
- `AI -> Agent` opens;
- Agent panel shows `Model...`, Chat/Preview/Log, Background Agent, Send, and Stop.

Actual: Computer Use verified the launched build-tree PCB Editor had no
missing-DLL/system-error modal, `AI -> Agent` opened, and the Agent panel showed
`Model...`, Chat/Preview/Log, Background Agent, Send, and Stop.

- [x] **Step 3: Static checks**

Run:

```powershell
git diff --check -- common/kisurf/ai/ai_agent_panel_semantic.cpp common/kisurf/ai/ai_agent_panel.cpp qa/tests/common/test_ai_agent_panel_semantic.cpp docs/superpowers/specs/2026-06-19-ai-model-settings-semantic-node-design.md docs/superpowers/plans/2026-06-19-ai-model-settings-semantic-node-implementation.md
$prefix = 'sk' + '-'
$pattern = $prefix + '[A-Za-z0-9_-]{20,}|OPENAI_API_KEY=.*' + $prefix
rg -n $pattern common/kisurf/ai/ai_agent_panel_semantic.cpp common/kisurf/ai/ai_agent_panel.cpp qa/tests/common/test_ai_agent_panel_semantic.cpp docs/superpowers/specs/2026-06-19-ai-model-settings-semantic-node-design.md docs/superpowers/plans/2026-06-19-ai-model-settings-semantic-node-implementation.md
```

Expected: no whitespace errors; secret scan has no output.

Actual: `git diff --check` exited 0 with only CRLF normalization warnings, and
the secret scan had no matches.

- [ ] **Step 4: Commit**

Run:

```powershell
git add common/kisurf/ai/ai_agent_panel_semantic.cpp common/kisurf/ai/ai_agent_panel.cpp qa/tests/common/test_ai_agent_panel_semantic.cpp docs/superpowers/specs/2026-06-19-ai-model-settings-semantic-node-design.md docs/superpowers/plans/2026-06-19-ai-model-settings-semantic-node-implementation.md
git commit -m "feat: expose model settings in agent semantic ui"
```

Do not stage unrelated `qa/tests/pcbnew/test_module.cpp`.

## Self-Review

- Spec coverage: semantic visibility, JSON serialization, invocation, tests, GUI smoke, and secret safety all map to tasks.
- Placeholder scan: no TBD/TODO/implement-later placeholders remain.
- Type consistency: node id is consistently `agent.model.settings`; label is consistently `Model Settings`.
