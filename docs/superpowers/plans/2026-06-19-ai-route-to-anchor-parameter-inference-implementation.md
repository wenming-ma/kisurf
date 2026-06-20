# AI Route-To-Anchor Parameter Inference Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let `kisurf_preview_route_to_anchor` infer `net`, `layer`, and `width` from route anchors or active routing tool state while preserving explicit overrides.

**Architecture:** Extend the semantic handler with a tiny local `ROUTE_PARAMETERS` resolver that applies explicit arguments first, then start anchor details, target anchor details, and active PCB routing tool state. Relax the OpenAI-compatible tool schema so only anchor ids are required, while native validation still fails closed when route parameters cannot be resolved.

**Tech Stack:** KiSurf common C++20, `AI_SEMANTIC_TOOL_CALL_HANDLER`, `AI_CONTEXT_ANCHOR`, `AI_TOOL_STATE_SNAPSHOT`, nlohmann JSON, Boost unit tests in `qa_common`.

---

## File Structure

- Modify `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`
  - Add route-to-anchor inference tests before the existing failure tests.
  - Extend the anchor helper with optional details JSON.
- Modify `qa/tests/common/test_ai_provider.cpp`
  - Update provider schema assertions for optional route parameters.
- Modify `common/kisurf/ai/ai_semantic_tool_call_handler.cpp`
  - Add route-parameter resolver helpers.
  - Change route-to-anchor argument parsing to require only anchor ids.
- Modify `common/kisurf/ai/ai_provider.cpp`
  - Change route-to-anchor schema required fields.
  - Update parameter descriptions for optional overrides.
- Modify `docs/superpowers/plans/2026-06-19-ai-route-to-anchor-parameter-inference-implementation.md`
  - Check off completed steps after verification.

## Task 1: Handler And Provider Red Tests

**Files:**
- Modify: `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`
- Modify: `qa/tests/common/test_ai_provider.cpp`

- [x] **Step 1: Extend positioned anchor test helper**

In `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`, change:

```cpp
AI_CONTEXT_ANCHOR positionedAnchor( const wxString& aId, AI_CONTEXT_ANCHOR_KIND aKind,
                                    const VECTOR2I& aPosition )
```

to:

```cpp
AI_CONTEXT_ANCHOR positionedAnchor( const wxString& aId, AI_CONTEXT_ANCHOR_KIND aKind,
                                    const VECTOR2I& aPosition,
                                    const wxString& aDetailsJson = wxEmptyString )
```

and assign details before confidence:

```cpp
    anchor.m_DetailsJson = aDetailsJson;
    anchor.m_Confidence = 1.0;
```

- [x] **Step 2: Add anchor-details inference test**

Add this test after `RouteToAnchorCreatesRouteSegmentPreviewSuggestion`:

```cpp
BOOST_AUTO_TEST_CASE( RouteToAnchorInfersRouteParametersFromAnchorDetails )
{
    std::optional<AI_SUGGESTION_RECORD> captured;

    AI_SEMANTIC_TOOL_CALL_HANDLER handler(
            [&]( AI_SUGGESTION_RECORD aSuggestion )
            {
                aSuggestion.m_Id = 106;
                captured = aSuggestion;
                return captured;
            } );

    AI_PROVIDER_REQUEST request = requestWithRouteAnchors();
    request.m_ContextSnapshot.m_Anchors.front().m_DetailsJson =
            wxS( "{\"net\":\"/GPIO\",\"layer\":\"F.Cu\",\"width\":150000}" );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            request,
            toolCall( wxS( "kisurf_preview_route_to_anchor" ),
                      wxS( "{\"start_anchor_id\":\"pcb.track.start\","
                           "\"target_anchor_id\":\"pcb.pad.target\"}" ) ) );

    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_REQUIRE( captured.has_value() );

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( captured->m_ArgumentsJson );
    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsRouteSegmentPreview() );
    BOOST_CHECK_EQUAL( operation->m_NetName, wxString( wxS( "/GPIO" ) ) );
    BOOST_CHECK_EQUAL( operation->m_LayerName, wxString( wxS( "F.Cu" ) ) );
    BOOST_CHECK_EQUAL( operation->m_Width, 150000 );
    BOOST_CHECK_EQUAL( operation->m_Start.x, 100 );
    BOOST_CHECK_EQUAL( operation->m_End.x, 500 );
}
```

- [x] **Step 3: Add tool-state fallback test**

Add this test after the anchor-details inference test:

```cpp
BOOST_AUTO_TEST_CASE( RouteToAnchorFallsBackToRoutingToolStateForMissingParameters )
{
    std::optional<AI_SUGGESTION_RECORD> captured;

    AI_SEMANTIC_TOOL_CALL_HANDLER handler(
            [&]( AI_SUGGESTION_RECORD aSuggestion )
            {
                aSuggestion.m_Id = 107;
                captured = aSuggestion;
                return captured;
            } );

    AI_PROVIDER_REQUEST request = requestWithRouteAnchors();
    request.m_ContextSnapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_ContextSnapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::RoutingTrack;
    request.m_ContextSnapshot.m_ToolState.m_ModeContextJson =
            wxS( "{\"net\":\"/UART\",\"layer\":\"B.Cu\",\"width\":120000,"
                 "\"start\":{\"x\":100,\"y\":200}}" );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            request,
            toolCall( wxS( "kisurf_preview_route_to_anchor" ),
                      wxS( "{\"start_anchor_id\":\"pcb.track.start\","
                           "\"target_anchor_id\":\"pcb.pad.target\"}" ) ) );

    BOOST_CHECK( result.m_Allowed );
    BOOST_REQUIRE( captured.has_value() );

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( captured->m_ArgumentsJson );
    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK_EQUAL( operation->m_NetName, wxString( wxS( "/UART" ) ) );
    BOOST_CHECK_EQUAL( operation->m_LayerName, wxString( wxS( "B.Cu" ) ) );
    BOOST_CHECK_EQUAL( operation->m_Width, 120000 );
}
```

- [x] **Step 4: Add explicit override test**

Add this test after the tool-state fallback test:

```cpp
BOOST_AUTO_TEST_CASE( RouteToAnchorUsesExplicitRouteParametersOverInferredDetails )
{
    std::optional<AI_SUGGESTION_RECORD> captured;

    AI_SEMANTIC_TOOL_CALL_HANDLER handler(
            [&]( AI_SUGGESTION_RECORD aSuggestion )
            {
                aSuggestion.m_Id = 108;
                captured = aSuggestion;
                return captured;
            } );

    AI_PROVIDER_REQUEST request = requestWithRouteAnchors();
    request.m_ContextSnapshot.m_Anchors.front().m_DetailsJson =
            wxS( "{\"net\":\"/DETAIL\",\"layer\":\"F.Cu\",\"width\":150000}" );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            request,
            toolCall( wxS( "kisurf_preview_route_to_anchor" ),
                      wxS( "{\"start_anchor_id\":\"pcb.track.start\","
                           "\"target_anchor_id\":\"pcb.pad.target\","
                           "\"net\":\"/EXPLICIT\",\"layer\":\"B.Cu\","
                           "\"width\":80000}" ) ) );

    BOOST_CHECK( result.m_Allowed );
    BOOST_REQUIRE( captured.has_value() );

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( captured->m_ArgumentsJson );
    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK_EQUAL( operation->m_NetName, wxString( wxS( "/EXPLICIT" ) ) );
    BOOST_CHECK_EQUAL( operation->m_LayerName, wxString( wxS( "B.Cu" ) ) );
    BOOST_CHECK_EQUAL( operation->m_Width, 80000 );
}
```

- [x] **Step 5: Add missing-parameter denial test**

Add this test before `RouteToAnchorFailsClosedForMissingAnchor`:

```cpp
BOOST_AUTO_TEST_CASE( RouteToAnchorFailsClosedWhenRouteParametersCannotBeResolved )
{
    bool sinkCalled = false;

    AI_SEMANTIC_TOOL_CALL_HANDLER handler(
            [&]( AI_SUGGESTION_RECORD aSuggestion )
            {
                wxUnusedVar( aSuggestion );
                sinkCalled = true;
                return std::optional<AI_SUGGESTION_RECORD>();
            } );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithRouteAnchors(),
            toolCall( wxS( "kisurf_preview_route_to_anchor" ),
                      wxS( "{\"start_anchor_id\":\"pcb.track.start\","
                           "\"target_anchor_id\":\"pcb.pad.target\"}" ) ) );

    BOOST_CHECK( !result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode,
                       wxString( wxS( "missing_route_parameters" ) ) );
    BOOST_CHECK( !sinkCalled );
}
```

- [x] **Step 6: Update provider required-field assertions**

In `qa/tests/common/test_ai_provider.cpp`, replace the route required checks with:

```cpp
                const nlohmann::json& routeParameters =
                        toolByName["kisurf_preview_route_to_anchor"]["function"]["parameters"];
                const std::string routeRequired = routeParameters["required"].dump();
                BOOST_CHECK( routeRequired.find( "start_anchor_id" ) != std::string::npos );
                BOOST_CHECK( routeRequired.find( "target_anchor_id" ) != std::string::npos );
                BOOST_CHECK( routeRequired.find( "net" ) == std::string::npos );
                BOOST_CHECK( routeRequired.find( "layer" ) == std::string::npos );
                BOOST_CHECK( routeRequired.find( "width" ) == std::string::npos );
                BOOST_CHECK( routeParameters["properties"].contains( "net" ) );
                BOOST_CHECK( routeParameters["properties"].contains( "layer" ) );
                BOOST_CHECK( routeParameters["properties"].contains( "width" ) );
```

- [x] **Step 7: Run red**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\pcbnew;$root\common;$root\common\gal;$root\api;$env:PATH"
& "$root\qa\tests\common\qa_common.exe" --run_test=AiSemanticToolCallHandler --log_level=test_suite
& "$root\qa\tests\common\qa_common.exe" --run_test=AiNativeProvider/OpenAiProviderDeclaresKiSurfTools --log_level=test_suite
```

Expected: build exits 0; semantic handler tests fail because missing `net`, `layer`, and `width` still produce `malformed_arguments`; provider declaration test fails because `net`, `layer`, and `width` are still required.

## Task 2: Semantic Handler Route Parameter Resolver

**Files:**
- Modify: `common/kisurf/ai/ai_semantic_tool_call_handler.cpp`

- [x] **Step 1: Add route parameter struct**

In the anonymous namespace after `selectedLabelsFingerprint()`, add:

```cpp
struct ROUTE_PARAMETERS
{
    wxString m_Net;
    wxString m_Layer;
    int      m_Width = 0;

    bool HasAll() const
    {
        return !m_Net.IsEmpty() && !m_Layer.IsEmpty() && m_Width > 0;
    }
};
```

- [x] **Step 2: Split JSON value helpers**

Replace `jsonStringField()` with these helpers:

```cpp
bool jsonStringValue( const nlohmann::json& aValue, wxString& aOut )
{
    if( !aValue.is_string() )
        return false;

    aOut = wxString::FromUTF8( aValue.get_ref<const std::string&>().c_str() );
    aOut.Trim( true ).Trim( false );
    return !aOut.IsEmpty();
}


bool jsonStringField( const nlohmann::json& aArgs, const char* aField, wxString& aOut )
{
    if( !aArgs.contains( aField ) )
        return false;

    return jsonStringValue( aArgs[aField], aOut );
}
```

Replace `jsonPositiveIntegerField()` with:

```cpp
bool jsonPositiveIntegerValue( const nlohmann::json& aValue, int& aOut )
{
    if( aValue.is_number_unsigned() )
    {
        const uint64_t value = aValue.get<uint64_t>();

        if( value == 0 || value > static_cast<uint64_t>( std::numeric_limits<int>::max() ) )
            return false;

        aOut = static_cast<int>( value );
        return true;
    }

    if( !aValue.is_number_integer() )
        return false;

    const int64_t value = aValue.get<int64_t>();

    if( value <= 0 || value > static_cast<int64_t>( std::numeric_limits<int>::max() ) )
        return false;

    aOut = static_cast<int>( value );
    return true;
}


bool jsonPositiveIntegerField( const nlohmann::json& aArgs, const char* aField, int& aOut )
{
    if( !aArgs.contains( aField ) )
        return false;

    return jsonPositiveIntegerValue( aArgs[aField], aOut );
}
```

- [x] **Step 3: Add explicit route parameter parser**

After `findAnchorById()`, add:

```cpp
bool applyExplicitRouteParameters( const nlohmann::json& aArgs,
                                   ROUTE_PARAMETERS& aParameters )
{
    if( aArgs.contains( "net" )
        && !jsonStringValue( aArgs["net"], aParameters.m_Net ) )
    {
        return false;
    }

    if( aArgs.contains( "layer" )
        && !jsonStringValue( aArgs["layer"], aParameters.m_Layer ) )
    {
        return false;
    }

    if( aArgs.contains( "width" )
        && !jsonPositiveIntegerValue( aArgs["width"], aParameters.m_Width ) )
    {
        return false;
    }

    return true;
}
```

- [x] **Step 4: Add anchor details inference**

After `applyExplicitRouteParameters()`, add:

```cpp
std::optional<nlohmann::json> parseAnchorDetails( const AI_CONTEXT_ANCHOR& aAnchor )
{
    if( aAnchor.m_DetailsJson.IsEmpty() )
        return std::nullopt;

    nlohmann::json details =
            nlohmann::json::parse( toUtf8String( aAnchor.m_DetailsJson ),
                                   nullptr, false );

    if( details.is_discarded() || !details.is_object() )
        return std::nullopt;

    return details;
}


void applyRouteParametersFromDetails( const AI_CONTEXT_ANCHOR& aAnchor,
                                      ROUTE_PARAMETERS& aParameters )
{
    std::optional<nlohmann::json> details = parseAnchorDetails( aAnchor );

    if( !details )
        return;

    if( aParameters.m_Net.IsEmpty() )
    {
        wxString net;

        if( details->contains( "net" ) && jsonStringValue( ( *details )["net"], net ) )
            aParameters.m_Net = net;
        else if( details->contains( "net_name" )
                 && jsonStringValue( ( *details )["net_name"], net ) )
        {
            aParameters.m_Net = net;
        }
    }

    if( aParameters.m_Layer.IsEmpty() && details->contains( "layer" ) )
    {
        wxString layer;

        if( jsonStringValue( ( *details )["layer"], layer ) )
            aParameters.m_Layer = layer;
    }

    if( aParameters.m_Width <= 0 && details->contains( "width" ) )
    {
        int width = 0;

        if( jsonPositiveIntegerValue( ( *details )["width"], width ) )
            aParameters.m_Width = width;
    }
}
```

- [x] **Step 5: Add routing tool-state inference**

After `applyRouteParametersFromDetails()`, add:

```cpp
void applyRouteParametersFromToolState( const AI_PROVIDER_REQUEST& aRequest,
                                        ROUTE_PARAMETERS& aParameters )
{
    const AI_TOOL_STATE_SNAPSHOT& toolState = aRequest.m_ContextSnapshot.m_ToolState;

    if( effectiveEditorKind( aRequest ) != AI_EDITOR_KIND::Pcb
        || toolState.m_EditorKind != AI_EDITOR_KIND::Pcb
        || toolState.m_Kind != AI_TOOL_STATE_KIND::RoutingTrack )
    {
        return;
    }

    nlohmann::json modeContext =
            nlohmann::json::parse( toUtf8String( toolState.m_ModeContextJson ),
                                   nullptr, false );

    if( modeContext.is_discarded() || !modeContext.is_object() )
        return;

    if( aParameters.m_Net.IsEmpty() && modeContext.contains( "net" ) )
    {
        wxString net;

        if( jsonStringValue( modeContext["net"], net ) )
            aParameters.m_Net = net;
    }

    if( aParameters.m_Layer.IsEmpty() && modeContext.contains( "layer" ) )
    {
        wxString layer;

        if( jsonStringValue( modeContext["layer"], layer ) )
            aParameters.m_Layer = layer;
    }

    if( aParameters.m_Width <= 0 && modeContext.contains( "width" ) )
    {
        int width = 0;

        if( jsonPositiveIntegerValue( modeContext["width"], width ) )
            aParameters.m_Width = width;
    }
}
```

- [x] **Step 6: Update routeToAnchorFromArgs**

In `routeToAnchorFromArgs()`, keep required anchor ids but remove `net`, `layer`, and `width` from the initial required-field check. Replace the local declarations and malformed check with:

```cpp
    wxString startAnchorId;
    wxString targetAnchorId;
    ROUTE_PARAMETERS routeParameters;

    if( !jsonStringField( aArgs, "start_anchor_id", startAnchorId )
        || !jsonStringField( aArgs, "target_anchor_id", targetAnchorId ) )
    {
        aErrorCode = wxS( "malformed_arguments" );
        aErrorMessage = wxS( "route_to_anchor requires start_anchor_id and "
                             "target_anchor_id." );
        return std::nullopt;
    }

    if( !applyExplicitRouteParameters( aArgs, routeParameters ) )
    {
        aErrorCode = wxS( "malformed_arguments" );
        aErrorMessage = wxS( "route_to_anchor optional net, layer, and width "
                             "arguments must be non-empty strings and a positive width." );
        return std::nullopt;
    }
```

After the anchor position check, add:

```cpp
    applyRouteParametersFromDetails( *start, routeParameters );
    applyRouteParametersFromDetails( *target, routeParameters );
    applyRouteParametersFromToolState( aRequest, routeParameters );

    if( !routeParameters.HasAll() )
    {
        aErrorCode = wxS( "missing_route_parameters" );
        aErrorMessage = wxS( "route_to_anchor could not resolve net, layer, "
                             "and width from arguments, anchors, or active "
                             "routing tool state." );
        return std::nullopt;
    }
```

Then change operation construction to:

```cpp
    aOperation = { { "operation", "route_segment_preview" },
                   { "net", toUtf8String( routeParameters.m_Net ) },
                   { "layer", toUtf8String( routeParameters.m_Layer ) },
                   { "width", routeParameters.m_Width },
                   { "start", pointJson( start->m_Position ) },
                   { "end", pointJson( target->m_Position ) } };
```

- [x] **Step 7: Run handler green**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\pcbnew;$root\common;$root\common\gal;$root\api;$env:PATH"
& "$root\qa\tests\common\qa_common.exe" --run_test=AiSemanticToolCallHandler --log_level=test_suite
```

Expected: `AiSemanticToolCallHandler` exits 0.

- [x] **Step 8: Commit handler implementation**

```bash
git add common/kisurf/ai/ai_semantic_tool_call_handler.cpp qa/tests/common/test_ai_semantic_tool_call_handler.cpp
git commit -m "feat: infer route-to-anchor parameters"
```

## Task 3: Provider Schema Relaxation

**Files:**
- Modify: `common/kisurf/ai/ai_provider.cpp`
- Modify: `qa/tests/common/test_ai_provider.cpp`

- [x] **Step 1: Update schema descriptions and required list**

In `routeToAnchorToolParameters()`, change `net`, `layer`, and `width` descriptions to:

```cpp
                 { "net",
                   { { "type", "string" },
                     { "description",
                       "Optional target net override. When omitted, KiSurf infers it "
                       "from route anchors or active routing state." } } },
                 { "layer",
                   { { "type", "string" },
                     { "description",
                       "Optional PCB copper layer override. When omitted, KiSurf "
                       "infers it from route anchors or active routing state." } } },
                 { "width",
                   { { "type", "integer" },
                     { "minimum", 1 },
                     { "description",
                       "Optional trace width override in board internal units. When "
                       "omitted, KiSurf infers it from route anchors or active routing "
                       "state." } } } } },
             { "required",
               nlohmann::json::array( { "start_anchor_id", "target_anchor_id" } ) },
```

- [x] **Step 2: Run provider green**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\pcbnew;$root\common;$root\common\gal;$root\api;$env:PATH"
& "$root\qa\tests\common\qa_common.exe" --run_test=AiNativeProvider/OpenAiProviderDeclaresKiSurfTools --log_level=test_suite
```

Expected: provider declaration test exits 0 and route-to-anchor schema still has five KiSurf tools.

- [x] **Step 3: Commit provider schema**

```bash
git add common/kisurf/ai/ai_provider.cpp qa/tests/common/test_ai_provider.cpp
git commit -m "feat: relax route-to-anchor tool schema"
```

## Task 4: Final Verification And Plan Status

**Files:**
- Modify: `docs/superpowers/plans/2026-06-19-ai-route-to-anchor-parameter-inference-implementation.md`

- [x] **Step 1: Run common AI route verification**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\pcbnew;$root\common;$root\common\gal;$root\api;$env:PATH"
& "$root\qa\tests\common\qa_common.exe" --run_test=AiSemanticToolCallHandler --log_level=nothing
& "$root\qa\tests\common\qa_common.exe" --run_test=AiNativeProvider/OpenAiProviderDeclaresKiSurfTools --log_level=nothing
& "$root\qa\tests\common\qa_common.exe" --run_test=AiSuggestionOperations --log_level=nothing
```

Expected: all commands exit 0.

- [x] **Step 2: Run editor target smoke build**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target pcbnew -- -j 2"
```

Expected: `pcbnew` builds with exit code 0.

- [x] **Step 3: Run whitespace and secret checks**

Run:

```powershell
git diff --check
rg -n "sk-[0-9A-Za-z_-]{20,}|OPENAI_API_KEY\s*=|KISURF_AI_API_KEY\s*=" common include pcbnew qa docs
```

Expected: whitespace check exits 0; secret scan has no matches.

- [x] **Step 4: Update this plan status**

Check off each completed step in this file.

- [x] **Step 5: Commit final plan status**

```bash
git add docs/superpowers/plans/2026-06-19-ai-route-to-anchor-parameter-inference-implementation.md
git commit -m "docs: update route-to-anchor inference plan status"
```

## Self-Review

- Spec coverage: Tasks implement optional schema fields, native inference order, explicit overrides, missing-parameter denial, and final verification.
- Placeholder scan: Every step names exact files, commands, expected failures, expected passes, and commit messages.
- Type consistency: `ROUTE_PARAMETERS`, `AI_CONTEXT_ANCHOR`, `AI_TOOL_STATE_KIND::RoutingTrack`, `AI_SUGGESTION_OPERATION`, and existing helper names match the current source.
- Scope check: Design-rule width inference, route search, visual overlays, and accepted edits remain outside this plan.
