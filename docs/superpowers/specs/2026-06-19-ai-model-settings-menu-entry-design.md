# AI Model Settings Menu Entry Design

## Purpose

KiSurf already has a model settings dialog inside the Agent panel and a
user-level model configuration store. This slice makes model configuration a
dedicated editor command so users can configure OpenAI-compatible or
Anthropic-compatible endpoints without first opening or discovering the chat
panel.

## Scope

This slice adds:

- A common `ACTIONS::showAiModelSettings` command.
- A top-level `AI -> Model Settings...` menu entry in both PCB editor and
  Schematic editor.
- PCB and schematic tool-controller bindings that invoke the same model
  settings dialog already used by the Agent panel.
- Tests that prove the action is defined, present in both AI menus, and bound
  by both editors.
- README wording that names the top-level AI menu entry as the primary settings
  path while keeping the Agent panel `Model...` button as an alternate path.

This slice does not add Anthropic runtime transport. `Anthropic-compatible`
remains a visible provider kind that persists and returns the existing
unsupported-runtime message when used.

## Configuration And Security Boundary

The existing `AI_MODEL_CONFIG_STORE` remains the source of truth. Normal
OpenAI-compatible runtime calls load provider kind, base URL, model, and API key
from Model Settings. `OPENAI_API_KEY`, `OPENAI_BASE_URL`, and model environment
variables are not normal runtime inputs.

The settings JSON stores only non-secret fields and `api_key_ref`. The API key
stays in the platform secret store under the existing KiSurf service/key. Local
developer or user machines may be preconfigured by writing to that user settings
file and platform credential entry outside the repository. Real API keys must
not appear in source, tests, docs, logs, commits, or generated artifacts.

The secret-store lookup is tied to a successfully loaded settings file. If the
settings JSON is absent, KiSurf should not treat a leftover platform credential
as active configuration.

## User Experience

Both editors expose:

- `AI -> Agent` to show or hide the Agent panel.
- `AI -> Model Settings...` to open model configuration directly.

The direct command opens the same dialog as the Agent panel `Model...` button:
provider kind, base URL, model, and password-style API key. Saving immediately
reloads the Agent panel model providers when the panel exists, so the next chat
send or background Agent suggestion uses the saved settings without restarting.

If the Agent panel is hidden, the menu command still works because the panel
object already exists in both editor frames. The command does not force the pane
visible; it only opens the modal settings dialog.

## Architecture

`ACTIONS::showAiModelSettings` is a common action next to
`ACTIONS::showAgentPanel`. The PCB and schematic menu builders add the action to
their existing top-level AI menus.

`PCB_EDIT_FRAME` and `SCH_EDIT_FRAME` each get a small
`ShowAgentModelSettings()` wrapper. The wrapper delegates to
`m_agentPanel->ShowModelSettingsDialog()` when the panel exists. Editor control
classes bind the new action to those wrappers, matching the existing
`ToggleAgentPanel` pattern.

No provider, JSON, or HTTP request protocol changes are required in this slice.

## Tests

Focused `qa_common` coverage should verify:

- `common/tool/actions.cpp` defines `ACTIONS::showAiModelSettings` with the
  stable action name `common.Control.showAiModelSettings` and friendly label
  `Model Settings...`.
- `include/tool/actions.h` declares the action.
- `pcbnew/menubar_pcb_editor.cpp` and `eeschema/menubar.cpp` add
  `ACTIONS::showAiModelSettings` to their top-level AI menus.
- `pcbnew/tools/board_editor_control.cpp` and
  `eeschema/tools/sch_editor_control.cpp` bind the action to model-settings
  handlers.

Existing model-config tests continue to prove OpenAI-compatible configuration
uses Model Settings, stores secrets outside JSON, and reserves
Anthropic-compatible as unsupported runtime.

## Self-Review

- Marker scan: clean; no incomplete requirements.
- Consistency check: this extends the existing Model Settings dialog instead of
  introducing a second settings store or key path.
- Scope check: this is one menu/command discoverability slice, not an Anthropic
  transport or Agent UI redesign.
- Ambiguity check: "dedicated settings entry" means a direct top-level AI menu
  command plus the existing in-panel button, both backed by the same store.
