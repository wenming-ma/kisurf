# AI Agent wxFormBuilder Settings and Composer Design

## Purpose

The Agent panel must use wxFormBuilder for user-facing UI layout work. The current model settings dialog is hand-built in `ai_agent_panel.cpp`, and the chat composer is still a plain multiline text box plus separate buttons. This slice moves the model settings dialog into a generated wxFormBuilder base class and polishes the composer structure so the Agent panel feels closer to a modern chat surface while keeping KiCad-native controls.

## Scope

This slice includes:

- A new wxFormBuilder project for `AI_MODEL_SETTINGS_DIALOG_BASE`.
- Generated C++ base files for the model settings dialog.
- An `AI_MODEL_SETTINGS_DIALOG` implementation that inherits the generated base instead of building controls by hand.
- A refined Agent panel composer layout in `ai_agent_panel_base.fbp`, keeping the transcript, preview, log, mode selector, model settings button, background toggle, send, and stop controls.
- Tests that prove generated UI surfaces exist for provider, base URL, model, API key, help text, input, send, stop, and composer container controls.
- wxFormBuilder generation as the source of generated `.h/.cpp` files.

This slice does not change the model provider semantics. OpenAI-compatible remains the implemented provider, Anthropic-compatible remains visible and persisted but runtime-unsupported, and API keys remain stored through the existing local secret backend instead of source files or environment variables.

## UX Design

### Model Settings Dialog

The dialog uses a compact two-column form:

- Provider choice.
- Base URL text field.
- Model text field.
- API key password field.
- A short help text that says settings apply to the next chat/background request and that API keys are stored locally.
- Standard OK/Cancel buttons.

The dialog should feel like a real preferences surface, not a temporary debug prompt. Labels are aligned, text fields grow with the window, and the dialog has a stable minimum width.

### Chat Composer

The composer becomes a generated container panel at the bottom of the Agent panel:

- Multiline prompt input with a comfortable minimum height.
- A compact footer row with a status/help label on the left and Send/Stop buttons on the right.
- The input and buttons stay together as one visual composer area, instead of floating as unrelated controls.

The visual target is ChatGPT-like in interaction rhythm: transcript above, prompt below, primary send action close to the input. Because wxWidgets does not provide CSS-style rounded chat controls, this slice uses native spacing, border, panel grouping, and stable sizing rather than custom paint code.

## Architecture

`AI_MODEL_SETTINGS_DIALOG_BASE` lives in the common AI UI layer next to the Agent panel base. It is generated from `ai_model_settings_dialog_base.fbp`.

`AI_AGENT_PANEL` owns the concrete `AI_MODEL_SETTINGS_DIALOG` subclass in `ai_agent_panel.cpp`. The subclass only fills choices, transfers data between controls and `AI_MODEL_CONFIG`, and handles provider switching. The layout and event wiring remain generated.

The Agent panel generated base keeps existing member names for existing behavior and adds only composer container/status members. Existing tests that look for `m_Input`, `m_SendButton`, and `m_StopButton` remain valid.

## Error Handling

Loading and saving errors keep the existing behavior:

- Load errors show a warning before opening the dialog with normalized defaults.
- Save errors show an error and do not reload providers.
- Cancel leaves the current provider untouched.

Provider switching inside the dialog clears the key when switching to a different provider kind, preventing an OpenAI-compatible key from being silently shown as an Anthropic-compatible key.

## Tests

Add focused `qa_common` coverage:

- `AI_MODEL_SETTINGS_DIALOG_BASE` exposes provider, base URL, model, API key, help text, and button sizer controls.
- `AI_AGENT_PANEL_BASE` exposes the generated composer container, composer status label, input, send button, and stop button.
- Existing model config, provider, panel model, and panel tests continue to pass.

## Verification

The phase is verified by:

- A red test before generated UI changes.
- wxFormBuilder generation for both `.fbp` projects.
- `qa_common` targeted Agent/model tests.
- The broader `Ai*` test suite.
- `pcbnew` build.
- Computer Use inspection of the launched PCB Editor window for visible UI state or blocking popup evidence.
- Secret scanning that rejects committed API keys.
