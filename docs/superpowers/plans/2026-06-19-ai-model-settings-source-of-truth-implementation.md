# AI Model Settings Source Of Truth Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Lock Model Settings in as the normal Agent provider configuration source and prevent OpenAI environment variables from becoming runtime defaults again.

**Architecture:** Keep the existing `AI_MODEL_CONFIG_STORE`, `AI_AGENT_PANEL` settings dialog, and OpenAI-compatible provider factory. Add regression tests around `MakeDefaultAiProvider()` so normal runtime construction ignores OpenAI-compatible environment variables unless explicit stub mode is selected.

**Tech Stack:** C++17, wxWidgets environment helpers, Boost unit tests, existing KiSurf AI provider layer.

---

## File Map

- Modify `qa/tests/common/test_ai_provider.cpp`: add a default-provider regression test that sets OpenAI-compatible environment variables but points `KISURF_AI_MODEL_CONFIG_PATH` at a missing config file.
- Modify `README.md`: clarify that OpenAI-compatible environment variables are legacy/test helpers, while normal runtime setup happens through `Model...` settings.

## Task 1: Default Provider Regression Test

**Files:**
- Modify: `qa/tests/common/test_ai_provider.cpp`

- [ ] **Step 1: Write the failing test**

Add `DefaultProviderIgnoresOpenAiEnvironmentVariables` near the existing `DefaultProviderReportsMissingKeyWhenUnconfigured` test:

```cpp
BOOST_AUTO_TEST_CASE( DefaultProviderIgnoresOpenAiEnvironmentVariables )
{
    ENV_GUARD providerGuard( wxS( "KISURF_AI_PROVIDER" ) );
    ENV_GUARD keyGuard( wxS( "OPENAI_API_KEY" ) );
    ENV_GUARD kisurfBaseGuard( wxS( "KISURF_AI_BASE_URL" ) );
    ENV_GUARD openaiBaseGuard( wxS( "OPENAI_BASE_URL" ) );
    ENV_GUARD lowerBaseGuard( wxS( "base_url" ) );
    ENV_GUARD modelGuard( wxS( "OPENAI_MODEL" ) );
    ENV_GUARD kisurfModelGuard( wxS( "KISURF_AI_MODEL" ) );
    ENV_GUARD modelConfigGuard( wxS( "KISURF_AI_MODEL_CONFIG_PATH" ) );

    wxUnsetEnv( wxS( "KISURF_AI_PROVIDER" ) );
    wxSetEnv( wxS( "OPENAI_API_KEY" ), wxS( "unit-env-key" ) );
    wxSetEnv( wxS( "KISURF_AI_BASE_URL" ), wxS( "https://env.example.test/v1" ) );
    wxSetEnv( wxS( "OPENAI_BASE_URL" ), wxS( "https://openai-env.example.test/v1" ) );
    wxSetEnv( wxS( "base_url" ), wxS( "https://lower-env.example.test/v1" ) );
    wxSetEnv( wxS( "OPENAI_MODEL" ), wxS( "env-model" ) );
    wxSetEnv( wxS( "KISURF_AI_MODEL" ), wxS( "kisurf-env-model" ) );
    wxSetEnv( wxS( "KISURF_AI_MODEL_CONFIG_PATH" ), missingModelConfigPath() );

    std::unique_ptr<AI_PROVIDER> provider = MakeDefaultAiProvider();

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 46;
    request.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_UserText = wxS( "connect using configured model" );

    AI_PROVIDER_RESPONSE response = provider->Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 46 );
    BOOST_CHECK( response.m_Body.Contains( wxS( "Model Settings" ) ) );
    BOOST_CHECK( !response.m_Body.Contains( wxS( "OPENAI_API_KEY" ) ) );
    BOOST_CHECK( !response.m_Body.Contains( wxS( "Stub response" ) ) );
}
```

- [ ] **Step 2: Run the test to verify red or existing green**

Run:

```powershell
$env:KICAD_RUN_FROM_BUILD_DIR='1'
& out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiNativeProvider/DefaultProviderIgnoresOpenAiEnvironmentVariables
```

Expected before implementation hardening: the test should fail if `MakeDefaultAiProvider()` still consumes `OPENAI_API_KEY`; if the current code already ignores it, the test passes and becomes a regression guard.

## Task 2: Implementation Hardening

**Files:**
- Modify: `common/kisurf/ai/ai_provider.cpp` only if Task 1 fails

- [ ] **Step 1: Keep default provider model-settings first**

If the red test fails, change `MakeDefaultAiProvider()` so it keeps only the explicit stub check and otherwise returns:

```cpp
return MakeAiProviderFromModelConfig( AI_MODEL_CONFIG_STORE::LoadUserConfig() );
```

Do not call `AI_PROVIDER_SETTINGS::FromEnvironment()` from `MakeDefaultAiProvider()`.

- [ ] **Step 2: Run targeted tests**

Run:

```powershell
$env:KICAD_RUN_FROM_BUILD_DIR='1'
& out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiNativeProvider,AiModelConfig
```

Expected: all targeted provider/model-config tests pass.

## Task 3: README Clarification

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Clarify normal setup**

In the AI-native status/setup section, keep the `Model...` flow as the normal setup path and add one sentence that OpenAI-compatible environment variables are not read by the normal default provider path.

- [ ] **Step 2: Verify docs do not contain secrets**

Run:

```powershell
$prefix = 'sk' + '-'
$pattern = $prefix + '[A-Za-z0-9_-]{20,}|OPENAI_API_KEY=.*' + $prefix
rg -n $pattern common docs include qa pcbnew README.md
```

Expected: no output, exit code 1.

## Task 4: Final Verification And Commit

**Files:**
- Modify: `qa/tests/common/test_ai_provider.cpp`
- Modify: `README.md`
- Create: `docs/superpowers/specs/2026-06-19-ai-model-settings-source-of-truth-design.md`
- Create: `docs/superpowers/plans/2026-06-19-ai-model-settings-source-of-truth-implementation.md`

- [ ] **Step 1: Build and test**

Run:

```powershell
cmd.exe /S /C 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build out\build\x64-release --target qa_common --config Release'
$env:KICAD_RUN_FROM_BUILD_DIR='1'
& out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiNativeProvider,AiModelConfig,AiAgentPanel
```

Expected: build succeeds and all targeted tests pass.

- [ ] **Step 2: Run diff check and secret scan**

Run:

```powershell
git diff --check
$prefix = 'sk' + '-'
$pattern = $prefix + '[A-Za-z0-9_-]{20,}|OPENAI_API_KEY=.*' + $prefix
rg -n $pattern common docs include qa pcbnew README.md
```

Expected: no whitespace errors other than known line-ending warnings, and no secret scan matches.

- [ ] **Step 3: Commit**

Run:

```powershell
git add docs/superpowers/specs/2026-06-19-ai-model-settings-source-of-truth-design.md docs/superpowers/plans/2026-06-19-ai-model-settings-source-of-truth-implementation.md qa/tests/common/test_ai_provider.cpp README.md
git commit -m "test: lock AI model settings as provider source"
```

Expected: commit succeeds and unrelated `qa/tests/pcbnew/test_module.cpp` remains unstaged.

## Self-Review

- Spec coverage: the regression test covers normal runtime source-of-truth behavior; existing tests cover secret persistence and UI surface.
- Placeholder scan: no TBD or TODO markers.
- Type consistency: all referenced classes and functions already exist in the current codebase.
