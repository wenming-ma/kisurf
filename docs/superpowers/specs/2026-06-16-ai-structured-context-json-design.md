# AI Structured Context JSON Design

Date: 2026-06-16

## Problem

The provider currently gives the model a human-readable context prompt. That is useful for chat, but
tool use and grounded suggestions are more stable when the model also receives a machine-readable
projection of the same native context.

## Goals

- Add a structured JSON serialization for `AI_CONTEXT_SNAPSHOT`.
- Include editor kind, context version, visible objects, selected objects, action catalog, recent
  activity, and visual metadata.
- Keep the JSON bounded so large boards do not explode the prompt.
- Send this JSON inside the OpenAI-compatible user message text, alongside the existing prompt text.
- Avoid non-standard OpenAI request fields.
- Avoid duplicating image data URIs inside the JSON; visual pixels continue to travel through the
  multimodal image content path.

## Non-Goals

- No wire-protocol change for KiCad IPC.
- No new model tool.
- No replacement of the existing human-readable prompt.
- No full board serialization or netlist export.

## Design

- Add `AI_CONTEXT_SNAPSHOT::AsJsonText( maxObjects, maxActions, maxActivity )`.
- The root object is `{ "kisurf_context": { ... } }`.
- The context object contains:
  - `editor`
  - `version`
  - `summary`
  - `visible_object_count`, `visible_objects`
  - `selected_object_count`, `selected_objects`
  - `action_count`, `actions`
  - `recent_activity_count`, `recent_activity`
  - `visual`
- Object records include label, type, and uuid.
- Action records include name, friendly name, description, safety, enabled, and editor.
- Activity records include sequence, kind, editor, action, arguments JSON, result JSON, error code,
  allowed, executed, and message.
- Visual metadata includes source, mime type, width, height, byte size, and `has_pixels`; it does not
  include `data_uri`.
- The OpenAI-compatible provider appends this JSON to user text under a stable heading:
  `Structured KiSurf context JSON:`.

## Verification

- Add unit coverage for `AI_CONTEXT_SNAPSHOT::AsJsonText`.
- Add provider request-body coverage proving the structured JSON reaches the user message.
- Re-run targeted provider/type tests.

## Self Review

- The design keeps compatibility with OpenAI Chat Completions because it only changes user message
  text content.
- The image payload remains multimodal and is not duplicated in text JSON.
- The serializer is read-only and has no editor mutation path.
