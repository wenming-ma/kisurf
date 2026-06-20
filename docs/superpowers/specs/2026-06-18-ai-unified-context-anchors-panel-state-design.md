# AI Unified Context Anchors And Panel State Design

Date: 2026-06-18

## Purpose

The Agent needs one model-facing interface for what the user can currently see
and do. Today `AI_CONTEXT_SNAPSHOT` already carries visible objects, selected
objects, available actions, recent activity, active tool state, and visual
snapshot metadata. That is enough for first chat and suggestion loops, but it
does not yet cover two important pieces from the AI-native target architecture:

- semantic anchor points for routing, placement, pattern continuation, and
  preview `go_to` style tools;
- live panel state for dialogs, inspectors, setup tables, and other UI surfaces
  where the user is editing structured values.

This spec extends the shared context contract so chat, background Agent, and
future IPC/MCP projections can consume the same native context instead of
building separate ad-hoc payloads.

## Current Source Audit

Existing common-layer primitives:

- `AI_CONTEXT_SNAPSHOT` is the provider-bound context carrier.
- `AI_CONTEXT_INDEX` is the editor-side incremental index that builds snapshots.
- `AI_VISUAL_SNAPSHOT` carries current canvas image metadata and optional pixels.
- `AI_TOOL_STATE_SNAPSHOT` carries active tool kind, cursor position, shared
  JSON, and mode-specific JSON.
- `AI_ACTIVITY_RECORD` records recent user, tool, and runtime activity.
- `AI_AGENT_OBSERVABILITY_LOG` renders model input/tool/output debug entries
  from traces and activity.

Existing gaps:

- No first-class semantic anchors are available for route-point, via-pattern,
  shape, zone, placement, or panel-cell candidates.
- No snapshot field carries live UI panel text/table/focus state.
- Visual snapshots cannot name generated overlay anchors in a stable way.
- A model cannot ask a preview tool to move to "anchor 3" because anchors are
  currently embedded, if at all, in unstructured mode JSON.
- `AI_CONTEXT_INDEX` cannot carry panel state or generated anchors through to
  provider requests.

Related specs:

- `2026-06-16-ai-context-tool-provider-phase2-design.md`
- `2026-06-16-ai-structured-context-json-design.md`
- `2026-06-16-ai-native-visual-snapshot-design.md`
- `2026-06-17-ai-tool-state-next-action-preview-design.md`
- `2026-06-18-ai-agent-observability-log-design.md`

## Requirements

1. Keep one context carrier for chat and background Agent requests.
2. Add semantic anchors as bounded, stable, model-visible records.
3. Add panel state as bounded, structured, model-visible records.
4. Keep raw visual pixels out of text JSON; visual pixels remain in
   `AI_VISUAL_SNAPSHOT`.
5. Make anchors addressable by stable IDs so future tools can call
   `go_to_anchor`, `preview_route_to_anchor`, or similar commands.
6. Support anchors with and without board coordinates because panel cells and
   table rows are semantic locations, not PCB coordinates.
7. Support routing and placement constraints without forcing the model to reason
   over pixels.
8. Keep the first implementation common-layer and testable without opening a
   KiCad GUI.
9. Make later native PCB/schematic/panel providers responsible for generating
   domain-specific anchors; the common layer only carries and serializes them.
10. Keep all output bounded and deterministic.

## Approaches Considered

### Approach A: Put Everything Into Tool-State JSON

Extend `AI_TOOL_STATE_SNAPSHOT::m_ModeContextJson` with anchors and panel
state.

Pros:

- Minimal new C++ types.
- Existing provider requests already include tool state.

Cons:

- Anchors would be hidden inside opaque JSON.
- Panel state is not always tied to the current canvas tool.
- Tests cannot enforce stable ordering, bounds, or field names.
- Future IPC projections would need to parse tool-state internals.

### Approach B: Separate Context APIs

Add independent APIs such as `GetVisualContext`, `GetAnchorContext`, and
`GetPanelContext`.

Pros:

- Each subsystem can evolve independently.
- Easy to wire specialized providers.

Cons:

- The model-facing request path becomes fragmented.
- Chat and background Agent can disagree about what "current context" means.
- Every provider must learn several sources and merge policy.
- It works against the user's requirement for one flexible interface.

### Approach C: Extend `AI_CONTEXT_SNAPSHOT`

Add first-class anchor and panel-state vectors to `AI_CONTEXT_SNAPSHOT`, expose
them through prompt and JSON serialization, and let `AI_CONTEXT_INDEX` carry
them into snapshots.

Recommendation: Approach C. It keeps `AI_CONTEXT_SNAPSHOT` as the single
model-facing interface while preserving domain-specific generation behind native
providers. It also gives future IPC a clean projection surface.

## Data Contracts

### Anchor Kind

Add a compact enum:

```cpp
enum class AI_CONTEXT_ANCHOR_KIND
{
    Unknown,
    RouteStart,
    RouteTarget,
    RouteCandidate,
    OrthogonalBreakout,
    FortyFiveIntersection,
    PlacementCandidate,
    PatternContinuation,
    ShapeCorner,
    ZoneVertex,
    PanelCell,
    General
};
```

The kind describes how the Agent should interpret the anchor, not how it is
rendered. Domain providers may generate many route candidates, but the common
layer only serializes bounded records.

### Semantic Anchor

Add:

```cpp
struct AI_CONTEXT_ANCHOR
{
    wxString               m_Id;
    AI_CONTEXT_ANCHOR_KIND m_Kind = AI_CONTEXT_ANCHOR_KIND::Unknown;
    AI_EDITOR_KIND         m_EditorKind = AI_EDITOR_KIND::Unknown;
    wxString               m_Label;
    wxString               m_Summary;
    VECTOR2I               m_Position = VECTOR2I( 0, 0 );
    bool                   m_HasPosition = false;
    int                    m_Layer = -1;
    wxString               m_DetailsJson;
    double                 m_Confidence = 0.0;

    bool IsValid() const;
    wxString KindAsString() const;
};
```

Rules:

- `m_Id` is stable within a snapshot and should be short, such as
  `route.candidate.1`, `via.matrix.next`, or `panel.clearance.row3.col2`.
- `m_Label` is human-readable and may be shown as an overlay label.
- `m_Position` is in internal board units when `m_HasPosition` is true.
- `m_Layer` is optional; `-1` means not layer-specific.
- `m_DetailsJson` stores compact structured metadata such as net, width,
  target object ID, panel row/column, or candidate reason.
- `m_Confidence` is bounded to `[0.0, 1.0]` by producers.
- Invalid anchors are omitted by producers when possible; serializers still
  handle empty fields safely.

### Panel State

Add:

```cpp
struct AI_PANEL_STATE_RECORD
{
    wxString m_Id;
    wxString m_Title;
    wxString m_FocusedControlId;
    wxString m_FocusedControlLabel;
    wxString m_SelectedText;
    wxString m_Summary;
    wxString m_StateJson;

    bool HasState() const;
};
```

Rules:

- `m_Id` names the pane/dialog/table, such as `pcb.board_setup.clearance`.
- `m_Title` is user-visible panel title.
- Focus fields describe the current cell, field, or control.
- `m_StateJson` is bounded structured text. It may include table dimensions,
  focused row/column, visible column names, or relevant nearby cells.
- It must not contain provider secrets or raw large documents.

### Context Snapshot Extension

Extend `AI_CONTEXT_SNAPSHOT` with:

```cpp
std::vector<AI_CONTEXT_ANCHOR>      m_Anchors;
std::vector<AI_PANEL_STATE_RECORD>  m_PanelStates;
```

Serialization updates:

- `AsPromptText(...)` adds bounded `semantic anchors` and `panel states`
  sections.
- `AsJsonText(...)` adds:
  - `anchor_count`, `anchors`
  - `panel_state_count`, `panel_states`
- Anchor and panel details parse JSON when valid and otherwise preserve raw
  text as a safe string field.
- Existing visual metadata remains unchanged and still excludes `data_uri`.

### Context Index Extension

Extend `AI_CONTEXT_INDEX` with:

```cpp
const std::vector<AI_CONTEXT_ANCHOR>& Anchors() const;
const std::vector<AI_PANEL_STATE_RECORD>& PanelStates() const;
void SetAnchors( std::vector<AI_CONTEXT_ANCHOR> aAnchors );
void SetPanelStates( std::vector<AI_PANEL_STATE_RECORD> aPanelStates );
```

`SetAnchors` and `SetPanelStates` bump the view revision. They describe what the
user sees or can act on now, not persistent board data.

Sorting:

- Anchors sort by ID, then kind, then label.
- Panel states sort by ID, then title.
- Stable ordering is required so model prompts, tests, logs, and IPC projections
  do not churn.

## Anchor Generation Policy

This spec does not implement PCB anchor generation, but it defines the contract
that later providers must follow.

Routing anchors should be generated from geometric and electrical constraints:

- active route start point;
- route target pads/vias on the same net;
- horizontal and vertical breakout candidates from the current point;
- 45-degree intersection candidates between current point and target;
- layer-specific candidates when the active layer is known;
- clearance-validated candidates when native validation is available.

Placement anchors should be generated from:

- selected footprint bounding boxes;
- nearby related pads/nets;
- board outline and keepout/rule-area constraints;
- aligned component rows/columns;
- user-created shape corners and duplicate-offset candidates.

Pattern anchors should be generated from recent activity:

- equal-spaced via sequences;
- row/column via matrix continuation;
- repeated shape placement;
- repeated zone vertex edits.

Panel anchors should be generated from UI state:

- focused table cell;
- focused row/column names;
- selected value and neighboring values;
- validation status for visible cells.

Generation must be deterministic. No provider may create random anchor IDs.

## Single Interface Semantics

The single interface is still `AI_CONTEXT_SNAPSHOT`, with parameters applied at
capture/serialization boundaries:

- visual capture options decide pixel size and future overlay layers;
- context serialization bounds object, action, activity, anchor, and panel
  counts;
- panel providers may include or omit detailed state depending on privacy and
  size policy;
- future tools can target `anchor_id` without reading pixels.

This keeps the model workflow simple:

1. Read context prompt, structured JSON, and optional image.
2. Pick a semantic target such as `route.candidate.2`.
3. Call a typed tool with that `anchor_id`.
4. Native code resolves the anchor into preview geometry and renders it.

## Safety And Privacy

- Anchors and panel state are transient context, not project file data.
- Panel state must be bounded and must not capture passwords, tokens, or hidden
  application data.
- Raw image/base64 data stays out of text JSON.
- Board coordinates, net names, and visible UI labels are allowed because they
  are already model-facing editor context.
- Future persistence or telemetry requires a separate retention spec.

## First Implementation Slice

The first implementation should be common-layer only:

1. Add anchor and panel-state data types to `ai_types`.
2. Extend prompt and JSON serialization.
3. Extend `AI_CONTEXT_INDEX` to carry anchors and panel states.
4. Add tests for validity, stable ordering, prompt output, JSON output, and view
   revision bumps.

Do not implement PCB-specific anchor generation, panel walkers, or `go_to`
tools in this slice. Those come after the shared contract is stable.

## Testing Requirements

Use test-first development for:

- `AI_CONTEXT_ANCHOR::IsValid()` and `KindAsString()`.
- `AI_PANEL_STATE_RECORD::HasState()`.
- `AI_CONTEXT_SNAPSHOT::HasContext()` returns true for anchors or panel state.
- Prompt text includes bounded anchors and panel states.
- JSON text includes `anchors` and `panel_states` with parsed details JSON.
- Raw malformed details remain safe text instead of breaking serialization.
- `AI_CONTEXT_INDEX::SetAnchors()` and `SetPanelStates()` sort entries and bump
  view revision.
- Existing context tests continue to pass.

## Acceptance Criteria

- Chat and background Agent provider requests can receive semantic anchors and
  panel state through the same `AI_CONTEXT_SNAPSHOT` object.
- Structured context JSON contains bounded, deterministic anchor and panel-state
  arrays.
- Human-readable prompt text gives the model enough anchor names and summaries
  to select a target without pixel reasoning.
- No raw visual pixels are duplicated in text JSON.
- The implementation is fully covered by common-layer unit tests and does not
  require GUI automation.

## Deferred Follow-Up

After this common contract lands:

- Add PCB routing anchor provider for horizontal, vertical, and 45-degree
  candidates.
- Add via-pattern and placement anchor providers.
- Add panel-state providers for selected board setup tables and inspector
  controls.
- Add typed preview tools that accept `anchor_id`.
- Add visual overlay options so canvas snapshots can highlight selected anchors.
- Project the same context through IPC/MCP after native behavior is stable.

## Spec Self-Review

- Placeholder scan: no placeholder sections or unresolved markers remain.
- Consistency check: the design extends existing context and index contracts
  instead of creating a parallel interface.
- Scope check: first implementation is common-layer only and testable without
  GUI automation; PCB geometry generation is deferred.
- Ambiguity check: anchor IDs, sorting, prompt/JSON output, privacy, and
  deferred responsibilities are explicit.
