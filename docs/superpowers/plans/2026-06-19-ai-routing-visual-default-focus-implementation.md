# AI Routing Visual Default Focus Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make visual-frame and workspace-view reads automatically include routing net/layer focus directives while PCB routing is active.

**Architecture:** Keep the existing `VISUAL_FRAME_TOOL_OPTIONS` parser and `render_directives` JSON builder. Add a post-parse augmentation step that fills missing directive fields from active `RoutingTrack` tool-state and positional routing anchors before validation and JSON rendering.

**Tech Stack:** C++17, nlohmann JSON, Boost unit tests, KiSurf common AI semantic tool-call handler.

---

## File Map

- Modify `common/kisurf/ai/ai_semantic_tool_call_handler.cpp`: add routing-default visual directive augmentation.
- Modify `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`: update visual-frame defaults for routing context and add non-routing guard coverage.
- Modify `README.md`: mention routing visual reads carry default focus directives.
- Create `docs/superpowers/specs/2026-06-19-ai-routing-visual-default-focus-design.md`.
- Create `docs/superpowers/plans/2026-06-19-ai-routing-visual-default-focus-implementation.md`.

## Task 1: Visual Frame Red Tests

**Files:**
- Modify: `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`

- [ ] **Step 1: Update routing default visual test**

In `VisualFrameToolReturnsMetadataWithoutPixelsByDefault`, replace the assertion that `render_directives` is absent with assertions that routing defaults are present:

```cpp
BOOST_REQUIRE( visual.contains( "render_directives" ) );
const nlohmann::json& directives = visual["render_directives"];
BOOST_CHECK_EQUAL( directives["focus_layer"].get<std::string>(), "F.Cu" );
BOOST_CHECK_EQUAL( directives["focus_net"].get<std::string>(), "/GPIO" );
BOOST_CHECK( directives["dim_unfocused_layers"].get<bool>() );
BOOST_REQUIRE( directives.contains( "highlight_anchor_ids" ) );
BOOST_REQUIRE_EQUAL( directives["highlight_anchor_ids"].size(), 2 );
BOOST_CHECK_EQUAL( directives["highlight_anchor_ids"][0].get<std::string>(),
                   "tool.routing.start" );
BOOST_CHECK_EQUAL( directives["highlight_anchor_ids"][1].get<std::string>(),
                   "pcb.pad.target" );
```

- [ ] **Step 2: Add non-routing guard test**

Add `VisualFrameToolOmitsRenderDirectivesByDefaultOutsideRouting` after the routing-default test:

```cpp
BOOST_AUTO_TEST_CASE( VisualFrameToolOmitsRenderDirectivesByDefaultOutsideRouting )
{
    AI_PROVIDER_REQUEST request = requestWithSelection();
    request.m_ContextSnapshot.m_Visual.m_Source = wxS( "pcbnew.canvas" );
    request.m_ContextSnapshot.m_Visual.m_MimeType = wxS( "image/png" );
    request.m_ContextSnapshot.m_Visual.m_WidthPx = 640;
    request.m_ContextSnapshot.m_Visual.m_HeightPx = 480;
    request.m_ContextSnapshot.m_Visual.m_ByteSize = 0;

    AI_SEMANTIC_TOOL_CALL_HANDLER handler( nullptr );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            request,
            toolCall( wxS( "kisurf_get_visual_frame" ), wxS( "{}" ) ) );

    BOOST_CHECK( result.m_Allowed );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK( !payload["visual"].contains( "render_directives" ) );
}
```

- [ ] **Step 3: Add workspace default assertion**

In `WorkspaceViewToolReturnsAllSectionsByDefault`, assert `view["visual"]["render_directives"]` contains `focus_layer = "F.Cu"`, `focus_net = "/GPIO"`, and `dim_unfocused_layers = true`.

- [ ] **Step 4: Verify red**

Run:

```powershell
cmd.exe /S /C 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build out\build\x64-release --target qa_common --config Release'
$env:KICAD_RUN_FROM_BUILD_DIR='1'
& out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiSemanticToolCallHandler/VisualFrameToolReturnsMetadataWithoutPixelsByDefault,AiSemanticToolCallHandler/WorkspaceViewToolReturnsAllSectionsByDefault
```

Expected: the routing-default assertions fail because no default render directives are generated yet.

## Task 2: Production Implementation

**Files:**
- Modify: `common/kisurf/ai/ai_semantic_tool_call_handler.cpp`

- [ ] **Step 1: Add explicit-field tracking**

Extend `VISUAL_FRAME_TOOL_OPTIONS` with:

```cpp
bool m_HasExplicitDimUnfocusedLayers = false;
bool m_HasExplicitHighlightAnchorIds = false;
```

Set these booleans in `parseVisualFrameToolOptions()` when the corresponding keys are present.

- [ ] **Step 2: Add routing helpers**

Add helper functions near the visual-frame code:

```cpp
bool routingToolStateModeContext( const AI_CONTEXT_SNAPSHOT& aSnapshot,
                                  nlohmann::json& aModeContext );
bool isRoutingVisualAnchorKind( AI_CONTEXT_ANCHOR_KIND aKind );
void applyRoutingVisualDefaults( const AI_CONTEXT_SNAPSHOT& aSnapshot,
                                 VISUAL_FRAME_TOOL_OPTIONS& aOptions );
```

`routingToolStateModeContext` returns true only when snapshot editor is PCB, tool-state editor is PCB, tool-state kind is `RoutingTrack`, and `m_ModeContextJson` parses to an object.

`applyRoutingVisualDefaults` fills missing `m_FocusLayer` and `m_FocusNet` from `layer` and `net`, sets dimming to true unless explicitly supplied, and fills `m_HighlightAnchorIds` with up to 32 positional routing anchors unless explicit highlights were supplied.

- [ ] **Step 3: Invoke defaults before validation**

In both `visualFrameResult(...)` and `workspaceViewResult(...)`, call:

```cpp
applyRoutingVisualDefaults( aRequest.m_ContextSnapshot, *options );
```

for the visual options before `validateVisualFrameToolOptions(...)`.

- [ ] **Step 4: Verify green**

Run:

```powershell
$env:KICAD_RUN_FROM_BUILD_DIR='1'
& out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiSemanticToolCallHandler
```

Expected: all semantic handler tests pass.

## Task 3: Docs And Verification

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update README**

Mention that visual-frame and workspace-view visual reads now include routing net/layer focus directives automatically during active routing.

- [ ] **Step 2: Run full AI verification and editor builds**

Run:

```powershell
cmd.exe /S /C 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build out\build\x64-release --target qa_common --config Release'
$env:KICAD_RUN_FROM_BUILD_DIR='1'
& out\build\x64-release\qa\tests\common\qa_common.exe --run_test=Ai*
cmd.exe /S /C 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build out\build\x64-release --target pcbnew --config Release && cmake --build out\build\x64-release --target eeschema --config Release'
git diff --check
$prefix = 'sk' + '-'
$pattern = $prefix + '[A-Za-z0-9_-]{20,}|OPENAI_API_KEY=.*' + $prefix
rg -n $pattern common docs include qa pcbnew README.md
```

Expected: tests and builds pass, diff check is clean except known line-ending warnings, and secret scan has no matches.

- [ ] **Step 3: Commit**

Run:

```powershell
git add docs/superpowers/specs/2026-06-19-ai-routing-visual-default-focus-design.md docs/superpowers/plans/2026-06-19-ai-routing-visual-default-focus-implementation.md common/kisurf/ai/ai_semantic_tool_call_handler.cpp qa/tests/common/test_ai_semantic_tool_call_handler.cpp README.md
git commit -m "feat: add routing visual focus defaults"
```

Expected: commit succeeds and unrelated `qa/tests/pcbnew/test_module.cpp` remains unstaged.

## Self-Review

- Spec coverage: routing defaults, explicit overrides, workspace nesting, non-routing guard, docs, and verification are all covered.
- Placeholder scan: no TBD or TODO markers.
- Type consistency: helper names and enum names match the existing code.
