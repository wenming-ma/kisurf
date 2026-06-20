# AI OpenAI Multimodal Visual Transport Design

Date: 2026-06-16

## Purpose

KiSurf already captures native canvas pixels into
`AI_CONTEXT_SNAPSHOT::m_Visual`, but the OpenAI-compatible provider currently
serializes only `AsPromptText()` into a plain user message. That means the model
learns that pixels exist but does not receive the actual board or schematic
image.

This spec completes the first visual transport path: when a context snapshot has
an in-memory PNG data URI, the OpenAI-compatible provider sends it as a
multimodal `image_url` content part alongside the textual editor context.

## Source Anchors

- `include/kisurf/ai/ai_types.h` defines `AI_VISUAL_SNAPSHOT`.
- `common/kisurf/ai/ai_visual_snapshot.cpp` produces
  `data:image/png;base64,...` payloads from native canvas images.
- `common/kisurf/ai/ai_types.cpp` exposes text context through
  `AI_CONTEXT_SNAPSHOT::AsPromptText(...)`.
- `common/kisurf/ai/ai_provider.cpp` builds OpenAI-compatible
  `/chat/completions` requests.
- `qa/tests/common/test_ai_provider.cpp` already validates provider request
  bodies through an injected HTTP handler without network access.

## Goals

- Preserve existing text-only provider behavior when no valid visual pixels are
  available.
- When `AI_CONTEXT_SNAPSHOT::m_Visual.HasPixels()` is true, send the user message
  content as an array with a text part and an image part.
- Use the existing data URI directly as `image_url.url`.
- Keep visual metadata in the text part through `AsPromptText()` so the model can
  correlate the image with editor kind, selection, actions, and recent activity.
- Avoid logging, truncating, or rewriting the base64 payload inside traces.
- Keep all tests offline through the existing injected HTTP handler seam.

## Non-Goals

- No new canvas capture code; capture already exists.
- No persistence of visual snapshots.
- No image resizing in the provider; the visual snapshot layer owns payload size.
- No support for cropped regions, selected-object crops, or high-resolution
  offscreen renders in this slice.
- No switch to the newer Responses API; the current provider remains
  `/chat/completions` compatible.

## Design

Text-only requests remain unchanged:

```json
{
  "role": "user",
  "content": "User request:\n..."
}
```

Requests with pixels use OpenAI-compatible multimodal content:

```json
{
  "role": "user",
  "content": [
    {
      "type": "text",
      "text": "User request:\n...\n\nEditor context:\n..."
    },
    {
      "type": "image_url",
      "image_url": {
        "url": "data:image/png;base64,..."
      }
    }
  ]
}
```

The provider should only emit the image part when the data URI is non-empty.
`m_Source`, `m_MimeType`, and size metadata remain in the text context; the model
does not need a second metadata object in the image part.

## Safety And Privacy

- The API key remains in the `Authorization` header only.
- The data URI is included only in the HTTP request body sent to the configured
  OpenAI-compatible provider.
- Error messages must not echo the data URI.
- Tests use tiny fake data URIs and do not require real credentials.
- The provider must not write image bytes to disk.

## Testing Requirements

Add provider tests that:

- Build a request with `m_ContextSnapshot.m_Visual.m_DataUri`.
- Parse the outgoing JSON body in the fake HTTP handler.
- Verify the second user message uses array content.
- Verify the array contains exactly one text part and one image URL part.
- Verify the image URL is the exact data URI from the context snapshot.
- Verify text-only requests continue to use a string content field.

## Acceptance Criteria

- OpenAI-compatible provider requests include `image_url.url` when native visual
  pixels are available.
- Existing provider tests for text-only prompts, tool declaration, tool-call
  parsing, and missing API key continue to pass.
- The change is isolated to provider serialization and tests.
- No secrets or raw image payloads are written to logs or trace text by this
  provider change.

## Spec Self-Review

- Open-marker scan: no unresolved placeholders remain.
- Scope check: this spec only covers provider request serialization for existing
  visual snapshots.
- Compatibility check: text-only behavior remains unchanged for providers that do
  not receive visual pixels.
- Safety check: payload transport is explicit, offline-tested, and does not echo
  API keys or image data in errors.
