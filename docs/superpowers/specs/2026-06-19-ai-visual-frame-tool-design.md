# AI Visual Frame Tool Design

Date: 2026-06-19

## Goal

Add a model-callable read-only tool that returns the current editor visual frame captured by KiSurf from the live canvas snapshot held in memory.  This gives the Agent an explicit way to ask, during a tool-call turn, "what does the engineer currently see?"

## Problem

KiSurf already captures `AI_VISUAL_SNAPSHOT` from the PCB and schematic canvases and attaches it to `AI_CONTEXT_SNAPSHOT`.  The OpenAI-compatible provider can include `m_DataUri` as multimodal input in the initial request when pixels are present.

That is useful, but it is passive.  During multi-step tool calling, the model also needs a bounded read interface for the current visual frame, parallel to the semantic context snapshot tool.  Without it, the model can call editing/preview tools but cannot explicitly refresh or inspect the captured visual frame through the tool system.

## Requirements

1. Provide a new semantic tool named `kisurf_get_visual_frame`.
2. The tool must be read-only:
   - `allowed=true`
   - `executed=false`
   - no suggestion creation
   - no editor mutation
3. The tool must work without a suggestion sink.
4. The tool must return valid JSON with:
   - `tool`
   - `allowed`
   - `executed`
   - `status="visual_ready"`
   - `visual`
5. The `visual` object must include:
   - `source`
   - `mime_type`
   - `width_px`
   - `height_px`
   - `byte_size`
   - `has_pixels`
6. Pixel payload is opt-in with `include_pixels=true`.
7. Pixel payload must be returned as `visual.data_uri` only when:
   - pixels are present
   - `include_pixels=true`
   - `byte_size <= max_bytes`
8. Missing `include_pixels` defaults to `false`.
9. Missing `max_bytes` defaults to `262144`.
10. `max_bytes` must be clamped to `1048576`.
11. Invalid argument types produce `malformed_arguments`.
12. If pixels exist but exceed the requested limit, the tool must fail closed with `visual_too_large`.

## Interface

Tool name:

```text
kisurf_get_visual_frame
```

Parameters:

```json
{
  "include_pixels": false,
  "max_bytes": 262144
}
```

All properties are optional.

Response without pixels:

```json
{
  "tool": "kisurf_get_visual_frame",
  "allowed": true,
  "executed": false,
  "status": "visual_ready",
  "visual": {
    "source": "pcbnew.canvas",
    "mime_type": "image/png",
    "width_px": 1280,
    "height_px": 720,
    "byte_size": 2048,
    "has_pixels": true
  }
}
```

Response with pixels:

```json
{
  "tool": "kisurf_get_visual_frame",
  "allowed": true,
  "executed": false,
  "status": "visual_ready",
  "visual": {
    "source": "pcbnew.canvas",
    "mime_type": "image/png",
    "width_px": 1280,
    "height_px": 720,
    "byte_size": 2048,
    "has_pixels": true,
    "data_uri": "data:image/png;base64,..."
  }
}
```

Oversize denial:

```json
{
  "tool": "kisurf_get_visual_frame",
  "allowed": false,
  "executed": false,
  "status": "denied",
  "error_code": "visual_too_large",
  "message": "Visual frame pixel payload exceeds max_bytes."
}
```

## Non-goals

This slice does not add:

- viewport crop requests
- image tiles
- downscaling
- OCR or object detection
- live visual subscriptions
- new canvas capture mechanics

Those can be layered later once this explicit visual read contract exists.

## Self-check

- The tool uses the existing `AI_VISUAL_SNAPSHOT` captured from live editor canvas memory.
- Pixels are opt-in and bounded to avoid giant tool-result payloads.
- The tool is separate from semantic context so the model can ask for only what it needs.
- The design works for PCB and schematic contexts because it is common-layer only.
- The tool complements, rather than replaces, provider-level multimodal prompt input.
