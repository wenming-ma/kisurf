# AI Model Suggestion Provider Design

Date: 2026-06-16

## Purpose

The current `AI_AGENT_SUGGESTION_PROVIDER` creates a deterministic local
suggestion from the current selection. That keeps the preview lifecycle testable,
but it is not yet AI native: model output cannot become a structured
`AI_SUGGESTION_RECORD`.

This spec introduces a narrow model-backed suggestion path. The model may return
one JSON suggestion, while KiSurf owns grounding, validation, and lifecycle
state. The slice does not execute edits directly; it only converts model output
into the existing previewable suggestion record.

## Source Anchors

- `include/kisurf/ai/ai_agent_suggestion_provider.h` declares the suggestion
  provider surface.
- `common/kisurf/ai/ai_agent_suggestion_provider.cpp` currently creates the
  deterministic suggestion.
- `include/kisurf/ai/ai_types.h` defines `AI_SUGGESTION_TRIGGER`,
  `AI_SUGGESTION_RECORD`, `AI_CONTEXT_SNAPSHOT`, and provider request/response
  records.
- `common/kisurf/ai/ai_suggestion_orchestrator.cpp` owns ids, duplicate
  suppression, preview, accept, reject, and stale expiration.
- `common/kisurf/ai/ai_agent_panel_model.cpp` wires the default suggestion
  provider into the agent panel model.

## Goals

- Allow `AI_AGENT_SUGGESTION_PROVIDER` to be constructed with an `AI_PROVIDER`.
- Send the trigger activity, editor kind, context version, and context snapshot
  to the model as a bounded suggestion request.
- Parse one JSON object from the model response into `AI_SUGGESTION_RECORD`.
- Resolve all model-referenced objects from the current context by label.
- Reject model suggestions that reference objects absent from the supplied
  context.
- Preserve the deterministic selection suggestion as a safe fallback when no
  model provider is installed or the provider returns unstructured text.
- Keep the existing orchestrator as the only owner of ids, status transitions,
  duplicate suppression, and preview/accept lifecycle.

## Non-Goals

- No direct model edit execution.
- No geometry changes, placement moves, routing, or board mutation.
- No model-generated arbitrary UUID acceptance.
- No schema registry or external JSON schema dependency in this slice.
- No streaming suggestion updates.

## Model Contract

The suggestion provider asks the model to return exactly one JSON object. The
accepted shape is:

```json
{
  "kind": "preview",
  "title": "Short title",
  "body": "Human-readable rationale",
  "fingerprint": "optional stable key",
  "arguments": { "optional": "preview metadata" },
  "preview_objects": [ { "label": "U1.1" } ],
  "edit_objects": [ { "label": "U1.1" } ]
}
```

Rules:

- `kind` defaults to `preview`; `preview`, `edit`, and `chat` are recognized.
- `title` or `body` must be present.
- `preview_objects` must resolve to at least one object in the current context.
- `edit_objects` is optional and defaults to the resolved preview objects.
- Object references are accepted as either strings or objects with a `label`
  string.
- Every referenced label must resolve against selected objects first, then
  visible objects.
- If a response is not parseable JSON, the provider may fall back to the
  deterministic selection suggestion.
- If the model explicitly returns an empty object or `{"no_suggestion": true}`,
  no suggestion should be produced.

## Safety

- The model never gets to mint trusted object identity. Labels are only lookup
  keys into the current `AI_CONTEXT_SNAPSHOT`.
- The model never gets to set suggestion ids, sequence numbers, or lifecycle
  status.
- The model can carry opaque `arguments` for future preview materialization, but
  those arguments are inert in this slice.
- Missing or stale context results in no model suggestion or the same safe
  deterministic fallback used today.

## Testing Requirements

Unit tests must verify:

- The default provider still creates a previewable deterministic suggestion.
- A provider-backed model JSON response becomes a grounded
  `AI_SUGGESTION_RECORD`.
- The model request includes the suggestion contract, trigger reason, activity,
  and context prompt.
- Unknown object labels are rejected and do not become preview/edit objects.
- Explicit no-suggestion JSON returns no suggestion.
- Unstructured provider text falls back to the deterministic selection
  suggestion.
- Existing missing-selection and missing-activity gates remain in place.

## Acceptance Criteria

- Model-backed suggestion generation is possible through constructor injection.
- The default agent panel model can install a model-backed suggestion provider
  without sharing ownership of the chat runtime provider.
- All model object references are grounded locally before they reach the
  orchestrator.
- Existing suggestion lifecycle tests continue to pass.

## Spec Self-Review

- Open-marker scan: no unresolved open markers remain.
- Scope check: model output is converted to suggestions only; no edit execution
  is introduced.
- Safety check: object identity, ids, lifecycle status, and mutation authority
  remain local.
