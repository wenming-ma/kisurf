# AI Agent Panel wxFormBuilder Redesign Design

## Purpose

The current Agent panel is a manually assembled wxWidgets panel. It works, but
the chat area and input controls still look like a debug surface. KiSurf needs a
more polished, maintainable Agent UI that follows the same generated-base plus
derived-logic pattern used throughout KiCad.

This slice moves the Agent panel layout into wxFormBuilder-generated files and
keeps `AI_AGENT_PANEL` focused on runtime behavior. The first visual target is a
ChatGPT-inspired desktop pane: readable conversation history, compact mode/model
controls, a clear background-Agent switch, preview/log tabs, and a more usable
multi-line composer.

## Requirements

1. Use wxFormBuilder 4.2.1 or newer to create an Agent panel `.fbp` file and
   generate matching C++ base files.
2. Add generated files:
   - `common/kisurf/ai/ai_agent_panel_base.fbp`
   - `common/kisurf/ai/ai_agent_panel_base.h`
   - `common/kisurf/ai/ai_agent_panel_base.cpp`
3. The generated class must be named `AI_AGENT_PANEL_BASE`.
4. `AI_AGENT_PANEL` must inherit from `AI_AGENT_PANEL_BASE`, not directly from
   `wxPanel`.
5. Generated base files must contain layout and virtual event hooks only. Agent
   runtime logic stays in `AI_AGENT_PANEL`.
6. The base layout must expose protected members for:
   - mode choice
   - model-settings button
   - background-Agent checkbox
   - notebook
   - chat transcript HTML window
   - preview text control
   - log text control
   - multi-line prompt input
   - send and stop buttons
   - preview, accept, and reject buttons
7. The chat transcript should use KiCad's `HTML_WINDOW` wrapper so the panel can
   render message cards and theme-aware HTML instead of plain text.
8. The input composer must be multi-line and sized for prompt writing, while
   still supporting Enter-to-send through existing event handling.
9. Preview and Log remain separate notebook tabs.
10. Existing Agent behavior must remain intact:
    - send text
    - stop request
    - model settings
    - background-Agent toggle
    - preview/accept/reject
    - semantic UI tree
    - observability log updates
11. The redesign must not expose or store API keys in generated UI files.
12. After build, the real PCB Editor must be opened and inspected with Computer
    Use to verify the panel renders without missing-DLL or runtime popup errors.

## Visual Direction

The pane should feel like a compact professional assistant embedded inside an
EDA editor, not a landing page. Use restrained native controls with better
spacing:

- top row: mode selector, model-settings button, background-Agent toggle;
- center: notebook with Chat, Preview, and Log pages;
- Chat page: HTML transcript area with readable message blocks;
- bottom: multi-line prompt composer with Send and Stop aligned to the right.

The UI should prioritize repeated engineering use: scannable, calm, and dense
enough for a side panel.

## Architecture

`AI_AGENT_PANEL_BASE` owns the generated widget tree and virtual event hooks.
`AI_AGENT_PANEL` derives from it and binds or overrides the behavior. Existing
model/provider/suggestion code remains in place.

`RefreshTranscript()` will move from plain text output to HTML rendering. The
HTML generation should be a small helper that can be unit-tested without a
running wx event loop. It must escape message content before inserting it into
HTML.

## Testing

1. A red API-surface test proves `AI_AGENT_PANEL` derives from
   `AI_AGENT_PANEL_BASE`.
2. A red API-surface test proves the generated class exposes the expected
   protected controls through a test shim.
3. A red transcript rendering test proves message content is escaped and role
   labels are included.
4. After implementation, run targeted `AiAgentPanel` tests.
5. Run the broader common `Ai*` test slice.
6. Build `pcbnew`.
7. Open build-tree PCB Editor through `tools/run_from_build.ps1` and use
   Computer Use to verify:
   - no missing-DLL/system-error modal;
   - `AI -> Agent` opens;
   - Agent panel shows the redesigned controls;
   - text can be typed into the composer and Send remains available.

## Non-Goals

- No model/provider behavior changes.
- No Anthropic runtime implementation.
- No new network calls.
- No full custom-drawn chat canvas.
- No shortcut overhaul beyond preserving the existing enter-to-send behavior.

## Self-Review

- Placeholder scan: no TBD/TODO markers remain.
- Scope check: one UI-structure slice, not a full Agent runtime rewrite.
- Safety check: API key data stays in the existing model-settings dialog/store.
- Verification check: includes generated-code, tests, build, and real GUI smoke.
