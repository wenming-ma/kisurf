# AI Model Suggestion Provider Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the suggestion provider turn one grounded model JSON response into
an `AI_SUGGESTION_RECORD`, while preserving the existing deterministic fallback
and orchestrator lifecycle.

**Architecture:** Add provider injection to `AI_AGENT_SUGGESTION_PROVIDER`.
When a provider is present, build an `AI_PROVIDER_REQUEST` from the trigger,
ask for strict JSON, parse and ground object labels against the current context,
and return the structured suggestion. If no provider is installed or the
provider returns unstructured text, fall back to the existing deterministic
selection suggestion. Keep ids and status transitions in
`AI_SUGGESTION_ORCHESTRATOR`.

**Tech Stack:** C++17, KiSurf AI common layer, nlohmann/json, Boost.Test.

---

## File Structure

- Modify: `include/kisurf/ai/ai_agent_suggestion_provider.h`
  - Add provider-injected constructor and private `AI_PROVIDER` ownership.
- Modify: `common/kisurf/ai/ai_agent_suggestion_provider.cpp`
  - Build suggestion prompts, parse model JSON, resolve object labels, preserve
    deterministic fallback.
- Modify: `common/kisurf/ai/ai_agent_panel_model.cpp`
  - Install a separate default model-backed suggestion provider created from
    `MakeDefaultAiProvider()`.
- Modify: `qa/tests/common/test_ai_agent_suggestion_provider.cpp`
  - Add RED tests for model-backed parsing, request content, unknown labels, and
    explicit no-suggestion.
- Modify: `docs/superpowers/specs/2026-06-16-kisurf-ai-native-spec-index.md`
  - Register this spec and implementation phase.

## Verification Commands

Suggestion-focused verification:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_common -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiAgentSuggestionProvider,AiSuggestionOrchestrator,AiAgentPanelModel --log_level=test_suite
```

The known missing schema warning is acceptable only when Boost reports no
errors.

## Task 1: RED Model Suggestion Tests

**Files:**
- Modify: `qa/tests/common/test_ai_agent_suggestion_provider.cpp`

- [x] **Step 1: Add a capturing suggestion AI provider**

Create a local fake `AI_PROVIDER` that stores the last request and returns a
configurable `AI_PROVIDER_RESPONSE`.

- [x] **Step 2: Add model JSON parse test**

Construct `AI_AGENT_SUGGESTION_PROVIDER` with the fake provider and make it
return:

```json
{
  "kind": "preview",
  "title": "Inspect U1 pad clearance",
  "body": "U1.1 was selected after routing activity.",
  "fingerprint": "model:u1.1",
  "arguments": { "intent": "clearance-preview" },
  "preview_objects": [ { "label": "U1.1" } ]
}
```

Assert the returned suggestion uses the model title/body/fingerprint,
`m_ArgumentsJson`, resolved preview object, defaulted edit object, context
version, and trigger activity sequence.

- [x] **Step 3: Assert request prompt content**

In the same test, assert the captured request contains the editor kind, context
version, trigger reason, activity action, selected object context, and JSON
contract language.

- [x] **Step 4: Add unknown-label rejection test**

Make the provider return a preview object label that is not present in selected
or visible context. Assert no model suggestion is produced.

- [x] **Step 5: Add explicit no-suggestion test**

Make the provider return `{"no_suggestion":true}`. Assert no suggestion is
produced.

- [x] **Step 6: Add unstructured fallback test**

Make the provider return plain text. Assert the deterministic selection
suggestion is returned.

- [x] **Step 7: Run RED verification**

Run the suggestion-focused verification command.

Expected: compile or test failures because the injected constructor and model
parsing behavior do not exist yet.

## Task 2: Provider Injection And Parser

**Files:**
- Modify: `include/kisurf/ai/ai_agent_suggestion_provider.h`
- Modify: `common/kisurf/ai/ai_agent_suggestion_provider.cpp`

- [x] **Step 1: Add provider ownership**

Add a default constructor, an explicit constructor taking
`std::unique_ptr<AI_PROVIDER>`, and a private `m_Provider` member.

- [x] **Step 2: Preserve deterministic fallback**

Move the current deterministic behavior into a helper and use it when no model
provider is installed or model output is unstructured.

- [x] **Step 3: Build model request**

Create an `AI_PROVIDER_REQUEST` from the trigger:

- request id from activity sequence when present
- editor kind from trigger
- effective context version
- context snapshot
- user text containing the JSON contract, trigger reason, activity summary, and
  context prompt

- [x] **Step 4: Parse grounded JSON**

Parse one JSON object from the response body. Support object references as
strings or `{ "label": "..." }`, resolve labels against selected objects first
and visible objects second, reject unresolved labels, default edit objects to
preview objects, and preserve inert `arguments` JSON.

- [x] **Step 5: Run GREEN verification**

Run the suggestion-focused verification command.

Expected: all targeted suggestion, orchestrator, and panel-model suites pass.

## Task 3: Default Wiring, Index, Hygiene, And Commit

**Files:**
- Modify: `common/kisurf/ai/ai_agent_panel_model.cpp`
- Modify: `docs/superpowers/specs/2026-06-16-kisurf-ai-native-spec-index.md`

- [x] **Step 1: Wire default model-backed suggestion provider**

Change the single-provider `AI_AGENT_PANEL_MODEL` constructor to create a
separate `AI_AGENT_SUGGESTION_PROVIDER( MakeDefaultAiProvider() )` for
suggestions.

- [x] **Step 2: Update spec index**

Add:

```markdown
22. [AI Model Suggestion Provider](./2026-06-16-ai-model-suggestion-provider-design.md)
   - Defines grounded model JSON conversion into previewable suggestion records.
```

Add implementation order:

```markdown
26. Phase 19 model-backed suggestion provider that turns grounded JSON into previewable suggestions.
```

- [x] **Step 3: Run diff hygiene checks**

Run:

```powershell
git diff --check
git status --short
git diff --stat
```

Expected: no whitespace errors and only files from this plan changed.

- [x] **Step 4: Commit**

```powershell
git add include/kisurf/ai/ai_agent_suggestion_provider.h common/kisurf/ai/ai_agent_suggestion_provider.cpp common/kisurf/ai/ai_agent_panel_model.cpp qa/tests/common/test_ai_agent_suggestion_provider.cpp docs/superpowers/specs/2026-06-16-ai-model-suggestion-provider-design.md docs/superpowers/specs/2026-06-16-kisurf-ai-native-spec-index.md docs/superpowers/plans/2026-06-16-ai-model-suggestion-provider-implementation.md
git commit -m "feat: ground model suggestions"
```

## Plan Self-Review

- Spec coverage: tasks cover provider injection, model request construction,
  JSON parsing, label grounding, fallback behavior, default wiring, and tests.
- Open-marker scan: no unresolved open markers remain.
- Safety check: model output cannot bypass local context grounding or suggestion
  lifecycle ownership.
