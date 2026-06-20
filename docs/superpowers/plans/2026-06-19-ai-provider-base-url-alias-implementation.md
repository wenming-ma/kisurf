# AI Provider Base URL Alias Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Accept `base_url` as a fallback environment alias for the OpenAI-compatible provider base URL.

**Architecture:** Keep provider configuration centralized in `AI_PROVIDER_SETTINGS::FromEnvironment()`.  Add focused provider tests for alias and priority behavior, then update the quickstart documentation to match the runtime contract.

**Tech Stack:** C++17, wxWidgets environment helpers, Boost unit tests, Markdown docs.

---

Date: 2026-06-19
Spec: `docs/superpowers/specs/2026-06-19-ai-provider-base-url-alias-design.md`

## Files

- Modify: `qa/tests/common/test_ai_provider.cpp`
  - Add environment alias and priority tests.
- Modify: `common/kisurf/ai/ai_provider.cpp`
  - Add `base_url` fallback to `AI_PROVIDER_SETTINGS::FromEnvironment()`.
- Modify: `README.md`
  - Document supported base URL environment names and priority.

## Task 1: Add Failing Provider Configuration Tests

- [x] Add `ENV_GUARD lowerBaseGuard( wxS( "base_url" ) );` in tests that manipulate provider base URL variables.
- [x] Add `ProviderSettingsReadOpenAiBaseUrlAlias`:

```cpp
BOOST_AUTO_TEST_CASE( ProviderSettingsReadOpenAiBaseUrlAlias )
{
    ENV_GUARD kisurfBaseGuard( wxS( "KISURF_AI_BASE_URL" ) );
    ENV_GUARD openaiBaseGuard( wxS( "OPENAI_BASE_URL" ) );
    ENV_GUARD lowerBaseGuard( wxS( "base_url" ) );

    wxUnsetEnv( wxS( "KISURF_AI_BASE_URL" ) );
    wxSetEnv( wxS( "OPENAI_BASE_URL" ), wxS( "https://openai.example.test/v1/" ) );
    wxSetEnv( wxS( "base_url" ), wxS( "https://lower.example.test/v1/" ) );

    AI_PROVIDER_SETTINGS settings = AI_PROVIDER_SETTINGS::FromEnvironment();

    BOOST_CHECK_EQUAL( settings.m_BaseUrl,
                       wxString( wxS( "https://openai.example.test/v1" ) ) );
}
```

- [x] Add `ProviderSettingsReadLowercaseBaseUrlAlias`:

```cpp
BOOST_AUTO_TEST_CASE( ProviderSettingsReadLowercaseBaseUrlAlias )
{
    ENV_GUARD kisurfBaseGuard( wxS( "KISURF_AI_BASE_URL" ) );
    ENV_GUARD openaiBaseGuard( wxS( "OPENAI_BASE_URL" ) );
    ENV_GUARD lowerBaseGuard( wxS( "base_url" ) );

    wxUnsetEnv( wxS( "KISURF_AI_BASE_URL" ) );
    wxUnsetEnv( wxS( "OPENAI_BASE_URL" ) );
    wxSetEnv( wxS( "base_url" ), wxS( "https://lower.example.test/v1/" ) );

    AI_PROVIDER_SETTINGS settings = AI_PROVIDER_SETTINGS::FromEnvironment();

    BOOST_CHECK_EQUAL( settings.m_BaseUrl,
                       wxString( wxS( "https://lower.example.test/v1" ) ) );
}
```

- [x] Add `ProviderSettingsPrefersKiSurfBaseUrlAlias`:

```cpp
BOOST_AUTO_TEST_CASE( ProviderSettingsPrefersKiSurfBaseUrlAlias )
{
    ENV_GUARD kisurfBaseGuard( wxS( "KISURF_AI_BASE_URL" ) );
    ENV_GUARD openaiBaseGuard( wxS( "OPENAI_BASE_URL" ) );
    ENV_GUARD lowerBaseGuard( wxS( "base_url" ) );

    wxSetEnv( wxS( "KISURF_AI_BASE_URL" ), wxS( "https://kisurf.example.test/v1/" ) );
    wxSetEnv( wxS( "OPENAI_BASE_URL" ), wxS( "https://openai.example.test/v1/" ) );
    wxSetEnv( wxS( "base_url" ), wxS( "https://lower.example.test/v1/" ) );

    AI_PROVIDER_SETTINGS settings = AI_PROVIDER_SETTINGS::FromEnvironment();

    BOOST_CHECK_EQUAL( settings.m_BaseUrl,
                       wxString( wxS( "https://kisurf.example.test/v1" ) ) );
}
```

- [x] Run:

```powershell
cmd /s /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build out\build\x64-release --target qa_common --config Release'
```

- [x] Expected result: FAIL because `base_url` is not read yet.

## Task 2: Implement Alias

- [x] Modify `AI_PROVIDER_SETTINGS::FromEnvironment()` base URL selection:

```cpp
if( envValue( wxS( "KISURF_AI_BASE_URL" ), value )
    || envValue( wxS( "OPENAI_BASE_URL" ), value )
    || envValue( wxS( "base_url" ), value ) )
{
    settings.m_BaseUrl = normalizedUrl( value );
}
```

- [x] Run:

```powershell
cmd /s /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build out\build\x64-release --target qa_common --config Release'
```

- [x] Run:

```powershell
cmd /s /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && set PATH=%CD%\out\build\x64-release\api;%CD%\out\build\x64-release\common;%CD%\out\build\x64-release\common\gal;%CD%\out\build\x64-release\qa\tests\common;%PATH% && out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiNativeProvider --log_level=test_suite'
```

- [x] Expected result: PASS with the existing schema-file warning acceptable only if the executable exits 0.

## Task 3: Update Quickstart Documentation

- [x] In `README.md`, replace the optional base URL snippet with:

```powershell
# Preferred KiSurf-specific setting
Set-Item Env:KISURF_AI_BASE_URL 'https://sub2api.wenming-dev.org/v1'

# Also accepted when KISURF_AI_BASE_URL is absent
Set-Item Env:OPENAI_BASE_URL 'https://sub2api.wenming-dev.org/v1'
Set-Item Env:base_url 'https://sub2api.wenming-dev.org/v1'

Set-Item Env:KISURF_AI_MODEL 'gpt-4.1-mini'
```

- [x] Add one sentence after the snippet:

```text
Base URL priority is `KISURF_AI_BASE_URL`, then `OPENAI_BASE_URL`, then `base_url`, then the built-in default.
```

## Task 4: Verify And Commit

- [x] Run `git diff --check`.
- [x] Run dynamic secret scan without echoing any key:

```powershell
$prefix = 'sk' + '-'; $pattern = $prefix + '[A-Za-z0-9_-]{20,}|OPENAI_API_KEY=.*' + $prefix; rg -n $pattern common docs include qa README.md
```

- [x] Build `pcbnew`.
- [x] Build `eeschema`.
- [x] Stage only:
  - `common/kisurf/ai/ai_provider.cpp`
  - `qa/tests/common/test_ai_provider.cpp`
  - `README.md`
  - `docs/superpowers/specs/2026-06-19-ai-provider-base-url-alias-design.md`
  - `docs/superpowers/plans/2026-06-19-ai-provider-base-url-alias-implementation.md`
- [x] Commit with:

```text
feat: accept base_url AI provider alias
```

## Handoff Notes

- Do not print or store any API key while testing this slice.
- Do not stage unrelated `qa/tests/pcbnew/test_module.cpp` changes.
