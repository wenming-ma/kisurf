# AI Action Tool Call Handler Implementation Plan

> For agentic workers: use `superpowers:executing-plans` or
> `superpowers:subagent-driven-development` before implementing this plan.

**Goal:** Add a common-layer handler that converts parsed model tool calls into
existing policy-gated action invocation requests, then expose a panel-model setter
so editor code can install the handler later.

**Spec:** `docs/superpowers/specs/2026-06-16-ai-action-tool-call-handler-design.md`

**Architecture:** Keep action resolution descriptor-based and editor-agnostic.
The handler reads `AI_PROVIDER_REQUEST.m_ContextSnapshot.m_Actions`, falls back
to supplied descriptors, parses JSON with `nlohmann::json`, and invokes the
existing `AI_TOOL_EXECUTOR`.

## File Structure

- Create: `include/kisurf/ai/ai_action_tool_call_handler.h`
- Create: `common/kisurf/ai/ai_action_tool_call_handler.cpp`
- Modify: `common/CMakeLists.txt`
- Create: `qa/tests/common/test_ai_action_tool_call_handler.cpp`
- Modify: `qa/tests/common/CMakeLists.txt`
- Modify: `include/kisurf/ai/ai_agent_panel_model.h`
- Modify: `common/kisurf/ai/ai_agent_panel_model.cpp`
- Modify: `qa/tests/common/test_ai_agent_panel_model.cpp`
- Modify: `docs/superpowers/specs/2026-06-16-kisurf-ai-native-spec-index.md`

## Verification Command

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_common -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiActionToolCallHandler,AiAgentPanelModel,AiNativeRuntime,AiToolExecution --log_level=test_suite
```

Expected final result: exit code `0`. The known missing schema warning is
acceptable when Boost reports no errors.

## Task 1: Common Handler Tests

- [ ] Add `qa/tests/common/test_ai_action_tool_call_handler.cpp`.
- [ ] Register it in `qa/tests/common/CMakeLists.txt`.
- [ ] Write tests for:
  - `kisurf_check_action` dry-run allowed and runner not called.
  - `kisurf_run_action` executes an allowlisted read-only action.
  - request context descriptor wins over fallback descriptor.
  - unknown tool returns `unknown_tool`.
  - malformed JSON returns `malformed_arguments`.
  - missing action returns `unknown_action`.
  - modifying actions are denied with `requires_preview`.
- [ ] Run `qa_common --run_test=AiActionToolCallHandler` and verify RED because
  the header does not exist.

## Task 2: Common Handler Implementation

- [ ] Add `include/kisurf/ai/ai_action_tool_call_handler.h`.
- [ ] Add `common/kisurf/ai/ai_action_tool_call_handler.cpp`.
- [ ] Register implementation in `common/CMakeLists.txt`.
- [ ] Implement:
  - constructor storing policy, runner, and activity log references.
  - `SetFallbackActions(...)`.
  - `HandleToolCall(...)`.
  - JSON parser for `action`, optional `arguments`, optional `dry_run`.
  - forced dry-run for `kisurf_check_action`.
  - context-first descriptor resolution.
  - fail-closed helper results.
- [ ] Run `qa_common --run_test=AiActionToolCallHandler` and verify GREEN.

## Task 3: Agent Panel Model Setter

- [ ] Add `void SetToolCallHandler( AI_TOOL_CALL_HANDLER* aHandler );` to
  `AI_AGENT_PANEL_MODEL`.
- [ ] Implement by forwarding to `m_Runtime.SetToolCallHandler(...)`.
- [ ] Add a test with a provider that returns a tool call and a fake handler;
  assert the handler result is copied into `AI_PROVIDER_RESPONSE.m_ToolCalls`.
- [ ] Run `qa_common --run_test=AiAgentPanelModel,AiNativeRuntime`.

## Task 4: Verification And Commit

- [ ] Run the full verification command above.
- [ ] Run `git diff --check`.
- [ ] Run `git status --short` and `git diff --stat`.
- [ ] Commit:

```powershell
git add docs\superpowers\specs\2026-06-16-ai-action-tool-call-handler-design.md docs\superpowers\specs\2026-06-16-kisurf-ai-native-spec-index.md docs\superpowers\plans\2026-06-16-ai-action-tool-call-handler-implementation.md include\kisurf\ai\ai_action_tool_call_handler.h common\kisurf\ai\ai_action_tool_call_handler.cpp common\CMakeLists.txt qa\tests\common\test_ai_action_tool_call_handler.cpp qa\tests\common\CMakeLists.txt include\kisurf\ai\ai_agent_panel_model.h common\kisurf\ai\ai_agent_panel_model.cpp qa\tests\common\test_ai_agent_panel_model.cpp
git commit -m "feat: add ai action tool call handler"
```

## Plan Self-Review

- Spec coverage: every goal in the handler spec maps to tests or implementation.
- Scope check: no editor-specific `TOOL_MANAGER` runner is added here.
- Safety check: modifying and destructive action descriptors continue through
  deny-by-default policy.
- Testability check: all tests use fake provider/runner/handler objects and no
  live network or API key.
