# AI Activity Timeline Tool Implementation Plan

Date: 2026-06-19
Spec: `docs/superpowers/specs/2026-06-19-ai-activity-timeline-tool-design.md`

## Target

Implement `kisurf_get_activity_timeline`, a read-only semantic tool that returns a bounded and optionally filtered view of `AI_CONTEXT_SNAPSHOT::m_RecentActivity`.

## Current Architecture Notes

- `AI_EDITOR_ACTIVITY_RECORDER` already maps editor tool events into `AI_ACTIVITY_RECORD`.
- PCB and schematic frames already register event observers and forward records into the Agent panel model.
- `AI_AGENT_PANEL_MODEL::SendUserText()` already merges stored activity into the context snapshot sent to the provider.
- `AI_CONTEXT_SNAPSHOT::AsJsonText()` already serializes recent activity, but fetching the full context is heavier than asking for the activity timeline only.

## Implementation Steps

1. Add red tests for semantic activity tool behavior.
   - Default call returns all recent activity up to the default limit.
   - `max_activity` bounds returned records while `activity_count` reports filtered total.
   - `kind` filters by activity kind.
   - `action_contains` filters by action substring.
   - Tool works without a suggestion sink.
   - Invalid kind, empty `action_contains`, and invalid max values return `malformed_arguments`.

2. Add red tests for provider schema.
   - Provider advertises eight tools.
   - `kisurf_get_activity_timeline` is present.
   - All parameters are optional.
   - Unknown arguments are rejected through `additionalProperties=false`.
   - `max_activity`, `kind`, and `action_contains` properties are declared.

3. Implement common semantic-tool helpers.
   - Parse activity timeline options beside the existing context and visual read tools.
   - Default `max_activity=64`.
   - Clamp `max_activity` to `128`.
   - Map `kind` strings to `AI_ACTIVITY_KIND`.
   - Reject unknown fields and malformed values.

4. Implement activity result generation.
   - Iterate `aRequest.m_ContextSnapshot.m_RecentActivity` in existing order.
   - Apply kind/action filters.
   - Count all filtered records.
   - Return up to `max_activity` serialized records.
   - Use the same JSON field names as `AI_CONTEXT_SNAPSHOT::AsJsonText()`.

5. Extend provider schema.
   - Add `activityTimelineToolParameters()`.
   - Add `kisurf_get_activity_timeline` to the OpenAI-compatible tools list with a read-only bounded description.

6. Verification.
   - Build `qa_common`.
   - Run `AiSemanticToolCallHandler`.
   - Run `AiNativeProvider`.
   - Run `git diff --check`.
   - Run a secret scan for touched files.

## Done Criteria

- Tests fail before implementation for missing activity timeline behavior.
- Tests pass after implementation.
- Activity tool response is valid JSON.
- No editor state is mutated.
- No unrelated files are staged.
- No plaintext API key is introduced.

## Implementation Status

Completed on 2026-06-19.

- Added `kisurf_get_activity_timeline` provider schema with optional `max_activity`, `kind`, and `action_contains` filters.
- Added read-only semantic handler support that works without a suggestion sink.
- Returned filtered activity count before truncation and bounded activity rows.
- Added tests for default bounded output, kind/action filtering, malformed arguments, and provider schema exposure.

Verification:

- `cmake --build out\build\x64-release --target qa_common --config Release`
- `qa_common.exe --run_test=AiSemanticToolCallHandler`
- `qa_common.exe --run_test=AiNativeProvider`
- `git diff --check`
- Secret scan for touched files

## Self-check

- This plan follows the spec-first rule.
- It does not duplicate or replace event observer wiring.
- It keeps activity retrieval common-layer and editor-agnostic.
- It makes the user's recent work inspectable through a focused tool.
- It preserves bounded tool-result behavior.
