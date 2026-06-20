# AI Anchor Focus Preview Tool Design

## Purpose

KiSurf already generates semantic routing anchors such as `tool.routing.start`, orthogonal breakouts, and 45-degree intersections. The model can also create a route segment directly with `kisurf_preview_route_to_anchor`, but that jumps from reasoning to an edit preview too quickly.

This slice adds a lighter model-facing tool that lets the model select one current semantic anchor and ask KiSurf to present a focus preview for it. The preview is intentionally operation-only: it can highlight or focus an anchor for review, but it does not edit the PCB and cannot be accepted as a board mutation.

## Scope

Add a new tool:

```text
kisurf_preview_anchor_focus
```

The tool creates an `AI_SUGGESTION_RECORD` whose operation is:

```json
{
  "operation": "anchor_focus_preview",
  "anchor_id": "tool.routing.orthogonal.horizontal",
  "position": { "x": 500, "y": 200 },
  "focus_layer": "F.Cu",
  "focus_net": "/GPIO",
  "dim_unfocused_layers": true
}
```

The operation is previewable but not acceptable because it contains no edit objects. Later GUI renderers can consume the operation to pan, zoom, highlight the selected anchor, and apply the same visual focus semantics already used by `kisurf_get_visual_frame` render directives.

## Tool Contract

Parameters:

- `anchor_id` string, required. Must reference a current `AI_CONTEXT_ANCHOR`.
- `focus_layer` string, optional. If omitted, KiSurf tries to infer it from anchor details.
- `focus_net` string, optional. If omitted, KiSurf tries to infer it from anchor details.
- `dim_unfocused_layers` boolean, optional, defaults to true.

Validation:

- Missing or malformed arguments return `malformed_arguments`.
- Unknown `anchor_id` returns `missing_anchor`.
- Anchors without positions return `anchor_without_position`.
- Non-PCB editors are allowed only for non-routing/general anchors. PCB-specific focus metadata is included when present, but the tool remains operation-only.

Result:

- On success, the handler stores a suggestion and returns the existing `preview_ready` tool result with the operation payload and `preview_required: true`.
- On failure, it returns the existing denied-tool JSON shape.

## Safety Boundary

`kisurf_preview_anchor_focus` never mutates the board. It does not call native KiCad actions, does not move the cursor, and does not accept any suggestion. It only creates a reviewable preview record. User acceptance remains disabled unless a future renderer deliberately maps the operation to a non-mutating UI state change.

## Architecture

`AI_SUGGESTION_OPERATION_KIND` gains `AnchorFocusPreview`, with fields for `m_AnchorId`, `m_Position`, `m_FocusLayer`, `m_FocusNet`, and `m_DimUnfocusedLayers`.

`AI_SEMANTIC_TOOL_CALL_HANDLER` adds support for `kisurf_preview_anchor_focus`. It resolves `anchor_id` against `aRequest.m_ContextSnapshot.m_Anchors`, requires a positional anchor, fills omitted focus fields from the anchor details JSON when available, and stores an operation-only suggestion.

`AI_OPENAI_COMPAT_PROVIDER` advertises the new tool schema. `AiDirectUseSmoke` includes it in the required direct-use tool surface.

## Tests

Add focused `qa_common` coverage:

- `ParseAiSuggestionOperation` parses `anchor_focus_preview`.
- The provider advertises `kisurf_preview_anchor_focus` with required `anchor_id`, optional focus fields, `dim_unfocused_layers`, and no extra properties.
- The semantic handler creates an operation-only preview suggestion for a positional anchor and infers focus metadata from anchor details.
- The semantic handler rejects unknown anchors, anchors without positions, malformed arguments, and unsupported extra fields.
- The resulting suggestion is previewable but not acceptable through `AI_AGENT_PANEL_MODEL`.
- Direct-use smoke expects the new tool in the provider tool surface.
