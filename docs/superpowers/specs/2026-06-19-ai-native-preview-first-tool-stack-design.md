# AI-Native Preview-First Tool Stack Design

## Context

KiSurf now has early pieces of an AI-native layer: model configuration, chat UI,
tool-call dispatch, workspace snapshots, visual snapshots, panel-state records,
preview sessions, edit sessions, route/zone/panel preview suggestions, and
background suggestion providers. The next layer should not be more UI polish.
The foundation must be a shared tool architecture that both upper-level agents
can use.

There are two upper-level workflows:

1. Chat Agent: the user instructs the system through the chat panel. The agent
   plans and calls tools to produce a reviewable preview.
2. G2R Preview Agent: a 7x24 background agent, when enabled, observes the user's
   current work state and autonomously proposes the next action as a live
   preview on the editor canvas.

The two workflows may share context, atomic board operations, composite tools,
script execution, logs, validation, preview rendering, and accept/reject
infrastructure. They are not the same workflow. The chat panel is a command
surface. The preview appears in the PCB or schematic working surface.

## Core Principle

All user-visible AI actions are preview-first.

- Tool calls may create a preview suggestion.
- Preview suggestions render in the editor workspace, not inside the chat panel.
- No board or schematic mutation is final until the user accepts it.
- Chat workflow Accept is explicit through UI.
- Background workflow Accept can be a fast editor gesture such as Tab; Esc or
  clicking elsewhere cancels the active preview.
- Every accepted action must be replayable from the logged tool/script inputs.

## Layers

### Layer 0: Read Context

These are read-only interfaces shared by both agents.

- Workspace view: one entry point returning visual content, semantic board or
  schematic objects, anchors, action catalog, activity, panel state, and chat
  panel state when requested.
- Visual frame: pixels from the editor's rendered working view, with options for
  layers, net highlighting, anchors, crop, scale, and overlays.
- Semantic context: board/schematic objects, selected items, visible objects,
  anchors, tool state, dynamic mode, recent activity, and panel records.
- Panel state: semantic table/control state for side panels and dialogs, including
  focused cell, selected text, row/column ids, and parsed state JSON.

Layer 0 never mutates documents and can be called freely by both agents.

### Layer 1: Atomic Operations

Atomic operations are the smallest stable editor operations that can be previewed
and later committed. They correspond to user-capable operations, but they are
structured APIs rather than raw mouse replay.

PCB examples:

- Create via.
- Create track segment.
- Create shape.
- Create copper zone.
- Move object.
- Delete object.
- Place or move footprint.
- Set layer.
- Select objects.
- Set panel cell value.

Schematic examples:

- Place symbol.
- Create wire segment.
- Move symbol or label.
- Edit property.
- Set panel cell value.

Each atomic operation has:

- typed arguments;
- precondition validation;
- preview renderer;
- accept implementation;
- reject/cancel behavior;
- undo/redo integration;
- deterministic log payload.

### Layer 2: Composite Tools

Composite tools combine atomic operations into common editor tasks while still
returning a preview suggestion.

Examples:

- Route to semantic anchor.
- Place via at anchor.
- Fill panel column cells.
- Move selected group by vector.
- Create rectangle around selected items.
- Extend copper zone outline.
- Place footprint at recommended anchor.
- Generate next row or column in a via matrix.

Composite tools must explain which atomic operations they would stage. They do
not directly commit.

### Layer 3: Script Layer

The script layer lets agents compose tool calls for open-ended tasks such as
creating a 10x10 via array. It is similar in spirit to KiCad's scriptable IPC and
Python workflows, but it must run inside KiSurf's preview-first policy.

A script run is a transaction-like preview bundle:

- The script can call read-context tools.
- The script can call atomic and composite tools.
- The script returns a staged preview bundle with one or more operations.
- Accept commits the bundle in one undoable transaction.
- Reject discards all preview items.
- Logs store script source, tool calls, validations, and final outcome.

Scripts must be bounded by operation count, time, editor kind, and allowed
capabilities.

## Preview Suggestions

A preview suggestion is the shared bridge between both agents and the editor.

Required fields:

- id and sequence;
- source workflow: chat or background;
- editor kind: PCB or schematic;
- context version;
- title/body for human review;
- operation bundle;
- preview objects or operation-only preview directives;
- validation summary;
- accept policy;
- reject policy;
- fingerprint for duplicate suppression;
- log links to model input and tool calls.

The chat panel can list suggestions and offer Accept/Reject controls, but it
does not own canvas preview state. The editor integration owns preview rendering,
Tab/Esc handling, and cleanup.

## Dynamic Contexts

The system should model at least these dynamic contexts:

- Routing: active route point, current net, current layer, width, route anchors,
  legal orthogonal or 45-degree candidate anchors, highlighted same-net objects.
- Layout: current footprint or item being placed, placement anchors, collision or
  clearance hints, alignment points, nearby related objects.
- Shape and zone editing: active outline, candidate expansion points, copied
  shape suggestions, zone adjustment anchors.
- Matrix pattern: detected via/pad/shape rows and columns, inferred pitch,
  missing candidate cells.
- General selection: selected objects, likely next actions, property panels, and
  safe object-level actions.
- Panel/table editing: focused panel, focused cell, table rows/columns, repeated
  values, empty cells, and column-fill candidates.

Dynamic contexts are input to the background agent and are also available to the
chat agent when the user asks for an operation.

## Anchors

Anchors are semantic editor positions that let models choose meaningful points
without pixel-level reasoning.

Anchor categories:

- object anchors: pad centers, via centers, footprint bounding corners, shape
  corners, route segment endpoints;
- tool anchors: current route start, cursor candidate, placement cursor;
- derived route anchors: horizontal/vertical/45-degree intersections, dogleg
  points, midpoint cross points, same-net target projections;
- pattern anchors: matrix next cell, row extension, column extension, missing
  grid point;
- panel anchors: table cell id, row id, column id, focused control id.

Anchor generation must be deterministic and explainable. It should consider
active tool mode, selected objects, visible objects, current net/layer, board
constraints, and recent user actions. Anchors can be rendered as temporary
overlays in visual snapshots and canvas previews.

## Visual Frame Requirements

The single workspace view must make visual content flexible enough for both
agents:

- choose editor surface: PCB, schematic, panel, or combined workspace;
- choose layers or layer sets;
- highlight current net and active layer;
- include or omit anchors;
- include anchor labels or numeric ids;
- crop around selection, cursor, route start, or target anchors;
- choose rendered scale and maximum bytes;
- report when pixels are unavailable and fall back to semantic context.

For routing, the default visual frame should highlight the active net and active
layer while preserving enough surrounding context to judge legal route choices.

## Interaction Rules

Chat workflow:

- User asks for a task in chat.
- Chat Agent calls read, atomic, composite, or script tools.
- Result appears as a preview in the editor workspace.
- User clicks Accept or Reject in the chat/review surface.

Background workflow:

- User works in the editor.
- Context observer detects tool state and activity changes.
- Background agent proposes a next action preview.
- Preview appears directly in the workspace.
- Tab or an explicit confirm gesture accepts and requests the next suggestion.
- Esc or clicking elsewhere rejects/cancels and clears the preview.

Both workflows use the same accept/reject core.

## Logging

Every agent turn and preview lifecycle must be observable:

- model input;
- visual/context snapshot references;
- tool calls and arguments;
- tool results;
- generated preview suggestion;
- preview render result;
- user accept/reject/cancel event;
- edit commit result;
- failure reason.

Logs should be exposed to the chat panel and stored in structured records for
debugging and replay.

## Implementation Strategy

Start from the bottom:

1. Define a typed operation bundle model for atomic operations.
2. Normalize preview and edit adapters around that bundle model.
3. Add a registry of atomic operation descriptors.
4. Implement a small set of PCB atomic operations: via, track segment, shape,
   zone, move selected.
5. Add composite tools on top: route to anchor, via matrix candidate, panel fill.
6. Add script execution for bounded operation bundles.
7. Route both Chat Agent and G2R Preview Agent through the same preview-first
   suggestion pipeline.
8. Add Tab/Esc editor handling for background previews.
9. Expand single workspace view options for visual layers, anchors, panels, and
   active dynamic context.

## Testing Requirements

- Unit tests for each atomic operation parser, validation, preview output, accept
  commit, reject cleanup, and logs.
- Integration tests for chat-created preview suggestions.
- Integration tests for background-generated preview suggestions.
- GUI smoke tests with Computer Use after build, including opening PCB editor,
  checking for runtime popups, and exercising at least one preview/accept path
  when test fixtures allow it.
- Regression tests for workspace view payloads, panel state, visual snapshot
  metadata, and anchor generation.

## Self-Review

- Marker scan: no incomplete marker text remains.
- Scope check: this spec defines foundation and phases; it does not prescribe UI
  beautification work.
- Ambiguity check: chat and background agents are separate workflows with shared
  lower-level preview-first tooling.
