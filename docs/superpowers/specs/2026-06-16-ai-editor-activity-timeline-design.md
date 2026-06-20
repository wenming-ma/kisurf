# AI Editor Activity Timeline Design

Date: 2026-06-16

## Purpose

This spec implements the user-action sensing part of KiSurf's AI-native context
contract. The model should be able to understand recent editor activity such as
tool activation, explicit actions, selection changes, clicks, and move/update
notifications without relying on external IPC polling or desktop automation.

Phase 6 adds a native activity timeline that is populated from KiCad's tool
event pipeline and attached to `AI_CONTEXT_SNAPSHOT` when the Agent sends a
request.

## Source Research Anchors

Local source anchors:

- `include/tool/tool_dispatcher.h` and `common/tool/tool_dispatcher.cpp` translate
  wxWidgets mouse and keyboard events into GUI-independent `TOOL_EVENT` values.
- `include/tool/tool_manager.h` and `common/tool/tool_manager.cpp` are the central
  action and tool event execution path. `TOOL_MANAGER::doRunAction(...)` creates
  command events from `TOOL_ACTION`, and `TOOL_MANAGER::processEvent(...)` handles
  immediate, posted, activation, and recursive event dispatch.
- `include/tool/tool_event.h` and `common/tool/tool_event.cpp` provide event
  categories, actions, mouse/key helpers, `Format()`, `IsSelectionEvent()`, and
  selection-related event classification.
- `common/tool/actions.cpp` defines shared selection actions and messages such as
  `common.InteractiveSelection.selectItem`, `common.InteractiveSelection.clear`,
  `EVENTS::SelectedEvent`, `EVENTS::ClearedEvent`,
  `EVENTS::SelectedItemsModified`, and `EVENTS::SelectedItemsMoved`.
- `pcbnew/tools/pcb_selection_tool.cpp` and `eeschema/tools/sch_selection_tool.cpp`
  emit selection events through `m_toolMgr->ProcessEvent(...)`.
- `pcbnew/pcb_edit_frame.cpp` and `eeschema/sch_edit_frame.cpp` own the editor
  Agent panels and have access to both `GetToolManager()` and `m_agentPanel`.
- `include/kisurf/ai/ai_activity_log.h` already provides a bounded, thread-safe
  activity log used by model tool-call tracing.
- `include/kisurf/ai/ai_types.h` already has `AI_ACTIVITY_RECORD`; it needs a
  context-facing recent-activity container.

External practice anchors:

- OpenTelemetry's logs data model treats logs and events as structured records
  with timestamps, body, resource, attributes, and event names, and explicitly
  calls out efficient representation and semantic mapping as design goals:
  https://opentelemetry.io/docs/specs/otel/logs/data-model/
- VS Code's telemetry API routes extension telemetry through a logger that
  respects enablement, cleans sensitive data, and separates event names from
  key-value payloads:
  https://code.visualstudio.com/api/extension-guides/telemetry
- MCP separates model-invoked tools from application-provided resources/context:
  tools are model-controlled operations, while resources are application-driven
  context surfaces:
  https://modelcontextprotocol.io/specification/2025-11-25/server/tools
  and https://modelcontextprotocol.io/specification/2025-11-25/server/resources

## Goals

- Add a generic, non-AI `TOOL_MANAGER` event observer hook.
- Keep `TOOL_MANAGER` independent of KiSurf AI types.
- Add an AI activity mapper that converts high-signal `TOOL_EVENT` values into
  `AI_ACTIVITY_RECORD` entries.
- Record recent user/editor activity in `AI_AGENT_PANEL_MODEL`.
- Attach recent user activity to `AI_CONTEXT_SNAPSHOT` on send.
- Include recent activity in `AI_CONTEXT_SNAPSHOT::AsPromptText(...)` with a
  small bounded prompt representation.
- Register PCB and schematic Agent panels as subscribers to their editor
  `TOOL_MANAGER`, and remove observers during frame destruction.

## Non-Goals

- No remote telemetry upload.
- No collection of arbitrary user text, file contents, project paths, or
  clipboard data.
- No raw mouse-motion stream by default.
- No IPC API for the activity timeline in this phase.
- No model-triggered automatic preview based solely on activity; later phases can
  consume the timeline to schedule previews.
- No persistence of activity records beyond the in-memory bounded log.

## Design Decision

Use a native observer pipeline:

1. `TOOL_MANAGER` exposes `AddEventObserver(...)` and `RemoveEventObserver(...)`
   for any native subscriber.
2. `TOOL_MANAGER::processEvent(...)` notifies observers for every normalized
   `TOOL_EVENT`.
3. KiSurf AI code maps only high-signal events into `AI_ACTIVITY_RECORD`:
   command/action events, tool activation/cancel events, selection events,
   selected-items-modified/moved events, and mouse clicks/double-clicks.
4. `AI_AGENT_PANEL_MODEL` stores these records in an in-memory `AI_ACTIVITY_LOG`.
5. When the user sends an Agent message, the model copies recent activity into
   `AI_CONTEXT_SNAPSHOT.m_RecentActivity` before provider submission.

This keeps low-level event dispatch generic while making AI-specific retention,
filtering, and prompt formatting explicit.

## Data Contract

Add to `AI_CONTEXT_SNAPSHOT`:

```cpp
std::vector<AI_ACTIVITY_RECORD> m_RecentActivity;
```

Prompt formatting:

```text
recent activity: <count>
- #<sequence> <kind> <action> allowed=<yes/no> executed=<yes/no> message=<summary>
```

The prompt format must not include raw project paths or large parameter payloads.
`m_ArgumentsJson` may contain a compact structured event summary, but the first
implementation should keep it to event category/action/position only.

## Tool Event Observer Contract

`TOOL_MANAGER` adds:

```cpp
using EVENT_OBSERVER = std::function<void( const TOOL_EVENT& )>;

uint64_t AddEventObserver( EVENT_OBSERVER aObserver );
void RemoveEventObserver( uint64_t aObserverId );
```

Observer IDs are monotonically increasing per manager. Notification copies the
observer list before iteration so an observer can remove itself safely.

## Activity Mapping

The AI mapper should record:

- `TC_COMMAND` events with a non-empty command string.
- `TC_MESSAGE` events where `TOOL_EVENT::IsSelectionEvent()` is true.
- `TC_MESSAGE` events named `common.Interactive.modified` or
  `common.Interactive.moved`.
- `TC_MOUSE` click and double-click events.
- `TA_ACTIVATE` and `TA_CANCEL_TOOL` events.

The mapper should skip:

- `TA_MOUSE_MOTION`.
- Repeated `TA_MOUSE_DRAG` events in this phase.
- Empty/no-op events.
- Full serialized `ki::any` parameters.

## Lifecycle

- `PCB_EDIT_FRAME` and `SCH_EDIT_FRAME` store an observer id member initialized
  to `0`.
- After `m_agentPanel` is constructed, each frame registers a `TOOL_MANAGER`
  observer that maps tool events into `AI_ACTIVITY_RECORD` and calls
  `m_agentPanel->RecordActivity(...)`.
- In the frame destructor, remove the observer before shutting down tools or
  deleting pane children.

## IPC Comparison

An IPC plugin could poll action state or receive explicit notifications after
extra API work, but the native observer sees events at the same point KiCad tools
consume them. This gives lower-latency, complete local context without committing
to a transport schema first. A later IPC/MCP surface can expose a read-only
projection of `m_RecentActivity`, but it should not be the first source of truth.

## Test Strategy

- Unit-test `TOOL_MANAGER` observer registration, notification, and removal.
- Unit-test AI event mapping from synthetic `TOOL_EVENT` values.
- Unit-test `AI_CONTEXT_SNAPSHOT::AsPromptText(...)` recent-activity formatting.
- Unit-test `AI_AGENT_PANEL_MODEL` copies recorded activity into provider
  requests and keeps prompt text stable.
- Build `pcbnew` and `eeschema` after frame integration.

## Acceptance Criteria

- The model request context includes recent user/editor activity without external
  screenshot or IPC polling.
- `TOOL_MANAGER` has a generic observer seam with no dependency on KiSurf AI.
- PCB and schematic editors register and unregister observers safely.
- Tests cover observer behavior, event mapping, prompt formatting, and Agent
  model propagation.
- Activity capture remains bounded, in-memory, and non-persistent.

## Risks And Mitigations

- **Event volume:** Raw mouse motion and drag are skipped in this phase.
- **Lifecycle dangling callbacks:** Frame destructors must remove observer IDs.
- **Core coupling:** `TOOL_MANAGER` exposes only a generic observer API.
- **Privacy/prompt bloat:** The mapper records event names and compact summaries,
  not payload objects, paths, clipboard data, or full serialized parameters.
- **Double counting:** Tool-call activity and user/editor activity remain in
  separate logs until a later unified timeline spec decides merge semantics.

## Spec Self-Review

- Source anchors cover KiCad's wx-event-to-tool-event path, action path,
  selection messages, frame ownership, and existing AI log types.
- External anchors cover structured event records, telemetry hygiene, and the
  tool/context separation used by MCP-like integrations.
- Scope is limited to in-memory recent activity and prompt/context propagation.
- Deferred work is explicit: IPC read APIs, real-time preview scheduling,
  persisted telemetry, and raw mouse-drag sampling are not included.
