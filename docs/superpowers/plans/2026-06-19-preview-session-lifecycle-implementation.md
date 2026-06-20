# Preview Session Lifecycle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep preview ownership in the editor workflow while letting chat-panel review controls delegate Preview, Accept, and Reject actions.

**Architecture:** Keep preview rendering in existing adapters. The chat panel exposes review commands and delegates them to editor-provided callbacks. PCB editor callbacks render, commit, reject, and clear canvas preview graphics.

**Tech Stack:** C++20, wxWidgets, KiCad GAL `KIGFX::VIEW`, Boost.Test.

---

## File Structure

- `include/kisurf/ai/ai_agent_panel.h`: add `SUGGESTION_REJECT_HANDLER` and a third optional handler parameter.
- `common/kisurf/ai/ai_agent_panel.cpp`: delegate Reject to the configured editor handler when present; do not store visible-preview state.
- `include/kisurf/ai/ai_preview_session.h`: add read-only `CurrentPreviewId()` and `HasActivePreview()` accessors.
- `pcbnew/pcb_edit_frame.cpp`: clear the canvas preview inside PCB editor Accept and Reject callbacks.
- `qa/tests/common/test_ai_agent_panel.cpp`: compile-time surface coverage for the Reject handler.
- `qa/tests/common/test_ai_agent_panel_semantic.cpp`: keep semantic tree coverage focused on chat command/review controls, with no visible-preview node.
- `qa/tests/common/test_ai_preview_session.cpp`: active-state accessor tests.

### Task 1: RED Tests

**Files:**
- Modify: `qa/tests/common/test_ai_agent_panel.cpp`
- Modify: `qa/tests/common/test_ai_preview_session.cpp`

- [x] **Step 1: Add panel configuration surface test**

Extend `AgentPanelExposesSuggestionReviewConfigurationSurface` to assert that
`AI_AGENT_PANEL::ConfigureSuggestionReview` is invocable with preview, accept,
and reject handlers:

```cpp
using Preview = AI_AGENT_PANEL::SUGGESTION_PREVIEW_HANDLER;
using Accept = AI_AGENT_PANEL::SUGGESTION_ACCEPT_HANDLER;
using Reject = AI_AGENT_PANEL::SUGGESTION_REJECT_HANDLER;
BOOST_CHECK( ( std::is_invocable_v<
        decltype( &AI_AGENT_PANEL::ConfigureSuggestionReview ),
        AI_AGENT_PANEL*, Preview, Accept, Reject> ) );
```

- [x] **Step 2: Add preview-session accessor test**

In `test_ai_preview_session.cpp`, assert:

```cpp
BOOST_CHECK( !session.HasActivePreview() );
uint64_t id = session.Begin();
BOOST_CHECK( session.HasActivePreview() );
BOOST_CHECK_EQUAL( session.CurrentPreviewId(), id );
session.Clear();
BOOST_CHECK( !session.HasActivePreview() );
```

- [x] **Step 3: Verify RED**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
```

Expected: compile failure because the new reject handler type and
preview-session accessors do not exist.

### Task 2: GREEN Implementation

**Files:**
- Modify: `include/kisurf/ai/ai_agent_panel.h`
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`
- Modify: `include/kisurf/ai/ai_preview_session.h`
- Modify: `pcbnew/pcb_edit_frame.cpp`

- [x] **Step 1: Add preview-session accessors**

Add inline accessors:

```cpp
uint64_t CurrentPreviewId() const { return m_CurrentPreviewId; }
bool     HasActivePreview() const { return m_CurrentPreviewId != 0; }
```

- [x] **Step 2: Add panel reject handler**

Add:

```cpp
using SUGGESTION_REJECT_HANDLER =
        std::function<bool( AI_AGENT_PANEL_MODEL& aModel, uint64_t aSuggestionId )>;
SUGGESTION_REJECT_HANDLER m_RejectSuggestionHandler;
```

Make `ConfigureSuggestionReview` accept the third handler with a default value.

- [x] **Step 3: Update Reject lifecycle**

In `RejectLatestSuggestion()`, call the configured reject handler when present;
otherwise fall back to `m_Model->RejectSuggestion()`. Do not store canvas preview
state in the chat panel.

- [x] **Step 4: Wire PCB callbacks**

In the PCB Accept callback, clear `GetCanvas()->GetView()->ClearPreview()` only
after `AcceptAiPcbSuggestion()` succeeds. In the PCB Reject callback, call
`aModel.RejectSuggestion( aSuggestionId )`, then clear the canvas preview only
after rejection succeeds.

- [ ] **Step 5: Verify GREEN**

Run targeted common tests:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiAgentPanel,AiAgentPanelSemantic,AiPreviewSession --report_level=short"
```

Expected: targeted tests pass.

### Task 3: Verification and Commit

**Files:**
- Verify all files touched in Tasks 1 and 2.

- [ ] **Step 1: Run full AI suite**

Run `qa_common.exe --run_test=Ai* --report_level=short`.

- [ ] **Step 2: Build pcbnew**

Run `cmake --build out/build/x64-release --target pcbnew -- -j 2`.

- [ ] **Step 3: Inspect with Computer Use**

Launch build-tree `pcbnew` and inspect the desktop for missing DLL or runtime
popups.

- [ ] **Step 4: Hygiene and commit**

Run `git diff --check`, scan staged content for `sk-` style secrets, stage only
this slice's files, and commit:

```powershell
git commit -m "ai: separate preview ownership from chat panel"
```

## Self-Review

- Spec coverage: every goal maps to Task 1 or Task 2.
- Marker scan: no incomplete marker instructions remain.
- Type consistency: handler and field names match across header, implementation,
  tests, and PCB integration.
