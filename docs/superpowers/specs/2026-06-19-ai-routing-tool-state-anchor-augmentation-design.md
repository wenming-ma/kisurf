# AI Routing Tool-State Anchor Augmentation Design

## Purpose

Expose live routing preview points as first-class semantic anchors. PCB geometry anchors describe existing board objects; this slice adds transient anchors derived from the active routing tool state so the model can choose a named route landing point instead of reasoning over raw cursor coordinates.

## Source Observations

- `KISURF_AI_PCB_CONTEXT_ADAPTER` emits factual anchors from existing board geometry.
- `KISURF_AI_PCB_TOOL_STATE_PROVIDER` already writes routing `m_ModeContextJson` with `net`, `layer`, `width`, `start`, `current_end`, and `cursor` when the router is active.
- `AI_ROUTING_SEGMENT_NEXT_ACTION_PROVIDER` already parses routing tool state and creates direct route preview suggestions from `start` to `cursor`.
- `AI_CONTEXT_SNAPSHOT` is assembled in `pcbnew/pcb_edit_frame.cpp` by building the PCB context index, then attaching tool state.
- `AI_CONTEXT_ANCHOR_KIND` already includes `RouteStart`, `RouteCandidate`, `OrthogonalBreakout`, and `FortyFiveIntersection`.

## Goals

1. Add a common-layer helper that derives routing anchors from `AI_CONTEXT_SNAPSHOT::m_ToolState`.
2. Generate deterministic, bounded anchors only when the active tool state is PCB `RoutingTrack`.
3. Preserve existing board-object anchors while appending transient tool-state anchors.
4. Add anchors for the route start, current end, orthogonal corner candidates, and valid 45-degree breakout candidates.
5. Include net, layer, width, start, target, role, and position in each anchor's details JSON.
6. Sort the resulting anchor list deterministically after augmentation.
7. Integrate the helper into the PCB Agent context path after tool state is attached.
8. Add common tests for anchor generation and an editor smoke build for integration.

## Non-Goals

- No router pathfinding, clearance checking, or obstacle avoidance is added in this slice.
- No accepted board edit is performed.
- No new model-facing tool is added in this slice.
- No visual overlay rendering changes are added in this slice.
- No schematic routing anchors are added.
- No attempt is made to infer a real target pad; this slice uses the active routing target from `cursor` or `current_end`.

## New API

Create a common header:

```cpp
#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_types.h>

KICOMMON_API void AppendAiToolStateAnchors( AI_CONTEXT_SNAPSHOT& aSnapshot );
```

`AppendAiToolStateAnchors()` mutates only the transient snapshot passed to it. It does not mutate the board, the context index, tool state providers, or project files.

## Routing Input Contract

The helper reads `aSnapshot.m_ToolState` and returns without changes unless:

- `aSnapshot.m_EditorKind == AI_EDITOR_KIND::Pcb`
- `aSnapshot.m_ToolState.m_EditorKind == AI_EDITOR_KIND::Pcb`
- `aSnapshot.m_ToolState.m_Kind == AI_TOOL_STATE_KIND::RoutingTrack`
- `m_ModeContextJson` is a JSON object with non-empty `net`, non-empty `layer`, positive integer `width`, and a valid `start` point.
- A valid target point exists in `cursor`, then `current_end`, then `m_CursorBoardPosition`.
- `start` and target are different points.

## Anchor Set

For a start point `S` and target point `T`, generate at most five anchors:

1. `tool.routing.start`
   - kind: `RouteStart`
   - position: `S`
   - label: `route:start`

2. `tool.routing.current_end`
   - kind: `RouteCandidate`
   - position: `T`
   - label: `route:current_end`

3. `tool.routing.orthogonal.horizontal`
   - kind: `OrthogonalBreakout`
   - position: `(T.x, S.y)`
   - label: `route:orthogonal:horizontal`

4. `tool.routing.orthogonal.vertical`
   - kind: `OrthogonalBreakout`
   - position: `(S.x, T.y)`
   - label: `route:orthogonal:vertical`

5. 45-degree breakout candidates:
   - `tool.routing.fortyfive.horizontal`
     - position: `(T.x - sign(T.x - S.x) * abs(T.y - S.y), S.y)`
     - valid only when `abs(T.x - S.x) >= abs(T.y - S.y)`
   - `tool.routing.fortyfive.vertical`
     - position: `(S.x, T.y - sign(T.y - S.y) * abs(T.x - S.x))`
     - valid only when `abs(T.y - S.y) >= abs(T.x - S.x)`

Candidate points identical to `S` or `T` are omitted, except `tool.routing.start` and `tool.routing.current_end`.

## Details JSON

Every generated anchor must include:

```json
{
  "source": "tool_state",
  "mode": "routing_track",
  "role": "orthogonal_horizontal",
  "net": "/GPIO",
  "layer": "F.Cu",
  "width": 150000,
  "start": { "x": 100, "y": 200 },
  "target": { "x": 500, "y": 350 },
  "position": { "x": 500, "y": 200 }
}
```

The helper may add extra fields later, but these fields are required for this slice.

## Ordering And Replacement

- Generated ids are stable and independent of cursor coordinates.
- Before appending new tool-state anchors, remove any existing anchors whose id starts with `tool.routing.`.
- Preserve all non-tool-state anchors.
- Sort final anchors by id, then kind, then label using the same ordering policy as `AI_CONTEXT_INDEX`.
- Incrementing context versions is not required because augmentation occurs after the snapshot has been built; the routing tool state already carries the current context version.

## PCB Agent Integration

In `pcbnew/pcb_edit_frame.cpp`, after assigning `snapshot.m_ToolState`, call:

```cpp
AppendAiToolStateAnchors( snapshot );
```

This places dynamic routing anchors in the same prompt and JSON channel as factual board anchors. Schematic editor integration is not changed in this slice.

## Failure Behavior

- Malformed JSON produces no generated anchors.
- Missing or invalid routing fields produce no generated anchors.
- Idle, selection, placement, via, zone, footprint, schematic, or unknown tool states produce no generated anchors.
- Existing non-tool-state anchors must remain unchanged.
- Duplicate generated anchor ids must not appear in the final snapshot.

## Test Requirements

Add `qa/tests/common/test_ai_context_anchor_provider.cpp` and register it in `qa/tests/common/CMakeLists.txt`.

1. `RoutingToolStateAddsDynamicRouteAnchors`
   - Build a PCB snapshot with one existing pad anchor.
   - Attach routing tool state with `net`, `layer`, `width`, `start`, and `cursor`.
   - Call `AppendAiToolStateAnchors()`.
   - Verify the existing pad anchor remains.
   - Verify start, current-end, horizontal orthogonal, vertical orthogonal, and valid horizontal 45-degree anchors exist with expected positions and kinds.
   - Verify generated details JSON contains `source`, `mode`, `net`, `layer`, `width`, `start`, `target`, and `position`.

2. `RoutingToolStateUsesCurrentEndWhenCursorMissing`
   - Use mode context with `current_end` but no `cursor`.
   - Verify `tool.routing.current_end` uses `current_end`.

3. `RoutingToolStateFallsBackToToolCursorPosition`
   - Use mode context without `cursor` or `current_end`, set `m_HasCursorBoardPosition`.
   - Verify `tool.routing.current_end` uses `m_CursorBoardPosition`.

4. `InactiveOrIncompleteToolStateAddsNoAnchors`
   - Verify selecting mode, schematic editor kind, malformed JSON, missing width, and identical start/target do not add tool-state anchors.
   - Verify the existing pad anchor remains in every case.

## Verification Requirements

- Run red by building `qa_common` and running `AiContextAnchorProvider` before production implementation.
- Run green by building `qa_common` and running `AiContextAnchorProvider`.
- Run `AiNativeTypes`, `AiContextIndex`, and `AiSemanticToolCallHandler` because the anchor contract and route-to-anchor tool consume the generated anchors.
- Build `pcbnew`.
- Run whitespace and secret scans before committing implementation.

## Self-Review

- Spec coverage: This slice turns active routing tool state into model-visible anchors without changing board geometry, router behavior, or accepted edit flow.
- Source alignment: It reuses `AI_TOOL_STATE_SNAPSHOT`, `AI_CONTEXT_SNAPSHOT::m_Anchors`, and existing anchor kinds instead of adding a second context channel.
- Safety check: The helper mutates only an in-memory snapshot and does not touch the board or undo stack.
- Scope check: Route search, target inference, visual overlays, and new model tools remain separate implementation slices.
