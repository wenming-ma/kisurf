# AI Tool State And Next Action Preview Design

Date: 2026-06-17

## Purpose

The current AI-native foundation can open a native Agent pane, send provider
requests with context and visual snapshots, record selected editor activity,
parse bounded model suggestions, preview selected-object moves, and accept those
moves through native edit adapters. That is enough for a first chat and review
loop, but it is not yet the real-time co-engineering loop requested for PCB
layout and routing.

This spec defines the next foundation layer:

- current active tool state as first-class model context,
- per-context background workspace state that follows what the user is doing,
- low-latency Next Action Preview for routing and via placement,
- typed semantic command tools for commands such as moving selected components
  or creating a copper zone,
- a native-first architecture that can later be projected through IPC without
  making IPC the source of truth.

## Current Capability Audit

Implemented and usable now:

- `ACTIONS::showAgentPanel` exists as `common.Control.showAgentPanel`.
- PCB and schematic editors instantiate `AI_AGENT_PANEL`.
- Context snapshots include visible objects, selected objects, actions, recent
  activity, summary text, and native visual snapshots.
- `TOOL_MANAGER::AddEventObserver(...)` exists and is used by PCB and schematic
  frames to record activity.
- `MakeAiActivityRecordFromToolEvent(...)` records command, selection,
  modified/moved, click, and double-click events.
- `AI_SUGGESTION_ORCHESTRATOR` owns pending/preview/accept/reject/expire
  lifecycle.
- `AI_AGENT_SUGGESTION_PROVIDER` can turn model JSON into suggestion records.
- `ParseAiSuggestionMoveDelta(...)` supports the first operation shape:
  `{"operation":"move","dx":...,"dy":...}`.
- PCB and schematic preview/edit adapters can preview and accept bounded move
  suggestions for resolved object references.

Important gaps:

- The current activity recorder intentionally filters motion and drag events.
  That is correct for chat history, but insufficient for real-time routing and
  placement preview.
- The model-visible context has no explicit current tool mode such as
  `routing_track`, `placing_via`, `placing_footprint`, or `drawing_zone`.
- The action runner installed in live editors is deny-by-default and currently
  allowlists only `common.Control.showAgentPanel`.
- Suggestion operations support move only; there is no typed route segment,
  via, footprint placement, or copper zone operation.
- Preview adapters can show object refs and moved clones, but there is no common
  contract for synthetic geometry previews such as "the next route segment" or
  "the next via at this coordinate".
- Agent model has one visible panel state, but the background Preview Agent
  still needs per-context workspace state for routing, via placement, component
  placement, zone creation, and selection editing.

## User-Facing Requirements

1. Command execution from chat:
   - "move selected component by X/Y" should become a typed semantic edit
     request, not a raw arbitrary action invocation.
   - "create a copper zone" should become a typed zone creation request with a
     preview and accept step.

2. Next Action Preview:
   - While routing, KiSurf can preview a proposed next segment.
   - While placing vias, KiSurf can preview a proposed next via position.
   - If the user places two or three vias on the same horizontal or vertical
     line with equal spacing, KiSurf can infer the next equally spaced via and
     preview it.

3. Context injection:
   - Current active tool/action is injected automatically when the user switches
     tools.
   - Shared context is preserved, while action-specific context is added for
     routing, via placement, footprint placement, zone drawing, and selection
     movement.
   - Real-time clicks, cursor position, active layer, active net, selected item
     refs, and recent placed items are available to providers.

4. Dynamic workspace context state:
   - The background Preview Agent switches its workspace context when the editor
     switches tools.
   - Routing, via placement, component placement, zone creation, selection edit,
     and general activity each preserve their own transient context state.
   - Switching contexts restores the previous state for that context without
     changing the visible Chat Panel.

## Native-First Design Decision

Make the native editor process the source of truth for real-time collaboration.
IPC can later expose read-only projections and explicit commands, but IPC should
not own low-latency preview generation, transient router state, preview lifetime,
or commit/undo integration.

The native path is better for:

- access to in-memory board/schematic objects without serializing everything,
- access to current `TOOL_MANAGER` and interactive tool state,
- access to PNS router state such as route mode and active route state,
- non-persistent preview ownership through native `KIGFX::VIEW` preview groups,
- accepted edit materialization through `BOARD_COMMIT` and `SCH_COMMIT`,
- cancellation and stale-context expiration inside the editor event loop.

The IPC path remains useful for:

- stable external automation APIs,
- Python/plugin ecosystem integration,
- headless or remote workflows,
- debugger/replay tools,
- exposing the same context and suggestion records to external agents.

The architecture stance is therefore:

```text
Native source of truth -> stable AI contracts -> optional IPC/MCP projection
```

## Data Contracts

### Tool State

Add a common tool-state layer:

```cpp
enum class AI_TOOL_STATE_KIND
{
    Unknown,
    Idle,
    Selecting,
    RoutingTrack,
    PlacingVia,
    PlacingFootprint,
    DrawingZone,
    MovingSelection
};

struct AI_TOOL_STATE_SNAPSHOT
{
    AI_EDITOR_KIND      m_EditorKind = AI_EDITOR_KIND::Unknown;
    AI_TOOL_STATE_KIND  m_Kind = AI_TOOL_STATE_KIND::Unknown;
    AI_CONTEXT_VERSION  m_ContextVersion;
    wxString            m_ActiveActionName;
    VECTOR2I            m_CursorBoardPosition;
    bool                m_HasCursorBoardPosition = false;
    wxString            m_SharedContextJson;
    wxString            m_ModeContextJson;
};
```

`m_SharedContextJson` holds compact shared properties such as selected refs,
active layer, units, grid, and recent click sequence. `m_ModeContextJson` holds
mode-specific details such as active net, route width, via diameter, via drill,
last route points, placed via refs, or zone layer.

### Workspace Context State

Add a workspace context state record for the background preview workflow:

```cpp
enum class AI_AGENT_WORKSPACE_CONTEXT_KIND
{
    General,
    Routing,
    ViaPlacement,
    FootprintPlacement,
    ZoneCreation,
    SelectionEdit
};

struct AI_AGENT_WORKSPACE_CONTEXT_STATE
{
    AI_AGENT_WORKSPACE_CONTEXT_KIND m_ContextKind = AI_AGENT_WORKSPACE_CONTEXT_KIND::General;
    wxString                        m_Title;
    wxString                        m_StateJson;
    uint64_t                        m_LastActivitySequence = 0;
};
```

The Agent model keeps one state record per workspace context in memory. This is
not a visible Chat Panel mode, not a tab, and not user-selectable. It is
background Preview Agent state that helps correlate recent routing, placement,
zone, or selection activity. It is not saved into the project file in the first
implementation.

### Next Action Trigger And Candidate

Add a trigger separate from chat activity:

```cpp
enum class AI_NEXT_ACTION_KIND
{
    RouteSegment,
    ViaPlacement,
    FootprintPlacement,
    CopperZone,
    MoveSelection
};

struct AI_NEXT_ACTION_TRIGGER
{
    AI_NEXT_ACTION_KIND    m_Kind;
    AI_CONTEXT_SNAPSHOT    m_ContextSnapshot;
    AI_TOOL_STATE_SNAPSHOT m_ToolState;
    AI_ACTIVITY_RECORD     m_Activity;
    wxString               m_Reason;
};

struct AI_NEXT_ACTION_CANDIDATE
{
    uint64_t               m_Id = 0;
    AI_NEXT_ACTION_KIND    m_Kind;
    AI_CONTEXT_VERSION     m_ContextVersion;
    wxString               m_Title;
    wxString               m_Body;
    wxString               m_OperationJson;
    AI_VALIDATION_SUMMARY  m_Validation;
};
```

The existing `AI_SUGGESTION_RECORD` can carry accepted candidates, but the
candidate type keeps low-latency preview generation independent from chat
transcript rendering.

## Provider And Controller

Add a provider interface:

```cpp
class AI_NEXT_ACTION_PROVIDER
{
public:
    virtual ~AI_NEXT_ACTION_PROVIDER() = default;

    virtual std::optional<AI_NEXT_ACTION_CANDIDATE> SuggestNextAction(
            const AI_NEXT_ACTION_TRIGGER& aTrigger ) = 0;
};
```

Add a controller:

```cpp
class AI_NEXT_ACTION_CONTROLLER
{
public:
    void OnToolStateChanged( AI_TOOL_STATE_SNAPSHOT aState );
    void OnActivity( AI_ACTIVITY_RECORD aActivity );
    void OnContextChanged( AI_CONTEXT_SNAPSHOT aSnapshot );
    bool PreviewLatest();
    bool AcceptLatest();
    bool RejectLatest();
};
```

The controller owns throttling, cancellation, stale-context expiration,
deduplication, and handoff to preview/edit adapters. It must not mutate the
board unless the user accepts a candidate.

## First Providers

### Deterministic Via Pattern Provider

The first low-risk provider is deterministic:

- Inspect recent PCB vias from the current board context.
- Filter to vias on the same net and compatible via type/drill/diameter.
- If at least two recent vias are collinear and equally spaced, propose the next
  via at `last + delta`.
- Prefer requiring three vias before auto-surfacing a confident suggestion; two
  vias may create a lower-confidence candidate visible only in the Agent pane.
- Reject candidates outside the board outline, inside keepout/rule areas, or
  violating obvious clearance constraints when that data is available.

The preview operation shape:

```json
{
  "operation": "place_via_preview",
  "x": 10000000,
  "y": 20000000,
  "net": "GND",
  "layer_pair": "F.Cu/B.Cu",
  "diameter": 600000,
  "drill": 300000,
  "source": "equal_spacing"
}
```

### Routing Segment Provider

The first routing provider should be rule-based and cheap:

- Activate only when the current tool state is `RoutingTrack`.
- Read active net, layer, route width, last committed route point, and cursor
  board position.
- Propose at most one short next segment using the current routing constraint
  style.
- Do not call a model on mouse motion.
- Use PNS/router validation where available before surfacing the candidate.

The preview operation shape:

```json
{
  "operation": "route_segment_preview",
  "start": { "x": 10000000, "y": 20000000 },
  "end": { "x": 13000000, "y": 20000000 },
  "layer": "F.Cu",
  "net": "GND",
  "width": 150000
}
```

### Semantic Command Provider

Chat commands should not invoke arbitrary KiCad actions directly. They should be
converted into typed semantic operations:

- `move_selected`
- `create_copper_zone`
- `place_via`
- `route_segment`

Each semantic operation gets:

- typed argument validation,
- policy classification,
- preview support when it changes the board,
- user accept before commit,
- native commit/undo integration.

## Model Use Policy

Do not call the model on every cursor motion or drag event.

Use this sequence:

1. Native deterministic providers run on high-frequency events.
2. Model-backed providers run only on coarse triggers such as command text,
   explicit refresh, tool activation, or a debounced stable pause.
3. Model requests receive bounded context, current visual snapshot, tool state,
   and recent activity.
4. Model output becomes a candidate, not a commit.
5. Preview and accept stay native and policy-gated.

## Preview And Accept Semantics

Preview:

- uses a preview adapter,
- never stages a native commit,
- has an owner ID,
- is cleared on reject, accept, context mismatch, tool switch, or pane teardown.

Accept:

- revalidates context version,
- parses typed operation JSON,
- creates a native edit session,
- applies through `BOARD_COMMIT` or `SCH_COMMIT`,
- records activity and validation result,
- updates workspace context state.

## Dynamic Workspace Context Behavior

The background Preview Agent tracks active workspace context:

- `General` for idle or non-specialized editor activity,
- `Routing` when `pcbnew.InteractiveRouter.SingleTrack` or related route state
  is active,
- `ViaPlacement` when `pcbnew.InteractiveDrawing.drawVia` is active or recent
  via pattern triggers,
- `FootprintPlacement` when `pcbnew.EditorControl.placeFootprint` is active,
- `ZoneCreation` when `pcbnew.InteractiveDrawing.drawZone` is active,
- `SelectionEdit` when selected objects are being moved or modified.

Each context stores compact mode JSON, last candidate summary, last preview
status, and last accepted/rejected candidate ID. Switching context must not
change the visible Chat Panel. The Chat Panel remains a single command surface;
Routing, ViaPlacement, FootprintPlacement, ZoneCreation, and SelectionEdit are
internal context labels for sensing and suggestion generation.

## IPC Comparison

An IPC-only design can expose actions, board objects, screenshots, and event
streams. It is viable for external tooling and later interoperability. Its main
weaknesses for this feature set are:

- every high-frequency cursor/tool event becomes a transport/schema problem,
- PNS router transient state is harder to expose without leaking internals,
- non-persistent preview ownership must cross a process boundary,
- accepting edits must map back to native undo/commit boundaries anyway,
- latency and cancellation are harder to reason about when the preview loop is
  outside the editor process.

A native AI design has lower latency and better access to live editor memory,
but must be careful not to become untestable. The mitigation is to keep all
native sensing and editing behind stable common AI contracts and to expose those
contracts later through IPC as projections.

## Test Strategy

Common tests:

- Tool-state structs serialize to bounded prompt/JSON text.
- Workspace context switching preserves per-context background state.
- Next-action controller deduplicates candidates and expires stale context.
- Operation parsers accept route segment, via placement, zone creation, and
  selected move shapes while rejecting malformed JSON.

PCB tests:

- Via pattern provider proposes the next equal-spacing via for two/three aligned
  vias and rejects non-collinear sequences.
- Via preview adapter creates and clears a non-persistent via preview.
- Route segment parser and preview adapter create and clear a non-persistent
  segment preview.
- Accepting via/route/zone candidates uses `BOARD_COMMIT` and participates in
  undo.

UI/build tests:

- Agent pane exposes one chat command surface and does not expose Routing,
  ViaPlacement, FootprintPlacement, ZoneCreation, or SelectionEdit as visible
  tabs, chips, or public panel mode APIs.
- Background workspace context can switch between Routing, ViaPlacement,
  ZoneCreation, and SelectionEdit without changing the visible Chat Panel.
- PCB and schematic builds continue to pass.

## Acceptance Criteria

- The model/provider context includes explicit active tool state.
- The Agent model preserves separate transient state per workspace context while
  the visible Chat Panel remains a single command surface.
- A deterministic via pattern provider can preview the next equally spaced via.
- A routing provider can preview a next segment without committing it.
- Chat command execution for board-changing operations uses typed semantic
  operations, preview, policy, and user accept.
- No automatic background model call can mutate the board.
- IPC remains compatible as a projection layer, not a dependency of the native
  preview loop.

## Spec Self-Review

- Scope check: this spec defines the next architecture layer and first providers;
  it does not promise full autorouting.
- Safety check: all board-changing operations require preview and accept through
  native commit/edit boundaries.
- Latency check: high-frequency events use native deterministic providers, not
  network model calls.
- IPC check: IPC is explicitly supported as a later projection, not the core
  owner of real-time state.
