# AI Provider Configuration Diagnostics Design

## Purpose

Make the Agent panel honest about whether it is connected to a real model. Today `MakeDefaultAiProvider()` silently falls back to `AI_STUB_PROVIDER` when `OPENAI_API_KEY` is not visible to the process. That is useful for tests, but confusing for direct use: the panel appears to work while never contacting the configured model endpoint.

## Source Observations

- `AI_PROVIDER_SETTINGS::DefaultBaseUrl()` already returns `https://sub2api.wenming-dev.org/v1`.
- `AI_PROVIDER_SETTINGS::FromEnvironment()` reads `KISURF_AI_BASE_URL`, `OPENAI_BASE_URL`, `OPENAI_API_KEY`, `KISURF_AI_MODEL`, and `OPENAI_MODEL`.
- `AI_OPENAI_COMPAT_PROVIDER::Generate()` already returns an explicit "OPENAI_API_KEY is not available" provider error when no key is present.
- `MakeDefaultAiProvider()` currently returns `AI_STUB_PROVIDER` whenever the key is missing, so that diagnostic is hidden from normal Agent panel usage.
- `KISURF_AI_PROVIDER=stub` already exists as an explicit opt-in for stub mode.

## Goals

1. Use the OpenAI-compatible provider by default, even when the API key is missing.
2. Preserve explicit stub mode through `KISURF_AI_PROVIDER=stub`.
3. Keep the existing default base URL.
4. Keep secrets out of logs, tests, docs, and committed files.
5. Add tests proving the no-key default returns the explicit missing-key diagnostic instead of stub text.
6. Add tests proving explicit stub mode still returns deterministic stub output.

## Non-Goals

- No UI settings screen is added.
- No credential storage is added.
- No attempt is made to set Windows user environment variables.
- No key value is printed or persisted.
- No network call is made in the no-key diagnostic path.

## Provider Selection Rules

`MakeDefaultAiProvider()` must use this order:

1. If `KISURF_AI_PROVIDER=stub` case-insensitively, return `AI_STUB_PROVIDER`.
2. Otherwise construct `AI_OPENAI_COMPAT_PROVIDER` from `AI_PROVIDER_SETTINGS::FromEnvironment()`.
3. Let `AI_OPENAI_COMPAT_PROVIDER::Generate()` report missing-key diagnostics when `settings.HasApiKey()` is false.

This keeps the default path aligned with real use: users see a clear configuration error if the key is missing, rather than a stub response.

## Test Requirements

Add tests to `qa/tests/common/test_ai_provider.cpp`:

1. `DefaultProviderReportsMissingKeyWhenUnconfigured`
   - Temporarily unset `KISURF_AI_PROVIDER`, `OPENAI_API_KEY`, `KISURF_AI_BASE_URL`, and `OPENAI_BASE_URL`.
   - Call `MakeDefaultAiProvider()`.
   - Generate a request.
   - Verify the response body contains `OPENAI_API_KEY`.
   - Verify the response body does not contain `Stub response`.

2. `DefaultProviderCanBeForcedToStub`
   - Temporarily set `KISURF_AI_PROVIDER=stub`.
   - Temporarily unset `OPENAI_API_KEY`.
   - Call `MakeDefaultAiProvider()`.
   - Generate a request.
   - Verify the response body contains `Stub response`.

Both tests must restore any environment variables they modify.

## Verification Requirements

- Run red after adding tests and before production changes.
- Run green by building `qa_common` and running `AiNativeProvider`.
- Run whitespace and secret scans before committing.
- Build `pcbnew` because Agent panel construction uses `MakeDefaultAiProvider()`.

## Self-Review

- Spec coverage: The design changes only default provider selection and tests both default diagnostic and explicit stub behavior.
- Safety check: Missing-key handling does not make network calls and does not expose secret values.
- Scope check: Credential storage, UI configuration, and Windows environment editing remain separate slices.
