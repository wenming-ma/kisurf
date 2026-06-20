# AI Context Snapshot Tool Implementation Plan

Date: 2026-06-19
Spec: `docs/superpowers/specs/2026-06-19-ai-context-snapshot-tool-design.md`

## Target

Implement `kisurf_get_context_snapshot`, a read-only semantic tool that returns a bounded JSON view of the current `AI_CONTEXT_SNAPSHOT`.

## Current Architecture Notes

- `AI_CONTEXT_SNAPSHOT::AsJsonText()` already serializes the unified common context into a `kisurf_context` JSON object.
- `AI_OPENAI_COMPAT_PROVIDER::Generate()` declares model-facing tool schemas.
- `AI_SEMANTIC_TOOL_CALL_HANDLER::HandleToolCall()` handles model semantic tools before the action dispatcher.
- Existing semantic tools create preview suggestions and therefore require a suggestion sink.
- The new context tool is read-only and must not require a suggestion sink.

## Implementation Steps

1. Add red tests for the semantic context tool.
   - `kisurf_get_context_snapshot` returns `context_ready`.
   - It includes bounded objects, actions, activity, tool state, anchors, panels, and visual metadata.
   - It works when `AI_SEMANTIC_TOOL_CALL_HANDLER` has no suggestion sink.
   - Include flags remove requested sections while preserving count fields.
   - Invalid include/max argument types return `malformed_arguments`.

2. Add red tests for provider schema.
   - Provider advertises six tools.
   - `kisurf_get_context_snapshot` is present.
   - All parameters are optional.
   - Unknown arguments are rejected via `additionalProperties=false`.
   - Include booleans and max integer properties are declared.

3. Implement common semantic-tool helpers.
   - Add a `CONTEXT_TOOL_OPTIONS` helper struct in `ai_semantic_tool_call_handler.cpp`.
   - Parse optional booleans with defaults.
   - Parse optional positive integer limits.
   - Clamp limits to spec caps.
   - Use `AI_CONTEXT_SNAPSHOT::AsJsonText()` to avoid duplicating serialization rules.
   - Remove omitted sections from the parsed `kisurf_context` object.

4. Extend semantic tool dispatch.
   - Add `kisurf_get_context_snapshot` to supported semantic tools.
   - Route it before the suggestion-sink requirement.
   - Return an `AI_TOOL_INVOCATION_RESULT` with `allowed=true`, `executed=false`, and JSON result content.

5. Extend provider tool schema.
   - Add `contextSnapshotToolParameters()`.
   - Add the tool to the OpenAI-compatible `tools` array with a description emphasizing read-only bounded context.

6. Verification.
   - Run targeted common tests:
     - `AiSemanticToolCallHandler`
     - `AiNativeProvider`
   - Build or run the `qa_common` target if available.
   - Run `git diff --check`.
   - Run a secret scan for API-key literals and provider-key assignment patterns in touched files.

## Done Criteria

- Tests fail before implementation for the new behavior.
- Tests pass after implementation.
- The new tool response is valid JSON.
- No editor state is mutated.
- No unrelated files are staged.
- No plaintext API key is introduced.

## Implementation Status

Completed on 2026-06-19.

- Added `kisurf_get_context_snapshot` provider schema with optional include flags and bounded max values.
- Added read-only semantic handler support that works without a suggestion sink.
- Reused `AI_CONTEXT_SNAPSHOT::AsJsonText()` as the serialization source of truth.
- Added tests for bounded context output, omitted sections, malformed arguments, and provider schema exposure.

Verification:

- `cmake --build out\build\x64-release --target qa_common --config Release`
- `qa_common.exe --run_test=AiSemanticToolCallHandler`
- `qa_common.exe --run_test=AiNativeProvider`
- `git diff --check`
- Secret scan for touched files

## Self-check

- This plan follows the spec-first rule.
- The implementation is common-layer first and does not add PCB-specific plumbing.
- The plan keeps the visual pixel transport out of scope.
- The new tool composes with the existing OpenAI-compatible tool-call loop and dispatcher.
- The test plan covers both behavior and schema exposure.
