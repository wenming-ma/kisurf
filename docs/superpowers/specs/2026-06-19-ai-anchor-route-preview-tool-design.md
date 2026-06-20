# AI Anchor Route Preview Tool Design

## Purpose

Let the model turn model-visible PCB semantic anchors into a native route preview suggestion. This is the first bridge from the anchor context channel to an executable semantic tool: the model selects two anchor ids, KiSurf resolves them against the current `AI_CONTEXT_SNAPSHOT`, and the existing preview/materialize pipeline receives a typed `route_segment_preview` operation.

## Source Observations

- `AI_CONTEXT_SNAPSHOT` already carries `m_Anchors` and serializes them into prompt text and structured JSON.
- `KISURF_AI_PCB_CONTEXT_ADAPTER` now emits pad, via, track, arc, shape, and footprint anchors from real board geometry.
- `AI_SEMANTIC_TOOL_CALL_HANDLER` already converts model semantic tools into `AI_SUGGESTION_RECORD` previews for moving selections and creating copper zones.
- `AI_SUGGESTION_OPERATION` already supports `RouteSegmentPreview` with `net`, `layer`, `width`, `start`, and `end`.
- `AI_OPENAI_COMPAT_PROVIDER` already declares function tools and disables parallel tool calls.

## Goals

1. Add a model-facing tool named `kisurf_preview_route_to_anchor`.
2. Require explicit `start_anchor_id`, `target_anchor_id`, `net`, `layer`, and `width` arguments for this first slice.
3. Resolve both anchor ids from the current `AI_CONTEXT_SNAPSHOT::m_Anchors`.
4. Convert anchor positions into a `route_segment_preview` suggestion operation.
5. Fail closed when arguments are malformed, the editor is not PCB, an anchor is missing, an anchor has no board position, or the route operation is invalid.
6. Declare the tool in the OpenAI-compatible provider schema so the model knows how to call it.
7. Add common-layer tests that prove suggestion creation, provider declaration, and denial paths.

## Non-Goals

- No automatic route search or 45-degree bend generation is added in this slice.
- No `go_to_anchor` viewport navigation tool is added in this slice.
- No visual overlay rendering changes are added in this slice.
- No accepted board edit is performed directly by the tool.
- No inference of `net`, `layer`, or `width` from anchor details or active router state is added in this slice.
- No schematic anchor preview tool is added in this slice.

## Tool Contract

Tool name:

```text
kisurf_preview_route_to_anchor
```

Arguments:

```json
{
  "start_anchor_id": "pcb.track.<uuid>.end",
  "target_anchor_id": "pcb.pad.<uuid>.center",
  "net": "/GND",
  "layer": "F.Cu",
  "width": 150000
}
```

Argument rules:

- `start_anchor_id` must be a non-empty string.
- `target_anchor_id` must be a non-empty string.
- `net` must be a non-empty string.
- `layer` must be a non-empty string.
- `width` must be a positive integer in internal units.
- Extra arguments are rejected by the provider schema and ignored by native validation only if a provider bypasses the schema; native validation still requires the five fields above.

## Resolution Rules

The semantic handler resolves anchors against `aRequest.m_ContextSnapshot.m_Anchors`.

For each id:

1. Match exact `AI_CONTEXT_ANCHOR::m_Id`.
2. Require `m_HasPosition == true`.
3. Use `m_Position` as the preview point.

This slice deliberately does not require a specific `AI_CONTEXT_ANCHOR_KIND`. Pad, via, track-end, generated route-candidate, and future panel-generated board anchors can all participate as long as they carry a board coordinate.

## Preview Operation

The handler builds:

```json
{
  "operation": "route_segment_preview",
  "net": "/GND",
  "layer": "F.Cu",
  "width": 150000,
  "start": { "x": 1000, "y": 2000 },
  "end": { "x": 3000, "y": 4000 }
}
```

The operation must pass `ParseAiSuggestionOperation()` before a suggestion is stored.

The suggestion must set:

- `m_EditorKind`: `AI_EDITOR_KIND::Pcb`.
- `m_Kind`: `AI_SUGGESTION_KIND::Preview`.
- `m_ContextVersion`: effective request context version.
- `m_Title`: `Preview route to anchor`.
- `m_Body`: `Preview this route segment before applying it.`
- `m_ArgumentsJson`: the generated operation JSON.
- `m_PreviewObjects` and `m_EditObjects`: one synthetic `PCB_TRACE_T` object with label `preview:route_to_anchor` and details set to the operation JSON.
- `m_Fingerprint`: stable value containing `semantic|route_to_anchor`, context version, start id, target id, and operation JSON.

## Failure Behavior

The handler returns denied tool results with stable error codes:

- `unknown_tool`: tool name is not supported by the semantic handler.
- `handler_not_configured`: no suggestion sink is installed.
- `editor_not_supported`: request effective editor kind is not PCB.
- `malformed_arguments`: arguments are not a JSON object or required fields have the wrong shape.
- `missing_anchor`: either anchor id is not present in the context snapshot.
- `anchor_without_position`: a matching anchor has no board position.
- `invalid_operation`: generated JSON does not parse as a valid `route_segment_preview`.
- `suggestion_not_stored`: the suggestion sink rejects or drops the preview.

Every denial must keep `m_Allowed == false`, `m_Executed == false`, and include machine-readable `m_ResultJson` using the existing semantic denied-result envelope.

## Provider Declaration

`AI_OPENAI_COMPAT_PROVIDER` must declare a fifth function tool:

- name: `kisurf_preview_route_to_anchor`
- description: asks KiSurf to preview one PCB route segment between two current semantic anchors.
- parameters: object with the five required fields above and `additionalProperties: false`.

`parallel_tool_calls` remains false.

## Test Requirements

Add tests to `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`:

1. `RouteToAnchorCreatesRouteSegmentPreviewSuggestion`
   - Build a PCB provider request with two positioned anchors.
   - Call `kisurf_preview_route_to_anchor` with the two ids, `net`, `layer`, and `width`.
   - Verify the handler allows the tool but does not execute an edit.
   - Verify the captured suggestion title, synthetic trace object, parsed `RouteSegmentPreview`, start/end coordinates, net, layer, and width.
   - Verify result JSON has `status: preview_ready`.

2. `RouteToAnchorFailsClosedForMissingAnchor`
   - Build a request with only the start anchor.
   - Call the tool with a missing target id.
   - Verify denial code `missing_anchor` and that the suggestion sink is not called.

3. `RouteToAnchorFailsClosedForAnchorWithoutPosition`
   - Build a request where the target anchor exists but `m_HasPosition` is false.
   - Verify denial code `anchor_without_position` and that the suggestion sink is not called.

Add tests to `qa/tests/common/test_ai_provider.cpp`:

4. Extend `OpenAiProviderDeclaresKiSurfTools`
   - Expect five tools.
   - Verify `kisurf_preview_route_to_anchor` exists.
   - Verify its required field list includes `start_anchor_id`, `target_anchor_id`, `net`, `layer`, and `width`.

## Verification Requirements

- Run red after adding tests and before production changes.
- Run green by building `qa_common` and running `AiSemanticToolCallHandler` and `AiNativeProvider`.
- Run existing suggestion operation tests if available through the common suite because the new tool depends on `RouteSegmentPreview`.
- Build `pcbnew` to catch integration issues in editor-linked common code.
- Run whitespace and secret scans before committing implementation.

## Self-Review

- Spec coverage: This slice connects current context anchors to a model-callable route preview tool while keeping routing algorithms, viewport navigation, and visual overlay changes separate.
- Source alignment: It reuses `AI_CONTEXT_SNAPSHOT`, `AI_SEMANTIC_TOOL_CALL_HANDLER`, `AI_SUGGESTION_OPERATION`, and provider tool declarations instead of adding a parallel action API.
- Safety check: The tool only creates a preview suggestion and never mutates the board without the existing user acceptance path.
- Risk check: Required explicit net/layer/width keeps the first implementation deterministic; inference from active routing state can be added later with its own tests.
