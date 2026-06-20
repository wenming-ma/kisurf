# AI Tool State And Next Action Preview Implementation Plan

Date: 2026-06-17

## Goal

Implement the first usable AI-native real-time collaboration layer on top of the
existing Agent pane, context snapshots, visual snapshots, suggestion lifecycle,
preview adapters, and edit adapters.

This plan is derived from:

- `docs/superpowers/specs/2026-06-17-ai-tool-state-next-action-preview-design.md`
- `docs/superpowers/specs/2026-06-17-ai-agent-entry-discoverability-design.md`

## Current Status

- [x] Agent pane exists in PCB and schematic editors.
- [x] Agent context includes visible/selected objects, actions, activity, and
  native visual snapshots.
- [x] Suggestions can be previewed/accepted/rejected for bounded move
  operations.
- [x] PCB and schematic now expose a top-level `AI > Agent` entry.
- [x] Tool-state and per-context workspace data contracts exist.
- [x] Agent panel model can preserve independent background workspace context
  state.
- [x] Context snapshots can carry tool-state prompt and JSON payloads.
- [x] PCB Agent context captures current tool/action/cursor state.
- [x] PCB tool-state context now includes board-derived shared context and
  routing/via mode context: active layer, track width, via diameter/drill,
  cursor, and active PNS router net/start/end when routing is in progress.
- [x] Schematic Agent context reports baseline idle/selecting state.
- [x] Chat semantic tools for `move_selected` and `create_copper_zone` are
  complete.
- [x] Next Action Preview for routing and via placement is complete.
- [x] Per-context background workspace state is complete.
- [x] PCB suggestion review tests cover reject-without-board-mutation and a
  real `BOARD_COMMIT`-backed via accept path.

## Implementation Steps

1. Tool-state data contracts
   - [x] Add `AI_TOOL_STATE_KIND`, `AI_TOOL_STATE_SNAPSHOT`,
     `AI_AGENT_WORKSPACE_CONTEXT_KIND`, and
     `AI_AGENT_WORKSPACE_CONTEXT_STATE`.
   - [x] Add prompt/JSON formatting helpers.
   - [x] Add `qa_common` tests for defaults, validity, stable JSON, and prompt
     bounds.

2. Background workspace-context model
   - [x] Extend `AI_AGENT_PANEL_MODEL` with per-context state storage for the
     background Preview Agent.
   - [x] Add model APIs to set active workspace context, update state JSON, and
     retrieve saved context state.
   - [x] Add tests that switching General/Routing/ViaPlacement preserves prior
     candidate summaries without exposing these contexts as Chat Panel modes.

3. Tool-state capture
   - [x] Add a common `AI_TOOL_STATE_PROVIDER` interface.
   - [x] Add a PCB provider that maps tool/action state into
     `RoutingTrack`, `PlacingVia`, `PlacingFootprint`, `DrawingZone`, and
     `MovingSelection`.
   - [x] Use `TOOL_MANAGER` event observers for activation/click events.
   - [x] Populate PCB shared/mode JSON from `BOARD_DESIGN_SETTINGS`,
     active layer, cursor state, and active PNS router placement state.
   - [x] Add a separate bounded path for cursor/motion state because existing
     `AI_EDITOR_ACTIVITY_RECORDER` intentionally filters motion and drag.
   - [x] Keep schematic support as `Idle`/`Selecting` until schematic-specific
     next actions are specified.

4. Context injection
   - [x] Extend `AI_CONTEXT_SNAPSHOT` or a wrapper request payload to include
     current tool state.
   - [x] Include tool state in provider-bound JSON and prompt text.
   - [x] Add tests proving provider requests receive active tool state and mode
     context.

5. Next-action operation parser
   - [x] Extend `AI_SUGGESTION_OPERATION` or add `AI_NEXT_ACTION_OPERATION`.
   - [x] Parse `route_segment_preview`.
   - [x] Parse `place_via_preview`.
   - [x] Parse `create_copper_zone_preview`.
   - [x] Parse `move_selected`.
   - [x] Reject malformed JSON, missing coordinates, invalid dimensions, and
     unsupported operations.

6. Synthetic geometry preview adapter
   - [x] Add a PCB preview API for synthetic route segments and vias.
   - [x] Ensure preview objects are owned by preview sessions and cleared by
     owner ID.
   - [x] Add PCB tests for show/clear route segment and via previews.

7. Deterministic via pattern provider
   - [x] Build a provider that reads recent vias from context/tool state.
   - [x] Detect equal-spacing horizontal and vertical via patterns.
   - [x] Produce a low-confidence candidate for two vias and a normal candidate
     for three vias.
   - [x] Add tests for aligned, non-aligned, wrong-net, and stale-context cases.

8. Routing segment provider
   - [x] Activate only while PCB routing state is active.
   - [x] Read active net/layer/width/start/cursor from tool state.
   - [x] Produce one short preview candidate.
   - [x] Add tests for inactive tool, missing net, stale context, and valid
     candidate.

9. Next-action controller
   - [x] Add `AI_NEXT_ACTION_CONTROLLER` using the existing
     `AI_SUGGESTION_PROVIDER` contract.
   - [x] Implement debounce/deduplication on top of existing stale expiration.
   - [x] Connect candidates to preview/accept/reject handlers.
   - [x] Add common tests for lifecycle behavior.

10. Agent UI integration
    - [x] Keep `AI_AGENT_PANEL` as a single chat command surface.
    - [x] Render candidate summary and Preview/Accept/Reject controls without
      claiming completion before a candidate exists.
    - [x] Preserve per-context background state when switching tools without
      adding Routing/Place/Zone tabs or chips to the Chat Panel.
    - [x] Add UI model tests where possible.
    - [x] Build both editors.

11. Semantic command tools
    - [x] Add typed semantic tool handler separate from raw action execution.
    - [x] Implement `move_selected` through existing move preview/edit support.
    - [x] Implement `create_copper_zone` as preview first, accept through
      `BOARD_COMMIT`.
    - [x] Keep raw KiCad action execution deny-by-default.
    - [x] Add policy and parser tests.

12. Verification and manual smoke test
    - [x] Run targeted `qa_common` AI suites.
    - [x] Run targeted `qa_pcbnew` AI suites.
    - [x] Build `pcbnew` and `eeschema`.
    - [x] Launch PCB Editor from `out/build/x64-release`.
    - [x] Confirm `AI > Agent` opens the pane.
    - [x] Verify reject does not mutate the board through
      `AiPcbSuggestionReview`.
    - [x] Verify accept can use a real `BOARD_COMMIT` boundary through
      `AiPcbOperationEditAdapter`.
    - [ ] Place three equal-spaced vias and confirm next via preview appears.
    - [ ] Route a track segment and confirm next segment preview can appear.
    - [ ] Confirm accept through the live GUI undo stack.

    Note: via and routing preview generation are covered by
    `AiNextActionProvider`; synthetic route/via/zone previews are covered by
    `AiPcbPreviewAdapter`; accept/reject materialization is covered by
    `AiPcbOperationEditAdapter` and `AiPcbSuggestionReview`; PCB tool-state
    board/routing/via context is covered by `AiPcbToolStateProvider`. The
    remaining unchecked items are hand-driven GUI smoke items, not missing core
    contracts.

## Execution Rules

- Use test-first development for each behavior slice.
- Do not put user API keys or provider secrets in code, tests, docs, or commits.
- Do not auto-run modifying model tools without preview and user accept.
- Do not add IPC transport first; implement native contracts first and project
  them later.
- Keep large model calls off mouse-motion paths.

## Open Decisions

- Whether two-via equal spacing should show only in the Agent pane or on-canvas.
  Recommended: Agent pane only until the third via confirms intent.
- Whether route segment preview should be accepted by a keyboard shortcut.
  Recommended: defer until basic preview/accept UI is stable.
- Whether background workspace context state should persist across app sessions.
  Recommended: no project persistence in the first implementation.

## Completion Criteria

This work is complete only when all of the following are true:

- `AI > Agent` is visible in PCB and schematic editors.
- Tool-state context is present in provider requests.
- Background workspace context state switches and restores correctly without
  visible Chat Panel categories.
- Via equal-spacing preview works in PCB Editor.
- Routing next-segment preview works in PCB Editor.
- `move_selected` and `create_copper_zone` semantic commands preview before
  accepting.
- Targeted tests and editor builds pass.
