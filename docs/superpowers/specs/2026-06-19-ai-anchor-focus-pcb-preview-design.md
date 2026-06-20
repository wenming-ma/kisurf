# AI Anchor Focus PCB Preview Design

## Purpose

KiSurf already lets the model create an `anchor_focus_preview` operation for a semantic anchor. The missing piece is canvas consumption: an operation-only suggestion can become `Previewing`, but the PCB preview adapter has no hook that receives the operation, so the engineer does not see a board-level marker.

This slice makes anchor-focus previews visible on the PCB canvas without turning them into editable board changes. It is a small infrastructure step toward the larger AI-native routing flow where the model chooses semantic anchors and KiSurf renders the concrete preview.

## Scope

This slice adds:

- A preview-session operation hook alongside object preview.
- Orchestrator dispatch of parsed suggestion operations during `BeginPreview`.
- A PCB preview adapter implementation for `AnchorFocusPreview`.
- Tests that prove operation-only suggestions call the preview hook and produce PCB preview marker items.

This slice does not implement accepting anchor-focus operations, dimming full layer stacks, or routing to the focused anchor. It also does not change schematic preview behavior; non-PCB adapters keep the default no-op operation hook.

## Preview Contract

`AI_PREVIEW_ADAPTER` gains:

```cpp
virtual void ShowOperation( uint64_t aPreviewId,
                            const AI_SUGGESTION_OPERATION& aOperation );
```

The method has a default no-op implementation so existing adapters remain source-compatible. `AI_PREVIEW_SESSION` gains `ShowOperation(...)`; if no preview is active, it starts one before delegating, matching `Show(...)`.

`AI_SUGGESTION_ORCHESTRATOR::BeginPreview()` parses `m_ArgumentsJson` once. If it is a previewable operation, the preview session receives it. Preview objects are still shown through the existing object path.

## PCB Marker Rendering

`KISURF_AI_PCB_PREVIEW_ADAPTER::ShowOperation()` handles only `AnchorFocusPreview`.

For a valid active preview id, it creates two synthetic `PCB_SHAPE` segment items that form a crosshair centered on `operation.m_Position`. The marker:

- Uses `operation.m_FocusLayer` when it resolves to a board layer.
- Falls back to `F_Cu` if the focus layer is empty or unknown.
- Uses a fixed, modest marker radius and stroke width in internal units.
- Is added with `m_View.AddToPreview( item, true )` and tracked in `m_PreviewedItems`.
- Never adds items to `BOARD`, so it cannot mutate the design.

This intentionally keeps rendering simple. Later slices can replace the marker with richer layer/net highlighting or dimming while preserving the same operation hook.

## Error Handling

- Stale preview ids are ignored.
- Non-anchor-focus operations are ignored by the PCB adapter.
- Missing or malformed operation JSON remains filtered by `ParseAiSuggestionOperation`.
- Unknown focus layers fall back to `F_Cu` instead of failing the preview.

## Tests

Add focused coverage:

- `AiPreviewSession`: `ShowOperation` begins a preview and delegates the parsed operation.
- `AiSuggestionOrchestrator`: an operation-only anchor-focus suggestion dispatches an operation to the preview adapter.
- `AiAgentPanelModel`: anchor-focus preview-only suggestions dispatch operations through the model path.
- `AiPcbPreviewAdapter`: anchor-focus operation creates two synthetic `PCB_SHAPE` markers at the expected center/layer and does not add board drawings; stale preview ids are ignored.

## Acceptance Criteria

- Operation-only anchor-focus suggestions produce visible PCB preview items.
- Existing object preview behavior remains unchanged.
- Anchor-focus suggestions remain preview-only and cannot be accepted.
- Common and PCB tests pass.
- Secret scanning remains clean.
