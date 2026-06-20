# AI Visual Anchor Overlays Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add bounded semantic anchor overlay metadata to `kisurf_get_visual_frame` and the nested `visual` section of `kisurf_get_workspace_view`.

**Architecture:** Reuse `AI_CONTEXT_SNAPSHOT::m_Anchors` as the single source of truth. Extend existing visual-frame option parsing and JSON generation so both standalone and workspace visual reads share one contract.

**Tech Stack:** C++17, wxWidgets strings, nlohmann::json, Boost unit tests, existing KiSurf AI common-layer helpers.

---

Date: 2026-06-19
Spec: `docs/superpowers/specs/2026-06-19-ai-visual-anchor-overlays-design.md`

## Files

- Modify: `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`
  - Add tests for default overlays, opt-out overlays, bounded overlay count, and malformed overlay arguments.
- Modify: `qa/tests/common/test_ai_provider.cpp`
  - Add provider schema assertions for `include_anchor_overlays` and `max_anchor_overlays`.
- Modify: `common/kisurf/ai/ai_semantic_tool_call_handler.cpp`
  - Extend visual options, argument parsing, visual JSON generation, and workspace visual reuse.
- Modify: `common/kisurf/ai/ai_provider.cpp`
  - Advertise new visual parameters in the provider tool schema.

## Task 1: Add Failing Semantic Handler Tests

- [x] **Step 1: Add visual-frame default overlay test**

Add a test named `VisualFrameToolIncludesAnchorOverlaysByDefault` in `qa/tests/common/test_ai_semantic_tool_call_handler.cpp` near the existing visual-frame tests.  It should call `kisurf_get_visual_frame` with `{}` and assert:

```cpp
BOOST_CHECK_EQUAL( visual["anchor_overlay_count"].get<int>(), 2 );
BOOST_REQUIRE( visual.contains( "anchor_overlays" ) );
BOOST_REQUIRE_EQUAL( visual["anchor_overlays"].size(), 2 );
BOOST_CHECK_EQUAL( visual["anchor_overlays"][0]["id"].get<std::string>(),
                   "tool.routing.start" );
BOOST_CHECK_EQUAL( visual["anchor_overlays"][0]["kind"].get<std::string>(),
                   "route_start" );
BOOST_CHECK_EQUAL( visual["anchor_overlays"][0]["position"]["x"].get<int>(), 100 );
BOOST_CHECK_EQUAL( visual["anchor_overlays"][0]["position"]["y"].get<int>(), 200 );
BOOST_CHECK( !visual.contains( "data_uri" ) );
```

- [x] **Step 2: Add opt-out and limit tests**

Add tests named `VisualFrameToolCanOmitAnchorOverlays` and `VisualFrameToolLimitsAnchorOverlays`.  The opt-out test should pass `{"include_anchor_overlays":false}` and assert `anchor_overlay_count == 2` and no `anchor_overlays` property.  The limit test should pass `{"max_anchor_overlays":1}` and assert `anchor_overlay_count == 2` and array size `1`.

- [x] **Step 3: Add workspace nested visual option test**

Extend `WorkspaceViewToolReturnsOnlyRequestedViewsWithNestedOptions` or add a new test that passes:

```json
{"views":["visual"],"visual":{"include_anchor_overlays":false}}
```

Assert the nested `workspace_view.visual` contains `anchor_overlay_count == 2` and omits `anchor_overlays`.

- [x] **Step 4: Add malformed-argument assertions**

Extend `VisualFrameToolRejectsMalformedArguments` so calls with these arguments are denied with `malformed_arguments`:

```json
{"include_anchor_overlays":"yes"}
{"max_anchor_overlays":0}
```

- [x] **Step 5: Run semantic handler tests to verify RED**

Run:

```powershell
$root = Get-Location
$env:PATH = (Join-Path $root 'out\build\x64-release\api') + ';' + (Join-Path $root 'out\build\x64-release\common') + ';' + (Join-Path $root 'out\build\x64-release\common\gal') + ';' + (Join-Path $root 'out\build\x64-release\qa\tests\common') + ';' + $env:PATH
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiSemanticToolCallHandler --log_level=test_suite
```

Expected: FAIL because visual JSON does not yet include anchor overlay fields or does not accept the new arguments.

## Task 2: Add Failing Provider Schema Tests

- [x] **Step 1: Update provider schema assertions**

In `qa/tests/common/test_ai_provider.cpp`, extend the visual tool parameter assertions to require:

```cpp
BOOST_CHECK( visualProperties.contains( "include_anchor_overlays" ) );
BOOST_CHECK( visualProperties.contains( "max_anchor_overlays" ) );
```

If the test already inspects nested `workspace_view.visual`, assert the same properties there.  If not, use the existing reuse of `visualFrameToolParameters()` through the workspace schema as implementation coverage.

- [x] **Step 2: Run provider test to verify RED**

Run:

```powershell
$root = Get-Location
$env:PATH = (Join-Path $root 'out\build\x64-release\api') + ';' + (Join-Path $root 'out\build\x64-release\common') + ';' + (Join-Path $root 'out\build\x64-release\common\gal') + ';' + (Join-Path $root 'out\build\x64-release\qa\tests\common') + ';' + $env:PATH
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiNativeProvider --log_level=test_suite
```

Expected: FAIL because the provider schema does not yet advertise the new parameters.

## Task 3: Implement Shared Visual Overlay Output

- [x] **Step 1: Extend visual options**

In `common/kisurf/ai/ai_semantic_tool_call_handler.cpp`, extend `VISUAL_FRAME_TOOL_OPTIONS`:

```cpp
bool   m_IncludePixels = false;
size_t m_MaxBytes = 262144;
bool   m_IncludeAnchorOverlays = true;
size_t m_MaxAnchorOverlays = 64;
```

- [x] **Step 2: Extend option parsing**

Allow `include_anchor_overlays` and `max_anchor_overlays` in `parseVisualFrameToolOptions()`.  Use `jsonBooleanField()` for the boolean and `jsonLimitField( ..., 128, ... )` for the max overlay count.  Return `malformed_arguments` with a specific message when either field is invalid.

- [x] **Step 3: Add overlay JSON helpers**

Add helper functions beside `visualFrameJson()`:

```cpp
size_t visualAnchorOverlayCount( const AI_CONTEXT_SNAPSHOT& aSnapshot );
nlohmann::json anchorOverlayJson( const AI_CONTEXT_ANCHOR& aAnchor );
nlohmann::json anchorOverlaysJson( const AI_CONTEXT_SNAPSHOT& aSnapshot,
                                   size_t aMaxAnchorOverlays );
```

Each overlay record should include `id`, `kind`, `label`, `summary`, `position`, `layer`, and `confidence`.  Only anchors with `m_HasPosition` are included.

- [x] **Step 4: Route visual JSON through the snapshot**

Change `visualFrameJson()` to receive `const AI_CONTEXT_SNAPSHOT&` and `const VISUAL_FRAME_TOOL_OPTIONS&`, then read `snapshot.m_Visual` internally.  Always set `anchor_overlay_count`.  Set `anchor_overlays` only when `m_IncludeAnchorOverlays` is true.

- [x] **Step 5: Update call sites**

Update both `visualFrameResult()` and `workspaceViewResult()` to call the new `visualFrameJson( aRequest.m_ContextSnapshot, *options )` shape.

- [x] **Step 6: Run semantic handler tests to verify GREEN**

Run the semantic handler command from Task 1.  Expected: PASS.

## Task 4: Implement Provider Schema

- [x] **Step 1: Add schema properties**

In `common/kisurf/ai/ai_provider.cpp`, add:

```cpp
{ "include_anchor_overlays",
  booleanParameter( "When true, include bounded semantic anchor overlay metadata for anchors that have positions." ) },
{ "max_anchor_overlays",
  limitParameter( 128, "Maximum positional semantic anchor overlay records to return." ) }
```

inside `visualFrameToolParameters()`.

- [x] **Step 2: Run provider tests to verify GREEN**

Run the provider command from Task 2.  Expected: PASS.

## Task 5: Full Verification and Commit

- [x] **Step 1: Build affected targets**

Run:

```powershell
cmd.exe /S /C '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build out\build\x64-release --target qa_common --config Release'
cmd.exe /S /C '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build out\build\x64-release --target pcbnew --config Release'
cmd.exe /S /C '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build out\build\x64-release --target eeschema --config Release'
```

- [x] **Step 2: Run focused test suites**

Run:

```powershell
$root = Get-Location
$env:PATH = (Join-Path $root 'out\build\x64-release\api') + ';' + (Join-Path $root 'out\build\x64-release\common') + ';' + (Join-Path $root 'out\build\x64-release\common\gal') + ';' + (Join-Path $root 'out\build\x64-release\qa\tests\common') + ';' + $env:PATH
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiSemanticToolCallHandler --log_level=test_suite
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiNativeProvider --log_level=test_suite
```

- [x] **Step 3: Run hygiene checks**

Run:

```powershell
git diff --check
$prefix = 'sk' + '-'
$pattern = $prefix + '[A-Za-z0-9_-]{20,}|OPENAI_API_KEY=.*' + $prefix
rg -n $pattern docs/superpowers/specs/2026-06-19-ai-visual-anchor-overlays-design.md docs/superpowers/plans/2026-06-19-ai-visual-anchor-overlays-implementation.md common/kisurf/ai/ai_semantic_tool_call_handler.cpp common/kisurf/ai/ai_provider.cpp qa/tests/common/test_ai_semantic_tool_call_handler.cpp qa/tests/common/test_ai_provider.cpp
```

Expected: `git diff --check` exits `0`; the secret scan returns no matches.

- [x] **Step 4: Stage only touched files and commit**

Run:

```powershell
git add docs/superpowers/specs/2026-06-19-ai-visual-anchor-overlays-design.md docs/superpowers/plans/2026-06-19-ai-visual-anchor-overlays-implementation.md common/kisurf/ai/ai_semantic_tool_call_handler.cpp common/kisurf/ai/ai_provider.cpp qa/tests/common/test_ai_semantic_tool_call_handler.cpp qa/tests/common/test_ai_provider.cpp
git commit -m "feat: add visual anchor overlays"
```

## Implementation Status

Completed on 2026-06-19.

- Added default positional anchor overlays to `kisurf_get_visual_frame`.
- Reused the same overlay contract for `kisurf_get_workspace_view.visual`.
- Added `include_anchor_overlays` and `max_anchor_overlays` parsing with fail-closed malformed-argument handling.
- Added provider schema properties for standalone visual and nested workspace visual tools.
- Verified RED before production implementation, then GREEN after implementation.
- Built `qa_common`, `pcbnew`, and `eeschema`.
- Re-ran focused `AiSemanticToolCallHandler` and `AiNativeProvider` suites.
- Ran `git diff --check` and a dynamic secret-pattern scan over touched files.

## Self-review

- The plan maps every spec requirement to tests or implementation steps.
- No placeholder instructions remain.
- The new option names are consistent across tests, schema, and implementation.
- The plan keeps rendered overlays and coordinate transforms out of scope.
- The verification includes builds, focused tests, whitespace checks, and secret scanning.
