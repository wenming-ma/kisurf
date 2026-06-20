# AI Route-To-Anchor Parameter Inference Design

## Purpose

Make `kisurf_preview_route_to_anchor` usable with the routing anchors exposed in the current editor context. The model should be able to select a start anchor and a target anchor, then let KiSurf infer `net`, `layer`, and `width` from explicit arguments, anchor metadata, or the active PCB routing tool state.

## Source Observations

- `kisurf_preview_route_to_anchor` currently requires `start_anchor_id`, `target_anchor_id`, `net`, `layer`, and `width`.
- `AppendAiToolStateAnchors()` now generates transient `tool.routing.*` anchors whose details JSON includes `net`, `layer`, `width`, `start`, `target`, `role`, and `position`.
- PCB factual anchors often expose `net_name` and `layer`; track object refs expose width, but track anchors do not always carry width.
- `AI_TOOL_STATE_SNAPSHOT::m_ModeContextJson` already carries active router `net`, `layer`, and `width`.
- The OpenAI-compatible provider currently declares all five route-to-anchor fields as required, so the model is pushed to send raw routing parameters even when KiSurf already knows them.

## Approaches Considered

1. Handler-only inference with no provider schema change.
   - Pros: native validation becomes more flexible immediately.
   - Cons: model still sees `net`, `layer`, and `width` as required and may keep over-specifying them.

2. Provider-only schema relaxation.
   - Pros: model can omit route parameters.
   - Cons: native validation would still fail closed because the handler requires all five fields.

3. Combined schema relaxation and native inference.
   - Pros: model-facing contract matches native behavior; explicit overrides still work; native validation stays closed and deterministic.
   - Cons: slightly more handler parsing code.

Chosen approach: option 3.

## Goals

1. Keep `start_anchor_id` and `target_anchor_id` mandatory.
2. Make `net`, `layer`, and `width` optional in the provider schema.
3. Preserve explicit `net`, `layer`, and `width` arguments as overrides when supplied.
4. Infer missing route parameters from anchor details, preferring start anchor details over target anchor details.
5. Infer any still-missing route parameters from active PCB `RoutingTrack` tool state.
6. Support both `net` and `net_name` detail fields for net inference.
7. Fail closed with a stable denial code when route parameters remain unavailable.
8. Add common tests proving inference, explicit override behavior, failure behavior, and provider schema changes.

## Non-Goals

- No route search, obstacle avoidance, clearance checking, or shove routing is added.
- No accepted board edit is performed.
- No new model-facing tool is added.
- No visual overlay changes are added.
- No attempt is made to infer width from board design rules.
- No mutation of anchor details or tool state is performed.

## Tool Contract

Tool name remains:

```text
kisurf_preview_route_to_anchor
```

Minimum arguments:

```json
{
  "start_anchor_id": "tool.routing.start",
  "target_anchor_id": "tool.routing.fortyfive.horizontal"
}
```

Optional explicit overrides:

```json
{
  "start_anchor_id": "tool.routing.start",
  "target_anchor_id": "pcb.pad.target.center",
  "net": "/GPIO",
  "layer": "F.Cu",
  "width": 150000
}
```

Argument rules:

- `start_anchor_id` must be a non-empty string.
- `target_anchor_id` must be a non-empty string.
- If supplied, `net` must be a non-empty string.
- If supplied, `layer` must be a non-empty string.
- If supplied, `width` must be a positive integer in board internal units.
- Extra arguments remain rejected by the provider schema and ignored by native validation if a provider bypasses the schema.

## Inference Rules

The handler resolves anchors before route-parameter inference:

1. Resolve both anchor ids from `aRequest.m_ContextSnapshot.m_Anchors`.
2. Require both anchors to have board positions.
3. Resolve `net`, `layer`, and `width` independently.

For each route parameter:

1. Use the explicit tool argument when present and valid.
2. Otherwise inspect start anchor `m_DetailsJson`.
3. Otherwise inspect target anchor `m_DetailsJson`.
4. Otherwise inspect `aRequest.m_ContextSnapshot.m_ToolState.m_ModeContextJson`, but only when the effective editor kind is PCB and the tool state kind is `AI_TOOL_STATE_KIND::RoutingTrack`.

Detail fields:

- `net`: accept detail field `net`; if absent, accept `net_name`.
- `layer`: accept detail field `layer`.
- `width`: accept detail field `width`.

Tool-state fields:

- `net`: field `net`.
- `layer`: field `layer`.
- `width`: field `width`.

Malformed anchor details JSON is ignored as an inference source. Malformed active tool-state JSON is ignored as an inference source. Invalid explicit arguments are not ignored; they return `malformed_arguments`.

## Failure Behavior

Existing failure codes remain unchanged:

- `malformed_arguments`: JSON arguments are not an object, required anchor ids are missing, or a supplied optional route parameter has the wrong type or shape.
- `missing_anchor`: either anchor id is not present in the current context snapshot.
- `anchor_without_position`: either resolved anchor lacks a board position.
- `invalid_operation`: generated route preview JSON does not parse as a valid operation.
- `suggestion_not_stored`: the suggestion sink rejects or drops the preview.

New failure code:

- `missing_route_parameters`: one or more of `net`, `layer`, or `width` is absent after all inference sources are checked.

Every denial keeps `m_Allowed == false`, `m_Executed == false`, and the existing denied-result JSON envelope.

## Provider Declaration

`routeToAnchorToolParameters()` must keep the same properties but change `required` from five fields to:

```json
["start_anchor_id", "target_anchor_id"]
```

Descriptions for `net`, `layer`, and `width` must say they are optional overrides. The schema continues to use `additionalProperties: false`.

## Test Requirements

Add tests to `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`:

1. `RouteToAnchorInfersRouteParametersFromAnchorDetails`
   - Build two positioned anchors with details JSON containing `net`, `layer`, and `width`.
   - Call `kisurf_preview_route_to_anchor` with only `start_anchor_id` and `target_anchor_id`.
   - Verify the generated route preview uses inferred net, layer, width, start, and end.

2. `RouteToAnchorFallsBackToRoutingToolStateForMissingParameters`
   - Build positioned anchors without route details.
   - Attach PCB `RoutingTrack` tool state with valid `net`, `layer`, `width`, and `start`.
   - Call the tool with only anchor ids.
   - Verify the generated route preview uses tool-state net, layer, and width.

3. `RouteToAnchorUsesExplicitRouteParametersOverInferredDetails`
   - Build anchors with details JSON for one route context.
   - Call the tool with explicit `net`, `layer`, and `width` for another route context.
   - Verify the generated route preview uses explicit values.

4. `RouteToAnchorFailsClosedWhenRouteParametersCannotBeResolved`
   - Build positioned anchors without route details and no routing tool state.
   - Call the tool with only anchor ids.
   - Verify denial code `missing_route_parameters` and no suggestion sink call.

Update `qa/tests/common/test_ai_provider.cpp`:

5. `OpenAiProviderDeclaresKiSurfTools`
   - Keep expecting five tools.
   - Verify `kisurf_preview_route_to_anchor` required fields include `start_anchor_id` and `target_anchor_id`.
   - Verify required fields do not include `net`, `layer`, or `width`.
   - Verify `net`, `layer`, and `width` still exist as properties.

## Verification Requirements

- Run red after adding tests and before production changes.
- Run green by building `qa_common` and running `AiSemanticToolCallHandler` and `AiNativeProvider`.
- Run `AiSuggestionOperations` because the route preview operation parser remains the final validation gate.
- Build `pcbnew`.
- Run whitespace and secret scans before committing implementation.

## Self-Review

- Spec coverage: The design covers handler inference, provider schema changes, failure modes, and tests.
- Source alignment: It reuses `AI_CONTEXT_ANCHOR::m_DetailsJson`, `AI_TOOL_STATE_SNAPSHOT::m_ModeContextJson`, existing route preview operations, and the existing semantic suggestion path.
- Safety check: The tool still creates only preview suggestions and never mutates board geometry.
- Scope check: Routing algorithms, design-rule width inference, visual overlays, and accepted edits stay out of this slice.
