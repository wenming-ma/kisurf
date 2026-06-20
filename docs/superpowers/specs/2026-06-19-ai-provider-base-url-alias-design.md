# AI Provider Base URL Alias Design

Date: 2026-06-19

## Goal

Make the OpenAI-compatible provider easier to use with common environment variable names by accepting `base_url` as a fallback base URL alias.

## Problem

KiSurf currently reads `KISURF_AI_BASE_URL` first and `OPENAI_BASE_URL` second.  This already covers the documented KiSurf setting and the common OpenAI-compatible setting, but a user can still reasonably provide `base_url=https://...` because many API snippets use that field name.  In that case KiSurf silently falls back to its built-in default, which makes direct use confusing.

## Requirements

1. Preserve the existing base URL priority:
   - `KISURF_AI_BASE_URL`
   - `OPENAI_BASE_URL`
   - default base URL
2. Insert `base_url` between `OPENAI_BASE_URL` and the default.
3. Normalize the chosen URL exactly as the existing variables are normalized.
4. Do not log, serialize, or print API keys.
5. Add tests that prove:
   - `OPENAI_BASE_URL` still works when the KiSurf variable is absent
   - `base_url` works when both higher-priority variables are absent
   - `KISURF_AI_BASE_URL` wins over `OPENAI_BASE_URL` and `base_url`
6. Update the quickstart to document all supported base URL names and the priority order.

## Design

`AI_PROVIDER_SETTINGS::FromEnvironment()` will keep its current `envValue()` helper.  The base URL selection will become one ordered chain:

```cpp
if( envValue( wxS( "KISURF_AI_BASE_URL" ), value )
    || envValue( wxS( "OPENAI_BASE_URL" ), value )
    || envValue( wxS( "base_url" ), value ) )
{
    settings.m_BaseUrl = normalizedUrl( value );
}
```

The lowercase alias is intentionally a fallback.  A product-specific variable should remain authoritative, and the OpenAI-compatible uppercase variable remains the preferred cross-tool setting.

## Non-goals

This slice does not add:

- persistent credential storage
- a graphical settings panel
- model alias changes
- network smoke tests against a real provider

## Self-review

- The change only affects provider configuration.
- The fallback alias makes the user's supplied `base_url` form work without weakening key handling.
- Tests cover the alias and priority order.
