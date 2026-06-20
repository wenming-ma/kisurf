# AI Editor Activity Timeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans
> and superpowers:test-driven-development. Implement task-by-task with a red
> test before production code for every behavior change.

**Goal:** Record recent native KiCad editor activity from `TOOL_MANAGER` events
and attach it to `AI_CONTEXT_SNAPSHOT.m_RecentActivity` for model requests.

**Architecture:** Add a generic observer seam to `TOOL_MANAGER`; keep AI mapping
in KiSurf AI code linked through the `common` static library; store recent user
activity in `AI_AGENT_PANEL_MODEL`; register/unregister observers in PCB and
schematic frames.

**Tech Stack:** C++20, Boost.Test, KiCad `TOOL_EVENT`/`TOOL_MANAGER`,
`AI_ACTIVITY_LOG`, existing `qa_common`, `pcbnew`, and `eeschema` targets.

---

## File Structure

- Modify: `include/tool/tool_manager.h`
  - Add `EVENT_OBSERVER`, observer registration/removal methods, and observer
    storage fields.
- Modify: `common/tool/tool_manager.cpp`
  - Notify copied observers from `processEvent(...)`.
- Modify: `include/tool/tool_event.h`
  - Add a public read-only `CommandString()` accessor.
- Create: `qa/tests/common/test_tool_manager_event_observer.cpp`
  - Unit-test observer registration, notification, and removal.
- Modify: `qa/tests/common/CMakeLists.txt`
  - Add observer and AI activity recorder tests.
- Modify: `include/kisurf/ai/ai_types.h`
  - Add `m_RecentActivity` to `AI_CONTEXT_SNAPSHOT`.
- Modify: `common/kisurf/ai/ai_types.cpp`
  - Include recent activity in `HasContext()` and `AsPromptText(...)`.
- Create: `include/kisurf/ai/ai_editor_activity_recorder.h`
  - Declare event mapping from `TOOL_EVENT` to optional `AI_ACTIVITY_RECORD`.
- Create: `common/kisurf/ai/ai_editor_activity_recorder.cpp`
  - Implement high-signal event filtering and compact summaries.
- Modify: `common/CMakeLists.txt`
  - Add `kisurf/ai/ai_editor_activity_recorder.cpp` to `COMMON_SRCS`.
- Create: `qa/tests/common/test_ai_editor_activity_recorder.cpp`
  - Unit-test command, selection, move, click, and ignored motion mapping.
- Modify: `qa/tests/common/test_ai_types.cpp`
  - Unit-test recent activity prompt formatting.
- Modify: `include/kisurf/ai/ai_agent_panel_model.h`
  - Add `RecordActivity(...)` and `ActivityRecords()`.
- Modify: `common/kisurf/ai/ai_agent_panel_model.cpp`
  - Store user activity and attach it to provider request context.
- Modify: `include/kisurf/ai/ai_agent_panel.h`
  - Add `RecordActivity(...)` passthrough.
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`
  - Delegate activity records into the model.
- Modify: `qa/tests/common/test_ai_agent_panel_model.cpp`
  - Verify recorded activity reaches provider request context and prompt text.
- Modify: `pcbnew/pcb_edit_frame.h`
  - Store AI activity observer id.
- Modify: `pcbnew/pcb_edit_frame.cpp`
  - Register/unregister observer and forward mapped records to Agent panel.
- Modify: `eeschema/sch_edit_frame.h`
  - Store AI activity observer id.
- Modify: `eeschema/sch_edit_frame.cpp`
  - Register/unregister observer and forward mapped records to Agent panel.

## Verification Command Template

Use the Visual Studio developer environment:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_common -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\common\qa_common.exe --run_test=ToolManagerEventObserver,AiEditorActivityRecorder,AiNativeTypes,AiAgentPanelModel --log_level=test_suite"
```

Expected final result: exit code `0`, targeted suites run, and Boost reports no
errors. The known schema warning about `qa/tests/schemas/api.v1.schema.json` is
acceptable when exit code is `0`.

## Task 1: Tool Manager Observer Seam

**Files:**
- Modify: `include/tool/tool_manager.h`
- Modify: `common/tool/tool_manager.cpp`
- Modify: `include/tool/tool_event.h`
- Create: `qa/tests/common/test_tool_manager_event_observer.cpp`
- Modify: `qa/tests/common/CMakeLists.txt`

- [ ] **Step 1: Write failing observer tests**

Create `test_tool_manager_event_observer.cpp`:

```cpp
#include <boost/test/unit_test.hpp>

#include <tool/tool_manager.h>

BOOST_AUTO_TEST_SUITE( ToolManagerEventObserver )

BOOST_AUTO_TEST_CASE( ObserverSeesProcessedToolEvents )
{
    TOOL_MANAGER manager;
    std::vector<std::string> seen;

    manager.AddEventObserver( [&]( const TOOL_EVENT& aEvent )
    {
        seen.push_back( aEvent.CommandString() );
    } );

    manager.ProcessEvent( TOOL_EVENT( TC_MESSAGE, TA_ACTION, "test.event" ) );

    BOOST_REQUIRE_EQUAL( seen.size(), 1 );
    BOOST_CHECK_EQUAL( seen.front(), "test.event" );
}

BOOST_AUTO_TEST_CASE( RemovedObserverStopsReceivingEvents )
{
    TOOL_MANAGER manager;
    int count = 0;

    const uint64_t id = manager.AddEventObserver( [&]( const TOOL_EVENT& )
    {
        ++count;
    } );

    manager.RemoveEventObserver( id );
    manager.ProcessEvent( TOOL_EVENT( TC_MESSAGE, TA_ACTION, "test.event" ) );

    BOOST_CHECK_EQUAL( count, 0 );
}

BOOST_AUTO_TEST_SUITE_END()
```

Expected RED: compile fails because observer methods and `CommandString()` do not
exist.

- [ ] **Step 2: Implement observer seam**

Add public API to `TOOL_MANAGER`:

```cpp
using EVENT_OBSERVER = std::function<void( const TOOL_EVENT& )>;

uint64_t AddEventObserver( EVENT_OBSERVER aObserver );
void RemoveEventObserver( uint64_t aObserverId );
```

Add private storage:

```cpp
uint64_t m_nextEventObserverId = 1;
std::vector<std::pair<uint64_t, EVENT_OBSERVER>> m_eventObservers;
void notifyEventObservers( const TOOL_EVENT& aEvent );
```

Call `notifyEventObservers( aEvent )` at the start of
`TOOL_MANAGER::processEvent(...)` after the trace log. Copy the observer vector
before iteration.

Add public `CommandString()` to `TOOL_EVENT`:

```cpp
const std::string& CommandString() const { return m_commandStr; }
```

- [ ] **Step 3: Run observer tests to verify GREEN**

Run `--run_test=ToolManagerEventObserver`.

## Task 2: AI Event Mapping

**Files:**
- Create: `include/kisurf/ai/ai_editor_activity_recorder.h`
- Create: `common/kisurf/ai/ai_editor_activity_recorder.cpp`
- Modify: `common/CMakeLists.txt`
- Create: `qa/tests/common/test_ai_editor_activity_recorder.cpp`
- Modify: `qa/tests/common/CMakeLists.txt`

- [ ] **Step 1: Write failing mapper tests**

Test cases:

- Command event maps to `AI_ACTIVITY_KIND::UserAction` with the command string
  as `m_ActionName`.
- `EVENTS::SelectedEvent` maps to `common.Interactive.selected`.
- `EVENTS::SelectedItemsMoved` maps to `common.Interactive.moved`.
- Mouse click maps to `mouse.click` and includes compact position information.
- Mouse motion returns empty optional.

Expected RED: compile fails because
`kisurf/ai/ai_editor_activity_recorder.h` does not exist.

- [ ] **Step 2: Implement mapper**

Header:

```cpp
#pragma once

#include <kisurf/ai/ai_types.h>

#include <optional>

class TOOL_EVENT;

std::optional<AI_ACTIVITY_RECORD> MakeAiActivityRecordFromToolEvent(
        const TOOL_EVENT& aEvent,
        AI_EDITOR_KIND aEditorKind );
```

Implementation:

- Include `<tool/tool_event.h>` and `<tool/actions.h>`.
- Skip mouse motion and drag.
- For accepted events, set:
  - `m_Kind = AI_ACTIVITY_KIND::UserAction`
  - `m_EditorKind = aEditorKind`
  - `m_ActionName` to command string or stable synthetic name such as
    `mouse.click`
  - `m_Message` to `aEvent.Format()`
  - `m_ArgumentsJson` to a compact hand-built JSON object with event category,
    action, and rounded position when available.

- [ ] **Step 3: Run mapper tests to verify GREEN**

Run `--run_test=AiEditorActivityRecorder`.

## Task 3: Context And Agent Propagation

**Files:**
- Modify: `include/kisurf/ai/ai_types.h`
- Modify: `common/kisurf/ai/ai_types.cpp`
- Modify: `qa/tests/common/test_ai_types.cpp`
- Modify: `include/kisurf/ai/ai_agent_panel_model.h`
- Modify: `common/kisurf/ai/ai_agent_panel_model.cpp`
- Modify: `include/kisurf/ai/ai_agent_panel.h`
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`
- Modify: `qa/tests/common/test_ai_agent_panel_model.cpp`

- [ ] **Step 1: Write failing context/model tests**

Add `AI_CONTEXT_SNAPSHOT` prompt assertions for a recent activity record.

Add `AI_AGENT_PANEL_MODEL` test:

1. `model.RecordActivity( record )`.
2. Send user text with a context snapshot.
3. Assert provider request contains one `m_RecentActivity` record.
4. Assert response prompt contains `recent activity` and the action name.

Expected RED: compile fails because `m_RecentActivity`, `RecordActivity(...)`,
or `ActivityRecords()` do not exist.

- [ ] **Step 2: Implement context/model propagation**

Add `std::vector<AI_ACTIVITY_RECORD> m_RecentActivity;` to
`AI_CONTEXT_SNAPSHOT`.

Update `HasContext()` and `AsPromptText(...)` to include recent activity.

Add to `AI_AGENT_PANEL_MODEL`:

```cpp
void RecordActivity( AI_ACTIVITY_RECORD aRecord );
std::vector<AI_ACTIVITY_RECORD> ActivityRecords() const;
```

Store activity in a private `AI_ACTIVITY_LOG m_UserActivityLog`.

In `SendUserText(...)`, append recorded activity into
`request.m_ContextSnapshot.m_RecentActivity` before provider submission.

Add `AI_AGENT_PANEL::RecordActivity(...)` passthrough.

- [ ] **Step 3: Run context/model tests to verify GREEN**

Run `--run_test=AiNativeTypes,AiAgentPanelModel`.

## Task 4: PCB/Schematic Integration

**Files:**
- Modify: `pcbnew/pcb_edit_frame.h`
- Modify: `pcbnew/pcb_edit_frame.cpp`
- Modify: `eeschema/sch_edit_frame.h`
- Modify: `eeschema/sch_edit_frame.cpp`

- [ ] **Step 1: Include mapper and register observers**

Add `#include <kisurf/ai/ai_editor_activity_recorder.h>` to both frame files.

Add `uint64_t m_aiActivityObserverId = 0;` beside `m_agentPanel`.

After Agent panel creation:

```cpp
if( GetToolManager() )
{
    m_aiActivityObserverId = GetToolManager()->AddEventObserver(
            [this]( const TOOL_EVENT& aEvent )
            {
                if( !m_agentPanel )
                    return;

                auto record = MakeAiActivityRecordFromToolEvent(
                        aEvent, AI_EDITOR_KIND::Pcb );

                if( record )
                    m_agentPanel->RecordActivity( *record );
            } );
}
```

Use `AI_EDITOR_KIND::Schematic` in schematic.

- [ ] **Step 2: Unregister in destructors**

Before tool shutdown/destruction:

```cpp
if( m_toolManager && m_aiActivityObserverId != 0 )
{
    m_toolManager->RemoveEventObserver( m_aiActivityObserverId );
    m_aiActivityObserverId = 0;
}
```

- [ ] **Step 3: Build editor targets**

Run:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target pcbnew eeschema -- -j 2
```

Expected: both targets build.

## Task 5: Final Verification And Commit

- [ ] **Step 1: Run targeted common tests**

Run the verification command with:

```text
--run_test=ToolManagerEventObserver,AiEditorActivityRecorder,AiNativeTypes,AiAgentPanelModel
```

- [ ] **Step 2: Build editor targets**

Run `pcbnew eeschema` build command from Task 4.

- [ ] **Step 3: Run diff check**

Run:

```powershell
git diff --check
```

Expected: no whitespace errors. LF/CRLF warnings are acceptable.

- [ ] **Step 4: Commit**

Run:

```powershell
git add docs\superpowers\plans\2026-06-16-ai-editor-activity-timeline-implementation.md include\tool\tool_manager.h common\tool\tool_manager.cpp include\tool\tool_event.h include\kisurf\ai\ai_types.h common\kisurf\ai\ai_types.cpp include\kisurf\ai\ai_editor_activity_recorder.h common\kisurf\ai\ai_editor_activity_recorder.cpp common\CMakeLists.txt include\kisurf\ai\ai_agent_panel_model.h common\kisurf\ai\ai_agent_panel_model.cpp include\kisurf\ai\ai_agent_panel.h common\kisurf\ai\ai_agent_panel.cpp qa\tests\common\CMakeLists.txt qa\tests\common\test_tool_manager_event_observer.cpp qa\tests\common\test_ai_editor_activity_recorder.cpp qa\tests\common\test_ai_types.cpp qa\tests\common\test_ai_agent_panel_model.cpp pcbnew\pcb_edit_frame.h pcbnew\pcb_edit_frame.cpp eeschema\sch_edit_frame.h eeschema\sch_edit_frame.cpp
git commit -m "feat: add ai editor activity timeline"
```

## Plan Self-Review

- Spec coverage: observer seam, AI mapping, context propagation, frame
  lifecycle, tests, and editor builds are covered.
- Boundary check: `TOOL_MANAGER` remains AI-agnostic; AI event mapping lives in
  KiSurf AI code linked through `common`.
- Privacy check: no raw text, paths, clipboard data, or serialized parameters are
  recorded.
- Scope check: IPC read APIs, raw mouse motion, drag sampling, and automatic
  preview scheduling remain deferred.
