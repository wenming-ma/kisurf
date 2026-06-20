# AI Agent Panel State Projection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Project the Agent pane semantic UI tree into `AI_PANEL_STATE_RECORD`
so the model can read Agent-pane state through the existing
`AI_CONTEXT_SNAPSHOT` and `kisurf_get_workspace_view` interfaces.

**Architecture:** Keep the projection pure in common code. Reuse
`AiAgentPanelSemanticTree()` for stable nodes, serialize the semantic tree into
bounded state JSON, then expose a live `AI_AGENT_PANEL` convenience method that
returns the same record from the current panel state.

**Tech Stack:** KiSurf common C++20, nlohmann JSON, wxString, Boost.Test
`qa_common`, existing `AI_AGENT_PANEL` and semantic tool call handler tests.

---

## File Structure

- Modify: `include/kisurf/ai/ai_agent_panel_semantic.h`
  - Declare pure panel-state projection helpers.
- Modify: `common/kisurf/ai/ai_agent_panel_semantic.cpp`
  - Serialize semantic nodes into `AI_PANEL_STATE_RECORD::m_StateJson`.
- Modify: `include/kisurf/ai/ai_agent_panel.h`
  - Add `SemanticPanelStateRecord() const`.
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`
  - Implement the live convenience method.
- Modify: `qa/tests/common/test_ai_agent_panel_semantic.cpp`
  - Add pure projection tests.
- Modify: `qa/tests/common/test_ai_agent_panel.cpp`
  - Add API surface test.
- Modify: `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`
  - Add workspace-view flow test using the Agent panel record.
- Update this plan with verification evidence.

## Task 1: Red Tests For Pure Projection

**Files:**
- Modify: `qa/tests/common/test_ai_agent_panel_semantic.cpp`
- Modify: `qa/tests/common/test_ai_agent_panel.cpp`
- Modify: `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`

- [x] **Step 1: Add pure Agent panel-state tests**

Add tests that create `AI_AGENT_PANEL_SEMANTIC_VIEW`, call
`AiAgentPanelSemanticStateRecord(view)`, parse `m_StateJson`, and verify:

- `record.m_Id == "agent.panel"`
- `record.m_Title == "Agent"`
- `record.m_FocusedControlId == "agent.input"`
- `record.m_Summary` contains mode, background state, message count,
  suggestion count, log count, send state, and active-suggestion state.
- JSON contains `frame_id`, `title`, `screenshot_available`, and `nodes`.
- `agent.send` has `action == "invoke"` and enabled state from input.
- `agent.input` has `text_policy == "redacted"` and no `text_value`.
- `agent.chat.transcript` has `text_policy == "plain"` and safe count text.

- [x] **Step 2: Add panel API surface test**

Add a compile-time test for `AI_AGENT_PANEL::SemanticPanelStateRecord`.

- [x] **Step 3: Add workspace-view flow test**

Create a request with:

```cpp
AI_AGENT_PANEL_SEMANTIC_VIEW panelView;
panelView.m_InputHasText = true;
panelView.m_MessageCount = 2;
request.m_ContextSnapshot.m_PanelStates.push_back(
        AiAgentPanelSemanticStateRecord( panelView ) );
```

Call:

```cpp
kisurf_get_workspace_view
```

with:

```json
{
  "views": ["context"],
  "context": { "include_panels": true }
}
```

Verify `workspace_view.context.panel_states[0].id == "agent.panel"` and that
the nested state contains `agent.send`.

- [x] **Step 4: Run tests to verify RED**

Build `qa_common`.

Expected: build fails because `AiAgentPanelSemanticStateRecord` and
`AI_AGENT_PANEL::SemanticPanelStateRecord` do not exist.

Actual: build failed as expected:

- `test_ai_agent_panel_semantic.cpp`: `AiAgentPanelSemanticStateRecord`
  identifier not found.
- `test_ai_agent_panel.cpp`: `SemanticPanelStateRecord` is not a member of
  `AI_AGENT_PANEL`.
- `test_ai_semantic_tool_call_handler.cpp`:
  `AiAgentPanelSemanticStateRecord` identifier not found.

## Task 2: Implement Pure Projection

**Files:**
- Modify: `include/kisurf/ai/ai_agent_panel_semantic.h`
- Modify: `common/kisurf/ai/ai_agent_panel_semantic.cpp`

- [x] **Step 1: Add declarations**

Declare:

```cpp
KICOMMON_API AI_PANEL_STATE_RECORD AiAgentPanelSemanticStateRecord(
        const AI_AGENT_PANEL_SEMANTIC_VIEW& aView );

KICOMMON_API AI_PANEL_STATE_RECORD AiAgentPanelSemanticStateRecord(
        const AI_SEMANTIC_UI_TREE& aTree );
```

- [x] **Step 2: Add JSON serialization helpers**

In the `.cpp`, add local helpers for:

- converting `AI_SEMANTIC_UI_TEXT_POLICY` to `none`, `plain`, or `redacted`
- converting `wxString` to UTF-8 strings
- serializing bounds only when available
- serializing `text_value` only for `Plain` nodes

- [x] **Step 3: Add record projection**

`AiAgentPanelSemanticStateRecord(const AI_SEMANTIC_UI_TREE&)` should:

- set `m_Id`, `m_Title`, fallback focused control fields, and empty selected
  text
- find the first focused node when present
- count/send/suggestion state from known nodes for the summary
- dump the semantic tree JSON into `m_StateJson`

`AiAgentPanelSemanticStateRecord(const AI_AGENT_PANEL_SEMANTIC_VIEW&)` should
call `AiAgentPanelSemanticTree(aView)` and delegate to the tree overload.

Actual: Added both projection helpers. The tree overload serializes stable
semantic nodes, omits `text_value` for redacted nodes, preserves safe plain
count text, and produces an Agent summary from known semantic nodes.

## Task 3: Wire Live Agent Panel Convenience API

**Files:**
- Modify: `include/kisurf/ai/ai_agent_panel.h`
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`

- [x] **Step 1: Add declaration**

Add:

```cpp
AI_PANEL_STATE_RECORD SemanticPanelStateRecord() const;
```

after `SemanticUiTree() const`.

- [x] **Step 2: Add implementation**

Implement:

```cpp
AI_PANEL_STATE_RECORD AI_AGENT_PANEL::SemanticPanelStateRecord() const
{
    return AiAgentPanelSemanticStateRecord( SemanticUiTree() );
}
```

Actual: `AI_AGENT_PANEL::SemanticPanelStateRecord()` delegates to
`AiAgentPanelSemanticStateRecord( SemanticUiTree() )`.

## Task 4: Verify And Commit

**Files:**
- Modify this plan with actual status and verification.

- [x] **Step 1: Build and targeted tests**

Run:

```powershell
cmd.exe /S /C '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build out\build\x64-release --target qa_common --config Release'
```

Then run:

```powershell
.\qa_common.exe --run_test=AiAgentPanelSemantic,AiAgentPanel,AiSemanticToolCallHandler
```

Expected: targeted tests exit 0. The known schema-file warning may still
appear.

Actual:

- `cmake --build out\build\x64-release --target qa_common --config Release`
  exited 0.
- `qa_common.exe --run_test=AiAgentPanelSemantic,AiAgentPanel,AiSemanticToolCallHandler`
  exited 0 with `No errors detected`.
- The known schema-file warning for
  `qa\tests\schemas\api.v1.schema.json` appeared during the test run.

Implementation note: the first GREEN build attempt linked with duplicate
nlohmann JSON symbols because the new common test included
`<nlohmann/json.hpp>` directly. Changing the test to include the project
wrapper `json_common.h` fixed the MSVC template instantiation issue.

- [x] **Step 2: Build editor targets**

Run:

```powershell
cmd.exe /S /C '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build out\build\x64-release --target pcbnew --config Release && cmake --build out\build\x64-release --target eeschema --config Release'
```

Expected: both editor targets build. Existing warnings unrelated to this slice
may still appear.

Actual: `pcbnew` and `eeschema` both built successfully. `pcbnew` emitted the
pre-existing `length_delay_calculation_item.h` C5266 warning.

- [x] **Step 3: Static and secret checks**

Run:

```powershell
git diff --check
rg -n "sk-[0-9A-Za-z_-]{20,}|OPENAI_API_KEY\s*=|KISURF_AI_API_KEY\s*=" include\kisurf\ai common\kisurf\ai qa\tests\common docs\superpowers\specs\2026-06-19-ai-agent-panel-state-projection-design.md docs\superpowers\plans\2026-06-19-ai-agent-panel-state-projection-implementation.md
```

Expected: whitespace check exits 0; secret scan has no matches.

Actual:

- `git diff --check` exited 0.
- Secret scan exited with no matches.

- [x] **Step 4: Commit**

Stage only files touched by this plan. Do not stage unrelated
`qa/tests/pcbnew/test_module.cpp`.

```powershell
git add include/kisurf/ai/ai_agent_panel_semantic.h common/kisurf/ai/ai_agent_panel_semantic.cpp include/kisurf/ai/ai_agent_panel.h common/kisurf/ai/ai_agent_panel.cpp qa/tests/common/test_ai_agent_panel_semantic.cpp qa/tests/common/test_ai_agent_panel.cpp qa/tests/common/test_ai_semantic_tool_call_handler.cpp docs/superpowers/specs/2026-06-19-ai-agent-panel-state-projection-design.md docs/superpowers/plans/2026-06-19-ai-agent-panel-state-projection-implementation.md
git commit -m "feat: project agent panel semantic state"
```

Observed: committed as `34819a3f feat: project agent panel semantic state`.

## Self-Review

- Spec coverage: Tasks map directly to the projection spec goals and
  acceptance criteria.
- Open-item scan: No deferred implementation markers remain.
- Scope check: The plan does not alter visual capture, IPC, or editor event
  recording.
- Safety check: Redacted nodes omit `text_value` and the static secret scan is
  required.
