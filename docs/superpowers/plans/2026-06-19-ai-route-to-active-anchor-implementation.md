# AI Route To Active Anchor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Allow the model to preview routing to a semantic anchor by passing only `target_anchor_id`, with KiSurf inferring the active route start from `tool.routing.start`.

**Architecture:** Keep the existing `kisurf_preview_route_to_anchor` tool, route suggestion operation, and preview/edit adapters. Relax the provider schema and semantic argument parser so `start_anchor_id` is optional and defaults to the existing active routing start anchor.

**Tech Stack:** C++17, nlohmann JSON, Boost unit tests, KiSurf common AI provider and semantic tool-call handler.

---

## File Map

- Modify `qa/tests/common/test_ai_provider.cpp`: update provider schema expectations for `kisurf_preview_route_to_anchor`.
- Modify `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`: add target-only route preview coverage.
- Modify `common/kisurf/ai/ai_provider.cpp`: relax route tool required parameters and clarify `start_anchor_id` description.
- Modify `common/kisurf/ai/ai_semantic_tool_call_handler.cpp`: default missing `start_anchor_id` to `tool.routing.start` and update malformed-argument wording.
- Modify `README.md`: mention target-only route-to-anchor preview calls for active routing context.

## Task 1: Provider Schema Red Test

**Files:**
- Modify: `qa/tests/common/test_ai_provider.cpp`

- [ ] **Step 1: Write failing schema expectations**

In the provider tool surface test, change the route required assertions to:

```cpp
const std::string routeRequired = routeParameters["required"].dump();
BOOST_CHECK( routeRequired.find( "start_anchor_id" ) == std::string::npos );
BOOST_CHECK( routeRequired.find( "target_anchor_id" ) != std::string::npos );
BOOST_CHECK( routeRequired.find( "net" ) == std::string::npos );
BOOST_CHECK( routeRequired.find( "layer" ) == std::string::npos );
BOOST_CHECK( routeRequired.find( "width" ) == std::string::npos );
BOOST_CHECK( routeParameters["properties"].contains( "start_anchor_id" ) );
BOOST_CHECK( routeParameters["properties"].contains( "target_anchor_id" ) );
BOOST_CHECK( routeParameters["properties"].contains( "net" ) );
BOOST_CHECK( routeParameters["properties"].contains( "layer" ) );
BOOST_CHECK( routeParameters["properties"].contains( "width" ) );
```

- [ ] **Step 2: Verify red**

Run:

```powershell
$env:KICAD_RUN_FROM_BUILD_DIR='1'
& out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiNativeProvider/OpenAiProviderAdvertisesKiSurfTools
```

Expected: the test fails because `start_anchor_id` is still required.

## Task 2: Semantic Handler Red Test

**Files:**
- Modify: `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`

- [ ] **Step 1: Add target-only preview test**

Add this test after `RouteToAnchorCreatesRouteSegmentPreviewSuggestion`:

```cpp
BOOST_AUTO_TEST_CASE( RouteToAnchorDefaultsStartAnchorToActiveRoutingAnchor )
{
    std::optional<AI_SUGGESTION_RECORD> captured;

    AI_SEMANTIC_TOOL_CALL_HANDLER handler(
            [&]( AI_SUGGESTION_RECORD aSuggestion )
            {
                aSuggestion.m_Id = 109;
                captured = aSuggestion;
                return captured;
            } );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithRouteAnchors(),
            toolCall( wxS( "kisurf_preview_route_to_anchor" ),
                      wxS( "{\"target_anchor_id\":\"pcb.pad.target\"}" ) ) );

    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString() );
    BOOST_REQUIRE( captured.has_value() );
    BOOST_CHECK( captured->m_Fingerprint.Contains( wxS( "tool.routing.start" ) ) );

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( captured->m_ArgumentsJson );
    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsRouteSegmentPreview() );
    BOOST_CHECK_EQUAL( operation->m_NetName, wxString( wxS( "/GPIO" ) ) );
    BOOST_CHECK_EQUAL( operation->m_LayerName, wxString( wxS( "F.Cu" ) ) );
    BOOST_CHECK_EQUAL( operation->m_Width, 150000 );
    BOOST_CHECK_EQUAL( operation->m_Start.x, 100 );
    BOOST_CHECK_EQUAL( operation->m_Start.y, 200 );
    BOOST_CHECK_EQUAL( operation->m_End.x, 500 );
    BOOST_CHECK_EQUAL( operation->m_End.y, 650 );
}
```

- [ ] **Step 2: Verify red**

Run:

```powershell
$env:KICAD_RUN_FROM_BUILD_DIR='1'
& out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiSemanticToolCallHandler/RouteToAnchorDefaultsStartAnchorToActiveRoutingAnchor
```

Expected: after rebuilding, the test fails with `malformed_arguments` because `start_anchor_id` is still required.

## Task 3: Production Changes

**Files:**
- Modify: `common/kisurf/ai/ai_provider.cpp`
- Modify: `common/kisurf/ai/ai_semantic_tool_call_handler.cpp`

- [ ] **Step 1: Relax provider schema**

In `routeToAnchorToolParameters()`, update `start_anchor_id` description to say it is optional and defaults to the active routing start anchor. Change `required` to:

```cpp
{ "required", nlohmann::json::array( { "target_anchor_id" } ) },
```

- [ ] **Step 2: Default route start in semantic handler**

In `routeToAnchorFromArgs(...)`, parse `target_anchor_id` as required, parse `start_anchor_id` as optional, and default it:

```cpp
if( !jsonStringField( aArgs, "target_anchor_id", targetAnchorId ) )
{
    aErrorCode = wxS( "malformed_arguments" );
    aErrorMessage = wxS( "route_to_anchor requires target_anchor_id." );
    return std::nullopt;
}

if( !jsonStringField( aArgs, "start_anchor_id", startAnchorId ) )
    startAnchorId = wxS( "tool.routing.start" );
```

Leave the rest of the route parameter inference untouched.

- [ ] **Step 3: Verify green**

Run:

```powershell
$env:KICAD_RUN_FROM_BUILD_DIR='1'
& out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiNativeProvider,AiSemanticToolCallHandler
```

Expected: all targeted tests pass.

## Task 4: Docs And Final Verification

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update README**

In the implemented tool list or quickstart section, mention that active-routing previews can target a semantic anchor with `kisurf_preview_route_to_anchor` without passing an explicit start anchor; KiSurf uses the current `tool.routing.start` anchor.

- [ ] **Step 2: Run broader verification**

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

Expected: builds succeed, AI tests pass, diff check reports no whitespace errors beyond known line-ending warnings, and secret scan has no matches.

- [ ] **Step 3: Commit**

Run:

```powershell
git add docs/superpowers/specs/2026-06-19-ai-route-to-active-anchor-design.md docs/superpowers/plans/2026-06-19-ai-route-to-active-anchor-implementation.md common/kisurf/ai/ai_provider.cpp common/kisurf/ai/ai_semantic_tool_call_handler.cpp qa/tests/common/test_ai_provider.cpp qa/tests/common/test_ai_semantic_tool_call_handler.cpp README.md
git commit -m "feat: infer active route start anchor"
```

Expected: commit succeeds and unrelated `qa/tests/pcbnew/test_module.cpp` remains unstaged.

## Self-Review

- Spec coverage: provider schema, handler behavior, error behavior, docs, and verification are each covered by a task.
- Placeholder scan: no TBD or TODO markers.
- Type consistency: all names match existing code symbols and tests.
