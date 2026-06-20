# AI Unified Activity Timeline Design

Date: 2026-06-16

## Problem

Agent user/editor activity and runtime tool activity are currently stored in separate logs. Each log
assigns its own sequence numbers, so model-visible recent activity can contain duplicate sequence
values and an ambiguous causal order after the two lists are merged.

## Goals

- Keep Agent-visible user actions, model tool requests, and tool results in one ordered timeline.
- Preserve the existing activity record shape and bounded log capacity.
- Keep standalone `AI_RUNTIME` usage working with its own internal log.

## Non-Goals

- No timestamp field in this slice.
- No change to activity kind names or result payloads.
- No cross-process or persisted activity journal.

## Design

`AI_RUNTIME` will support using an externally owned `AI_ACTIVITY_LOG`. `AI_AGENT_PANEL_MODEL` will
own one activity log for the Agent session and construct the runtime with that shared log. User
activity recorded through the panel model and runtime tool activity will then receive sequence
numbers from the same source.

Standalone runtime instances continue to use an internal activity log.

## Verification

- Add a failing model-level test proving user activity followed by a runtime tool call produces
  strictly increasing sequence numbers in `ActivityRecords()`.
- Re-run Agent panel model, runtime, activity log, and provider tests.

## Self Review

- This does not increase the amount of data sent to providers.
- Activity ordering becomes clearer without changing existing prompt or JSON serialization.
- Runtime remains usable outside the Agent panel.
