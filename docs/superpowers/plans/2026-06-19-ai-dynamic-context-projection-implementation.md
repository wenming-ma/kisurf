# AI Dynamic Context Projection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a stable `dynamic_context` projection to AI context JSON and prompt text.

**Architecture:** Build the projection inside `common/kisurf/ai/ai_types.cpp`, where context prompt and JSON serialization already live.  Keep it derived from existing `AI_CONTEXT_SNAPSHOT` fields so no new storage or editor-side capture path is required.

**Tech Stack:** C++17, wxWidgets strings, nlohmann::json, Boost unit tests.

---

Date: 2026-06-19
Spec: `docs/superpowers/specs/2026-06-19-ai-dynamic-context-projection-design.md`

## Files

- Modify: `common/kisurf/ai/ai_types.cpp`
  - Add helper functions for dynamic context projection.
  - Include `dynamic_context` in context JSON.
  - Include a compact prompt line.
- Modify: `qa/tests/common/test_ai_types.cpp`
  - Add tests for routing, layout, panel, general, idle, and prompt output.
- Modify: `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`
  - Assert `kisurf_get_context_snapshot` and `kisurf_get_workspace_view.context` carry `dynamic_context`.

## Task 1: Add Failing Type Tests

- [x] **Step 1: Routing projection**

Add a test that creates a PCB snapshot with `m_ToolState.m_Kind = AI_TOOL_STATE_KIND::RoutingTrack` and an active action.  Assert:

```cpp
context["dynamic_context"]["kind"] == "routing"
context["dynamic_context"]["source"] == "tool_state"
context["dynamic_context"]["tool_state_kind"] == "routing_track"
context["dynamic_context"]["active_action"] == "pcbnew.InteractiveRoute"
```

Also assert prompt contains `dynamic context: routing`.

- [x] **Step 2: Layout projection**

Add a test that uses `PlacingFootprint` and asserts `kind == "layout"`.

- [x] **Step 3: Panel projection**

Add a test with no active tool state and a panel record containing focused control fields.  Assert:

```cpp
kind == "panel"
source == "panel_state"
focused_panel_id == ...
focused_control_id == ...
```

- [x] **Step 4: General and idle projections**

Add assertions for:

- `Selecting` -> `general`
- `Idle` -> `idle`

- [x] **Step 5: Run type tests to verify RED**

Run:

```powershell
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiNativeTypes --log_level=test_suite
```

Expected: FAIL because `dynamic_context` is not yet serialized.

## Task 2: Add Failing Semantic Tool Tests

- [x] **Step 1: Context snapshot tool carries dynamic context**

Extend `ContextSnapshotToolReturnsBoundedUnifiedContextWithoutSuggestionSink` to assert:

```cpp
context["dynamic_context"]["kind"] == "routing"
```

- [x] **Step 2: Workspace view context carries dynamic context**

Extend `WorkspaceViewToolReturnsAllSectionsByDefault` to assert:

```cpp
view["context"]["dynamic_context"]["kind"] == "routing"
```

- [x] **Step 3: Run semantic handler tests to verify RED**

Run:

```powershell
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiSemanticToolCallHandler --log_level=test_suite
```

Expected: FAIL because context JSON does not yet include `dynamic_context`.

## Task 3: Implement Projection

- [x] **Step 1: Add helper to find focused panel**

In `ai_types.cpp`, add a helper that returns the first panel with a focused control id, focused control label, or selected text.

- [x] **Step 2: Add dynamic context kind helper**

Map tool state kinds:

- RoutingTrack -> routing
- PlacingVia/PlacingFootprint/DrawingZone/MovingSelection -> layout
- Selecting -> general
- Idle -> idle
- Unknown -> unknown

If tool state is unknown and a focused panel exists, return panel.

- [x] **Step 3: Build JSON object**

Return an object with `kind`, `source`, and `tool_state_kind`, plus optional `active_action` and focused panel fields.

- [x] **Step 4: Include JSON in context**

Add:

```cpp
context["dynamic_context"] = dynamicContextJson( *this );
```

- [x] **Step 5: Include prompt line**

Add a compact line near the existing editor/version/summary lines.

## Task 4: Verify

- [x] Build `qa_common`.
- [x] Run `AiNativeTypes`.
- [x] Run `AiSemanticToolCallHandler`.
- [x] Run `AiNativeProvider`.
- [x] Build `pcbnew`.
- [x] Build `eeschema`.
- [x] Run `git diff --check`.
- [x] Run dynamic secret scan without echoing any key.

## Task 5: Commit

- [x] Inspect `git diff`.
- [ ] Stage only files touched by this slice.
- [ ] Commit with:

```text
feat: add AI dynamic context projection
```

## Handoff Notes

- `dynamic_context` is a strategy-selection hint, not an execution engine.
- Keep the raw `tool_state` and `panel_states` fields unchanged.
- Panel mode intentionally yields to routing/layout/general tool states.
