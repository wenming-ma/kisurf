# KiSurf Next Action Source Spike - 2026-06-22

This source spike turns the recent Next Action research into source-backed
implementation decisions. It is intentionally narrower than the consolidated
research report: it asks where the current code can support the runtime today,
where the current code is still only a proving ground, and what the next
implementation slice should touch first.

## 1. Source-Backed Conclusion

The high-level Next Action architecture is already present in the tree, but it
is not yet production-grade for routing, placement, or auto-fill/refill.

The correct next step is not to add more old-style suggestion providers. The
next step is to harden the runtime substrate and then add work-state-specific
observation and tool adapters under the LLM-mediated loop.

Implementation order should be:

1. Harden `AI_NEXT_ACTION_RUNTIME` so hidden attempts really execute through
   session atomic ops, validation uses the PCB session validation service, and
   accept checks a current context fingerprint.
2. Wire routing active-state first, because the source already exposes useful
   router state: route in progress, current start, current end, net, layer, and
   widths.
3. Add placement active-state instrumentation, because active placement items
   are currently local to `PCB_TOOL_BASE::doInteractiveItemPlacement()` and are
   not fully visible to the AI context provider.
4. Add structured-surface adapters for auto-fill/refill, rather than growing
   the old panel-table provider.

In short: build the source-grounded substrate first, use routing as the first
real Next Action vertical slice, then placement, then structured surfaces.

## 2. Current Runtime State

Primary files:

- `include/kisurf/ai/ai_next_action_runtime.h`
- `common/kisurf/ai/ai_next_action_runtime.cpp`
- `common/kisurf/ai/ai_agent_panel_model.cpp`

What exists:

- `AI_NEXT_ACTION_RUNTIME` already owns the outer loop:
  `Update()` -> semantic event -> observation packet -> LLM decision -> hidden
  attempt -> render/validate facts -> LLM review -> publish or abandon.
- Runtime contract types already exist:
  `AI_NEXT_ACTION_CONTEXT_VERSION`, `AI_SEMANTIC_EVENT`,
  `AI_OBSERVATION_PACKET`, `AI_NEXT_ACTION_RUNTIME_STEP`,
  `AI_NEXT_ACTION_ATTEMPT_RECORD`, `AI_NEXT_ACTION_LLM_DECISION`,
  `AI_NEXT_ACTION_REVIEW_DECISION`, `AI_NEXT_ACTION_PUBLISH_DECISION`,
  `AI_PREVIEW_LEASE`, and `AI_ACCEPT_OWNERSHIP_TOKEN`.
- The scheduler already suppresses low-value raw pointer/mouse movement and
  applies a per-slot interval.
- The provider path already has separate Next Action request kinds for decision
  and review turns.

Current gaps:

- `AI_NEXT_ACTION_TOOL_REGISTRY::GenerateCandidates()` still instantiates
  `AI_VIA_PATTERN_NEXT_ACTION_PROVIDER`,
  `AI_ROUTING_SEGMENT_NEXT_ACTION_PROVIDER`, and
  `AI_PANEL_TABLE_NEXT_ACTION_PROVIDER`. That is useful as a proving-ground
  bridge, but those providers should not remain production publishers.
- `AI_NEXT_ACTION_TOOL_REGISTRY::ValidateAttempt()` is still a placeholder. It
  returns zero DRC errors and empty clearance/connectivity facts.
- `AI_NEXT_ACTION_TOOL_REGISTRY::BuildHiddenMutationResult()` only reports a
  JSON fact that a shadow mutation happened. The actual `buildAttempt()` path
  appends an operation record to an `AI_EXECUTION_SESSION`, but does not call
  `AI_ATOMIC_OPERATION_EXECUTOR::Execute()`.
- `AI_NEXT_ACTION_RUNTIME::Accept()` accepts through `AI_EDIT_SESSION` and a
  suggestion record. It does not yet replay the hidden attempt journal through
  `AI_ACCEPT_APPLIER`, nor does it require current context data at accept time.

Decision:

The next runtime implementation slice should replace the proving-ground hidden
attempt mechanics with real session execution:

```text
candidate or LLM tool plan
  -> lower to AI_SESSION_OPERATION_KIND + typed args
  -> AI_ATOMIC_OPERATION_EXECUTOR::Execute(session, kind, args)
  -> session preview service render
  -> session validation service validate
  -> LLM review
  -> published preview with attempt journal provenance
  -> accept via AI_ACCEPT_APPLIER
```

## 3. Session Atomic Ops Are Available

Primary files:

- `include/kisurf/ai/ai_execution_session.h`
- `common/kisurf/ai/ai_execution_session.cpp`
- `include/kisurf/ai/ai_atomic_operation_executor.h`
- `common/kisurf/ai/ai_atomic_operation_executor.cpp`

Available operation vocabulary:

- Query and observation:
  `QueryBoardSummary`, `QueryItems`, `QueryItem`, `QuerySelection`,
  `QueryNets`, `QueryLayers`, `QueryDesignRules`, `QueryViewport`,
  `QueryActivityTimeline`, `RenderPreview`, `ObserveStep`.
- PCB mutation:
  `CreateVia`, `CreateTrackSegment`, `CreateTrackPolyline`, `CreateZone`,
  `CreateShape`, `MoveItems`, `DeleteItems`, `UpdateItemGeometry`,
  `SetItemNet`, `SetItemLayer`, `SetItemProperties`, `SetMetadata`,
  `RefillZones`, `RebuildConnectivity`, `RunValidation`.

Important source detail:

- `AI_EXECUTION_SESSION::AppendOperation()` records journal data and advances
  epoch.
- `AI_ATOMIC_OPERATION_EXECUTOR::Execute()` is the real path that validates
  JSON arguments, mutates the shadow board, appends operation records, and
  returns execution facts.

Decision:

Next Action hidden attempts must call `AI_ATOMIC_OPERATION_EXECUTOR::Execute()`
or an equivalent typed execution service. They should not call
`AI_EXECUTION_SESSION::AppendOperation()` directly except for already-executed
or externally materialized operations.

## 4. Routing Active State Is Ready For A First Slice

Primary files:

- `pcbnew/kisurf_ai_pcb_tool_state_provider.h`
- `pcbnew/kisurf_ai_pcb_tool_state_provider.cpp`
- `pcbnew/router/router_tool.h`
- `pcbnew/router/router_tool.cpp`
- `pcbnew/router/pns_router.h`
- `pcbnew/router/pns_placement_algo.h`
- `pcbnew/router/pns_line_placer.h`

What exists:

- `KISURF_AI_PCB_TOOL_STATE_PROVIDER` observes `TOOL_MANAGER` events through
  `AddEventObserver()`.
- It already classifies active tool state into kinds such as
  `RoutingTrack`, `PlacingVia`, `PlacingFootprint`, `DrawingZone`,
  `MovingSelection`, and `Selecting`.
- For routing, `mergeRouterContext()` obtains:
  `ROUTER_TOOL`, `PNS::ROUTER`, `PNS::PLACEMENT_ALGO`, active layer, current
  net, track width, via diameter, via drill, `CurrentStart()`,
  `CurrentEnd()`, cursor fallback, and `IsPlacingVia()`.
- The router source exposes stable lifecycle signals:
  `ROUTER_TOOL::RoutingInProgress()`,
  `PNS::ROUTER::RoutingInProgress()`,
  `PNS::ROUTER::Placer()`,
  `PNS::ROUTER::FixRoute()`,
  `PNS::ROUTER::CommitRouting()`,
  `PNS::ROUTER::UndoLastSegment()`, and
  `PNS::PLACEMENT_ALGO::CurrentStart()` / `CurrentEnd()`.

Why this matters:

The user's preferred scheduling model is source-supported for routing:

```text
user starts interactive routing
  -> route active state becomes true
  -> Next Action can plan the next landing point from current route head
  -> if the user commits before preview is ready, the attempt expires
  -> after commit, a new route head/context starts a new semantic step
```

The runtime does not need to predict the mouse's next pixel. It should observe
the active route head and generate a next landing proposal from the current
route state.

Recommended first routing implementation:

1. Extend the scheduler/context version to emit routing semantic events when
   `RoutingInProgress()` transitions or when the route head changes.
2. Include a compact routing observation:
   net, layer, width, current route start, current route end, placing-via flag,
   visible candidate pads/vias/tracks in the relevant region, and rule facts.
3. Add routing tools under `AI_NEXT_ACTION_TOOL_REGISTRY`:
   candidate generation, route-segment/polyline hidden mutation, render attempt,
   validation, and rollback.
4. Keep all route publication behind the LLM review turn.

## 5. Placement Needs Source Instrumentation First

Primary files:

- `pcbnew/tools/pcb_tool_base.h`
- `pcbnew/tools/pcb_tool_base.cpp`
- `pcbnew/tools/drawing_tool.cpp`
- `pcbnew/tools/pad_tool.cpp`
- `pcbnew/microwave/microwave_tool.cpp`

What exists:

- Most interactive placement flows use
  `PCB_TOOL_BASE::doInteractiveItemPlacement()`.
- `INTERACTIVE_PLACER_BASE` provides `CreateItem()`, `SnapItem()`, and
  `PlaceItem()`.
- The placement loop keeps the current preview item in a local
  `std::unique_ptr<BOARD_ITEM> newItem`.
- The preview item is added to a local `PCB_SELECTION preview`, updated on
  motion, then released to `PlaceItem()` and committed through
  `BOARD_COMMIT::Push()` on final click.

Gap:

Current `KISURF_AI_PCB_TOOL_STATE_PROVIDER` can infer `PlacingVia`,
`PlacingFootprint`, or similar action kinds from the active tool/action name and
cursor. It cannot reliably expose the actual in-flight placement object,
orientation, bbox, pad/footprint metadata, or snap-adjusted preview geometry,
because those live inside `doInteractiveItemPlacement()`.

Decision:

Placement should not be implemented by guessing from cursor and tool name. Add
source-level instrumentation for active placement state.

Recommended placement implementation:

1. Introduce an `AI_ACTIVE_PLACEMENT_STATE` or equivalent snapshot type.
2. Add optional lifecycle hooks around `doInteractiveItemPlacement()`:
   start, preview item created, preview item updated, item committed, cancelled.
3. Expose a read-only snapshot through the PCB AI context provider:
   placement kind, item type, position, bbox, orientation, layer, net where
   applicable, source tool/action, and generation.
4. Let Next Action hidden attempts propose the next placement using atomic ops
   against the shadow board, then render and validate before publishing.

## 6. Preview Rendering Can Be Reused

Primary files:

- `include/kisurf/ai/ai_preview_manager.h`
- `common/kisurf/ai/ai_preview_manager.cpp`
- `pcbnew/kisurf_ai_pcb_preview_adapter.h`
- `pcbnew/kisurf_ai_pcb_preview_adapter.cpp`
- `pcbnew/kisurf_ai_pcb_session_preview_service.h`
- `pcbnew/kisurf_ai_pcb_session_preview_service.cpp`
- `common/view/view.cpp`

What exists:

- `AI_PREVIEW_MANAGER` manages preview provenance and delegates to a preview
  adapter.
- `KISURF_AI_PCB_PREVIEW_ADAPTER` can draw native canvas preview objects via
  `KIGFX::VIEW::AddToPreview()` / `ClearPreview()`.
- Supported synthetic preview object kinds include route segment, via, shape,
  copper zone, anchor focus, and validation overlay markers.
- `KISURF_AI_PCB_SESSION_PREVIEW_SERVICE::RenderPreview()` can render items
  from an `AI_EXECUTION_SESSION` shadow board into the PCB view.

Decision:

Next Action should reuse the session preview service for visible previews. The
runtime should not create another preview system. The missing part is not
rendering; the missing part is connecting hidden attempts to real shadow-board
execution and carrying preview lease/attempt provenance through to the visible
preview.

## 7. Validation Can Be Reused But Is Not Wired Into Next Action Yet

Primary files:

- `pcbnew/kisurf_ai_pcb_session_validation_service.h`
- `pcbnew/kisurf_ai_pcb_session_validation_service.cpp`
- `common/kisurf/ai/ai_session_tool_call_handler.cpp`
- `common/kisurf/ai/ai_next_action_runtime.cpp`

What exists:

- `KISURF_AI_PCB_SESSION_VALIDATION_SERVICE` can validate a session by cloning
  the live board through KiCad S-expression, replaying the session journal onto
  the validation board, rebuilding connectivity, and running `DRC_ENGINE`.
- Session tool-call tests already cover injected validation service behavior.
- The validation payload can carry warnings/issues and accept-grade validation
  sufficiency.

Gap:

`AI_NEXT_ACTION_TOOL_REGISTRY::ValidateAttempt()` does not call this service.
It returns a fixed placeholder payload.

Decision:

This is the first substrate code change to make. The Next Action tool registry
needs access to a validation service, or the runtime must call a session
validation facade during `buildAttempt()`. The review LLM should see real
validation facts, not placeholder facts.

## 8. Accept Path Exists For Sessions But Next Action Is Not Using It Yet

Primary files:

- `include/kisurf/ai/ai_accept_applier.h`
- `common/kisurf/ai/ai_accept_applier.cpp`
- `pcbnew/kisurf_ai_pcb_session_apply_adapter.h`
- `pcbnew/kisurf_ai_pcb_session_apply_adapter.cpp`
- `common/kisurf/ai/ai_next_action_runtime.cpp`

What exists:

- `AI_ACCEPT_APPLIER::Apply()` can enforce session base hash and selection
  revision checks, reject insufficient validation, replay mutation journal
  operations through an adapter, and mark the session accepted.
- `KISURF_AI_PCB_SESSION_APPLY_ADAPTER` can replay PCB mutation ops into the
  live board and commit them as one `BOARD_COMMIT` undo step.

Gap:

`AI_NEXT_ACTION_RUNTIME::Accept()` currently applies a suggestion through
`AI_EDIT_SESSION`, not through the hidden `AI_EXECUTION_SESSION` journal.
Published suggestion provenance contains attempt/session data, but runtime
accept does not yet reconstruct or own the hidden session for journal replay.

Decision:

Next Action publish records must keep enough live runtime state to accept the
specific attempt journal. Accept should require the current editor context or a
context fingerprint and should call the session accept path. Accept should fail
if the context/base hash/owner namespace/lease/attempt no longer match.

## 9. Auto-Fill / Refill Needs Structured Surface Adapters

Primary files:

- `include/kisurf/ai/ai_types.h`
- `common/kisurf/ai/ai_types.cpp`
- `include/kisurf/ai/ai_context_index.h`
- `common/kisurf/ai/ai_context_index.cpp`
- `common/kisurf/ai/ai_agent_panel_semantic.cpp`
- `common/kisurf/ai/ai_next_action_provider.cpp`
- KiCad UI examples under `pcbnew/dialogs/`, `common/dialogs/`, and
  `pcbnew/drc/rule_editor/`.

What exists:

- `AI_PANEL_STATE_RECORD` and `AI_CONTEXT_SNAPSHOT::m_PanelStates` already
  carry panel/focus observations.
- The old `AI_PANEL_TABLE_NEXT_ACTION_PROVIDER` can prove that panel focus can
  trigger a suggestion.

Gap:

This is not yet a production auto-fill/refill architecture. It lacks a typed
surface schema, cell/property addresses, shadow patch application, validation
mechanics, and reversible preview/accept.

Decision:

Do not expand the old panel-table provider. Introduce a structured surface
adapter layer:

```text
SurfaceSnapshot
  -> schema + focused field/cell + current values + validation rules
  -> LLM chooses scope and values
  -> SurfacePatchPlan
  -> hidden patch attempt
  -> render diff / validate
  -> LLM review
  -> publish SurfacePatch preview
  -> accept applies typed patch through the owning UI/model adapter
```

The first auto-fill implementation should target one concrete surface with a
stable model and tests, then generalize.

## 10. First Implementation Slice

The next code change should be a small, source-grounded vertical slice rather
than a broad tool catalog expansion.

Recommended slice:

1. Add tests showing that a Next Action hidden attempt must call the atomic
   executor instead of directly appending operations.
2. Add tests showing that Next Action validation facts come from an injected
   session validation service.
3. Add tests showing that accepting a published Next Action requires a matching
   context/base hash/lease/attempt token.
4. Implement a session-backed attempt object owned by `AI_NEXT_ACTION_RUNTIME`.
5. Replace placeholder validation facts with real session validation facts.
6. Keep legacy providers only as temporary candidate generators until the first
   routing tool is implemented; they must not bypass LLM review or publish.

After that substrate slice, implement the first routing vertical:

1. Emit routing semantic events from real `RoutingInProgress()` and route head
   changes.
2. Build routing observation packets from `mergeRouterContext()`.
3. Add routing hidden-attempt tools that lower to `CreateTrackSegment` or
   `CreateTrackPolyline`.
4. Render and validate the hidden route attempt.
5. Publish only after LLM review.
6. Accept through session journal replay into one undoable commit.

## 11. Open Source Spikes

The following need source spikes before broad implementation:

- Which object should own active placement state: `PCB_TOOL_BASE`, an AI
  service hanging off `PCB_EDIT_FRAME`, or `TOOL_MANAGER` context.
- How to represent active placement objects without exposing raw `BOARD_ITEM*`
  to the LLM/tool layer.
- Which first structured surface should prove auto-fill/refill: DRC rule editor,
  board setup track/via grid, netclass settings, or the current AI panel test
  harness.
- How to preserve hidden session state across publish so accept can replay the
  exact attempt journal instead of reinterpreting suggestion JSON.
- How to surface actual offscreen/rendered visual observations for LLM review,
  beyond current metadata and native canvas preview.

## 12. Implementation Progress

2026-06-22 source-spike implementation pass:

- `AI_NEXT_ACTION_RUNTIME::buildAttempt()` now executes mutation candidates
  through `AI_ATOMIC_OPERATION_EXECUTOR::Execute()` before journal serialization.
  This replaces the earlier proving-ground path that directly appended an
  operation record without mutating the shadow board.
- Next Action attempt journal serialization now includes resolved handles,
  created handles, warnings, and operation result payloads. This makes hidden
  attempt provenance useful for replay, review, and debugging.
- `AI_NEXT_ACTION_RUNTIME` can now receive an injected
  `AI_SESSION_VALIDATION_SERVICE`. `validate.hidden_attempt` uses that service
  when present and exposes the service result to the LLM review turn.
- Tests added:
  `RuntimeHiddenAttemptExecutesAtomicOperationIntoShadowJournal` and
  `NextActionRuntimeValidationFactsUseInjectedSessionValidationService`.

Still not done:

- Runtime accept still needs to move from suggestion/edit-object application to
  session-journal accept with current context and token validation.
- Hidden attempts still need persistent runtime-owned session state so accept can
  replay the exact accepted attempt without reinterpreting suggestion JSON.
- Routing, placement, and structured-surface tool namespaces are not yet split
  from the temporary candidate-provider bridge.
