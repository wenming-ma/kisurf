# AI Visual Render Directives Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add declarative visual render directives to `kisurf_get_visual_frame` and `kisurf_get_workspace_view.visual`.

**Architecture:** Extend the existing shared `VISUAL_FRAME_TOOL_OPTIONS` parse path so standalone and workspace visual reads expose one contract.  Validate highlighted anchors against positional anchors in the current `AI_CONTEXT_SNAPSHOT` before returning `render_directives`.

**Tech Stack:** C++17, wxWidgets strings, nlohmann::json, Boost unit tests, existing KiSurf AI common-layer helpers.

---

Date: 2026-06-19
Spec: `docs/superpowers/specs/2026-06-19-ai-visual-render-directives-design.md`

## Files

- Modify: `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`
  - Add tests for visual render directives, workspace nested visual directives, malformed directive arguments, and unknown/non-positional anchor validation.
- Modify: `qa/tests/common/test_ai_provider.cpp`
  - Add provider schema assertions for `focus_layer`, `focus_net`, `dim_unfocused_layers`, and `highlight_anchor_ids`.
- Modify: `common/kisurf/ai/ai_semantic_tool_call_handler.cpp`
  - Extend visual options, parse directive parameters, validate highlighted anchors, and emit `render_directives`.
- Modify: `common/kisurf/ai/ai_provider.cpp`
  - Advertise the new visual directive parameters in the provider tool schema.

## Task 1: Add Failing Semantic Handler Tests

- [x] **Step 1: Add standalone render directive test**

Add a test named `VisualFrameToolReturnsRenderDirectivesWhenRequested` near the existing visual-frame tests.  It should call:

```json
{
  "focus_layer": "F.Cu",
  "focus_net": "/GPIO",
  "dim_unfocused_layers": true,
  "highlight_anchor_ids": [
    "tool.routing.start",
    "pcb.pad.target"
  ]
}
```

Assert:

```cpp
BOOST_REQUIRE( visual.contains( "render_directives" ) );
BOOST_CHECK_EQUAL( directives["focus_layer"].get<std::string>(), "F.Cu" );
BOOST_CHECK_EQUAL( directives["focus_net"].get<std::string>(), "/GPIO" );
BOOST_CHECK( directives["dim_unfocused_layers"].get<bool>() );
BOOST_REQUIRE_EQUAL( directives["highlight_anchor_ids"].size(), 2 );
BOOST_CHECK_EQUAL( directives["highlight_anchor_ids"][0].get<std::string>(),
                   "tool.routing.start" );
```

- [x] **Step 2: Assert default visual frame omits empty directives**

Extend `VisualFrameToolReturnsMetadataWithoutPixelsByDefault` or add a small test asserting the default visual response does not contain `render_directives`.

- [x] **Step 3: Add workspace nested directive test**

Add a workspace view test that calls:

```json
{
  "views": ["visual"],
  "visual": {
    "focus_layer": "F.Cu",
    "highlight_anchor_ids": ["tool.routing.start"]
  }
}
```

Assert the nested `workspace_view.visual.render_directives` contains the same layer and anchor ID.

- [x] **Step 4: Add malformed directive tests**

Extend `VisualFrameToolRejectsMalformedArguments` or add a new test for:

```json
{"focus_layer":""}
{"focus_net":7}
{"dim_unfocused_layers":"yes"}
{"highlight_anchor_ids":"tool.routing.start"}
{"highlight_anchor_ids":[""]}
{"highlight_anchor_ids":["missing.anchor"]}
```

Each must be denied with `malformed_arguments`.

- [x] **Step 5: Add non-positional anchor validation test**

Create a request from `requestWithUnifiedContext()`, set one requested anchor's `m_HasPosition` to `false`, call with that ID in `highlight_anchor_ids`, and assert `malformed_arguments`.

- [x] **Step 6: Run semantic handler tests to verify RED**

Build `qa_common` if needed and run:

```powershell
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiSemanticToolCallHandler --log_level=test_suite
```

Expected: FAIL because the visual parser rejects the new fields or omits `render_directives`.

## Task 2: Add Failing Provider Schema Tests

- [x] **Step 1: Extend provider schema assertions**

In `qa/tests/common/test_ai_provider.cpp`, assert that both `kisurf_get_visual_frame` parameters and `kisurf_get_workspace_view.properties.visual` include:

```cpp
focus_layer
focus_net
dim_unfocused_layers
highlight_anchor_ids
```

- [x] **Step 2: Run provider tests to verify RED**

Run:

```powershell
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiNativeProvider --log_level=test_suite
```

Expected: FAIL because the schema does not yet advertise the directive parameters.

## Task 3: Implement Visual Directive Parsing and Output

- [x] **Step 1: Extend visual options**

Add fields to `VISUAL_FRAME_TOOL_OPTIONS`:

```cpp
wxString              m_FocusLayer;
wxString              m_FocusNet;
bool                  m_DimUnfocusedLayers = false;
std::vector<wxString> m_HighlightAnchorIds;
```

- [x] **Step 2: Extend allowed argument list**

Allow `focus_layer`, `focus_net`, `dim_unfocused_layers`, and `highlight_anchor_ids` in `parseVisualFrameToolOptions()`.

- [x] **Step 3: Parse strings and booleans**

Use existing string and boolean helpers.  Empty `focus_layer` or `focus_net` fails with `malformed_arguments`.

- [x] **Step 4: Parse bounded highlight anchor IDs**

Require an array, maximum `32`, all entries strings, all entries non-empty.  Keep order and duplicates as supplied for now.

- [x] **Step 5: Validate highlighted anchors**

Before returning visual data, validate each requested ID against `AI_CONTEXT_SNAPSHOT::m_Anchors`.  The anchor must exist and `m_HasPosition` must be true.  Otherwise return `malformed_arguments`.

- [x] **Step 6: Emit render_directives when requested**

Add helper(s) that build `render_directives` only if at least one directive option is supplied.  Include:

```json
{
  "focus_layer": "F.Cu",
  "focus_net": "/GPIO",
  "dim_unfocused_layers": true,
  "highlight_anchor_ids": ["tool.routing.start"]
}
```

Only include supplied string fields.  Include `dim_unfocused_layers` only when true.  Include `highlight_anchor_ids` only when non-empty.

## Task 4: Implement Provider Schema

- [x] **Step 1: Add schema fields**

Add parameters to `visualFrameToolParameters()`:

- `focus_layer`: string
- `focus_net`: string
- `dim_unfocused_layers`: boolean
- `highlight_anchor_ids`: array of strings

- [x] **Step 2: Keep nested workspace schema in sync**

Because `workspaceViewToolParameters()` reuses `visualFrameToolParameters()`, the nested workspace visual schema should inherit the same fields automatically.

## Task 5: Verify

- [x] Run `qa_common --run_test=AiSemanticToolCallHandler`.
- [x] Run `qa_common --run_test=AiNativeProvider`.
- [x] Build `pcbnew`.
- [x] Build `eeschema`.
- [x] Run `git diff --check`.
- [x] Run dynamic secret scan without echoing any key.

## Task 6: Commit

- [x] Inspect `git diff`.
- [ ] Stage only files touched by this slice.
- [ ] Commit with:

```text
feat: add visual render directives
```

## Handoff Notes

- This slice is deliberately metadata-only.  Do not claim canvas pixels are highlighted.
- `render_directives` is the future input contract for renderer overlays, annotated screenshots, and UI previews.
- Do not touch unrelated dirty files.
