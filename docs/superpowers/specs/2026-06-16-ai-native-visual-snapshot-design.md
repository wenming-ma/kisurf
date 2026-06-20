# AI Native Visual Snapshot Design

Date: 2026-06-16

## Purpose

This spec implements the visual half of KiSurf's AI-native context contract: the
model should be able to receive the same board or schematic view that the
engineer is looking at, without depending on an external desktop screenshot.

Phase 2 reserved `AI_VISUAL_SNAPSHOT` in `AI_CONTEXT_SNAPSHOT`. Phase 5 fills
that field from KiCad's native canvas pipeline. The first implementation captures
the current `EDA_DRAW_PANEL_GAL` view into an in-memory PNG data URI and attaches
it to the Agent panel context snapshot.

## Source Research Anchors

Local source anchors:

- `include/class_draw_panel_gal.h` exposes `EDA_DRAW_PANEL_GAL::GetScreenshot( wxImage& )`
  and documents it as current canvas capture.
- `common/draw_panel_gal.cpp` implements `GetScreenshot(...)` by forcing a repaint
  and delegating to `KIGFX::OPENGL_GAL::GetScreenshot(...)` when the backend is
  OpenGL.
- `common/gal/opengl/opengl_gal.cpp` implements `OPENGL_GAL::GetScreenshot(...)`
  with `glReadPixels(...)` from the active GAL framebuffer. This is a native
  memory read, not an OS-level desktop screenshot.
- `common/eda_draw_frame.cpp` implements `EDA_DRAW_FRAME::SaveCanvasImageToFile(...)`
  by first trying `GetCanvas()->GetScreenshot(...)` and falling back to a
  `wxClientDC` blit when the backend cannot provide a direct screenshot.
- `eeschema/tools/sch_editor_control.cpp` contains an offscreen schematic
  render-to-`wxImage` example using `CAIRO_PRINT_GAL`, a temporary `VIEW`, and a
  temporary painter. This is useful for future high-resolution crop rendering but
  is not the first capture path.
- `pcbnew/pcb_edit_frame.cpp` and `eeschema/sch_edit_frame.cpp` build Agent panel
  context snapshots from model adapters and action catalogs. These frame-level
  lambdas are the right place to add view pixels because they have access to
  `GetCanvas()`.

Existing AI anchors:

- `include/kisurf/ai/ai_types.h` defines `AI_VISUAL_SNAPSHOT` with `source`,
  `mimeType`, and `dataUri`.
- `common/kisurf/ai/ai_types.cpp` already reports whether a snapshot has visual
  pixels in `AI_CONTEXT_SNAPSHOT::AsPromptText(...)`.
- `common/kisurf/ai/ai_context_index.cpp` currently builds snapshots from model
  objects and revisions only; it needs a visual injection method.

## Goals

- Encode a native `wxImage` as `data:image/png;base64,...` without writing to disk.
- Add a small visual snapshot utility that can be unit-tested without an editor
  frame.
- Add a canvas capture helper that uses `EDA_DRAW_PANEL_GAL::GetScreenshot(...)`
  first and a `wxClientDC` blit fallback second.
- Attach visual snapshots to PCB and schematic Agent panel context snapshots.
- Bump the context view revision when a fresh visual snapshot is attached.
- Keep failure non-fatal: if capture fails, text/object/action context is still
  sent.

## Non-Goals

- No external desktop screenshot or Computer Use screenshot is used for model
  context.
- No high-resolution offscreen rendering pipeline in this phase.
- No cropped region, selected-object zoom crop, or layer-specific render target.
- No persistence of captured images.
- No upload policy UI or model-side image transport changes beyond filling the
  existing `AI_CONTEXT_SNAPSHOT.m_Visual` field.
- No attempt to capture obscuring modal dialogs or non-canvas UI chrome.

## Design Decision

Three approaches were considered:

1. **Canvas framebuffer capture.** Capture the active `EDA_DRAW_PANEL_GAL` pixels,
   matching current zoom, visible layers, overlays, and renderer state.
2. **Offscreen semantic render.** Rebuild a `VIEW` and painter from the model, then
   render to `CAIRO_PRINT_GAL` at a chosen resolution.
3. **External screenshot.** Use OS or Computer Use screenshots of the application
   window.

The first implementation should use option 1. It best satisfies "the model sees
what the engineer sees" and already has native KiCad source support. Option 2 is
valuable later for cropped/high-resolution model context, but it must rebuild
view state and can drift from the live canvas. Option 3 is only a testing or
fallback path because it includes UI chrome and is not an AI-native memory
interface.

## Data Model

`AI_VISUAL_SNAPSHOT` remains the model-facing contract:

- `m_Source`: a stable source label such as `canvas.opengl`, `canvas.dc`, or
  `test.image`.
- `m_MimeType`: `image/png`.
- `m_DataUri`: a complete `data:image/png;base64,...` URI.

Add optional metadata fields:

- `m_WidthPx`
- `m_HeightPx`
- `m_ByteSize`

These fields let the runtime and tests reason about capture size without parsing
the data URI.

## New Utility Boundary

Add `include/kisurf/ai/ai_visual_snapshot.h` and
`common/kisurf/ai/ai_visual_snapshot.cpp`.

Responsibilities:

- Validate that a `wxImage` is valid and has positive dimensions.
- Optionally downscale the image to a max edge to avoid bloating provider context.
- Encode to PNG in a `wxMemoryOutputStream`.
- Base64 encode the PNG bytes.
- Return an `AI_VISUAL_SNAPSHOT` with metadata and a data URI.

Suggested functions:

```cpp
struct AI_VISUAL_CAPTURE_OPTIONS
{
    int m_MaxEdgePixels = 1024;
};

KICOMMON_API AI_VISUAL_SNAPSHOT MakeAiVisualSnapshotFromImage(
        const wxImage& aImage,
        const wxString& aSource,
        const AI_VISUAL_CAPTURE_OPTIONS& aOptions = AI_VISUAL_CAPTURE_OPTIONS() );

KICOMMON_API bool CaptureAiVisualSnapshotFromCanvas(
        EDA_DRAW_PANEL_GAL& aCanvas,
        AI_VISUAL_SNAPSHOT& aSnapshot,
        const AI_VISUAL_CAPTURE_OPTIONS& aOptions = AI_VISUAL_CAPTURE_OPTIONS() );
```

The image encoding utility is pure enough for `qa_common` tests. The canvas helper
is editor/UI-thread code and should be covered first through a fake-image path and
later by UI smoke tests.

## Context Injection

Add to `AI_CONTEXT_INDEX`:

```cpp
void SetVisualSnapshot( AI_VISUAL_SNAPSHOT aVisual );
```

Rules:

- Store the visual snapshot in the index.
- Increment `m_ViewRevision` when `aVisual.HasPixels()` or `m_Source` is set.
- `BuildSnapshot()` copies `m_Visual` into `AI_CONTEXT_SNAPSHOT`.

PCB and schematic Agent panel lambdas should become:

1. Build the model/action context as today.
2. Capture the canvas into `AI_VISUAL_SNAPSHOT`.
3. Attach the visual snapshot to the `AI_CONTEXT_INDEX` before `BuildSnapshot()`,
   or set `snapshot.m_Visual` and bump view revision if the index is already built.
4. Return the snapshot even if capture fails.

The lambda must not block on model calls or write any image file to disk.

## Capture Semantics

Runtime behavior:

- Must run on the UI thread because `EDA_DRAW_PANEL_GAL`, OpenGL context locking,
  and `wxClientDC` are UI objects.
- Must call `EDA_DRAW_PANEL_GAL::GetScreenshot(...)` first.
- If that returns false, use the same `wxClientDC`/`wxMemoryDC::Blit(...)` fallback
  pattern as `EDA_DRAW_FRAME::SaveCanvasImageToFile(...)`.
- Must reject invalid or zero-size canvas images.
- Must downscale images larger than `m_MaxEdgePixels`.
- Must not capture parent frame UI, menus, dialogs, or the Agent pane itself.

Failure behavior:

- If capture fails, return `false` and leave the data URI empty.
- Context generation still succeeds.
- `AI_CONTEXT_SNAPSHOT::AsPromptText(...)` should report `pixels=no` only when a
  source/mime exists but no data URI is present.

## Privacy And Transport

- The snapshot is in-memory only.
- No snapshot is written to disk by this feature.
- No API key, file path, or project path is embedded in the image metadata.
- Provider code may later choose whether to transmit `m_DataUri` as a multimodal
  image part; this phase only makes the data available in native context.
- Large images are capped to keep model context and HTTP payload size bounded.

## Testing Requirements

Use test-first development for:

- `AI_VISUAL_SNAPSHOT::HasPixels()` continues to depend on `m_DataUri`.
- A valid `wxImage` becomes a PNG data URI with width/height/byte metadata.
- Oversized images are downscaled to the configured max edge.
- Invalid images produce an empty snapshot.
- `AI_CONTEXT_INDEX::SetVisualSnapshot(...)` copies visual data into
  `BuildSnapshot()` and increments `m_ViewRevision`.
- Agent panel model/provider tests prove a visual snapshot in context reaches the
  provider prompt text as `visual: ... pixels=yes`.

Canvas capture should be covered by code review and later UI smoke tests because
constructing a live `EDA_DRAW_PANEL_GAL` in `qa_common` is not stable in headless
test environments.

## Acceptance Criteria

- A native visual snapshot utility exists and is unit-tested.
- `AI_CONTEXT_INDEX` can carry visual snapshots.
- PCB and schematic Agent panel context providers attempt canvas capture.
- Provider prompts can observe `visual: canvas... image/png pixels=yes` when
  capture succeeds.
- Capture failure does not prevent Agent panel messages.
- No image is written to disk and no external screenshot API is required.

## Spec Self-Review

- Placeholder scan: no TBD/TODO/fill-in sections remain.
- Internal consistency: live canvas capture is the first path; offscreen rendering
  is explicitly deferred.
- Scope check: this is one subsystem: native canvas pixels into
  `AI_CONTEXT_SNAPSHOT.m_Visual`. Multimodal provider message formatting and
  cropped/high-resolution renders are later phases.
- Ambiguity check: source labels, failure behavior, UI-thread expectation, max
  image size, and context revision behavior are explicit.
