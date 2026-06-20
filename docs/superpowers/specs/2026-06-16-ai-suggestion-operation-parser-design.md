# AI Suggestion Operation Parser Design

Date: 2026-06-16

## Problem

Model-backed suggestions currently carry free-form `m_ArgumentsJson`. PCB and schematic
preview/accept handlers each parse the same move payload locally. That duplicates the trust boundary
for model-authored JSON and makes future operations harder to keep consistent across editors.

## Goals

- Move suggestion-operation parsing into the common KiSurf AI layer.
- Provide one typed result for supported model suggestion operations.
- Keep malformed, unknown, or out-of-range arguments from reaching editor adapters.
- Preserve current behavior for missing or unsupported arguments: no move delta is applied.
- Make PCB and schematic preview/accept handlers consume the same parser.

## Non-Goals

- No new editor operation beyond `move`.
- No autonomous placement, routing, deletion, or object creation.
- No UI error reporting changes.
- No changes to model suggestion generation policy.

## Supported Payload

The first supported operation is:

```json
{
  "operation": "move",
  "dx": 100,
  "dy": -25
}
```

Rules:

- The root value must be a JSON object.
- `operation` must be the string `move`.
- `dx` and `dy` must be integer numbers representable as native `int`.
- Invalid JSON, missing fields, unknown operations, non-integer deltas, and out-of-range deltas return
  no operation.

## Design

- Add `include/kisurf/ai/ai_suggestion_operations.h`.
- Add `common/kisurf/ai/ai_suggestion_operations.cpp`.
- Define `AI_SUGGESTION_OPERATION_KIND` with `Unknown` and `Move`.
- Define `AI_SUGGESTION_OPERATION` with:
  - `m_Kind`
  - `m_MoveDelta`
  - `IsMove()`
- Expose:
  - `ParseAiSuggestionOperation( const wxString& aArgumentsJson )`
  - `ParseAiSuggestionMoveDelta( const wxString& aArgumentsJson )`
- Update PCB and schematic frame suggestion handlers to use `ParseAiSuggestionMoveDelta`.

## Verification

- Add common unit coverage for valid move payloads.
- Add common unit coverage for malformed JSON, unsupported operations, missing deltas, non-integer
  deltas, and out-of-range integer deltas.
- Re-run common AI tests plus PCB/SCH preview/edit adapter tests.

## Self Review

- The parser is intentionally conservative and does not try to coerce strings or floats into integer
  movement deltas.
- The parser is common-layer only and has no editor object access, so it cannot mutate KiCad state.
- Existing editor behavior is preserved because callers still receive `std::nullopt` when no supported
  move operation is present.
