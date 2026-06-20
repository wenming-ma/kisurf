# AI Agent Entry Discoverability Design

Date: 2026-06-17

## Purpose

The first Agent pane is implemented in PCB and schematic editors, but the entry
is easy to miss because it is exposed only as `View > Panels > Agent`. Users who
look for an AI or chat surface can reasonably conclude that the feature is not
present.

This spec defines the minimal product contract for discovering the existing
Agent pane without changing the pane runtime, provider, context, or suggestion
logic.

## Source Anchors

- `common/tool/actions.cpp` defines `ACTIONS::showAgentPanel` with stable action
  name `common.Control.showAgentPanel`.
- `pcbnew/menubar_pcb_editor.cpp` and `eeschema/menubar.cpp` already include the
  Agent pane in the `Panels` submenu.
- `pcbnew/pcb_edit_frame.cpp` and `eeschema/sch_edit_frame.cpp` instantiate the
  Agent pane, provide context snapshots, configure tool-call handling, and
  configure suggestion review.
- `common/kisurf/ai/ai_agent_panel.cpp` owns the shared pane UI and model
  interaction.

## Goals

- Expose a top-level `AI` menu in PCB Editor.
- Expose a top-level `AI` menu in Schematic Editor.
- Put the existing `Agent` pane toggle in that menu.
- Keep the existing `View > Panels > Agent` entry for consistency with other
  dockable panes and existing habits.
- Add a low-cost regression test that prevents the entry from being hidden only
  under `Panels` again.

## Non-Goals

- No new Agent pane layout.
- No provider configuration change.
- No new model tools.
- No change to Footprint Editor in this slice.
- No claim that semantic command execution or Next Action Preview is complete.

## Design

Add an `ACTION_MENU* aiMenu` to both editor menubars:

```cpp
ACTION_MENU* aiMenu = new ACTION_MENU( false, selTool );

aiMenu->SetTitle( _( "AI" ) );
aiMenu->Add( ACTIONS::showAgentPanel, ACTION_MENU::CHECK );
```

Append it as a top-level menu after `View`:

```cpp
menuBar->Append( viewMenu, _( "&View" ) );
menuBar->Append( aiMenu,   _( "&AI" ) );
```

The menu is intentionally small. It is the stable home for later AI-native
commands such as suggestion refresh, preview controls, and model/tool settings,
but this slice only exposes the already implemented Agent pane action.

## Test Strategy

Add a `qa_common` source-level discoverability contract test that reads:

- `pcbnew/menubar_pcb_editor.cpp`
- `eeschema/menubar.cpp`

The test asserts each editor has:

- an `AI` menu title,
- an `ACTIONS::showAgentPanel` item in that menu,
- a top-level `menuBar->Append( aiMenu, _( "&AI" ) )` call.

This avoids constructing full native editor frames in a common test while still
locking the product contract.

## Acceptance Criteria

- PCB Editor has a top-level `AI > Agent` entry.
- Schematic Editor has a top-level `AI > Agent` entry.
- Existing `View > Panels > Agent` entries remain.
- Targeted `qa_common` discoverability test passes.
- Building `pcbnew` and `eeschema` still succeeds.

## Spec Self-Review

- Scope check: this spec only changes discoverability of the existing pane.
- Safety check: no editor mutation or model execution is added.
- Regression check: the test guards both PCB and schematic menus.
