# AI Model Settings Design

## Purpose

KiSurf needs a first-class model configuration entry so a user can connect the Agent panel and future background Agent without setting shell environment variables. The first implementation supports OpenAI-compatible runtime calls and reserves an explicit Anthropic-compatible provider kind for the UI and persisted configuration.

## Scope

This slice adds:

- A user-level model configuration object for provider kind, base URL, model name, and API key state.
- A settings store that persists non-secret fields in the KiCad user settings directory and stores API keys through the existing platform secret backend.
- Runtime provider selection that prefers the user-level model configuration over environment variables.
- An Agent panel `Model...` entry that opens a compact settings dialog, saves the configuration, and reloads the panel's chat/background model providers.
- Tests for config defaults, JSON persistence, secret persistence, OpenAI provider construction, unsupported Anthropic selection, model reload, and UI surface discoverability.

This slice does not implement Anthropic API request/response handling. If Anthropic-compatible is selected, the runtime reports that the provider kind is configurable but not implemented yet.

## Security Boundary

The default OpenAI-compatible base URL is the project default endpoint. API keys must not be committed into source, README examples, tests, generated specs, or logs. The settings JSON stores only provider kind, base URL, model name, and an API-key reference marker. The actual API key is stored via `OAUTH_SECRET_BACKEND`, using a KiSurf-specific service name and provider-specific key.

The UI may show the saved API key in a password-style control so users can replace or clear it. Observability entries and provider request traces must not include the API key; the provider still sends it only in the HTTP `Authorization` header.

## User Experience

The Agent panel gains a `Model...` button in the top control row near the mode and background-Agent controls. The dialog contains:

- Provider: `OpenAI-compatible` or `Anthropic-compatible`.
- Base URL.
- Model.
- API key, using `wxTE_PASSWORD`.

Defaults:

- Provider: `OpenAI-compatible`.
- Base URL: `https://sub2api.wenming-dev.org/v1`.
- Model: the existing KiSurf default model.
- API key: empty until saved in the local secret store.

Saving the dialog immediately reloads the panel model provider and its default background suggestion provider, so the next chat send or background-Agent suggestion uses the new settings without restarting KiCad.

## Architecture

`AI_MODEL_CONFIG` is a small common-layer value type. It converts to `AI_PROVIDER_SETTINGS` only when the provider kind is OpenAI-compatible.

`AI_MODEL_CONFIG_STORE` owns file and secret persistence. It accepts an explicit path and secret backend for tests, and has a `DefaultConfigPath()` for production. The path lives under `PATHS::GetUserSettingsPath()` as `kisurf_ai_model.json`.

`MakeDefaultAiProvider()` loads `AI_MODEL_CONFIG_STORE::LoadUserConfig()`. `KISURF_AI_PROVIDER=stub` remains as a developer/test escape hatch, but normal OpenAI-compatible base URL/model/key resolution no longer depends on `OPENAI_API_KEY` or base-url environment variables.

`AI_RUNTIME` gains a provider replacement method so `AI_AGENT_PANEL_MODEL` can reload providers without losing transcript, activity log, workspace context state, or installed tool-call handler. `AI_AGENT_PANEL_MODEL::ReloadDefaultProviders()` replaces both the chat runtime provider and the default model-backed suggestion provider.

## Error Handling

- Missing API key returns a user-facing provider configuration error naming `Model Settings`, not `OPENAI_API_KEY`.
- Missing or malformed model settings JSON falls back to safe defaults and an empty key. Malformed files return a load failure to callers that care, but the Agent stays usable enough to open settings.
- Unsupported Anthropic-compatible selection returns a deterministic provider error and never makes a network request.
- Secret-backend failures report a save/load error to the settings dialog; tests use an in-memory backend.

## Tests

Add focused `qa_common` coverage:

- `AI_MODEL_CONFIG` defaults normalize the OpenAI-compatible base URL and model.
- Saving config writes non-secret JSON and stores the API key in the secret backend, not in the JSON file.
- Loading config retrieves the secret-backed key.
- `MakeAiProviderFromModelConfig()` builds an OpenAI-compatible provider that sends the configured base URL/model/key.
- Anthropic-compatible config returns an unsupported-provider response.
- `AI_AGENT_PANEL_MODEL::ReloadDefaultProviders()` is available and preserves model state.
- `AI_AGENT_PANEL` exposes a model-settings command surface.

Update README to direct users to the Agent panel `Model...` settings instead of shell-level `OPENAI_API_KEY` setup.
