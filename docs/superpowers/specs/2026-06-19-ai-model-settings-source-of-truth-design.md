# AI Model Settings Source Of Truth Design

## Purpose

KiSurf already has a model settings entry and an OpenAI-compatible provider path. This slice makes the intended boundary explicit: the Agent runtime reads model connection details from Model Settings, not from shell-level OpenAI environment variables. Environment variables remain only for developer-only stub mode and test/config-path overrides.

## Scope

This slice adds regression coverage and documentation for the model configuration source of truth:

- `AI_AGENT_PANEL` keeps the dedicated `Model...` entry.
- `AI_MODEL_CONFIG_STORE` remains the user-level persistence boundary for provider kind, base URL, model, and API key reference.
- OpenAI-compatible runtime calls use the configured base URL, model, and API key loaded through `AI_MODEL_CONFIG_STORE`.
- Anthropic-compatible stays visible as a reserved provider kind but continues to return the existing unsupported-runtime message.
- `MakeDefaultAiProvider()` must ignore `OPENAI_API_KEY`, `OPENAI_BASE_URL`, `base_url`, and OpenAI model environment variables when it builds the normal runtime provider.
- `KISURF_AI_PROVIDER=stub` remains available as a deliberate offline developer/test mode.

This slice does not implement Anthropic request/response transport and does not store any real API key in source, docs, tests, logs, or generated artifacts.

## Security Boundary

The settings JSON may contain only non-secret fields and the `api_key_ref` marker. The actual API key belongs in the platform secret store under `AI_MODEL_CONFIG_STORE::SecretServiceName()` and `SecretKeyForProvider(AI_MODEL_PROVIDER_KIND::OpenAiCompatible)`.

The default OpenAI-compatible base URL is allowed to be present in defaults and docs. Real API keys must not be checked into the repository. Local developer machines can be configured through the `Model...` dialog or by writing to the same platform secret-store service/key outside the repository.

## Runtime Behavior

Normal Agent startup:

1. `AI_AGENT_PANEL` creates `AI_AGENT_PANEL_MODEL` with `MakeDefaultAiProvider()`.
2. `MakeDefaultAiProvider()` checks only `KISURF_AI_PROVIDER=stub`.
3. If stub mode is not selected, it loads `AI_MODEL_CONFIG_STORE::LoadUserConfig()`.
4. `MakeAiProviderFromModelConfig()` builds the OpenAI-compatible provider or the unsupported Anthropic-compatible provider.

Missing OpenAI-compatible API key produces the existing user-facing message that points to `Model Settings`. It must not mention `OPENAI_API_KEY`, because the user-facing configuration surface is no longer the shell.

## Tests

Add focused `qa_common` coverage:

- A default provider with `OPENAI_API_KEY`, OpenAI base URL aliases, and OpenAI model environment variables set must still report missing Model Settings API key when the model settings path points at a missing config file.
- The stub-mode test remains green, proving the only supported normal environment escape hatch is explicit `KISURF_AI_PROVIDER=stub`.
- Existing `AI_MODEL_CONFIG` tests continue to prove JSON does not contain the API key and OpenAI-compatible provider construction uses the configured endpoint/model/key.

## Self-Review

- Placeholder scan: no TBD or TODO markers.
- Consistency check: the spec keeps the existing UI and secret-store architecture, and only hardens the configuration source.
- Scope check: this is a single regression-hardening slice, not an Anthropic runtime implementation.
- Ambiguity check: environment variables may still exist as legacy helper inputs in direct unit tests, but normal `MakeDefaultAiProvider()` behavior must ignore OpenAI-compatible environment variables.
