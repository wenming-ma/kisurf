# KiSurf AI Native Session Runtime Spec

## Status

Accepted for current KiSurf AI Native development. This replaces the old
`script_run_operation_bundle`, `pcb_fill_via_matrix`, and thin preview-session
direction. Do not keep compatibility shims unless the user explicitly asks for
compatibility.

## Research Basis

This spec is based on the six deep research reports in `docs/` and the
user-approved replacement plan. The reports converge on one architecture:
Python may be the primary authoring/runtime surface, but C++ KiSurf session
core must own board truth, typed mutations, preview truth, validation,
rollback, journal replay, and final accept.

The durable boundary is not Python itself. The durable boundary is:

```text
Python SDK / Agent cells
        -> KiSurf session service
        -> typed atomic operation journal
        -> shadow board executor
        -> native KIGFX preview manager
        -> accept replay through one BOARD_COMMIT
```

## Decisions

- The first implementation target is PCB editor only.
- Schematic uses the same architecture later, but is not part of the first
  runtime slice.
- Python is the first exposed script language.
- Python must not receive raw `BOARD*`, raw `BOARD_ITEM*`, mutable KiCad IPC
  objects, or any authority that bypasses typed KiSurf session operations.
- All board mutations must go through typed atomic operations owned by C++.
- Shadow board is semantic truth during a session.
- Session journal is replay/audit/checkpoint truth.
- KIGFX preview scene is visual truth.
- Accept is a core-owned promotion, not a Python-owned write.
- Stale accept is rejected by default. Rebase is a later feature.
- Only one active AI execution session is allowed per board tab in the MVP.
- No legacy compatibility: old model-facing composite/script tools are removed.

## Non-Goals For The First Slice

- No arbitrary in-process embedded Python.
- No direct mutation of live board during preview.
- No full schematic runtime.
- No automatic rebase after live-board divergence.
- No model-facing bespoke tool for every composite action.
- No node graph or WASM runtime as a primary first implementation.

## Model-Facing Tool Surface

The model should see session-level tools and bounded observation tools, not an
ever-growing list of composite PCB commands.

Primary session/control tools:

- `kisurf_open_session`
- `kisurf_close_session`
- `kisurf_run_cell`
- `kisurf_begin_step`
- `kisurf_end_step`
- `kisurf_checkpoint`
- `kisurf_rollback_to`
- `kisurf_cancel_session`
- `kisurf_reject_session`
- `kisurf_accept_session`
- `kisurf_observe_step`
- `kisurf_query_board_summary`
- `kisurf_query_items`
- `kisurf_render_preview`
- `kisurf_run_validation`

Direct model-facing PCB mutation or preview-composite tools are not the
composition mechanism. Composite helpers such as via rings, fanout, stitching,
annular zones, or serpentine routing belong in the Python SDK/composite library
and must lower into atomic operations.

Removed model-facing tools:

- `script_run_operation_bundle`
- `pcb_fill_via_matrix`
- `pcb_create_via`
- `pcb_create_track_segment`
- `pcb_create_zone`
- `pcb_create_shape`
- `pcb_move_objects`
- `kisurf_preview_*`

## Atomic Operation Vocabulary

### Session And Control

- `OpenSession(board_id, base_hash, editor_context)`
- `CloseSession(session_id)`
- `RunCell(cell_text, cell_id)`
- `BeginStep(label, options)`
- `EndStep(step_id)`
- `Checkpoint(name)`
- `RollbackTo(checkpoint_id)`
- `CancelSession(reason)`
- `RejectSession()`
- `AcceptSession()`

### Query And Observation

- `QueryBoardSummary()`
- `QueryItems(filter)`
- `QueryItem(handle_or_id)`
- `QuerySelection()`
- `QueryNets()`
- `QueryLayers()`
- `QueryDesignRules()`
- `QueryViewport()`
- `QueryActivityTimeline()`
- `RenderPreview(region, layer_mask, mode)`
- `ObserveStep(step_id)`

### PCB Mutation

- `CreateVia(position, net, diameter, drill, layer_pair, alias, metadata)`
- `CreateTrackSegment(start, end, layer, net, width, alias, metadata)`
- `CreateTrackPolyline(points, layer, net, width, alias)`
- `CreateZone(outline, layer_set, net, clearance, priority, fill_mode, alias, metadata)`
- `CreateShape(shape_type, geometry, layer, width_or_fill, alias, metadata)`
- `MoveItems(handles, delta_or_target_positions)`
- `DeleteItems(handles)`
- `UpdateItemGeometry(handle, geometry_patch)`
- `SetItemNet(handle, net)`
- `SetItemLayer(handle, layer_or_layer_set)`
- `SetItemProperties(handle, typed_props)`
- `SetMetadata(handle, key_values)`
- `RefillZones(handles_or_area, all=false)`
- `RebuildConnectivity(scope)`
- `RunValidation(scope, level)`

`CreateTrackPolyline` is a convenience op at the SDK level. The executor must
lower it into `CreateTrackSegment` records in the journal unless a future native
polyline object requires a different internal representation.

## Core Components

### `AI_EXECUTION_SESSION`

Owns session lifecycle, base hash, current epoch, active step, checkpoints,
handle namespace, journal, preview manager pointer, validation state, and final
session status.

Required behavior:

- refuses multiple open steps in one session;
- records before/after epoch for operations;
- makes handles created after rollback stale;
- rejects accept when base hash does not match;
- never mutates the live board during preview.

### `AI_SESSION_JOURNAL`

Stores ordered atomic operation records.

Every operation record must include:

- session id;
- step id;
- operation id;
- operation kind;
- typed arguments;
- resolved handles;
- created handles;
- warnings;
- validation summary when available;
- structured result JSON for maintenance/validation/refill/connectivity operations;
- structured result JSON for observation/query/render-preview operations when
  they are executed inside a step;
- before/after epoch.

The journal is the canonical replay/audit artifact. Preview items and shadow
objects are derived from it, not the other way around.

### `AI_SESSION_HANDLE`

Opaque handle exposed to Python and agents instead of raw KiCad pointers.

Handle fields:

- session id;
- handle id;
- generation;
- optional alias.

Rollback invalidates handles created after the checkpoint. Stale handles must
fail deterministically and require re-query or alias resolution.

### `AI_SHADOW_BOARD`

Owns semantic preview state. It must not rely on `BOARD` copy construction,
because upstream `BOARD` explicitly deletes copy construction and assignment.

Allowed creation strategies:

- reconstruction from serialized board/project state;
- controlled clone utility that rebuilds caches, connectivity, UUID maps, and
  design settings intentionally;
- sparse snapshots plus journal replay after the first implementation.

### `AI_PREVIEW_MANAGER`

Owns native preview lifecycle in KIGFX.

MVP can reuse `KIGFX::VIEW::AddToPreview`, but the long-term shape is:

- session-level preview group;
- optional step-level groups;
- itemized preview objects for vias/tracks/zones/shapes;
- overlay annotations for labels, warnings, arrows, and measurement hints;
- provenance metadata on preview objects: session id, step id, checkpoint id,
  op id, session handle, shadow object id, preview style, validation status.

### `AI_SESSION_PREVIEW_SERVICE`

Bridges the common session runtime to editor-native preview infrastructure.
The common layer owns only the abstract contract:

- render the current session shadow board for the active editor;
- return preview id, rendered item count, and structured preview payload;
- clear preview for a session on rollback, reject, cancel, close, or accept.

PCB editor implements this contract with
`KISURF_AI_PCB_SESSION_PREVIEW_SERVICE`. It converts semantic shadow-board
items into existing synthetic KIGFX preview objects and sends them through
`AI_PREVIEW_MANAGER`/`KISURF_AI_PCB_PREVIEW_ADAPTER`. This keeps common code
free of pcbnew types while giving `kisurf_render_preview` and Python
`session.render_preview(...)` real canvas preview behavior.

### `AI_ACCEPT_APPLIER`

Promotes accepted journal state into the live board.

Required behavior:

- verify live-board base hash;
- reject Accept before replay when the latest explicit validation result says
  `validation.accept_validation_sufficient=false`;
- open one live `BOARD_COMMIT`;
- replay typed journal into live board;
- run required pre-push validation;
- `Push()` exactly once if all staging succeeds;
- drop/revert without half-mutating the board if staging fails.

Current common-layer implementation provides the replay boundary and adapter
contract. It checks the base hash before replay, calls exactly one adapter
transaction, applies ordered journal records, aborts on adapter failure, and
marks the session accepted only after adapter commit succeeds. It also scans
the journal from the end before replay: if the most recent explicit Accept-grade
validation marker is `false`, Accept fails with
`validation_not_accept_grade` before the adapter transaction starts. A later
validation with `accept_validation_sufficient=true` clears that block; validation
records without this field do not clear a known insufficient result.

The first pcbnew adapter, `KISURF_AI_PCB_SESSION_APPLY_ADAPTER`, replays journal
records into live `BOARD` objects under one `BOARD_COMMIT`. It covers created
vias, track segments, zones, shapes, move/delete, net/layer/layer-set changes,
typed properties, handle-scoped or affected-area zone refill, and connectivity
rebuild. It can also map shadow handles reconstructed from existing live-board
items through
`live_uuid` metadata. It intentionally does not yet solve automatic rebase or
the final shadow-board-native full DRC validation ladder.

At the session layer, `AI_SHADOW_BOARD::QueryItems` supports `bbox`,
`selection`, and `handle` filters for semantic observations. `handle` accepts
aliases, numeric handle ids, or handle objects with session id / handle id /
generation. `RefillZones(affected_area)` uses the same semantic query path to
resolve intersecting shadow zone handles into the journal before accept, so the
operation remains inspectable and replayable instead of becoming a live-board
only side effect.

`QuerySelection()` returns both selection sources visible to an agent. The
editor context side is reported as `selected_objects` /
`selected_context_count`. The session side is resolved from
`QueryItems({"selection": true})` and reported as `selected_shadow_items`,
`selected_handles`, and `selected_shadow_count`. `selected_count` is the sum, so
chat and preview agents can see what the user currently selected while still
receiving typed session handles they can pass to follow-up atomic operations.
The response also includes `selection_revision` with the session-open revision,
the current request revision, a `changed` flag, a `conflict` flag, and the
policy string `selection_handles_are_pinned_to_session_open`, making mid-session
live selection changes explicit to Chat Agent and Background Preview Agent
callers. Accept also checks the same policy: if the live selection revision has
changed while the board base hash still matches, `kisurf_accept_session` refuses
with `selection_conflict` and leaves the session open for re-observation,
rollback, or rejection. Pure view revision changes do not block accept.
`kisurf_run_cell` applies the same guard before invoking Python: when the live
selection revision has changed since session open, the cell is rejected with
`selection_conflict` and the worker is not called. Query tools, rollback,
reject, and validation remain available so the agent can inspect and recover.
When `QueryItems` is called with a selection filter, the response also includes
the same `selection_revision` block. This makes selected shadow handles
self-describing: if the live selection changed after session open, the agent can
see that the returned handles are pinned to the original session selection
without having to make a separate `QuerySelection()` call.

### `AI_PYTHON_WORKER`

Per-session out-of-process Python worker.

Required behavior:

- persistent namespace across cells;
- protobuf/local IPC session service;
- request/reply control path;
- event/observation stream;
- separate cancel/control path;
- soft cancel and hard kill;
- no raw board access;
- SDK calls create typed session operations.

The C++ integration point is the durable interface: `kisurf_run_cell` creates
an `AI_PYTHON_CELL_REQUEST`, sends it to an `AI_PYTHON_WORKER`, receives typed
operation requests, and applies them through `AI_ATOMIC_OPERATION_EXECUTOR`.
The subprocess/local IPC transport is an implementation of that interface, not
the architectural boundary itself.

Current worker IPC uses a length-prefixed frame:
`KISURF_AI_FRAME_V1 <byte_length>\n<payload>`. The payload is the strict
`kisurf.ai.session.v1` protobuf contract in `api/proto/ai/session.proto`.
The current messages are `WorkerRequest` and `WorkerResponse`; operation
arguments and event payloads remain JSON strings inside typed protobuf fields
so the C++ atomic executor can keep using the existing typed JSON argument
validators while the cross-process boundary is versioned protobuf. A successful
cell response must include:

- `protocol: "kisurf.ai.session.v1"`;
- `cell_result`;
- `session.id`;
- `session.board_id`;
- `session.base_hash`;
- `session.epoch`;
- `operations.kind`;
- `operations.arguments_json`.

The C++ handler rejects any successful or mutating response whose session id,
board id, base hash, or epoch does not match the request that launched the
cell. This prevents stale/cross-session Python output from entering the shadow
board or journal.

The framed transport also defines `cancel_session`. In the Windows local worker
the main request/reply path uses stdio pipes, while cancellation uses an
independent inherited control pipe so `AI_PYTHON_LOCAL_WORKER::Cancel()` can
reach the Python worker while a cell is executing. The Python control thread
clears the matching per-session namespace and tries to interrupt the executing
cell with `KeyboardInterrupt`. Because CPython cannot reliably interrupt every
blocking/native call such as `time.sleep()`, C++ applies a short cancel grace
period and then terminates the subprocess as a hard fallback. In both cases the
running cell is reported as `python_cancelled`, the failed cell does not enter
the shadow board or journal, and the next cell starts a fresh worker if the
fallback killed the old subprocess.

The Windows local worker also has an independent inherited event pipe. When
Python code calls `session.event(...)`, the SDK still stores the event in the
final `cell_result.events[]`, but the worker immediately writes a framed
protobuf `WorkerEvent` to the event pipe. `AI_PYTHON_LOCAL_WORKER` reads those
frames on a background thread and forwards them to an `AI_PYTHON_EVENT_SINK`,
so Chat/preview orchestration can observe progress and inspection events while
the cell is still executing.

## Current Implementation Slice

Implemented in the current C++ common layer:

- `AI_EXECUTION_SESSION` owns lifecycle, journal, handles, checkpoints, epoch,
  and a semantic `AI_SHADOW_BOARD`.
- `AI_SHADOW_BOARD` stores session handles, semantic item type, net/layer or
  layer-set,
  geometry JSON, properties, metadata, checkpoint snapshots, query summary,
  filtered item queries, move/delete/set metadata/set net/set layer/set props.
  The board summary includes seeded footprint and pad counts in addition to
  vias, track segments, zones, and drawing shapes.
  Semantic move operations translate position/start/end points, point rings,
  annular-zone outer/inner rings, holes, and nested outlines without moving
  non-coordinate fields such as pad size.
- `AI_ATOMIC_OPERATION_EXECUTOR` executes typed PCB atomic ops against the
  shadow board and journal. `CreateTrackPolyline` lowers into ordered
  `CreateTrackSegment` records. Created shadow-board items retain typed
  properties from their atomic arguments, such as via drill/diameter, track
  width, zone clearance/priority/fill mode, and shape fill/width. `SetItemProperties`
  is a typed property patch and merges into the existing semantic properties
  rather than replacing unrelated fields. `CreateZone` rejects malformed or
  negative clearance, negative priority, and unknown fill modes before creating
  handles or appending journal records; zone typed-property patches use the same
  pre-journal validation.
- `AI_PREVIEW_MANAGER` replaces the old thin preview session class and tracks
  preview provenance. It now records lightweight `AI_PREVIEW_ITEM` entries for
  shown objects and operations, carrying preview id, item kind, label, and
  provenance JSON. It also records `AI_PREVIEW_ITEM_OVERLAY` entries for
  itemized labels/warnings/validation messages, including optional geometry
  JSON and layer metadata, and exposes the current `AI_PREVIEW_BATCH_ITEM`
  snapshot for debugging, inspection, and editor-native overlay drawing.
- `AI_SESSION_PREVIEW_SERVICE` is the common preview bridge used by
  `kisurf_render_preview` and Python-emitted `session.render_preview(...)`.
  The pcbnew implementation renders shadow-board vias, track segments, zones,
  and shapes into the active KIGFX preview scene, then clears them by session
  id on reject/cancel/close/accept. The session handler records preview state
  at checkpoints; rollback clears the current preview and restores the
  checkpoint preview by re-rendering it against the rolled-back shadow board.
- `AI_ACCEPT_APPLIER` defines the one-transaction journal replay contract
  against an adapter. Observation/query/render-preview records remain in the
  journal for audit, but accept replay sends only mutation/maintenance records
  to the live-board adapter.
- `KISURF_AI_PCB_SESSION_APPLY_ADAPTER` wires the PCB editor Accept path to
  live `BOARD_COMMIT` replay for the first supported atomic mutation and
  maintenance set, including zone `layer_set` changes, native zone refill by
  explicit handles, affected area, or all zones, and connectivity rebuild.
  Created zones replay local clearance, assigned priority, and fill mode from
  their typed `CreateZone` arguments. `SetItemProperties` replays via/track/
  zone/shape typed properties through native KiCad setters, so property changes
  do not share the geometry patch path. Zone `UpdateItemGeometry` replaces the
  live outline and holes from the typed geometry patch, so scripts can inspect
  a generated zone, revise its outline, and accept the corrected result.
- `KISURF_AI_PCB_SESSION_SHADOW_SEEDER` reconstructs the live PCB board into
  shadow-board items for tracks, vias, zones, drawing shapes, footprints, and
  pads without using `BOARD` copy construction. It records `live_uuid` metadata
  so accept replay can apply journal mutations back to existing live items, and
  records `selected=true` metadata for selected live board items so session
  queries can target the user's current PCB selection. Pad items also carry
  footprint reference/UUID and pad number metadata, giving agents typed handles
  for the engineer's selected footprint pads instead of raw `PAD*` pointers.
  When a parent footprint is selected, its child pads are also exposed through
  `QueryItems({"selection": true})` with `selection_inherited_from` metadata so
  agents can operate on selected component pads without direct `FOOTPRINT*` or
  `PAD*` access.
- `AI_SESSION_VALIDATION_SERVICE` is the common validation bridge used by
  `kisurf_run_validation` and Python-emitted `session.run_validation(...)`.
  The pcbnew implementation, `KISURF_AI_PCB_SESSION_VALIDATION_SERVICE`, calls
  the editor-native `DRC_ENGINE` for `drc_lite` and `full_drc`, serializes DRC
  issue metadata into the operation result, and is wired into the PCB editor
  Agent Panel session handler. This gives Chat Agent sessions a native DRC
  feedback path while keeping common session code independent of pcbnew types.
- `AI_PYTHON_WORKER` is introduced as the `RunCell` bridge. When a connected
  worker returns typed operation requests, `kisurf_run_cell` creates a
  checkpoint, lowers mutation requests through `AI_ATOMIC_OPERATION_EXECUTOR`,
  records them in the journal/shadow board, executes non-mutating query,
  observe, and render-preview requests at their sequence point, records those
  observation results in the journal when a step is open, and returns both the
  step observation and per-operation `operation_results`. If the
  worker reports failure, an atomic op fails, or an observation request is
  malformed, the handler rolls back to the pre-cell checkpoint and leaves the
  live board untouched.
- When a Python cell creates or changes shadow-board content but does not
  explicitly request validation or preview, `kisurf_run_cell` now appends
  automatic step feedback before closing the step: geometry validation through
  the connected editor validation service and native preview rendering through
  the connected preview service. These automatic feedback records are journaled
  with the same step id as the mutation, returned as `step_results`, and skipped
  when the cell already emitted `session.run_validation(...)` or
  `session.render_preview(...)` explicitly.
- `kisurf_query_activity_timeline` and Python-emitted
  `session.query_activity_timeline()` return both recent editor activity from
  the current context snapshot and a lightweight summary of the current
  session journal operations observed so far in the step.
- `AI_PYTHON_WORKER_PROTOCOL` defines the current protobuf message shape for
  request/response transport. It requires protocol, response oneof, and session
  context on cell results, then maps protobuf `OperationKind` values such as
  `PCB_CREATE_VIA`, `SESSION_CHECKPOINT`, and `SESSION_ROLLBACK_TO` into typed
  C++ session operation kinds. Cell results also carry structured
  `events[]` records with kind, message, and JSON payload so scripts can report
  progress/inspection signals without encoding those signals as PCB mutations.
  The same protobuf `WorkerEvent` payload is also decodable as an independent
  streaming event frame.
- `AI_PYTHON_LOCAL_WORKER` runs the Python SDK through a persistent local
  Python subprocess. On Windows it uses native stdin/stdout/stderr pipes and a
  length-prefixed protobuf frame protocol for request/reply, a separate
  inherited control pipe for cancellation, and a separate inherited event pipe
  for live `session.event(...)` streaming. Python namespaces are keyed by
  session id so iterative cells can define variables/functions, observe the
  result, revise, and continue. The local worker has a configurable response
  timeout. If a cell exceeds that timeout, C++ hard-kills the subprocess,
  returns `python_worker_timeout`, and starts a fresh subprocess on the next
  cell; the failed cell never enters the shadow board or journal. The Windows
  stdout reader uses a blocking read thread with timeout cancellation, rather
  than `PeekNamedPipe` polling, so larger framed responses from composite
  scripts are read reliably. Cancel sends `cancel_session` over the control
  pipe, lets Python raise `KeyboardInterrupt` in the executing cell when
  possible, and uses process termination as the fallback for blocking/native
  calls that cannot be cooperatively interrupted. Cancellation is reported as
  `python_cancelled`, and the worker is reusable after either the soft path or
  the fallback restart. Streamed worker events are also retained in the final
  cell result for audit/replay visibility. The protobuf frame transport remains
  behind the `AI_PYTHON_WORKER` interface so the carrier can later move from
  stdio pipes to NNG/local sockets without changing the session API.
- `common/kisurf/ai/python/kisurf_ai` provides the first Python SDK surface:
  `session.step(...)`, atomic helpers such as `create_via(...)` and
  `create_track_polyline(...)`, mutation helpers, validation/refill requests,
  query/observe/render-preview requests, checkpoint/rollback control requests,
  structured-surface helpers such as `apply_surface_patch(...)`, and composite
  helpers such as `create_via_ring(...)` that lower into atomic operations
  rather than becoming model-facing tools. `apply_surface_patch(...)` accepts
  the same guard metadata used by the C++ runtime, including surface revision,
  schema version, selection fingerprint, overlap set, and explicit
  `write_policy` when the script needs to request a non-default overwrite
  mode.
- `common/kisurf/ai/python/pyproject.toml` makes the Python SDK installable as
  `kisurf-ai-session-sdk` and exposes `kisurf-ai-worker` as a console script
  entry point. The development build uses the source-tree SDK path when it
  exists for fast iteration; packaged builds fall back to
  `PATHS::GetStockScriptingPath()/kisurf_ai_session_sdk`, which is populated by
  CMake install rules. The SDK exports `__version__` and `PROTOCOL_VERSION`,
  and every Python cell result includes `sdk.name`, `sdk.version`, and
  `sdk.protocol`; C++ decodes and returns this metadata through
  `kisurf_run_cell` so agents can audit which local runtime executed a script.
- Python-emitted `session.checkpoint(name)` creates a C++ session checkpoint at
  its sequence point and returns the authoritative checkpoint id in
  `operation_results`. Python-emitted `session.rollback_to(checkpoint_id)`
  rolls the C++ session back to that checkpoint, truncating journal/shadow-board
  state and restoring the checkpoint preview state. These calls are not Python
  heap time travel; scripts must use checkpoint ids returned by earlier
  operation/tool results and re-query after rollback.
- `kisurf_run_validation` and Python-emitted `session.run_validation(...)`
  record a session validation operation and return structured validation
  output in `operation_results`. Supported validation levels are `geometry`,
  `drc_lite`, and `full_drc`; unknown levels are rejected before journal
  append. The common geometry validator reports malformed item geometry and
  zero-length track/shape segments from the semantic shadow board. When an
  editor validation service is present, `drc_lite` and `full_drc` are upgraded
  to editor-native validation. In pcbnew this currently routes through
  `DRC_ENGINE` and stores the returned issue list, status, backend name, and
  warnings on both the tool payload and the corresponding journal operation.
  Without an editor validation service, the common layer still returns the
  semantic geometry result plus a structured warning, so the session API shape
  remains stable across editors. For `drc_lite` and `full_drc`, that fallback
  payload sets `validation.validated_state=semantic_shadow_board`,
  `validation.preview_state_exact=true`,
  `validation.accept_validation_sufficient=false`, and
  `validation.accept_validation_reason=native_validation_service_not_connected`;
  Accept therefore cannot mistake a common-layer warning-only DRC request for
  final native board validation.
- Native pcbnew DRC payloads explicitly report their validation scope. For
  `drc_lite` and `full_drc`, the validation service runs on the live board when
  there are no unaccepted session mutations. When the session journal contains
  unaccepted mutations, it serializes the live board through KiCad's
  S-expression board IO, reconstructs a temporary preview board, replays the
  session journal into that board, and runs `DRC_ENGINE` there. The payload sets
  `validation.validated_state` to `live_board` or `preview_board`,
  `validation.preview_state_exact=true`,
  `validation.session_mutation_count` to the replayed mutation count,
  `validation.accept_validation_sufficient=true`, and
  `validation.accept_validation_reason=native_drc_matches_preview_state` when
  replay and native DRC complete. If preview-board reconstruction, replay, or
  native DRC execution fails, the payload instead reports
  `accept_validation_sufficient=false` with
  `accept_validation_reason=preview_board_native_drc_failed` or
  `native_drc_failed`, so Accept remains blocked rather than falling back to an
  inexact or failed DRC result.
- Validation issues that identify a session `handle`, `alias`, or live-board
  UUID are projected back onto shadow item metadata as `validation_status`,
  `validation_message`, `validation_issue_code`, optional
  `validation_geometry`, and optional `validation_layer`. Re-running validation
  clears stale validation metadata before applying the current issue set, so a
  follow-up render preview reflects the latest validation state.
- Accept replay records whether the adapter staged real board changes.
  Validation-only or record-only sessions may be accepted and closed with
  `board_mutated=false`; mutating PCB sessions still go through one
  `BOARD_COMMIT::Push()` when the adapter has staged changes.
- The Chat Agent dispatcher now constructs the session handler with the
  default local Python worker and the PCB session preview service when the PCB
  editor has an active board/view, so UI-triggered `kisurf_run_cell` can
  execute Python SDK cells and render native preview in the development build
  when `python` is on `PATH`.
- Session base hash selection now prefers an editor-provided `base_hash`,
  `board_hash`, or `board_content_hash` from the current context payload before
  falling back to the revision tuple. The PCB editor context now provides a
  sorted content hash over footprints, tracks/vias, zones, and drawings.
- Model-facing legacy `script_run_operation_bundle` and `pcb_fill_via_matrix`
  are removed.
- Session tools include open/close/run-cell/begin/end/observe/checkpoint/
  rollback/accept/reject/cancel/query summary/query items/render preview/run
  validation.
- `AI_SHADOW_BOARD::QueryItems` supports selection-scoped filtering via
  `{"selection": true}` over the selected metadata seeded from pcbnew or set by
  controlled session metadata operations.
- `AI_SHADOW_BOARD::QueryItems` supports handle-scoped filtering via
  `{"handle": ...}` where the handle may be an alias, numeric handle id, or
  typed handle object.
- Session item query results include `properties` alongside geometry and
  metadata, so Agents can inspect typed zone/via/track/shape settings before
  Accept. The properties reflect merged typed patches, preserving earlier
  atomic arguments that were not modified by the patch.
- Python SDK runtime distribution is defined: source-tree SDK for development,
  installed `kisurf_ai_session_sdk` under stock scripting data for packaged
  builds.
- `AI_SESSION_TOOL_CALL_HANDLER` consumes live `AI_PYTHON_EVENT_SINK` events
  during `RunCell`, de-duplicates matching final cell-result events, returns the
  per-cell `recorded_events` delta, and exposes the accumulated `python_events`
  stream through `QueryActivityTimeline()`.
- `AI_RUNTIME` projects `recorded_events` from a `kisurf_run_cell` tool result
  into separate activity records. Those records keep the originating tool call
  id, use action names such as `kisurf_run_cell.progress` or
  `kisurf_run_cell.inspection`, preserve the event JSON payload, and enter the
  same activity log used by Agent Panel observability and the next model
  context snapshot. This is the default presentation-independent path for
  script progress and intermediate inspection signals.
- pcbnew preview overlays are drawn on canvas for session items with
  `validation_status`/`validation_message` metadata. The common preview manager
  records `AI_PREVIEW_ITEM_OVERLAY` entries with optional `validation_geometry`,
  `validation_position`, and `validation_layer` bindings, the PCB adapter draws
  geometry-bound overlay markers before falling back to label-matched preview
  items, and the session preview service reports `rendered_overlay_count`.
  Geometry overlays support position/center, explicit segment, and path/points
  forms. Error overlays render with a stronger width and an additional
  cross-marker segment, so severe DRC feedback is visually distinct from
  warning/info overlays even before native DRC marker visuals are connected.

Not implemented yet:

- Product-level presentation policy for live Python worker events beyond the
  activity/context/observability path. Chat Agent and Background Preview Agent
  still need rules for when recorded progress/inspection events become
  transcript messages, canvas previews, notifications, or remain hidden
  telemetry;
- direct use of KiCad's full native DRC marker visuals. The MVP now supports
  severity-aware geometry-bound validation overlay markers, including segment
  and path geometry, but still draws lightweight synthetic preview shapes
  instead of embedding the DRC marker renderer directly;
- advanced selection-scoped handle enforcement beyond the current observable
  conflict report and execution/accept-time blocks. The MVP supports selected item
  metadata, parent-footprint-to-pad inheritance, `QueryItems({"selection":
  true})` for reconstructed tracks, vias, zones, shapes, footprints, and pads,
  `QuerySelection()` reports session/current selection revisions when the live
  selection changes mid-session, selection-filtered `QueryItems` responses carry
  the same revision/conflict block, `kisurf_run_cell` refuses to execute new
  Python cells under a changed selection, and accept refuses the changed
  selection with `selection_conflict`. Automatic selected-handle invalidation or
  rebase still needs a product policy;
- reconstructed-shadow-board native DRC. The current pcbnew validation service
  gives sessions a native DRC feedback path through the active board and marks
  whether that result is exact for the current session preview state. The final
  architecture still needs native DRC over a controlled reconstructed shadow
  board or equivalent preview-state board so validation reflects unaccepted
  session mutations exactly.

Automatic stale-session rebase is intentionally out of the first PCB session
runtime phase. Stale accept is rejected by base hash, matching the current
assumption that rebase should not be attempted until the shadow-board and
journal semantics are stable.

## Python SDK Contract

Python is an authoring and orchestration surface. It may do:

- variables;
- loops;
- conditionals;
- geometry calculations;
- batching;
- step context managers;
- query/observe/validate/checkpoint/rollback calls;
- composite helpers that lower into atomic ops.

Python must not:

- mutate raw KiCad board objects;
- own undo/redo semantics;
- own preview truth;
- bypass the journal;
- make live-board writes;
- keep authoritative raw pointers across rollback.

Example SDK shape:

```python
with session.step("place stitching vias") as step:
    for i in range(count):
        p = center + polar(radius, 2 * math.pi * i / count)
        step.create_via(
            position=p,
            net="GND",
            diameter=via_d,
            drill=drill_d,
            alias=f"ring_via_{i}",
        )

    session.query_board_summary()
    session.query_items(filter={"type": "via", "net": "GND"})
    session.render_preview(region=local_bbox, layer_mask=["F.Cu"], mode="native")
```

The SDK observation helpers do not synchronously mutate Python objects with
board truth. They emit typed non-mutating requests into the cell operation
stream. The C++ session handler evaluates those requests against the current
shadow board or preview manager at that sequence point and returns structured
`operation_results` to the Chat Agent/UI. A later event-stream worker can make
this interactive inside a long-running cell without changing the atomic
operation contract.

The SDK may expose composite helpers such as:

- `create_via_ring(...)`
- `create_annular_zone(...)`
- `fanout_pad(...)`
- `stitch_zone(...)`

Each helper must lower to atomic operation records and enter the journal.

For structured surfaces, Python emits `surface.apply_patch` operations through
`session.apply_surface_patch(...)`. The helper must forward guard fields and an
explicit `write_policy` without interpreting them in Python. Omitted
`write_policy` remains a runtime policy decision; explicit values such as
`allow_overwrite` are preserved in the operation journal for validation,
preview, and accept replay.

## Preview And Observation Flow

Each step should produce three synchronized observations:

- structured diff and changed handles;
- validation result and warnings;
- native preview frame or frame handle.

Default user view is native-canvas preview plus step-level semantic summary.
Atomic operation lists are audit/debug details, not the primary UI.

In the current handler, explicit observation requests made by Python are
returned in `operation_results`. Automatic close-of-step feedback generated by
the C++ session handler is returned in `step_results`, so the agent can
distinguish its own requested observations from the runtime's default
preview/validation loop.

## Rollback Semantics

Checkpoint stores:

- journal index;
- session epoch;
- handle generation/watermark;
- shadow-board snapshot or replay anchor;
- preview object mapping;
- validation cache generation.

Rollback does:

- restore shadow board to checkpoint epoch;
- truncate/mark journal records after checkpoint;
- stale handles created after checkpoint;
- restore preview scene for that checkpoint;
- leave live board untouched.

Rollback does not promise Python heap time travel. After rollback, Python code
must re-query or use valid aliases/handles.

## Validation Ladder

Validation is layered:

1. typed argument validation;
2. semantic validation: net/layer/rule existence;
3. geometry validation: degeneracy, bbox, self-intersection, budget limits;
4. incremental connectivity/ratsnest update;
5. selective zone refill and DRC-lite;
6. full DRC on demand or before high-risk accept.

Zone refill is a controlled validation phase, not an always-on side effect for
every tiny step.

## Chat Agent And Background Preview Agent

Both agents share the same low-level capabilities but have different workflow
policies.

Chat Agent:

- user-directed;
- can open execution sessions;
- runs cells/steps;
- presents previews and requires explicit Accept.

Background Preview Agent:

- autonomous/read-only by default in the first phase;
- observes user activity and proposes lightweight previews;
- stores background-triggered suggestions as `preview-only` records and strips
  edit objects before they reach Accept controls;
- cannot apply `AI_EDIT_SESSION` mutations from autonomous suggestions;
- does not get unrestricted mutation sessions until session core is proven;
- may later open constrained preview sessions with strict budgets.

## Implementation Phases

1. Commit ADR/spec and no-compatibility policy.
2. Add session/journal/handle/checkpoint/observation core and tests.
3. Remove old model-facing composite/script tools.
4. Add session tool handler and route Chat Agent through session tools.
5. Implement PCB atomic operation executor for via, track, zone, shape, move,
   delete, set props, and query.
6. Implement shadow board creation, epoch snapshots, checkpoint rollback, and
   stale handle detection against real shadow objects.
7. Implement `AI_PREVIEW_MANAGER` and migrate synthetic previews into native
   provenance-aware preview lifecycle.
8. Implement accept replay into one `BOARD_COMMIT::Push()`.
9. Introduce the `AI_PYTHON_WORKER` bridge and connect `RunCell` to typed
   atomic-op lowering with rollback on worker/op failure.
10. Implement concrete Python subprocess, protobuf/local IPC, session service,
    cell-result events, dedicated worker event stream, cancel/hard kill, and
    Python SDK.
11. Move Chat Agent complex editing to `RunCell` / `BeginStep`.
12. Keep Background Preview Agent read-only/lightweight until session core is
    proven.

## Test Plan

- Journal append/replay/grouping/epoch/checkpoint/rollback.
- Handles created handle, alias resolve, stale after rollback.
- Provider tool surface exposes session tools and not legacy tools.
- Session tool handler lifecycle: open/run cell/begin/end/checkpoint/rollback.
- Atomic ops validate parameters and mutate only shadow board.
- Preview shows after step, restores after rollback, clears on reject.
- Accept leaves live board unchanged before accept and creates one undo step
  after accept.
- Python worker run cell, step context manager, exception rollback, timeout,
  timeout-triggered hard kill, restart after timeout, cancel, hard kill.
- Background Preview Agent suggestions are preview-only: generated edit objects
  are stripped, preview remains available, and Accept stays disabled until a
  later constrained session-backed flow exists.
- Integration: Python creates annular zone plus via ring, verifies the lowered
  `CreateZone` + `CreateVia` records in the session journal and shadow board,
  renders preview, rolls back a bad spacing attempt, reruns corrected script,
  accepts, then native undo reverts the entire AI edit.
- GUI smoke: launch PCB editor with Computer Use, verify no error dialogs and
  verify canvas preview is visible.
