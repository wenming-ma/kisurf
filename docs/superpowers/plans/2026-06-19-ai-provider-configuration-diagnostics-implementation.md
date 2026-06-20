# AI Provider Configuration Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop silently falling back to the stub provider when the default Agent provider is unconfigured.

**Architecture:** Add environment-preserving tests around `MakeDefaultAiProvider()`, then change provider selection so explicit `KISURF_AI_PROVIDER=stub` is the only default stub path. The OpenAI-compatible provider remains responsible for the existing no-key diagnostic and avoids network calls when the key is absent.

**Tech Stack:** KiSurf common C++20, `AI_PROVIDER_SETTINGS`, `AI_OPENAI_COMPAT_PROVIDER`, `AI_STUB_PROVIDER`, wx environment helpers, Boost unit tests in `qa_common`.

---

## File Structure

- Modify `qa/tests/common/test_ai_provider.cpp`
  - Add environment guard helper.
  - Add default-provider no-key diagnostic test.
  - Add explicit stub mode test.
- Modify `common/kisurf/ai/ai_provider.cpp`
  - Change `MakeDefaultAiProvider()` selection behavior.
- Modify `docs/superpowers/plans/2026-06-19-ai-provider-configuration-diagnostics-implementation.md`
  - Check off completed steps after verification.

## Task 1: Provider Selection Tests

**Files:**
- Modify: `qa/tests/common/test_ai_provider.cpp`

- [x] **Step 1: Add environment guard helper**

In `qa/tests/common/test_ai_provider.cpp`, inside the anonymous namespace before `BOOST_AUTO_TEST_SUITE`, add:

```cpp
class ENV_GUARD
{
public:
    explicit ENV_GUARD( wxString aName ) :
            m_Name( std::move( aName ) ),
            m_HadValue( wxGetEnv( m_Name, &m_Value ) )
    {
    }

    ~ENV_GUARD()
    {
        if( m_HadValue )
            wxSetEnv( m_Name, m_Value );
        else
            wxUnsetEnv( m_Name );
    }

private:
    wxString m_Name;
    wxString m_Value;
    bool     m_HadValue = false;
};
```

Also add `<utility>` to the includes because the helper uses `std::move`.

- [x] **Step 2: Add no-key default diagnostic test**

Add this test after `ProviderSettingsReadEnvironmentWithDefaults`:

```cpp
BOOST_AUTO_TEST_CASE( DefaultProviderReportsMissingKeyWhenUnconfigured )
{
    ENV_GUARD providerGuard( wxS( "KISURF_AI_PROVIDER" ) );
    ENV_GUARD keyGuard( wxS( "OPENAI_API_KEY" ) );
    ENV_GUARD kisurfBaseGuard( wxS( "KISURF_AI_BASE_URL" ) );
    ENV_GUARD openaiBaseGuard( wxS( "OPENAI_BASE_URL" ) );

    wxUnsetEnv( wxS( "KISURF_AI_PROVIDER" ) );
    wxUnsetEnv( wxS( "OPENAI_API_KEY" ) );
    wxUnsetEnv( wxS( "KISURF_AI_BASE_URL" ) );
    wxUnsetEnv( wxS( "OPENAI_BASE_URL" ) );

    std::unique_ptr<AI_PROVIDER> provider = MakeDefaultAiProvider();

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 44;
    request.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_UserText = wxS( "connect to model" );

    AI_PROVIDER_RESPONSE response = provider->Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 44 );
    BOOST_CHECK( response.m_Body.Contains( wxS( "OPENAI_API_KEY" ) ) );
    BOOST_CHECK( !response.m_Body.Contains( wxS( "Stub response" ) ) );
}
```

- [x] **Step 3: Add explicit stub test**

Add this test after the no-key diagnostic test:

```cpp
BOOST_AUTO_TEST_CASE( DefaultProviderCanBeForcedToStub )
{
    ENV_GUARD providerGuard( wxS( "KISURF_AI_PROVIDER" ) );
    ENV_GUARD keyGuard( wxS( "OPENAI_API_KEY" ) );

    wxSetEnv( wxS( "KISURF_AI_PROVIDER" ), wxS( "stub" ) );
    wxUnsetEnv( wxS( "OPENAI_API_KEY" ) );

    std::unique_ptr<AI_PROVIDER> provider = MakeDefaultAiProvider();

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 45;
    request.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_UserText = wxS( "offline check" );

    AI_PROVIDER_RESPONSE response = provider->Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 45 );
    BOOST_CHECK( response.m_Body.Contains( wxS( "Stub response" ) ) );
}
```

- [x] **Step 4: Run red**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\pcbnew;$root\common;$root\common\gal;$root\api;$env:PATH"
& "$root\qa\tests\common\qa_common.exe" --run_test=AiNativeProvider/DefaultProviderReportsMissingKeyWhenUnconfigured --log_level=test_suite
```

Expected: build exits 0; the new no-key default test fails because the current default provider returns stub text.

## Task 2: Default Provider Selection Change

**Files:**
- Modify: `common/kisurf/ai/ai_provider.cpp`

- [x] **Step 1: Change MakeDefaultAiProvider**

Replace the final selection in `MakeDefaultAiProvider()` with:

```cpp
std::unique_ptr<AI_PROVIDER> MakeDefaultAiProvider()
{
    wxString mode;

    if( wxGetEnv( wxS( "KISURF_AI_PROVIDER" ), &mode ) && mode.CmpNoCase( wxS( "stub" ) ) == 0 )
        return std::make_unique<AI_STUB_PROVIDER>();

    return std::make_unique<AI_OPENAI_COMPAT_PROVIDER>(
            AI_PROVIDER_SETTINGS::FromEnvironment() );
}
```

- [x] **Step 2: Run provider green**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\pcbnew;$root\common;$root\common\gal;$root\api;$env:PATH"
& "$root\qa\tests\common\qa_common.exe" --run_test=AiNativeProvider --log_level=nothing
```

Expected: `AiNativeProvider` exits 0.

- [x] **Step 3: Commit implementation**

```bash
git add common/kisurf/ai/ai_provider.cpp qa/tests/common/test_ai_provider.cpp
git commit -m "fix: show missing ai provider configuration"
```

## Task 3: Final Verification And Plan Status

**Files:**
- Modify: `docs/superpowers/plans/2026-06-19-ai-provider-configuration-diagnostics-implementation.md`

- [x] **Step 1: Run final provider and editor verification**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\pcbnew;$root\common;$root\common\gal;$root\api;$env:PATH"
& "$root\qa\tests\common\qa_common.exe" --run_test=AiNativeProvider --log_level=nothing
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target pcbnew -- -j 2"
```

Expected: all commands exit 0.

- [x] **Step 2: Run whitespace and secret checks**

Run:

```powershell
git diff --check
rg -n "sk-[0-9A-Za-z_-]{20,}|OPENAI_API_KEY\s*=|KISURF_AI_API_KEY\s*=" common include pcbnew qa docs
```

Expected: whitespace check exits 0; secret scan has no matches.

- [x] **Step 3: Update this plan status**

Check off each completed step in this file.

- [x] **Step 4: Commit final plan status**

```bash
git add docs/superpowers/plans/2026-06-19-ai-provider-configuration-diagnostics-implementation.md
git commit -m "docs: update provider configuration diagnostics plan status"
```

## Self-Review

- Spec coverage: Tasks implement no-key default diagnostics, explicit stub preservation, tests, and final editor verification.
- Placeholder scan: Every step contains exact files, code snippets, commands, expected failures, expected passes, and commit messages.
- Type consistency: `ENV_GUARD`, `MakeDefaultAiProvider`, `AI_OPENAI_COMPAT_PROVIDER`, and `AI_STUB_PROVIDER` match current code.
- Scope check: UI configuration and credential storage remain outside this plan.
