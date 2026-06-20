# AI Dynamic Context Projection Design

Date: 2026-06-19

## Goal

Expose a stable `dynamic_context` summary in the unified AI context so the chat Agent and background Agent can quickly select the right behavior family: routing, layout, panel, general object work, idle, or unknown.

## Problem

KiSurf already exposes raw tool state (`tool_state.kind`), selected objects, panel state records, anchors, visual metadata, and activity.  That is rich, but it forces the model to repeatedly infer a higher-level mode from low-level fields.  The background Agent needs a consistent, cheap context switch signal because it must react to live editor changes and decide whether to propose route anchors, placement previews, panel-cell completions, or general object actions.

`dynamic_context` should not replace the detailed state.  It should be a projection that explains which source caused the current mode and points back to the raw state.

## Requirements

1. `AI_CONTEXT_SNAPSHOT::AsJsonText()` must include `dynamic_context`.
2. `AI_CONTEXT_SNAPSHOT::AsPromptText()` must include a compact dynamic-context line.
3. `kisurf_get_context_snapshot` and `kisurf_get_workspace_view.context` must inherit `dynamic_context` through the existing context JSON path.
4. `dynamic_context.kind` values:
   - `routing`
   - `layout`
   - `panel`
   - `general`
   - `idle`
   - `unknown`
5. Tool-state projection rules:
   - `RoutingTrack` maps to `routing`.
   - `PlacingVia`, `PlacingFootprint`, `DrawingZone`, and `MovingSelection` map to `layout`.
   - `Selecting` maps to `general`.
   - `Idle` maps to `idle`.
   - `Unknown` maps to `unknown` unless panel focus applies.
6. Focused panel projection:
   - If there is no active routing/layout/general/idle tool state and a panel state has a focused control, selected text, or focused control label, `dynamic_context.kind` maps to `panel`.
7. The JSON object must include:
   - `kind`
   - `source`
   - `tool_state_kind`
8. When panel context is selected, it must also include:
   - `focused_panel_id`
   - `focused_panel_title`
   - `focused_control_id`
   - `focused_control_label`
9. When tool state has an active action, the object must include `active_action`.
10. The projection must not remove or rename existing `tool_state` or `panel_states`.

## Interface

Example routing context:

```json
{
  "dynamic_context": {
    "kind": "routing",
    "source": "tool_state",
    "tool_state_kind": "routing_track",
    "active_action": "pcbnew.InteractiveRoute"
  }
}
```

Example panel context:

```json
{
  "dynamic_context": {
    "kind": "panel",
    "source": "panel_state",
    "tool_state_kind": "unknown",
    "focused_panel_id": "pcb.board_setup.clearance",
    "focused_panel_title": "Board Setup",
    "focused_control_id": "clearance.grid.row3.col2",
    "focused_control_label": "Minimum clearance"
  }
}
```

Prompt line:

```text
dynamic context: routing source=tool_state tool_state=routing_track
```

## Design Choices

### Projection, not replacement

The model still receives raw `tool_state`, `panel_states`, anchors, and activity.  `dynamic_context` is a small routing signal for Agent strategy selection.

### Tool state wins over panels

If the editor is actively routing or placing, that should drive the Agent mode even if the Agent panel is also open.  Panel context wins only when there is no more specific editor tool state.

### General object context

`Selecting` maps to `general` because selection-driven actions are broad: inspect, move, align, group, modify properties, or propose follow-up actions.

## Non-goals

This slice does not add:

- new Agent strategy execution
- background scheduling changes
- automatic suggestion generation changes
- UI rendering changes
- new panel semantic-tree capture

Those can consume `dynamic_context` after this projection is stable.

## Self-review

- The projection is deterministic and testable.
- It keeps existing raw context fields intact.
- It covers routing, layout, panel, general, idle, and unknown.
- The panel rule is conservative and only applies when no more specific tool state exists.
