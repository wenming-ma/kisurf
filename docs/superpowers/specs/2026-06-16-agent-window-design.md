# Agent Window Design

Date: 2026-06-16

## Purpose

This spec defines the first native Agent window for KiSurf. The Agent window gives users a dedicated place to ask AI for help, inspect suggestions, and later accept previewed edits.

The first implementation should provide a working native window shell connected to the AI runtime and deterministic stub provider. It does not need production model credentials or autonomous PCB actions.

## Goals

- Add a dedicated Agent window to the KiCad editor experience.
- Integrate the Agent window into PCB and schematic editors using existing native UI patterns.
- Let the user type a message and receive a deterministic response from the AI runtime.
- Display suggestion cards that can later connect to preview and materialization.
- Keep the UI useful before production model integration exists.

## Non-Goals

- No production multimodal model call in the first Agent window implementation.
- No automatic edit application from the Agent window in the first UI task.
- No custom browser-based UI surface in the first task.
- No semantic-tree debug automation in the Agent window task.

## UI Placement

The Agent window should be a dockable native pane, not a modal dialog.

Preferred integration:

- PCB editor: add a dockable AUI pane to `PCB_EDIT_FRAME`.
- Schematic editor: add a dockable AUI pane to `SCH_EDIT_FRAME`.
- Pane title: `Agent`.
- Default visibility: hidden unless opened by action or menu.
- Pane persistence: use the same settings pattern as search/properties/design-block panes.

The pane should be added through existing frame/pane patterns rather than inventing a new window manager.

## Proposed Files

The implementation plan should verify exact build target names before editing. Intended files:

- Create `include/kisurf/ai/agent_panel.h`
  - Native wx panel declaration.
- Create `common/kisurf/ai/agent_panel.cpp`
  - Shared panel implementation.
- Modify `pcbnew/pcb_edit_frame.h`
  - Add optional `AGENT_PANEL*` member or equivalent pane accessor.
- Modify `pcbnew/pcb_edit_frame.cpp`
  - Construct pane and add it to AUI manager.
- Modify `eeschema/sch_edit_frame.h`
  - Add optional Agent pane member or equivalent pane accessor.
- Modify `eeschema/sch_edit_frame.cpp`
  - Construct pane and add it to AUI manager.
- Modify toolbar/menu/action registration only if an existing action framework requires it.
- Add tests for non-GUI panel model logic where possible.

If sharing one `common` implementation creates build dependency problems, the implementation plan may split thin PCB/SCH wrappers around a shared non-GUI panel model.

## Panel Layout

The first Agent panel contains:

- Conversation transcript.
- Multiline input field.
- Send button.
- Cancel button.
- Context status line.
- Suggestion list area.

The context status line should show:

- Active editor type: PCB, schematic, symbol editor, footprint editor, or unavailable.
- Active document key when available.
- Selection count when available.
- Provider mode: stub or configured provider.

The suggestion list can initially display stub suggestion cards with disabled preview/apply buttons if preview/edit modules are not ready.

## Interaction Flow

1. User opens the Agent pane.
2. Pane asks `AI_RUNTIME` for current status.
3. User enters text and clicks Send.
4. Pane disables Send and enables Cancel.
5. Runtime receives message and current context summary.
6. Stub provider returns deterministic answer.
7. Pane appends assistant response.
8. Pane renders any returned suggestion card.
9. Send re-enables.

Cancellation:

- If the user clicks Cancel before response, the runtime cancels the request.
- The transcript records that the request was cancelled.
- No preview or edit is created.

## Data Flow

```text
AGENT_PANEL
  -> AI_RUNTIME::SubmitUserMessage(...)
  -> AI_PROVIDER::CreateSuggestion(...)
  -> AI_RUNTIME response
  -> AGENT_PANEL transcript and suggestion list
```

The panel does not call `BOARD_COMMIT`, `SCH_COMMIT`, or editor mutation methods directly.

## Error Handling

User-visible messages:

- `Agent runtime is unavailable.`
- `No active document context is available.`
- `The request was cancelled.`
- `The provider is not configured. Running in stub mode.`

Errors should not crash the editor or close the pane.

## Testing Requirements

The implementation plan must use test-first development for:

- Agent panel model appends user and assistant transcript entries in order.
- Sending while a request is pending is rejected or ignored.
- Cancelling a pending request changes the request state and does not append a provider response.
- Stub provider response is rendered as a suggestion card in panel model state.

Where GUI construction is hard to test in unit tests, extract a small `AGENT_PANEL_MODEL` or equivalent non-GUI state class for test coverage.

## Acceptance Criteria

- PCB editor can show and hide a native Agent pane.
- Schematic editor can show and hide a native Agent pane.
- A user can type a message and receive a stub response.
- Cancel works for pending stub requests.
- No production AI credentials are required.
- No accepted edit can be triggered from this first Agent pane until edit session support exists.
