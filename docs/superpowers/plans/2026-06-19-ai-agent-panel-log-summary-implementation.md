# AI Agent Panel Semantic Log Summary Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose a bounded, redacted recent Agent log summary through the Agent
pane semantic tree and panel state projection.

**Architecture:** Add an optional `m_LogSummary` field to the pure semantic
view, use it in the existing `agent.log.entries` text node, and make the live
panel compute a compact recent-entry summary from observability entries.

**Tech Stack:** KiSurf common C++20, wxString, Boost.Test `qa_common`,
existing Agent panel semantic builder and Agent panel observability helpers.

---

## File Structure

- Modify: `include/kisurf/ai/ai_agent_panel_semantic.h`
  - Add `wxString m_LogSummary`.
- Modify: `common/kisurf/ai/ai_agent_panel_semantic.cpp`
  - Use `m_LogSummary` for `agent.log.entries` text when non-empty.
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`
  - Add a local compact observability summary helper.
  - Pass the summary from live panel state into `AI_AGENT_PANEL_SEMANTIC_VIEW`.
- Modify: `qa/tests/common/test_ai_agent_panel_semantic.cpp`
  - Add RED tests for summary, redaction, and fallback count behavior.
- Modify this plan with RED/GREEN/verification evidence.

## Task 1: Red Tests For Pure Semantic Summary

**Files:**
- Modify: `qa/tests/common/test_ai_agent_panel_semantic.cpp`

- [x] **Step 1: Add summary text test**

Create a view with:

```cpp
view.m_LogEntryCount = 3;
view.m_LogSummary = wxS( "#1 Input: Model input\n#2 Tool: kisurf_get_workspace_view" );
```

Build the tree and verify `agent.log.entries` has text policy `Plain` and
text value equal to the supplied summary.

- [x] **Step 2: Add redaction test**

Set:

```cpp
view.m_LogSummary = wxString( wxS( "token: " ) ) + wxS( "secret-value " )
                    + wxS( "sk-" ) + wxS( "12345678901234567890" );
```

Build the tree and verify the log node text does not contain
`secret-value` or the raw key suffix and does contain `redacted`.

- [x] **Step 3: Add fallback count test**

Set only `view.m_LogEntryCount = 5`, leave `m_LogSummary` empty, and verify
the log node text remains `5 entries`.

- [x] **Step 4: Run build to verify RED**

Build `qa_common`.

Expected: build fails because `AI_AGENT_PANEL_SEMANTIC_VIEW::m_LogSummary`
does not exist.

Observed RED:

```text
qa/tests/common/test_ai_agent_panel_semantic.cpp(94): error C2039: 'm_LogSummary': is not a member of 'AI_AGENT_PANEL_SEMANTIC_VIEW'
qa/tests/common/test_ai_agent_panel_semantic.cpp(112): error C2039: 'm_LogSummary': is not a member of 'AI_AGENT_PANEL_SEMANTIC_VIEW'
```

## Task 2: Implement Pure Semantic Summary

**Files:**
- Modify: `include/kisurf/ai/ai_agent_panel_semantic.h`
- Modify: `common/kisurf/ai/ai_agent_panel_semantic.cpp`

- [x] **Step 1: Add field**

Add:

```cpp
wxString m_LogSummary;
```

to `AI_AGENT_PANEL_SEMANTIC_VIEW`.

- [x] **Step 2: Use summary in log node**

Before adding `agent.log.entries`, compute:

```cpp
const wxString logText = aView.m_LogSummary.IsEmpty()
        ? wxString::Format( wxS( "%zu entries" ), aView.m_LogEntryCount )
        : aView.m_LogSummary;
```

Use `logText` for the node text value.

## Task 3: Wire Live Agent Panel Summary

**Files:**
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`

- [x] **Step 1: Add compact summary helper**

Add a local helper in the anonymous namespace:

```cpp
wxString compactObservabilitySummary(
        const std::vector<AI_AGENT_OBSERVABILITY_ENTRY>& aEntries,
        size_t aMaxLines );
```

It should take the most recent `aMaxLines` entries, format:

```text
#<sequence> <kind>: <title> - <summary>
```

and omit the suffix when summary is empty.

- [x] **Step 2: Pass summary into semantic view**

In `AI_AGENT_PANEL::SemanticUiTree()`:

- fetch `std::vector<AI_AGENT_OBSERVABILITY_ENTRY> entries =
  m_Model->ObservabilityEntries(128);`
- set `view.m_LogEntryCount = entries.size();`
- set `view.m_LogSummary = compactObservabilitySummary(entries, 6);`

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
.\qa_common.exe --run_test=AiAgentPanelSemantic,AiAgentPanel,AiAgentObservabilityLog
```

Expected: tests exit 0. The known schema-file warning may still appear.

Observed GREEN:

- `qa_common` build exited 0.
- The Boost comma-separated `--run_test` filter returned non-zero without
  details in this Windows run, so the equivalent suites were run separately
  with the build DLL directories and `api` directory on temporary `PATH`.
- `AiAgentPanelSemantic` ran 10 test cases, exit 0.
- `AiAgentPanel` ran 12 test cases, exit 0.
- `AiAgentObservabilityLog` ran 7 test cases, exit 0.
- The known schema-file warning appeared after successful runs.

- [x] **Step 2: Build editor targets**

Run:

```powershell
cmd.exe /S /C '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build out\build\x64-release --target pcbnew --config Release && cmake --build out\build\x64-release --target eeschema --config Release'
```

Expected: both targets build.

Observed: `pcbnew` and `eeschema` targets both built with exit 0.

- [x] **Step 3: Static and secret checks**

Run:

```powershell
git diff --check
rg -n "sk-[0-9A-Za-z_-]{20,}|OPENAI_API_KEY\s*=|KISURF_AI_API_KEY\s*=" include\kisurf\ai\ai_agent_panel_semantic.h common\kisurf\ai\ai_agent_panel_semantic.cpp common\kisurf\ai\ai_agent_panel.cpp qa\tests\common\test_ai_agent_panel_semantic.cpp docs\superpowers\specs\2026-06-19-ai-agent-panel-log-summary-design.md docs\superpowers\plans\2026-06-19-ai-agent-panel-log-summary-implementation.md
```

Expected: whitespace check exits 0; secret scan has no matches.

Observed: `git diff --check` exited 0; secret scan returned no matches.

- [x] **Step 4: Commit**

Stage only files touched by this plan. Do not stage unrelated
`qa/tests/pcbnew/test_module.cpp`.

```powershell
git add include/kisurf/ai/ai_agent_panel_semantic.h common/kisurf/ai/ai_agent_panel_semantic.cpp common/kisurf/ai/ai_agent_panel.cpp qa/tests/common/test_ai_agent_panel_semantic.cpp docs/superpowers/specs/2026-06-19-ai-agent-panel-log-summary-design.md docs/superpowers/plans/2026-06-19-ai-agent-panel-log-summary-implementation.md
git commit -m "feat: expose agent log summary in semantic panel state"
```

Observed: committed as `073beedd feat: expose agent log summary in semantic panel state`.

## Self-Review

- Spec coverage: tasks cover optional summary, redaction, fallback behavior,
  live panel wiring, verification, and commit.
- Open-item scan: No deferred implementation markers remain.
- Scope check: No UI layout, provider, visual capture, or IPC changes are
  included.
