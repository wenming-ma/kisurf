# AI Action Tool Preview Acceptance Design

Date: 2026-06-19

## Goal

Bring model-requested editor actions under the same preview-first interaction
contract as semantic operation bundles and background preview suggestions.

## Problem

The earlier action tool handler allowed `kisurf_run_action` to execute an
allowlisted read-only action directly. That was useful for proving the
descriptor/policy bridge, but it violated the product rule that visible AI
actions should first become reviewable previews and only materialize after user
Accept.

This matters even for harmless-looking actions because the same tool pathway is
model-originated. A future action catalog will grow beyond `showAgentPanel`, and
the safety boundary must already be correct before more actions are exposed.

## Requirements

1. Treat all model-originated `kisurf_run_action` calls as dry runs, even when
   the model sends `dry_run:false`.
2. Preserve `kisurf_check_action` as a dry-run-only policy probe.
3. When a `kisurf_run_action` dry run is allowed, create a pending
   `AI_SUGGESTION_RECORD` with:
   - kind `Preview`;
   - title `Preview action`;
   - operation marker `{"operation":"action_preview"}`;
   - action name and original tool-call id;
   - current editor kind, context version, dynamic context metadata, and
     deterministic fingerprint.
4. Return model-visible result JSON with `status:"preview_ready"`,
   `preview_required:true`, `dry_run:true`, and the stored suggestion id.
5. Do not call the action runner while creating the preview suggestion.
6. Let the normal Agent/editor Accept path run the action once through the
   installed `AI_ACTION_RUNNER`.
7. Mark the suggestion accepted only after the runner reports success.
8. Keep generic edit acceptance conservative: suggestions without edit objects
   must not be accepted by `AI_EDIT_SESSION`.
9. Continue to log model input, tool calls, tool results, model output, and
   action-preview lifecycle details.

## Interaction Model

Chat Agent:

1. User requests an action in chat.
2. Model calls `kisurf_run_action`.
3. Native handler validates policy and creates an action preview suggestion.
4. User clicks Accept in the review controls.
5. The editor action runner executes the action and the suggestion becomes
   accepted.

Background Preview Agent:

1. Background logic may create the same action-preview suggestion shape when a
   future action provider is added.
2. Editor-level Tab/Enter acceptance can route through the same
   `AcceptLatestSuggestion()` path.
3. Esc or workspace pointer cancellation rejects the pending suggestion.

## Safety Notes

- Action preview suggestions are distinct from operation-only panel previews.
  Operation-only semantic previews remain unaccepted until a real edit adapter
  supplies edit objects.
- Action preview acceptance is allowed only because a named editor action runner
  exists and returns success.
- No model argument can bypass the forced dry-run normalization.
- The action runner remains installed by editor integration and still uses the
  action catalog/policy gate as the source of action exposure.

## Testing Requirements

- `AI_ACTION_TOOL_CALL_HANDLER` creates a pending action-preview suggestion for
  allowed `kisurf_run_action`.
- The fake action runner is not called during preview creation.
- The result JSON advertises `preview_ready` and includes the suggestion id.
- `AI_SUGGESTION_ORCHESTRATOR` can mark an action-preview suggestion accepted
  without treating it as an edit-object commit.
- `AI_AGENT_PANEL_MODEL` exposes the accepted status transition.
- `AI_AGENT_PANEL::AcceptLatestSuggestion()` executes action previews through
  the installed action runner before marking accepted.
- Common and PCB AI tests pass.
- GUI smoke launches the build-tree PCB Editor with Computer Use and confirms no
  missing-DLL/system-error modal appears.

## Self-Review

- Marker scan: no incomplete marker text remains.
- Scope check: this spec covers action preview acceptance only; it does not
  broaden the action catalog or add new modifying actions.
- Consistency check: all model-originated visible actions are preview-first.
- Safety check: actual execution remains behind explicit user Accept and the
  installed native action runner.
