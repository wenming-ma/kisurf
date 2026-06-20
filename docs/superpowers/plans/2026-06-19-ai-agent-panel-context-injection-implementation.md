# AI Agent Panel Context Injection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ensure live Agent panel requests automatically include the current
Agent pane semantic panel state in `AI_CONTEXT_SNAPSHOT::m_PanelStates`.

**Architecture:** Add a pure upsert helper for panel state records, test it
without wx construction, then make `AI_AGENT_PANEL` use one private snapshot
preparation method for `SendCurrentText()` and `RecordActivity()`.

**Tech Stack:** KiSurf common C++20, wxString, Boost.Test `qa_common`, existing
Agent panel common code.

---

## File Structure

- Modify: `include/kisurf/ai/ai_agent_panel_semantic.h`
  - Declare `AiUpsertPanelStateRecord()`.
- Modify: `common/kisurf/ai/ai_agent_panel_semantic.cpp`
  - Implement pure upsert by `m_Id`.
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`
  - Add a private snapshot-preparation helper implementation.
  - Use it from `SendCurrentText()` and `RecordActivity()`.
- Modify: `include/kisurf/ai/ai_agent_panel.h`
  - Add the private `contextSnapshotWithPanelState() const` declaration.
- Modify: `qa/tests/common/test_ai_agent_panel_semantic.cpp`
  - Add pure upsert tests.
- Update this plan with verification evidence.

## Task 1: Red Tests For Panel State Upsert

**Files:**
- Modify: `qa/tests/common/test_ai_agent_panel_semantic.cpp`

- [x] **Step 1: Add append test**

Create a snapshot and one valid `AI_PANEL_STATE_RECORD`, call
`AiUpsertPanelStateRecord(snapshot, record)`, and verify:

- `snapshot.m_PanelStates.size() == 1`
- the record ID and summary match

- [x] **Step 2: Add replace-by-ID test**

Create a snapshot with an existing `agent.panel` record, upsert a second
`agent.panel` record, and verify:

- size remains 1
- summary/state fields come from the replacement

- [x] **Step 3: Add empty-record test**

Upsert a default `AI_PANEL_STATE_RECORD` and verify the snapshot remains empty.

- [x] **Step 4: Run build to verify RED**

Build `qa_common`.

Expected: build fails because `AiUpsertPanelStateRecord` is not declared.

Actual: `qa_common` build failed as expected in
`test_ai_agent_panel_semantic.cpp` because `AiUpsertPanelStateRecord` was not
found.

## Task 2: Implement Upsert Helper

**Files:**
- Modify: `include/kisurf/ai/ai_agent_panel_semantic.h`
- Modify: `common/kisurf/ai/ai_agent_panel_semantic.cpp`

- [x] **Step 1: Add declaration**

Declare:

```cpp
KICOMMON_API void AiUpsertPanelStateRecord(
        AI_CONTEXT_SNAPSHOT& aSnapshot,
        AI_PANEL_STATE_RECORD aRecord );
```

- [x] **Step 2: Add implementation**

Implement:

- ignore empty records
- replace first existing record with matching non-empty `m_Id`
- append when no matching record exists

Actual: implemented in `ai_agent_panel_semantic.cpp` with move-based replace
and append behavior.

## Task 3: Wire AI_AGENT_PANEL Snapshot Preparation

**Files:**
- Modify: `include/kisurf/ai/ai_agent_panel.h`
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`

- [x] **Step 1: Add private helper declaration**

Add:

```cpp
AI_CONTEXT_SNAPSHOT contextSnapshotWithPanelState() const;
```

to the private section.

- [x] **Step 2: Implement helper**

The helper should call `m_ContextProvider()` when available, fill unknown
editor kind from `m_EditorKind`, upsert `SemanticPanelStateRecord()`, and
return the result.

Actual: `contextSnapshotWithPanelState()` centralizes provider fetch,
editor-kind fallback, and Agent panel-state upsert.

- [x] **Step 3: Use helper in send/background paths**

Update:

- `SendCurrentText()`
- `RecordActivity()`

to use the helper instead of duplicating provider/editor-kind logic.

Actual: `SendCurrentText()` and `RecordActivity()` now use the enriched
snapshot path.

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

Expected: tests exit 0. The known schema-file warning may still appear.

Actual:

- `cmake --build out\build\x64-release --target qa_common --config Release`
  exited 0.
- `qa_common.exe --run_test=AiAgentPanelSemantic,AiAgentPanel,AiSemanticToolCallHandler`
  ran 45 test cases and exited 0 with `No errors detected`.
- The known missing schema-file warning appeared during test startup.

- [x] **Step 2: Build editor targets**

Run:

```powershell
cmd.exe /S /C '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build out\build\x64-release --target pcbnew --config Release && cmake --build out\build\x64-release --target eeschema --config Release'
```

Expected: both targets build.

Actual: `pcbnew` and `eeschema` both built successfully. `pcbnew` emitted the
pre-existing `length_delay_calculation_item.h` C5266 warning.

- [x] **Step 3: Static and secret checks**

Run:

```powershell
git diff --check
rg -n "sk-[0-9A-Za-z_-]{20,}|OPENAI_API_KEY\s*=|KISURF_AI_API_KEY\s*=" include\kisurf\ai common\kisurf\ai qa\tests\common docs\superpowers\specs\2026-06-19-ai-agent-panel-context-injection-design.md docs\superpowers\plans\2026-06-19-ai-agent-panel-context-injection-implementation.md
```

Expected: whitespace check exits 0; secret scan has no matches.

Actual:

- `git diff --check` exited 0.
- Secret scan exited with no matches.

- [x] **Step 4: Commit**

Stage only files touched by this plan. Do not stage unrelated
`qa/tests/pcbnew/test_module.cpp`.

```powershell
git add include/kisurf/ai/ai_agent_panel_semantic.h common/kisurf/ai/ai_agent_panel_semantic.cpp include/kisurf/ai/ai_agent_panel.h common/kisurf/ai/ai_agent_panel.cpp qa/tests/common/test_ai_agent_panel_semantic.cpp docs/superpowers/specs/2026-06-19-ai-agent-panel-context-injection-design.md docs/superpowers/plans/2026-06-19-ai-agent-panel-context-injection-implementation.md
git commit -m "feat: inject agent panel state into context"
```

Observed: committed as `6b4f9ef3 feat: inject agent panel state into context`.

## Self-Review

- Spec coverage: Tasks map to upsert helper, live panel send path, background
  activity path, and verification.
- Open-item scan: No deferred implementation markers remain.
- Scope check: No visual capture, IPC, provider schema, or event recorder
  changes are included.
