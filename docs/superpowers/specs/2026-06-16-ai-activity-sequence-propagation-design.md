# AI Activity Sequence Propagation Design

Date: 2026-06-16

## Problem

`AI_ACTIVITY_LOG::Append` assigns stable sequence numbers, but `AI_AGENT_PANEL::RecordActivity`
currently copies the incoming activity before recording it. Suggestions triggered by that panel path
therefore receive the unsequenced pre-append activity, while the activity log stores the sequenced
record.

## Goals

- Preserve the sequence number assigned by the model's activity log.
- Use the sequenced activity as the suggestion trigger activity.
- Keep existing activity storage and context inclusion behavior unchanged.

## Non-Goals

- No new activity kinds.
- No new event filtering rules.
- No change to activity log capacity or ordering.

## Design

`AI_AGENT_PANEL_MODEL::RecordActivity` will return the appended `AI_ACTIVITY_RECORD`. Callers may
ignore the return value, but `AI_AGENT_PANEL::RecordActivity` will use it when building suggestion
triggers.

This keeps sequence assignment centralized in `AI_ACTIVITY_LOG` while making the recorded activity
the same object that drives suggestion generation.

## Verification

- Add unit coverage that `AI_AGENT_PANEL_MODEL::RecordActivity` returns the sequenced record and that
  the stored activity has the same sequence.
- Re-run Agent panel model, activity log, and suggestion provider tests.

## Self Review

- The change is source-compatible for callers that ignore the return value.
- It does not change which events are recorded.
- It makes request ids and trigger activity sequences more useful for audit and suggestion
  deduplication.
