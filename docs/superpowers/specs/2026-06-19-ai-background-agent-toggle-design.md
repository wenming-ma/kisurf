# AI Background Agent Toggle Design

Date: 2026-06-19

## Goal

Add an explicit background Agent switch so the 7x24 preview-prediction loop runs only when enabled.  The Agent should still record user activity and update context mode while disabled, but it must not generate new automatic suggestions until the switch is on.

## Problem

`AI_AGENT_PANEL::RecordActivity()` currently records activity, collects context, updates panel mode, expires stale suggestions, and calls `AI_AGENT_PANEL_MODEL::UpdateSuggestions()` for every recorded editor activity.

That gives KiSurf a background-agent skeleton, but it lacks the user-visible control required by the product goal:

- background Agent should be 7x24 only if the switch is enabled
- activity sensing should continue even when automatic suggestions are off
- context mode should still update from routing/layout/tool state
- automatic preview suggestions should be gated

## Requirements

1. Add background Agent enabled state to `AI_AGENT_PANEL_MODEL`.
2. The default state must be disabled.
3. Expose:
   - `SetBackgroundAgentEnabled(bool)`
   - `BackgroundAgentEnabled() const`
4. Add `UpdateSuggestionsIfBackgroundEnabled(...)` to the model:
   - returns no suggestion when disabled
   - does not call the suggestion provider when disabled
   - delegates to `UpdateSuggestions(...)` when enabled
5. Keep existing `UpdateSuggestions(...)` behavior unchanged for direct/manual callers.
6. Add a checkbox/toggle to `AI_AGENT_PANEL`.
7. The checkbox must drive the model background state.
8. `AI_AGENT_PANEL::RecordActivity()` must:
   - always record activity
   - always capture current context if a provider exists
   - always update background workspace context from tool state
   - always expire stale suggestions
   - only create new automatic suggestions when background Agent is enabled
9. Logs and activity timeline must still reflect recorded user activity when disabled.

## Non-goals

This slice does not add:

- scheduling/throttling
- async worker queues
- persistent preferences
- auto-preview without explicit user acceptance
- new prediction algorithms
- UI redesign beyond the switch

Those should be layered after the switch makes the runtime behavior controllable.

## Self-check

- The design makes background behavior explicit and user-controlled.
- It preserves activity sensing and dynamic context switching.
- It prevents unexpected automatic model suggestion calls while disabled.
- It keeps direct suggestion APIs available for tests and manual flows.
- It moves toward the requested 7x24 Agent without making it always-on.
