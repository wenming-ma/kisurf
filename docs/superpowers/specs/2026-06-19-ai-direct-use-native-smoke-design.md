# AI Direct-Use Native Smoke Design

## Problem

The AI-native foundation now has enough pieces for developer-preview use, but the proof is scattered across focused unit tests. When asked whether the system can be used directly, we need one fast, native smoke suite that validates the minimum non-GUI path without relying on manual clicking or blocked desktop automation.

## Goals

- Provide one `AiDirectUseSmoke` suite in `qa_common`.
- Prove the direct-use provider configuration path accepts the lowercase `base_url` alias used by the local environment.
- Prove the OpenAI-compatible provider advertises the core model-facing tool surface for context, visual frame, activity, workspace view, and preview actions.
- Prove the semantic workspace-view tool can return context, visual, activity, and panel state together.
- Prove operation-only suggestions are previewable but not directly acceptable without native edit objects.

## Non-Goals

- This is not a GUI smoke test. The GUI automation path remains separate because the local Computer Use helper currently fails before app discovery.
- This does not call a live model or network endpoint.
- This does not expand the tool schema or modify production behavior.

## Design

Add `qa/tests/common/test_ai_direct_use_smoke.cpp` with compact integration-style tests that use existing public AI APIs:

- `AI_PROVIDER_SETTINGS::FromEnvironment()` with guarded environment variables.
- `AI_OPENAI_COMPAT_PROVIDER` with an injected HTTP handler that captures the outgoing request body.
- `AI_SEMANTIC_TOOL_CALL_HANDLER` against a synthetic `AI_PROVIDER_REQUEST`.
- `AI_AGENT_PANEL_MODEL` with a synthetic panel-fill operation suggestion.

The suite intentionally overlaps a few existing unit-test assertions, but only for the first-use contract. The detailed edge cases stay in the original focused test suites.

## Acceptance

- `qa_common --run_test=AiDirectUseSmoke` passes.
- Existing focused AI suites still pass.
- `pcbnew` and `eeschema` still build.
- Secret scan reports no keys in tracked docs/code.
