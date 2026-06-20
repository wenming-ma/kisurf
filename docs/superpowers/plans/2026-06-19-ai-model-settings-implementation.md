# AI Model Settings Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a first-class Agent model settings entry backed by user settings and platform secret storage, with OpenAI-compatible runtime support.

**Architecture:** Add `AI_MODEL_CONFIG` and `AI_MODEL_CONFIG_STORE` in the common AI layer, wire default provider creation through that store, and expose a `Model...` dialog from `AI_AGENT_PANEL`. Runtime provider reload is handled by `AI_AGENT_PANEL_MODEL` so saved settings take effect without losing panel state.

**Tech Stack:** C++17, wxWidgets, nlohmann JSON, KiCad `PATHS`, existing `OAUTH_SECRET_BACKEND`, Boost unit tests, CMake.

---

## File Map

- Create `include/kisurf/ai/ai_model_config.h`: provider-kind enum, model config value type, settings store interface, provider factory from config.
- Create `common/kisurf/ai/ai_model_config.cpp`: JSON persistence, secret backend integration, normalization, unsupported-provider implementation.
- Modify `include/kisurf/ai/ai_provider.h`: include model config where needed or expose provider settings conversion without environment-first defaults.
- Modify `common/kisurf/ai/ai_provider.cpp`: use model config store in `MakeDefaultAiProvider()`, adjust missing-key error text, keep environment parsing only as legacy helper.
- Modify `include/kisurf/ai/ai_runtime.h` and `common/kisurf/ai/ai_runtime.cpp`: add `SetProvider(std::unique_ptr<AI_PROVIDER>)`.
- Modify `include/kisurf/ai/ai_agent_panel_model.h` and `common/kisurf/ai/ai_agent_panel_model.cpp`: add `ReloadDefaultProviders()` and `SetSuggestionProvider(...)`.
- Modify `include/kisurf/ai/ai_agent_panel.h` and `common/kisurf/ai/ai_agent_panel.cpp`: add model settings command surface, dialog, save/reload handling.
- Modify `common/CMakeLists.txt`: register `kisurf/ai/ai_model_config.cpp`.
- Create `qa/tests/common/test_ai_model_config.cpp`: config store and provider factory tests.
- Modify `qa/tests/common/test_ai_provider.cpp`: update default-provider tests for Model Settings wording.
- Modify `qa/tests/common/test_ai_agent_panel_model.cpp`: add reload-preserves-state test.
- Modify `qa/tests/common/test_ai_agent_panel.cpp`: add model settings command surface test.
- Modify `qa/tests/common/CMakeLists.txt`: register `test_ai_model_config.cpp`.
- Modify `README.md`: replace shell API-key setup instructions with Agent panel Model Settings flow.

## Task 1: Config Store Red Tests

**Files:**
- Create: `qa/tests/common/test_ai_model_config.cpp`
- Modify: `qa/tests/common/CMakeLists.txt`

- [ ] **Step 1: Write failing tests**

Add tests that instantiate `AI_MODEL_CONFIG`, `AI_MODEL_CONFIG_STORE` with a temp file and memory secret backend, save a config with API key `unit-test-key`, verify JSON does not contain the key, reload it, and verify the key returns from the backend. Add provider-factory tests for OpenAI-compatible and Anthropic-compatible behavior.

- [ ] **Step 2: Register test and verify red**

Run:

```powershell
cmake --build out\build\x64-release --target qa_common --config Release
```

Expected: build fails because `kisurf/ai/ai_model_config.h` and `test_ai_model_config.cpp` registration targets do not exist.

## Task 2: Config Store Implementation

**Files:**
- Create: `include/kisurf/ai/ai_model_config.h`
- Create: `common/kisurf/ai/ai_model_config.cpp`
- Modify: `common/CMakeLists.txt`

- [ ] **Step 1: Implement value types and store**

Add `AI_MODEL_PROVIDER_KIND`, provider-kind string conversion, `AI_MODEL_CONFIG`, and `AI_MODEL_CONFIG_STORE`. Persist JSON fields `provider`, `base_url`, `model`, and `api_key_ref`; store the actual key through `OAUTH_SECRET_BACKEND`.

- [ ] **Step 2: Implement provider factory**

Add `MakeAiProviderFromModelConfig(const AI_MODEL_CONFIG&)`. OpenAI-compatible returns `AI_OPENAI_COMPAT_PROVIDER`; Anthropic-compatible returns an unsupported provider that reports a deterministic configuration message.

- [ ] **Step 3: Verify green**

Run:

```powershell
qa_common.exe --run_test=AiModelConfig
```

Expected: all `AiModelConfig` tests pass.

## Task 3: Default Provider and Runtime Reload

**Files:**
- Modify: `include/kisurf/ai/ai_provider.h`
- Modify: `common/kisurf/ai/ai_provider.cpp`
- Modify: `include/kisurf/ai/ai_runtime.h`
- Modify: `common/kisurf/ai/ai_runtime.cpp`
- Modify: `include/kisurf/ai/ai_agent_panel_model.h`
- Modify: `common/kisurf/ai/ai_agent_panel_model.cpp`
- Modify: `qa/tests/common/test_ai_provider.cpp`
- Modify: `qa/tests/common/test_ai_agent_panel_model.cpp`

- [ ] **Step 1: Write red tests**

Update default-provider tests to expect `Model Settings` in missing-key errors. Add a model test that sends a message, records an activity, calls `ReloadDefaultProviders()`, and verifies transcript/activity/workspace context state remain present.

- [ ] **Step 2: Verify red**

Run:

```powershell
qa_common.exe --run_test=AiNativeProvider,AiAgentPanelModel
```

Expected: at least the new wording/reload API tests fail before implementation.

- [ ] **Step 3: Implement default provider and reload**

Change `MakeDefaultAiProvider()` to load user model config through `AI_MODEL_CONFIG_STORE::LoadUserConfig()` and build from config. Add `AI_RUNTIME::SetProvider()`, `AI_AGENT_PANEL_MODEL::SetSuggestionProvider()`, and `AI_AGENT_PANEL_MODEL::ReloadDefaultProviders()`.

- [ ] **Step 4: Verify green**

Run:

```powershell
qa_common.exe --run_test=AiNativeProvider,AiAgentPanelModel,AiModelConfig
```

Expected: all targeted tests pass.

## Task 4: Agent Panel Settings Entry

**Files:**
- Modify: `include/kisurf/ai/ai_agent_panel.h`
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`
- Modify: `qa/tests/common/test_ai_agent_panel.cpp`

- [ ] **Step 1: Write red API-surface test**

Add a test that `AI_AGENT_PANEL` exposes `ShowModelSettingsDialog()` or equivalent command surface.

- [ ] **Step 2: Verify red**

Run:

```powershell
qa_common.exe --run_test=AiAgentPanel
```

Expected: compile failure or test failure because the command surface is missing.

- [ ] **Step 3: Implement dialog and button**

Add a `Model...` button to the top row. Implement a local wx dialog with provider choice, base URL, model, and password API key field. On OK, save through `AI_MODEL_CONFIG_STORE`, call `m_Model->ReloadDefaultProviders()`, and refresh the log.

- [ ] **Step 4: Verify green**

Run:

```powershell
qa_common.exe --run_test=AiAgentPanel,AiAgentPanelModel,AiModelConfig
```

Expected: all targeted tests pass.

## Task 5: Docs, Full Verification, Commit

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update README**

Replace environment-variable-first setup with Agent panel Model Settings setup. Mention the default base URL, OpenAI-compatible support, Anthropic-compatible reserved UI option, and local secret storage. Keep stub-mode developer instructions.

- [ ] **Step 2: Run broad verification**

Run the established Windows build/test commands for `qa_common`, the AI suite, `pcbnew`, and `eeschema`. Run the diff check and dynamic secret scan.

- [ ] **Step 3: Commit**

Stage only files touched by this slice and commit:

```powershell
git add docs/superpowers/specs/2026-06-19-ai-model-settings-design.md docs/superpowers/plans/2026-06-19-ai-model-settings-implementation.md include/kisurf/ai/ai_model_config.h common/kisurf/ai/ai_model_config.cpp include/kisurf/ai/ai_provider.h common/kisurf/ai/ai_provider.cpp include/kisurf/ai/ai_runtime.h common/kisurf/ai/ai_runtime.cpp include/kisurf/ai/ai_agent_panel_model.h common/kisurf/ai/ai_agent_panel_model.cpp include/kisurf/ai/ai_agent_panel.h common/kisurf/ai/ai_agent_panel.cpp common/CMakeLists.txt qa/tests/common/test_ai_model_config.cpp qa/tests/common/test_ai_provider.cpp qa/tests/common/test_ai_agent_panel_model.cpp qa/tests/common/test_ai_agent_panel.cpp qa/tests/common/CMakeLists.txt README.md
git commit -m "feat: add AI model settings"
```

Expected: commit succeeds and unrelated `qa/tests/pcbnew/test_module.cpp` remains unstaged.
