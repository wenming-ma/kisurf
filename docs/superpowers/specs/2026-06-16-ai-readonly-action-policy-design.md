# AI Read-Only Action Policy Design

Date: 2026-06-16

## Purpose

The action catalog can expose many native KiCad/KiSurf actions to the model, but
`AI_TOOL_EXECUTION_POLICY` currently requires every action name to be explicitly
allowlisted before it can be executed. That is too narrow for read-only actions:
safe commands such as zoom, show, inspect, find, highlight, measure, and copy
should not require per-action plumbing once the catalog has classified them as
`AI_ACTION_SAFETY::ReadOnly`.

This spec updates policy evaluation so enabled read-only actions are allowed by
safety classification, while interactive, modifying, and destructive actions
remain gated.

## Source Anchors

- `common/kisurf/ai/ai_action_catalog.cpp` classifies action safety.
- `common/kisurf/ai/ai_tool_execution.cpp` evaluates allow/deny policy and
  invokes the runner.
- `qa/tests/common/test_ai_tool_execution.cpp` validates policy behavior.
- `common/kisurf/ai/ai_action_tool_call_handler.cpp` routes model tool calls
  into `AI_TOOL_EXECUTION_POLICY`.

## Goals

- Allow enabled read-only actions without an explicit action-name allowlist.
- Keep disabled actions denied.
- Keep destructive actions denied even if explicitly allowlisted.
- Keep modifying actions denied with `requires_preview`.
- Keep interactive actions requiring explicit allowlist entries.
- Preserve dry-run behavior for read-only actions.

## Non-Goals

- No action catalog reclassification in this slice.
- No broad execution of modifying, destructive, placement, routing, or edit
  commands.
- No UI confirmation flow for modifying action execution.
- No changes to the `AI_ACTION_RUNNER` interface.

## Design

Policy evaluation order should be:

1. Deny invalid descriptors as `unknown_action`.
2. Deny disabled descriptors as `disabled_action`.
3. Deny destructive descriptors as `destructive_denied`.
4. Deny modifying descriptors as `requires_preview`.
5. Allow read-only descriptors without checking the explicit allowlist.
6. For all remaining descriptors, require the explicit allowlist and deny as
   `not_allowlisted` when absent.

The explicit allowlist still matters for `Interactive` actions and any future
non-read-only safe categories. It no longer needs to contain every read-only
action name.

## Safety

- The change depends on action safety classification, so incorrectly classified
  actions remain the primary risk.
- The action catalog already classifies obvious edit verbs such as move, place,
  route, draw, add, update, cleanup, and fill as modifying.
- The action catalog already classifies delete, remove-file, and revert verbs as
  destructive.
- Tests must continue to prove that modifying and destructive actions are denied
  even when explicitly allowlisted.

## Testing Requirements

Unit tests must verify:

- Read-only actions are allowed without `AllowAction(...)`.
- Interactive actions remain denied as `not_allowlisted` without
  `AllowAction(...)`.
- Modifying actions remain denied as `requires_preview` even when allowlisted.
- Destructive actions remain denied as `destructive_denied` even when
  allowlisted.
- Executors can run an enabled read-only action without a manual allowlist entry.

## Acceptance Criteria

- Models can request safe read-only editor actions from the current catalog
  without bespoke per-action allowlist setup.
- Existing policy gates still block editor mutations and destructive commands.
- Existing tool-call handler behavior continues to use policy as the single
  execution gate.

## Spec Self-Review

- Open-marker scan: no unresolved placeholders remain.
- Scope check: this is policy-only and does not alter action classification.
- Safety check: the only newly auto-allowed category is `ReadOnly`.
