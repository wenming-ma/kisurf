# Preview Session Lifecycle Design

## Context

PCB suggestions can already render board previews through `AI_PREVIEW_SESSION`,
`KISURF_AI_PCB_PREVIEW_ADAPTER`, and editor integration callbacks. Accepted
route/via/zone previews can also be committed through `AI_EDIT_SESSION`.

There are two distinct upper-level workflows:

1. Chat panel: the user directly instructs the agent, similar to Codex.
2. Background agent: suggestions are produced continuously and appear in the
   user's working editor surface.

Both workflows may share lower-level suggestion, review, preview, and edit
infrastructure, but preview ownership belongs to the editor/workspace surface,
not to the chat panel. The missing contract is that terminal review actions
must clear editor-owned preview graphics without making the chat panel the
source of truth for visible canvas state.

## Goals

- Keep chat panel semantics limited to command/review controls and log state.
- Let editor integrations own preview render and preview cleanup.
- Add an editor-provided Reject handler alongside existing Preview and Accept
  handlers so canvas cleanup can happen in the editor workflow.
- Clear editor-owned previews after successful Accept or Reject.
- Keep existing preview rendering and edit commit behavior unchanged.

## Non-Goals

- No `visible_preview` state in `AI_AGENT_PANEL_SEMANTIC_VIEW`.
- No `agent.preview.visible` node in the chat panel semantic tree.
- No new GAL rendering primitives.
- No keyboard shortcut work in this slice.
- No change to model-generated suggestion formats.
- No attempt to persist preview state across editor teardown.

## Chosen Approach

`AI_AGENT_PANEL` remains the command surface. It exposes three review callbacks:

- Preview: the editor renders a suggestion into the working canvas.
- Accept: the editor commits the suggestion and clears any canvas preview.
- Reject: the editor marks the suggestion rejected and clears any canvas preview.

The panel does not record whether a preview is visible. Future canvas/workspace
semantic state should be exposed by editor context adapters, visual snapshots,
or workspace panel/canvas records, not by the chat panel.

The PCB editor implements Reject by calling `AI_AGENT_PANEL_MODEL::RejectSuggestion`
and then `GetCanvas()->GetView()->ClearPreview()` when rejection succeeds. The
Accept handler performs its existing edit commit and clears the same editor
preview on success.

## Alternatives Considered

- Store visible preview state in the chat panel semantic tree. Rejected because
  it merges two distinct workflows: chat command UI and editor-native realtime
  suggestions.
- Make `AI_PREVIEW_SESSION` own cleanup in its destructor. Rejected because the
  current PCB integration constructs the session inside the Preview handler; a
  destructor clear would remove the preview immediately.
- Move preview sessions into `PCB_EDIT_FRAME` as persistent members. This is more
  powerful but broader than needed for the current stale-preview problem.

## Lifecycle Rules

- Preview succeeds: the editor may show preview graphics on the working surface.
- Preview fails: no terminal state change occurs.
- Accept succeeds: the editor clears the canvas preview after committing.
- Reject succeeds: the editor clears the canvas preview after marking the
  suggestion rejected.
- Accept or Reject fails: the editor leaves preview graphics unchanged because no
  terminal transition was confirmed.

## Testing

Use TDD:

- Add panel surface coverage proving `ConfigureSuggestionReview` accepts
  Preview, Accept, and Reject handlers.
- Keep chat panel semantic tests free of canvas-visible preview state.
- Add preview-session coverage for a small read-only active-state accessor so
  editor-owned lifecycle tests can observe current preview state without
  reaching into adapters.
- Build PCB editor integration to verify the configured Accept and Reject
  callbacks can call `KIGFX::VIEW::ClearPreview()` without changing edit
  behavior.

## Self-Review

- Marker scan: no incomplete marker text remains.
- Scope check: the slice separates ownership and cleanup only; rendering and
  edit commit behavior stay as-is.
- Ambiguity check: clearing happens in editor callbacks after successful
  terminal actions, not in chat-panel semantic state.
