# AI Visual Frame Tool Implementation Plan

Date: 2026-06-19
Spec: `docs/superpowers/specs/2026-06-19-ai-visual-frame-tool-design.md`

## Target

Implement `kisurf_get_visual_frame`, a read-only semantic tool that returns the current `AI_VISUAL_SNAPSHOT` metadata and, when explicitly requested within a byte limit, the captured frame `data_uri`.

## Current Architecture Notes

- PCB and schematic frames already call `CaptureAiVisualSnapshotFromCanvas()` and attach the result to `AI_CONTEXT_SNAPSHOT`.
- `AI_OPENAI_COMPAT_PROVIDER::Generate()` can include `m_DataUri` in the initial multimodal user message.
- `AI_SEMANTIC_TOOL_CALL_HANDLER` now has a read-only path for `kisurf_get_context_snapshot`; the visual tool should follow the same no-suggestion-sink pattern.
- `AI_VISUAL_SNAPSHOT::HasPixels()` is true when `m_DataUri` is present.

## Implementation Steps

1. Add red tests for semantic visual tool behavior.
   - Metadata-only call returns `visual_ready`, includes source/mime/size/pixel availability, and omits `data_uri`.
   - `include_pixels=true` returns `data_uri` when pixels are present and within `max_bytes`.
   - Tool works without a suggestion sink.
   - Invalid boolean or max-byte argument types return `malformed_arguments`.
   - Oversize pixel request returns `visual_too_large`.

2. Add red tests for provider schema.
   - Provider advertises seven tools.
   - `kisurf_get_visual_frame` is present.
   - All parameters are optional.
   - Unknown arguments are rejected through `additionalProperties=false`.
   - `include_pixels` and `max_bytes` properties are declared.

3. Implement common semantic-tool helpers.
   - Add visual tool option parsing beside context tool parsing.
   - Default `include_pixels=false`.
   - Default `max_bytes=262144`.
   - Clamp `max_bytes` to `1048576`.
   - Reject unknown fields, non-boolean include flag, and non-positive/non-integer max bytes.

4. Implement visual result generation.
   - Build visual JSON from `aRequest.m_ContextSnapshot.m_Visual`.
   - Include `data_uri` only when requested, available, and within `max_bytes`.
   - Return `visual_too_large` before serializing pixels when the captured byte size exceeds the allowed max.

5. Extend provider schema.
   - Add `visualFrameToolParameters()`.
   - Add `kisurf_get_visual_frame` to the OpenAI-compatible tools list with a read-only bounded description.

6. Verification.
   - Build `qa_common`.
   - Run `AiSemanticToolCallHandler`.
   - Run `AiNativeProvider`.
   - Run `git diff --check`.
   - Run a secret scan for touched files.

## Done Criteria

- Tests fail before implementation for missing visual tool behavior.
- Tests pass after implementation.
- Tool response is valid JSON.
- Pixel payload is opt-in and bounded.
- No editor state is mutated.
- No unrelated files are staged.
- No plaintext API key is introduced.

## Implementation Status

Completed on 2026-06-19.

- Added `kisurf_get_visual_frame` provider schema with optional `include_pixels` and bounded `max_bytes`.
- Added read-only semantic handler support that works without a suggestion sink.
- Returned visual metadata by default and only includes `data_uri` when explicitly requested within the byte limit.
- Added oversize denial with `visual_too_large`.
- Added tests for metadata-only output, bounded pixel output, oversize denial, malformed arguments, and provider schema exposure.

Verification:

- `cmake --build out\build\x64-release --target qa_common --config Release`
- `qa_common.exe --run_test=AiSemanticToolCallHandler`
- `qa_common.exe --run_test=AiNativeProvider`
- `git diff --check`
- Secret scan for touched files

## Self-check

- This plan follows the spec-first rule.
- The implementation is common-layer only.
- The plan keeps crop/tile/downscale work out of scope.
- The tool complements existing provider-level multimodal input.
- The test plan covers both the safe default and explicit pixel path.
