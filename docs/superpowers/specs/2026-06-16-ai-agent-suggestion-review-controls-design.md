# AI Agent Suggestion Review Controls Design

Date: 2026-06-16

## Purpose

The current Agent pane can collect suggestion records and render them as text,
but users cannot act on them from the UI. The preview and edit adapter layers
also exist, but live PCB and schematic panes do not yet connect suggestion
review actions to the current editor state.

This spec adds the first native review loop: the Agent pane exposes Preview,
Accept, and Reject controls; the panel model can choose the newest active
suggestion; and PCB/schematic editors install review handlers that build native
preview or commit sessions against the current board/screen at click time.

## Source Anchors

- `include/kisurf/ai/ai_agent_panel.h` and
  `common/kisurf/ai/ai_agent_panel.cpp` own the dockable Agent UI and panel
  model lifetime.
- `include/kisurf/ai/ai_agent_panel_model.h` and
  `common/kisurf/ai/ai_agent_panel_model.cpp` expose suggestion lifecycle
  operations through `AI_SUGGESTION_ORCHESTRATOR`.
- `include/kisurf/ai/ai_suggestion_orchestrator.h` defines preview, accept,
  reject, and stale expiration rules for suggestion records.
- `pcbnew/kisurf_ai_pcb_preview_adapter.*` and
  `eeschema/kisurf_ai_sch_preview_adapter.*` show resolved AI references through
  `KIGFX::VIEW` preview groups.
- `pcbnew/kisurf_ai_pcb_move_edit_adapter.*` and
  `eeschema/kisurf_ai_sch_move_edit_adapter.*` materialize accepted move edits
  through `COMMIT`.
- `pcbnew/pcb_edit_frame.cpp` and `eeschema/sch_edit_frame.cpp` create the Agent
  panes and already provide current context snapshots.

## Goals

- Add `AI_AGENT_PANEL_MODEL::LatestActiveSuggestionId()` so UI code can operate
  on the newest pending or previewing suggestion without duplicating lifecycle
  rules.
- Add an `AI_AGENT_PANEL` suggestion-review configuration surface using editor
  callbacks instead of long-lived adapter ownership.
- Add Preview, Accept, and Reject buttons to the Agent pane.
- Have Preview and Accept refresh the suggestions text after handler execution.
- Have Reject update the model directly and refresh the suggestions text.
- Install PCB and schematic preview handlers that construct object resolvers and
  preview adapters from the current board/screen and current canvas view.
- Install PCB and schematic accept handlers that only execute a bounded move edit
  when `AI_SUGGESTION_RECORD::m_ArgumentsJson` has a supported move payload.
- Keep all handlers fail-closed: missing current editor state, invalid JSON,
  unsupported operation names, or invalid deltas return false and do not mutate
  the design.

## Non-Goals

- No autonomous placement, routing, trace optimization, or multi-step planning is
  introduced in this slice.
- No default movement is inferred from selected objects. A real edit must carry
  explicit parameters.
- No persistent preview session is shared across panel clicks. Each Preview
  action starts a fresh native preview group through the current view.
- No model-generated suggestion payload is required from the local deterministic
  suggestion provider in this slice.
- No IPC or external process interface is added for suggestion review.

## Design

### Latest Active Suggestion

`AI_AGENT_PANEL_MODEL` adds:

```cpp
std::optional<uint64_t> LatestActiveSuggestionId() const;
```

The method inspects `Suggestions()` in reverse sequence order and returns the
newest suggestion whose status is `Pending` or `Previewing`. Terminal statuses
(`Accepted`, `Rejected`, `Expired`) are skipped. If the orchestrator is absent
or no active record exists, it returns `std::nullopt`.

This keeps active-status policy in the model layer, close to the existing
preview/accept/reject methods.

### Panel Review Handlers

`AI_AGENT_PANEL` adds editor-provided handlers:

```cpp
using SUGGESTION_PREVIEW_HANDLER =
        std::function<bool( AI_AGENT_PANEL_MODEL& aModel, uint64_t aSuggestionId )>;
using SUGGESTION_ACCEPT_HANDLER =
        std::function<bool( AI_AGENT_PANEL_MODEL& aModel, uint64_t aSuggestionId )>;

void ConfigureSuggestionReview( SUGGESTION_PREVIEW_HANDLER aPreviewHandler,
                                SUGGESTION_ACCEPT_HANDLER aAcceptHandler );
bool PreviewLatestSuggestion();
bool AcceptLatestSuggestion();
bool RejectLatestSuggestion();
```

Preview and Accept are routed through handlers so editor-specific code can
construct short-lived resolver, preview adapter, commit, edit adapter, and
session objects from the current editor state. This avoids storing adapters that
hold references to a board or screen that may later be replaced.

Reject does not need editor state, so it calls
`AI_AGENT_PANEL_MODEL::RejectSuggestion(...)` directly.

### UI Controls

The Agent pane adds three buttons near the suggestions text:

- `Preview`
- `Accept`
- `Reject`

Each button calls the matching public method. Buttons may remain enabled even
when there is no active suggestion; the method returns false and makes no model
change in that state. This keeps the first UI slice simple while preserving
safe behavior.

### Editor Preview Handlers

PCB Preview handler:

1. Require `GetBoard()`, `GetCanvas()`, and `GetCanvas()->GetView()`.
2. Construct `KISURF_AI_PCB_OBJECT_RESOLVER resolver( *GetBoard() )`.
3. Construct `KISURF_AI_PCB_PREVIEW_ADAPTER adapter( resolver,
   *GetCanvas()->GetView() )`.
4. Construct `AI_PREVIEW_SESSION session( adapter )`.
5. Call `aModel.PreviewSuggestion( aSuggestionId, session )`.

Schematic Preview handler mirrors this flow with `GetScreen()`,
`KISURF_AI_SCH_OBJECT_RESOLVER`, and `KISURF_AI_SCH_PREVIEW_ADAPTER`.

### Move Edit Arguments

Accept handlers support only this first payload:

```json
{
  "operation": "move",
  "dx": 100,
  "dy": -25
}
```

Rules:

- `operation` is required and must be the string `move`.
- `dx` and `dy` are required integer internal-unit deltas.
- Unknown operations, missing fields, non-integer values, malformed JSON, and
  empty JSON fail closed.

PCB Accept handler:

1. Fetch the suggestion with `aModel.FindSuggestion( aSuggestionId )`.
2. Parse the move payload from `m_ArgumentsJson`.
3. Require `GetBoard()` and `GetToolManager()`.
4. Construct `BOARD_COMMIT commit( GetToolManager(), true, false )`.
5. Construct `KISURF_AI_PCB_OBJECT_RESOLVER resolver( *GetBoard() )`.
6. Construct `KISURF_AI_PCB_MOVE_EDIT_ADAPTER adapter( resolver, commit,
   delta )`.
7. Construct `AI_EDIT_SESSION session( adapter )`.
8. Call `aModel.AcceptSuggestion( aSuggestionId, session )`.

Schematic Accept handler mirrors this flow with `GetScreen()`,
`SCH_COMMIT commit( GetToolManager() )`, and
`KISURF_AI_SCH_MOVE_EDIT_ADAPTER`.

## Error Handling

- No active suggestion: panel methods return false.
- Missing preview handler: `PreviewLatestSuggestion()` returns false.
- Missing accept handler: `AcceptLatestSuggestion()` returns false.
- Missing board/screen/canvas/view/tool manager: editor handler returns false.
- Malformed or unsupported accept payload: editor handler returns false.
- Failed preview resolution: orchestrator returns false if there are no preview
  objects; adapters ignore unresolvable individual references.
- Failed edit resolution: edit session aborts through the adapter and returns
  false.
- Reject on a terminal suggestion returns false.

## Testing Requirements

Common tests:

- `AI_AGENT_PANEL_MODEL::LatestActiveSuggestionId()` returns no value when no
  active suggestions exist.
- It returns the newest pending suggestion when several active suggestions
  exist.
- It skips accepted, rejected, and expired suggestions.
- `AI_AGENT_PANEL` exposes `ConfigureSuggestionReview`,
  `PreviewLatestSuggestion`, `AcceptLatestSuggestion`, and
  `RejectLatestSuggestion`.

Editor verification:

- `qa_pcbnew` targeted AI suites still build and pass after PCB frame handler
  installation.
- `qa_eeschema` targeted AI suites still build and pass after schematic frame
  handler installation.

## Acceptance Criteria

- The Agent pane has visible Preview, Accept, and Reject controls next to the
  suggestion text.
- Preview routes the newest active suggestion to live PCB and schematic preview
  adapters built from the current editor state.
- Reject marks the newest active suggestion rejected without touching editor
  objects.
- Accept does not mutate anything unless the suggestion contains the supported
  move payload and all editor state required for a commit-backed edit is
  available.
- No long-lived resolver, preview adapter, edit adapter, board reference, or
  screen reference is stored by the common Agent pane.
- No API key, provider secret, pointer address, or raw editor object pointer is
  written into traces or tests.

## Spec Self-Review

- Open-marker scan: no unresolved placeholders remain.
- Scope check: this spec only bridges suggestion review UI to existing preview
  and move-edit infrastructure; routing/placement intelligence remains later.
- Safety check: Accept is fail-closed and requires explicit edit parameters.
- Lifetime check: editor handlers create adapters on demand from current editor
  state instead of storing references across project changes.
