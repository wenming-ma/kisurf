# AI Semantic UI Action Bridge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a guarded model-facing semantic UI action tool that can operate current Agent panel controls without bypassing human confirmation.

**Architecture:** Extend `AI_SEMANTIC_TOOL_CALL_HANDLER` with optional semantic UI tree/action callbacks, add the OpenAI-compatible tool schema, and wire the Agent panel dispatcher to the existing semantic UI action implementation.

**Tech Stack:** C++17, wxWidgets value types, nlohmann JSON, Boost unit tests, existing KiSurf AI common layer.

---

## File Map

- Modify `include/kisurf/ai/ai_semantic_tool_call_handler.h`: add UI tree/action callback typedefs and constructor.
- Modify `common/kisurf/ai/ai_semantic_tool_call_handler.cpp`: add tool support, argument parsing, confirmation guard, result JSON, and callback invocation.
- Modify `common/kisurf/ai/ai_provider.cpp`: advertise `kisurf_invoke_semantic_ui_action` in OpenAI-compatible tool schema.
- Modify `common/kisurf/ai/ai_agent_panel.cpp`: pass `SemanticUiTree()` and `InvokeSemanticUiAction(...)` into the semantic tool handler.
- Modify `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`: add handler behavior tests.
- Modify `qa/tests/common/test_ai_provider.cpp`: add provider schema coverage.

## Task 1: Red Tests

- [ ] Add helper semantic UI trees to `test_ai_semantic_tool_call_handler.cpp`.
- [ ] Add tests for successful `set_text`, confirmation-required refusal, disabled/unknown-node refusal, malformed arguments, missing bridge callbacks, and redacted callback failure messages.
- [ ] Update `test_ai_provider.cpp` to expect the new tool count and assert schema details.
- [ ] Build `qa_common` to confirm the new tests fail before implementation.

## Task 2: Handler API and Implementation

- [ ] Add `AI_SEMANTIC_UI_TREE_PROVIDER` and `AI_SEMANTIC_UI_ACTION_INVOKER` typedefs.
- [ ] Add a constructor that accepts suggestion sink, UI tree provider, and UI action invoker while keeping the existing constructor.
- [ ] Include `kisurf_invoke_semantic_ui_action` in `supportedTool`.
- [ ] Parse `node_id`, `action`, optional `text`, and optional `checked`; reject unknown fields.
- [ ] Read the live semantic UI tree, deny unknown, disabled, and confirmation-required nodes before invoking the callback.
- [ ] Invoke the callback with `m_UserConfirmed=false`.
- [ ] Return bounded JSON for success and callback failure, redacting messages.

## Task 3: Provider Schema and Panel Wiring

- [ ] Add `semanticUiActionToolParameters()` to `ai_provider.cpp`.
- [ ] Advertise `kisurf_invoke_semantic_ui_action` without a `user_confirmed` parameter.
- [ ] Update `AI_AGENT_PANEL::ConfigureActionToolCalls()` to pass semantic UI callbacks into the handler.

## Task 4: Verification and Commit

- [ ] Build `qa_common`.
- [ ] Run targeted AI suites including provider, semantic tool handler, Agent panel, semantic UI, runtime, and direct-use smoke.
- [ ] Build `pcbnew` and `eeschema`.
- [ ] Run `git diff --check`.
- [ ] Run the dynamic secret scan.
- [ ] Stage only files touched by this slice and commit with message `feat: bridge AI semantic UI actions`.
