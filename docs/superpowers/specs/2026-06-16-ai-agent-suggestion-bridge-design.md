# AI Agent Suggestion Bridge Design

Date: 2026-06-16

## Purpose

Phase 7 added a headless suggestion lifecycle: native context/activity triggers can
produce bounded suggestion records, and those records can move through preview,
accept, reject, and stale expiration states. The Agent pane does not yet expose
that lifecycle to the engineer. It still behaves like a transcript-only chat
surface.

This phase connects the suggestion lifecycle to the Agent model and pane without
claiming real canvas overlay or document mutation support. It gives KiSurf a
visible, testable suggestion bridge that later editor-bound preview and commit
adapters can use.

## Goals

- Add model-level APIs that turn current context plus recent activity into
  suggestion records.
- Keep suggestion generation deterministic and local in this phase.
- Show suggestion records in the Agent pane as inspectable text cards with stable
  id, status, title, and body.
- Allow model callers to preview, accept, reject, and expire suggestions through
  the existing `AI_SUGGESTION_ORCHESTRATOR` boundary.
- Keep all suggestion state in memory and bounded by the orchestrator capacity.
- Preserve existing chat behavior and provider request flow.

## Non-Goals

- No real GAL canvas preview drawing in this phase.
- No real `BOARD_COMMIT` or `SCH_COMMIT` materialization in this phase.
- No autonomous placement, routing, footprint generation, or net editing.
- No model-generated suggestion provider is required for this phase.
- No persistence of suggestion records across editor sessions.
- No UI promise that accepting a suggestion has changed the PCB or schematic
  until real editor edit adapters exist.

## Architecture

The bridge lives in the common AI layer:

- `AI_AGENT_PANEL_MODEL` owns a suggestion provider and an
  `AI_SUGGESTION_ORCHESTRATOR` alongside the existing chat runtime.
- A deterministic default suggestion provider creates one lightweight suggestion
  when context contains a selected object and recent activity indicates a user
  selection or editor interaction.
- The model exposes query and lifecycle methods:
  - `UpdateSuggestions(...)`
  - `Suggestions()`
  - `FindSuggestion(...)`
  - `PreviewSuggestion(...)`
  - `AcceptSuggestion(...)`
  - `RejectSuggestion(...)`
  - `ExpireSuggestions(...)`
- `AI_AGENT_PANEL` renders suggestion records in a read-only suggestion area
  below the transcript. It refreshes that area when user/editor activity is
  recorded.

The model remains the owner of suggestion lifecycle state. The UI only renders
records and forwards activity. Later editor-specific adapters can call the same
model preview and accept methods with real preview/edit sessions.

## Suggestion Trigger Rules

The first deterministic provider is intentionally conservative:

- It returns no suggestion when editor kind is unknown.
- It returns no suggestion when no selected objects are present.
- It returns no suggestion when there is neither context nor activity.
- It builds at most one suggestion per update.
- The suggestion title should identify that the selected item can be reviewed.
- The suggestion body should explain that this is a previewable suggestion, not
  an already-applied edit.
- Preview and edit object lists should carry the selected objects so lifecycle
  tests can exercise `AI_PREVIEW_SESSION` and `AI_EDIT_SESSION`.

Duplicate suppression stays in the orchestrator. If the same active fingerprint
already exists, model update returns no new suggestion.

## Agent Pane Rendering

The first UI bridge is textual and safe:

- A new read-only suggestion text control appears below the transcript.
- Empty suggestion state renders as an empty string.
- Each suggestion line includes:
  - id
  - status
  - title
  - body
- Terminal states remain visible while retained by the bounded queue.

No button-driven document acceptance is added in this phase. That is deliberate:
real acceptance must use editor-specific adapters backed by KiCad-native
commit/undo mechanisms.

## Data Flow

1. PCB or schematic editor records a native tool event.
2. The editor forwards an `AI_ACTIVITY_RECORD` to the Agent pane.
3. The Agent pane stores the activity in `AI_AGENT_PANEL_MODEL`.
4. If a context provider is available, the pane asks it for a current
   `AI_CONTEXT_SNAPSHOT`.
5. The model expires stale active suggestions against the snapshot version.
6. The model asks the suggestion orchestrator to update from the context and
   activity.
7. The Agent pane refreshes its suggestion text area.

Chat requests continue to attach recent activity to the provider request as they
do today.

## Error Handling

- Missing context provider means activity is recorded but no suggestion update is
  attempted.
- Invalid triggers return `std::nullopt` from the orchestrator and do not change
  visible suggestion state.
- Preview or accept on missing, terminal, or empty-object suggestions returns
  `false`.
- Validation blocking remains owned by `AI_EDIT_SESSION` and the orchestrator.
- Stale context marks active suggestions `Expired` instead of deleting them
  immediately.

## Testing

Unit tests must cover:

- The deterministic suggestion provider creates a suggestion from selected
  context plus user activity.
- The deterministic suggestion provider does not create suggestions without
  selected context.
- `AI_AGENT_PANEL_MODEL::UpdateSuggestions(...)` stores a suggestion and exposes
  it through `Suggestions()`.
- Duplicate active suggestions are suppressed.
- Model preview, accept, reject, and expire methods delegate to the orchestrator.
- Existing chat tests still pass and continue to attach activity to provider
  context.
- The shared Agent panel still compiles as a `wxPanel`.

## Acceptance Criteria

- `qa_common` targeted tests pass for:
  - `AiAgentSuggestionProvider`
  - `AiAgentPanelModel`
  - `AiSuggestionOrchestrator`
  - `AiAgentPanel`
- The Agent pane has a dedicated read-only suggestion area.
- The model can manage suggestions without any editor UI dependency.
- No real editor mutation is exposed from the UI in this phase.

## Spec Self-Review

- Placeholder scan: no placeholder text remains.
- Scope check: this is a single bridge layer between existing model/UI and the
  Phase 7 orchestrator.
- Ambiguity check: real canvas preview and real commit-backed accept are
  explicitly deferred.
- Consistency check: all lifecycle operations use existing Phase 7 types and do
  not introduce a parallel suggestion state machine.
