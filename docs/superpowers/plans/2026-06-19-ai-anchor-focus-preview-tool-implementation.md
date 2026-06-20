# AI Anchor Focus Preview Tool Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a model-facing anchor focus preview tool that turns current semantic anchors into reviewable, non-mutating preview suggestions.

**Architecture:** Extend the existing suggestion operation parser with `anchor_focus_preview`, add a semantic tool handler path that validates current context anchors, and advertise the tool through the OpenAI-compatible provider schema and direct-use smoke surface.

**Tech Stack:** C++17, wxWidgets, nlohmann JSON, Boost unit tests, KiSurf AI common layer.

---

## File Map

- Modify `include/kisurf/ai/ai_suggestion_operations.h`: add operation kind, fields, and `IsAnchorFocusPreview()`.
- Modify `common/kisurf/ai/ai_suggestion_operations.cpp`: parse `anchor_focus_preview`.
- Modify `common/kisurf/ai/ai_semantic_tool_call_handler.cpp`: support `kisurf_preview_anchor_focus` and create operation-only preview suggestions.
- Modify `common/kisurf/ai/ai_provider.cpp`: advertise the new OpenAI-compatible function tool.
- Modify `qa/tests/common/test_ai_suggestion_operations.cpp`: add parser coverage.
- Modify `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`: add success and failure tests.
- Modify `qa/tests/common/test_ai_provider.cpp`: add schema coverage.
- Modify `qa/tests/common/test_ai_agent_panel_model.cpp`: assert anchor-focus operation-only suggestions are preview-only.
- Modify `qa/tests/common/test_ai_direct_use_smoke.cpp`: include the new tool in the direct-use tool list.

## Task 1: Red Tests

- [ ] Add `ParsesAnchorFocusPreviewOperation` to `test_ai_suggestion_operations.cpp` with this payload:

```json
{"operation":"anchor_focus_preview","anchor_id":"tool.routing.orthogonal.horizontal","position":{"x":500,"y":200},"focus_layer":"F.Cu","focus_net":"/GPIO","dim_unfocused_layers":true}
```

Expected parsed fields: anchor id, position `(500, 200)`, focus layer `F.Cu`, focus net `/GPIO`, and dim flag true.

- [ ] Add provider schema assertions in `test_ai_provider.cpp`: tool count increases by one, tool name exists, `anchor_id` is required, `focus_layer`, `focus_net`, and `dim_unfocused_layers` are optional properties, and `additionalProperties` is false.
- [ ] Add semantic handler success test using an anchor with details JSON containing net/layer, and verify the stored suggestion has operation `anchor_focus_preview`, no edit objects, and `preview_ready`.
- [ ] Add semantic handler failure tests for missing anchor, anchor without position, malformed arguments, and unknown extra fields.
- [ ] Add model lifecycle test proving an anchor-focus operation-only suggestion is previewable but not acceptable.
- [ ] Add `kisurf_preview_anchor_focus` to the direct-use required tools list.
- [ ] Build `qa_common` to confirm these tests fail before implementation.

## Task 2: Operation Parser

- [ ] Add `AnchorFocusPreview` to `AI_SUGGESTION_OPERATION_KIND`.
- [ ] Add fields `m_AnchorId`, `m_FocusLayer`, `m_FocusNet`, and `m_DimUnfocusedLayers` to `AI_SUGGESTION_OPERATION`.
- [ ] Add `IsAnchorFocusPreview()`.
- [ ] Implement parsing for `anchor_focus_preview`: require non-empty `anchor_id`, require object `position` with integer `x` and `y`, accept optional string `focus_layer`, optional string `focus_net`, and optional boolean `dim_unfocused_layers` defaulting to false.
- [ ] Run `qa_common --run_test=AiSuggestionOperations` and verify the new parser test passes.

## Task 3: Semantic Tool Handler

- [ ] Include `kisurf_preview_anchor_focus` in `supportedTool`.
- [ ] Add argument parsing for `anchor_id`, optional focus fields, and optional dim flag; reject unknown fields.
- [ ] Resolve the anchor against `aRequest.m_ContextSnapshot.m_Anchors`.
- [ ] Reject missing anchors with `missing_anchor`.
- [ ] Reject anchors without positions with `anchor_without_position`.
- [ ] Infer `focus_net` from details keys `net` or `net_name` when omitted.
- [ ] Infer `focus_layer` from details key `layer` when omitted.
- [ ] Build operation JSON with `operation`, `anchor_id`, `position`, `focus_layer`, `focus_net`, and `dim_unfocused_layers`.
- [ ] Store an `AI_SUGGESTION_RECORD` with kind `Preview`, title `Preview anchor focus`, no edit objects, no preview objects, context metadata from the current request, and a stable fingerprint.
- [ ] Run `qa_common --run_test=AiSemanticToolCallHandler,AiAgentPanelModel,AiSuggestionOperations`.

## Task 4: Provider Schema and Direct Use

- [ ] Add `anchorFocusToolParameters()` to `ai_provider.cpp`.
- [ ] Add `functionTool("kisurf_preview_anchor_focus", ...)` before route-to-anchor preview tools.
- [ ] Update direct-use smoke and provider schema tests.
- [ ] Run `qa_common --run_test=AiNativeProvider,AiDirectUseSmoke`.

## Task 5: Verification and Commit

- [ ] Build `qa_common`.
- [ ] Run the broad AI suite:

```powershell
qa_common.exe --run_test=AiModelConfig,AiDirectUseSmoke,AiNativeProvider,AiSemanticToolCallHandler,AiAgentPanelModel,AiAgentPanelSemantic,AiAgentPanel,AiSemanticUi,AiSuggestionOrchestrator,AiSuggestionOperations,AiAgentObservabilityLog,AiVisualSnapshot,AiNativeTypes,AiContextIndex,AiEditorActivityRecorder,AiActivityLog
```

- [ ] Build `pcbnew` and `eeschema`.
- [ ] Run `git diff --check`.
- [ ] Run the dynamic secret scan.
- [ ] Stage only files touched by this slice and commit with message `feat: preview semantic anchor focus`.
