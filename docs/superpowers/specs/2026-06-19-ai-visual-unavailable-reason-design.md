# AI Visual Unavailable Reason Design

## Problem

The model can request a visual frame, but when pixels are unavailable the current contract only reports `has_pixels=false`. That is too opaque for an AI-native editor: the agent cannot tell whether the canvas was not ready, the screenshot failed, or the PNG encoding failed.

## Goals

- Preserve the current successful visual-frame behavior.
- Add a structured unavailable reason to `AI_VISUAL_SNAPSHOT`.
- Surface that reason in:
  - context prompt text;
  - structured context JSON;
  - `kisurf_get_visual_frame`;
  - `kisurf_get_workspace_view` visual sections;
  - observability visual summaries.
- Keep reasons short, stable, and non-sensitive.

## Non-Goals

- Do not retry capture or change rendering behavior in this slice.
- Do not include exception text, file paths, API keys, or raw OS diagnostics.
- Do not treat unavailable pixels as a tool-call failure unless the caller requested pixels that exist but exceed `max_bytes`.

## Contract

`AI_VISUAL_SNAPSHOT` gains:

- `m_UnavailableReason`: empty when pixels are present or the absence is intentionally unspecified.

Initial reason values:

- `invalid_image`: the source image is not valid or has no size.
- `png_encode_failed`: PNG serialization produced no payload.
- `base64_encode_failed`: base64 conversion produced no payload.
- `canvas_size_unavailable`: canvas screenshot and fallback capture could not determine a positive client size.

JSON includes `unavailable_reason` only when the value is non-empty.

## Acceptance

- Invalid images return a snapshot with source and `invalid_image`.
- Structured context and visual-frame tool output include `unavailable_reason`.
- Successful snapshots do not include an unavailable reason.
- Existing visual pixel transport and oversize-denial behavior remains unchanged.
