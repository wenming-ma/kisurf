# AI Suggestion Context Metadata Design

Date: 2026-06-19

## Goal

Attach a small, stable context metadata block to every generated AI suggestion so the Agent panel, observability log, and future strategy layer can tell whether a preview came from routing, layout, panel, general selection work, idle context, or an unknown fallback.

## Problem

`dynamic_context` now exists on the unified context snapshot, but suggestions still lose that strategy signal once they are stored.  A routing preview, via-layout preview, model-generated object suggestion, and semantic tool preview all look similar in the suggestion record unless a reader parses titles, fingerprints, or operation JSON.

That makes the background Agent harder to debug and harder to extend.  Suggestions need to carry their trigger context in a normalized place, while still keeping the detailed operation JSON and preview objects unchanged.

## Requirements

1. `AI_SUGGESTION_RECORD` must expose:
   - `m_ContextKind`
   - `m_ContextDetailsJson`
2. `m_ContextKind` values should follow the existing dynamic-context vocabulary:
   - `routing`
   - `layout`
   - `panel`
   - `general`
   - `idle`
   - `unknown`
3. Existing suggestions without explicit metadata remain valid.
4. Built-in next-action providers must set context metadata:
   - via pattern provider -> `layout`
   - routing segment provider -> `routing`
5. Model-backed Agent suggestions must set context metadata derived from the trigger context.
6. Semantic preview tools must set context metadata:
   - `kisurf_preview_route_to_anchor` -> `routing`
   - `kisurf_preview_move_selected` -> current trigger context, usually `general` or `layout`
   - `kisurf_preview_create_copper_zone` -> `layout`
7. Observability suggestion details must include `context_kind` and `context_details` when present.
8. Agent panel suggestion summaries must display context kind when present.
9. The metadata must not affect duplicate suppression, preview execution, accept/reject behavior, or operation parsing.

## Interface

Suggestion record example:

```cpp
AI_SUGGESTION_RECORD suggestion;
suggestion.m_ContextKind = wxS( "routing" );
suggestion.m_ContextDetailsJson =
        wxS( "{\"source\":\"tool_state\",\"tool_state_kind\":\"routing_track\","
             "\"reason\":\"route_segment\"}" );
```

Observability details example:

```json
{
  "id": 3,
  "status": "pending",
  "title": "Preview route segment",
  "context_kind": "routing",
  "context_details": {
    "source": "tool_state",
    "tool_state_kind": "routing_track",
    "reason": "route_segment"
  }
}
```

Panel summary example:

```text
#7 [Previewing] [routing] Preview route segment
```

## Design Choices

### Store both kind and details

`m_ContextKind` gives UI and filters a cheap string.  `m_ContextDetailsJson` keeps provenance for debugging and future strategy selection without bloating the top-level record.

### Derive from trigger when possible

Model-backed suggestions and generic semantic previews can use the same tool-state and panel projection as `dynamic_context`.  Specialized providers can set more specific reasons because they already know why they generated a suggestion.

### Optional metadata

The fields are optional so older tests and hand-created suggestions remain valid.  Empty fields mean "unknown or not supplied" rather than invalid.

## Non-goals

This slice does not add:

- new strategy scheduling
- context-based suggestion filtering
- renderer changes
- schema changes for model tool calls
- new accept/reject behavior

## Self-review

- The change is additive and low-risk.
- It connects the dynamic-context projection to stored suggestions.
- It gives logs and UI a consistent way to show why a suggestion exists.
- It keeps operation execution behavior unchanged.
