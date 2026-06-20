# AI Anchor Route Preview Tool Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `kisurf_preview_route_to_anchor`, a semantic model tool that resolves two current PCB anchor ids into a native `route_segment_preview` suggestion.

**Architecture:** Extend the existing common semantic tool handler and OpenAI-compatible provider declaration. The handler resolves anchor ids from `AI_CONTEXT_SNAPSHOT::m_Anchors`, creates a synthetic trace preview object, validates the generated operation through `ParseAiSuggestionOperation()`, and stores the suggestion through the existing sink. This slice deliberately stays preview-only and does not add route search or editor mutation.

**Tech Stack:** KiSurf common C++20, `AI_CONTEXT_SNAPSHOT`, `AI_CONTEXT_ANCHOR`, `AI_SEMANTIC_TOOL_CALL_HANDLER`, `AI_OPENAI_COMPAT_PROVIDER`, `AI_SUGGESTION_OPERATION`, Boost unit tests in `qa_common`.

---

## File Structure

- Modify `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`
  - Add anchor request helpers.
  - Add failing route-to-anchor success and denial tests.
- Modify `qa/tests/common/test_ai_provider.cpp`
  - Extend provider tool declaration test to expect and inspect the new tool schema.
- Modify `common/kisurf/ai/ai_semantic_tool_call_handler.cpp`
  - Recognize `kisurf_preview_route_to_anchor`.
  - Validate required arguments.
  - Resolve anchors from the request context snapshot.
  - Create the route segment preview suggestion.
- Modify `common/kisurf/ai/ai_provider.cpp`
  - Add route-to-anchor tool parameters and include the tool declaration.
- Modify `docs/superpowers/plans/2026-06-19-ai-anchor-route-preview-tool-implementation.md`
  - Check off completed steps after each verified commit.

## Task 1: Route-To-Anchor Handler Tests

**Files:**
- Modify: `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`

- [x] **Step 1: Add anchor helpers**

Add these helpers inside the anonymous namespace after `requestWithSelection()`:

```cpp
AI_CONTEXT_ANCHOR positionedAnchor( const wxString& aId, AI_CONTEXT_ANCHOR_KIND aKind,
                                    const VECTOR2I& aPosition )
{
    AI_CONTEXT_ANCHOR anchor;
    anchor.m_Id = aId;
    anchor.m_Kind = aKind;
    anchor.m_EditorKind = AI_EDITOR_KIND::Pcb;
    anchor.m_Label = aId;
    anchor.m_Summary = wxS( "test anchor" );
    anchor.m_Position = aPosition;
    anchor.m_HasPosition = true;
    anchor.m_Layer = 0;
    anchor.m_Confidence = 1.0;
    return anchor;
}


AI_PROVIDER_REQUEST requestWithRouteAnchors()
{
    AI_PROVIDER_REQUEST request = requestWithSelection();
    request.m_ContextSnapshot.m_SelectedObjects.clear();
    request.m_ContextSnapshot.m_Anchors.push_back(
            positionedAnchor( wxS( "pcb.track.start" ),
                              AI_CONTEXT_ANCHOR_KIND::RouteStart,
                              VECTOR2I( 100, 200 ) ) );
    request.m_ContextSnapshot.m_Anchors.push_back(
            positionedAnchor( wxS( "pcb.pad.target" ),
                              AI_CONTEXT_ANCHOR_KIND::RouteTarget,
                              VECTOR2I( 500, 650 ) ) );
    return request;
}
```

- [x] **Step 2: Add failing success test**

Append this test before `SemanticToolsFailClosedWithoutRequiredContext`:

```cpp
BOOST_AUTO_TEST_CASE( RouteToAnchorCreatesRouteSegmentPreviewSuggestion )
{
    std::optional<AI_SUGGESTION_RECORD> captured;

    AI_SEMANTIC_TOOL_CALL_HANDLER handler(
            [&]( AI_SUGGESTION_RECORD aSuggestion )
            {
                aSuggestion.m_Id = 105;
                captured = aSuggestion;
                return captured;
            } );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithRouteAnchors(),
            toolCall( wxS( "kisurf_preview_route_to_anchor" ),
                      wxS( "{\"start_anchor_id\":\"pcb.track.start\","
                           "\"target_anchor_id\":\"pcb.pad.target\","
                           "\"net\":\"/GPIO\",\"layer\":\"F.Cu\","
                           "\"width\":150000}" ) ) );

    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString() );
    BOOST_REQUIRE( captured.has_value() );
    BOOST_CHECK_EQUAL( captured->m_Title,
                       wxString( wxS( "Preview route to anchor" ) ) );
    BOOST_REQUIRE_EQUAL( captured->m_PreviewObjects.size(), 1 );
    BOOST_REQUIRE_EQUAL( captured->m_EditObjects.size(), 1 );
    BOOST_CHECK_EQUAL( captured->m_PreviewObjects.front().m_Type, PCB_TRACE_T );
    BOOST_CHECK_EQUAL( captured->m_PreviewObjects.front().m_Label,
                       wxString( wxS( "preview:route_to_anchor" ) ) );

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

    nlohmann::json resultJson = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( resultJson["status"].get<std::string>(), "preview_ready" );
    BOOST_CHECK_EQUAL( resultJson["suggestion_id"].get<int>(), 105 );
    BOOST_CHECK( resultJson["preview_required"].get<bool>() );
}
```

- [x] **Step 3: Add failing missing-anchor denial test**

Append this test after the success test:

```cpp
BOOST_AUTO_TEST_CASE( RouteToAnchorFailsClosedForMissingAnchor )
{
    bool sinkCalled = false;

    AI_SEMANTIC_TOOL_CALL_HANDLER handler(
            [&]( AI_SUGGESTION_RECORD aSuggestion )
            {
                wxUnusedVar( aSuggestion );
                sinkCalled = true;
                return std::optional<AI_SUGGESTION_RECORD>();
            } );

    AI_PROVIDER_REQUEST request = requestWithRouteAnchors();
    request.m_ContextSnapshot.m_Anchors.pop_back();

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            request,
            toolCall( wxS( "kisurf_preview_route_to_anchor" ),
                      wxS( "{\"start_anchor_id\":\"pcb.track.start\","
                           "\"target_anchor_id\":\"pcb.pad.target\","
                           "\"net\":\"/GPIO\",\"layer\":\"F.Cu\","
                           "\"width\":150000}" ) ) );

    BOOST_CHECK( !result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString( wxS( "missing_anchor" ) ) );
    BOOST_CHECK( !sinkCalled );
}
```

- [x] **Step 4: Add failing anchor-without-position denial test**

Append this test after the missing-anchor test:

```cpp
BOOST_AUTO_TEST_CASE( RouteToAnchorFailsClosedForAnchorWithoutPosition )
{
    bool sinkCalled = false;

    AI_SEMANTIC_TOOL_CALL_HANDLER handler(
            [&]( AI_SUGGESTION_RECORD aSuggestion )
            {
                wxUnusedVar( aSuggestion );
                sinkCalled = true;
                return std::optional<AI_SUGGESTION_RECORD>();
            } );

    AI_PROVIDER_REQUEST request = requestWithRouteAnchors();
    request.m_ContextSnapshot.m_Anchors.back().m_HasPosition = false;

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            request,
            toolCall( wxS( "kisurf_preview_route_to_anchor" ),
                      wxS( "{\"start_anchor_id\":\"pcb.track.start\","
                           "\"target_anchor_id\":\"pcb.pad.target\","
                           "\"net\":\"/GPIO\",\"layer\":\"F.Cu\","
                           "\"width\":150000}" ) ) );

    BOOST_CHECK( !result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode,
                       wxString( wxS( "anchor_without_position" ) ) );
    BOOST_CHECK( !sinkCalled );
}
```

- [x] **Step 5: Run red for handler tests**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\pcbnew;$root\common;$root\common\gal;$root\api;$env:PATH"
& "$root\qa\tests\common\qa_common.exe" --run_test=AiSemanticToolCallHandler --log_level=test_suite
```

Expected: build succeeds, and the new route-to-anchor success test fails with `unknown_tool` because the handler does not support `kisurf_preview_route_to_anchor` yet.

## Task 2: Provider Tool Declaration Test

**Files:**
- Modify: `qa/tests/common/test_ai_provider.cpp`

- [x] **Step 1: Extend provider declaration assertions**

Inside `OpenAiProviderDeclaresKiSurfTools`, change the tool count from 4 to 5 and add these checks after the copper-zone assertions:

```cpp
BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                        "kisurf_preview_route_to_anchor" )
             != toolNames.end() );
const std::string routeRequired =
        toolByName["kisurf_preview_route_to_anchor"]["function"]["parameters"]
                ["required"]
                        .dump();
BOOST_CHECK( routeRequired.find( "start_anchor_id" ) != std::string::npos );
BOOST_CHECK( routeRequired.find( "target_anchor_id" ) != std::string::npos );
BOOST_CHECK( routeRequired.find( "net" ) != std::string::npos );
BOOST_CHECK( routeRequired.find( "layer" ) != std::string::npos );
BOOST_CHECK( routeRequired.find( "width" ) != std::string::npos );
```

- [x] **Step 2: Run red for provider declaration**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\pcbnew;$root\common;$root\common\gal;$root\api;$env:PATH"
& "$root\qa\tests\common\qa_common.exe" --run_test=AiNativeProvider/OpenAiProviderDeclaresKiSurfTools --log_level=test_suite
```

Expected: test fails because only four provider tools are declared.

## Task 3: Semantic Handler Implementation

**Files:**
- Modify: `common/kisurf/ai/ai_semantic_tool_call_handler.cpp`

- [x] **Step 1: Add integer bounds includes**

Add these includes near the existing standard includes:

```cpp
#include <cstdint>
#include <limits>
```

- [x] **Step 2: Recognize the tool**

Update `supportedTool()`:

```cpp
bool supportedTool( const wxString& aToolName )
{
    return aToolName == wxS( "kisurf_preview_move_selected" )
           || aToolName == wxS( "kisurf_preview_create_copper_zone" )
           || aToolName == wxS( "kisurf_preview_route_to_anchor" );
}
```

- [x] **Step 3: Add route-to-anchor helper functions**

Add these helpers after `selectedLabelsFingerprint()`:

```cpp
bool jsonStringField( const nlohmann::json& aArgs, const char* aField, wxString& aOut )
{
    if( !aArgs.contains( aField ) || !aArgs[aField].is_string() )
        return false;

    aOut = wxString::FromUTF8( aArgs[aField].get_ref<const std::string&>().c_str() );
    aOut.Trim( true ).Trim( false );
    return !aOut.IsEmpty();
}


bool jsonPositiveIntegerField( const nlohmann::json& aArgs, const char* aField, int& aOut )
{
    if( !aArgs.contains( aField ) )
        return false;

    if( aArgs[aField].is_number_unsigned() )
    {
        const uint64_t value = aArgs[aField].get<uint64_t>();

        if( value == 0 || value > static_cast<uint64_t>( std::numeric_limits<int>::max() ) )
            return false;

        aOut = static_cast<int>( value );
        return true;
    }

    if( !aArgs[aField].is_number_integer() )
        return false;

    const int64_t value = aArgs[aField].get<int64_t>();

    if( value <= 0 || value > static_cast<int64_t>( std::numeric_limits<int>::max() ) )
        return false;

    aOut = static_cast<int>( value );
    return true;
}


const AI_CONTEXT_ANCHOR* findAnchorById( const AI_CONTEXT_SNAPSHOT& aSnapshot,
                                         const wxString& aAnchorId )
{
    for( const AI_CONTEXT_ANCHOR& anchor : aSnapshot.m_Anchors )
    {
        if( anchor.m_Id == aAnchorId )
            return &anchor;
    }

    return nullptr;
}


nlohmann::json pointJson( const VECTOR2I& aPoint )
{
    return { { "x", aPoint.x }, { "y", aPoint.y } };
}
```

- [x] **Step 4: Add route suggestion builder**

Add this helper after `copperZoneSuggestion()`:

```cpp
AI_SUGGESTION_RECORD routeToAnchorSuggestion( const AI_PROVIDER_REQUEST& aRequest,
                                              const wxString& aOperationJson,
                                              const wxString& aStartAnchorId,
                                              const wxString& aTargetAnchorId )
{
    AI_OBJECT_REF traceRef( KIID(), PCB_TRACE_T, wxS( "preview:route_to_anchor" ),
                            aOperationJson );

    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_EditorKind = AI_EDITOR_KIND::Pcb;
    suggestion.m_Kind = AI_SUGGESTION_KIND::Preview;
    suggestion.m_ContextVersion = effectiveVersion( aRequest );
    suggestion.m_Title = wxS( "Preview route to anchor" );
    suggestion.m_Body = wxS( "Preview this route segment before applying it." );
    suggestion.m_ArgumentsJson = aOperationJson;
    suggestion.m_PreviewObjects.push_back( traceRef );
    suggestion.m_EditObjects.push_back( traceRef );
    suggestion.m_Fingerprint = wxS( "semantic|route_to_anchor|" )
                               + suggestion.m_ContextVersion.AsString()
                               + wxS( "|" ) + aStartAnchorId
                               + wxS( "|" ) + aTargetAnchorId
                               + wxS( "|" ) + aOperationJson;
    return suggestion;
}
```

- [x] **Step 5: Add route operation construction**

Add this helper after `routeToAnchorSuggestion()`:

```cpp
std::optional<AI_SUGGESTION_RECORD> routeToAnchorFromArgs(
        const AI_PROVIDER_REQUEST& aRequest, const nlohmann::json& aArgs,
        nlohmann::json& aOperation, wxString& aErrorCode, wxString& aErrorMessage )
{
    wxString startAnchorId;
    wxString targetAnchorId;
    wxString net;
    wxString layer;
    int      width = 0;

    if( !jsonStringField( aArgs, "start_anchor_id", startAnchorId )
        || !jsonStringField( aArgs, "target_anchor_id", targetAnchorId )
        || !jsonStringField( aArgs, "net", net )
        || !jsonStringField( aArgs, "layer", layer )
        || !jsonPositiveIntegerField( aArgs, "width", width ) )
    {
        aErrorCode = wxS( "malformed_arguments" );
        aErrorMessage = wxS( "route_to_anchor requires start_anchor_id, target_anchor_id, net, layer, and positive width." );
        return std::nullopt;
    }

    const AI_CONTEXT_ANCHOR* start =
            findAnchorById( aRequest.m_ContextSnapshot, startAnchorId );
    const AI_CONTEXT_ANCHOR* target =
            findAnchorById( aRequest.m_ContextSnapshot, targetAnchorId );

    if( !start || !target )
    {
        aErrorCode = wxS( "missing_anchor" );
        aErrorMessage = wxS( "route_to_anchor anchor id was not present in the current context." );
        return std::nullopt;
    }

    if( !start->m_HasPosition || !target->m_HasPosition )
    {
        aErrorCode = wxS( "anchor_without_position" );
        aErrorMessage = wxS( "route_to_anchor anchors must have board positions." );
        return std::nullopt;
    }

    aOperation = { { "operation", "route_segment_preview" },
                   { "net", toUtf8String( net ) },
                   { "layer", toUtf8String( layer ) },
                   { "width", width },
                   { "start", pointJson( start->m_Position ) },
                   { "end", pointJson( target->m_Position ) } };

    const wxString operationJson = fromJson( aOperation );

    if( !ParseAiSuggestionOperation( operationJson ) )
    {
        aErrorCode = wxS( "invalid_operation" );
        aErrorMessage = wxS( "Semantic tool arguments did not form a valid route preview operation." );
        return std::nullopt;
    }

    return routeToAnchorSuggestion( aRequest, operationJson, startAnchorId,
                                    targetAnchorId );
}
```

- [x] **Step 6: Thread precise errors through buildSuggestion**

Change `buildSuggestion()` to accept error output parameters:

```cpp
std::optional<AI_SUGGESTION_RECORD> buildSuggestion( const AI_PROVIDER_REQUEST& aRequest,
                                                     const AI_TOOL_CALL_RECORD& aToolCall,
                                                     const nlohmann::json& aArgs,
                                                     nlohmann::json& aOperation,
                                                     wxString& aErrorCode,
                                                     wxString& aErrorMessage )
```

At the start of the function, add:

```cpp
aErrorCode = wxS( "invalid_operation" );
aErrorMessage = wxS( "Semantic tool arguments did not form a valid preview operation." );
```

Before the final `return std::nullopt;`, add:

```cpp
if( aToolCall.m_ToolName == wxS( "kisurf_preview_route_to_anchor" ) )
{
    return routeToAnchorFromArgs( aRequest, aArgs, aOperation, aErrorCode,
                                  aErrorMessage );
}
```

- [x] **Step 7: Deny route tool outside PCB and use precise build errors**

In `HandleToolCall()`, add this editor check after the copper-zone editor check:

```cpp
if( aToolCall.m_ToolName == wxS( "kisurf_preview_route_to_anchor" )
    && effectiveEditorKind( aRequest ) != AI_EDITOR_KIND::Pcb )
{
    return deniedResult( aRequest, aToolCall, wxS( "editor_not_supported" ),
                         wxS( "route_to_anchor is only available in the PCB editor." ) );
}
```

Then replace the `buildSuggestion()` call and denial block with:

```cpp
nlohmann::json operation;
wxString       buildErrorCode;
wxString       buildErrorMessage;
std::optional<AI_SUGGESTION_RECORD> suggestion =
        buildSuggestion( aRequest, aToolCall, *args, operation, buildErrorCode,
                         buildErrorMessage );

if( !suggestion )
{
    return deniedResult( aRequest, aToolCall, buildErrorCode, buildErrorMessage );
}
```

- [x] **Step 8: Run handler green**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\pcbnew;$root\common;$root\common\gal;$root\api;$env:PATH"
& "$root\qa\tests\common\qa_common.exe" --run_test=AiSemanticToolCallHandler --log_level=test_suite
```

Expected: `AiSemanticToolCallHandler` exits 0.

## Task 4: Provider Tool Declaration Implementation

**Files:**
- Modify: `common/kisurf/ai/ai_provider.cpp`

- [x] **Step 1: Add route-to-anchor parameter schema**

Add this helper after `copperZoneToolParameters()`:

```cpp
nlohmann::json routeToAnchorToolParameters()
{
    return { { "type", "object" },
             { "properties",
               { { "start_anchor_id",
                   { { "type", "string" },
                     { "description", "Current semantic anchor id for the route start." } } },
                 { "target_anchor_id",
                   { { "type", "string" },
                     { "description", "Current semantic anchor id for the route target." } } },
                 { "net", { { "type", "string" }, { "description", "Target net name." } } },
                 { "layer", { { "type", "string" }, { "description", "PCB copper layer name." } } },
                 { "width",
                   { { "type", "integer" },
                     { "minimum", 1 },
                     { "description", "Trace width in board internal units." } } } } },
             { "required",
               nlohmann::json::array(
                       { "start_anchor_id", "target_anchor_id", "net", "layer", "width" } ) },
             { "additionalProperties", false } };
}
```

- [x] **Step 2: Declare the new tool**

Add this function tool after the copper-zone declaration:

```cpp
functionTool( "kisurf_preview_route_to_anchor",
              "Create a preview suggestion for one PCB route segment between two current "
              "semantic anchors. This never edits the board until the user accepts the preview.",
              routeToAnchorToolParameters() )
```

- [x] **Step 3: Run provider green**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\pcbnew;$root\common;$root\common\gal;$root\api;$env:PATH"
& "$root\qa\tests\common\qa_common.exe" --run_test=AiNativeProvider/OpenAiProviderDeclaresKiSurfTools --log_level=test_suite
```

Expected: provider declaration test exits 0.

- [x] **Step 4: Commit implementation**

```bash
git add common/kisurf/ai/ai_semantic_tool_call_handler.cpp common/kisurf/ai/ai_provider.cpp qa/tests/common/test_ai_semantic_tool_call_handler.cpp qa/tests/common/test_ai_provider.cpp
git commit -m "feat: preview routes between ai anchors"
```

## Task 5: Final Verification And Plan Status

**Files:**
- Modify: `docs/superpowers/plans/2026-06-19-ai-anchor-route-preview-tool-implementation.md`

- [x] **Step 1: Run focused common AI tests**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\pcbnew;$root\common;$root\common\gal;$root\api;$env:PATH"
& "$root\qa\tests\common\qa_common.exe" --run_test=AiSemanticToolCallHandler,AiNativeProvider,AiSuggestionOperations --log_level=nothing
```

Expected: all selected common suites exit 0.

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
rg -n "sk-[0-9A-Za-z_-]{20,}|OPENAI_API_KEY\s*=|KISURF_AI_API_KEY\s*=" common qa docs
```

Expected: whitespace check exits 0; secret scan has no matches.

- [x] **Step 4: Update this plan status**

Check off each completed step in this file.

- [x] **Step 5: Commit final plan status**

```bash
git add docs/superpowers/plans/2026-06-19-ai-anchor-route-preview-tool-implementation.md
git commit -m "docs: update anchor route preview tool plan status"
```

## Self-Review

- Spec coverage: Tasks cover tool declaration, anchor resolution, route preview operation creation, denial codes, preview-only safety, and verification.
- Placeholder scan: Every step names exact files, code snippets, commands, expected red or green behavior, and commit messages.
- Type consistency: `kisurf_preview_route_to_anchor`, `start_anchor_id`, `target_anchor_id`, `AI_CONTEXT_ANCHOR`, `AI_CONTEXT_SNAPSHOT::m_Anchors`, `PCB_TRACE_T`, and `route_segment_preview` are consistently named.
- Scope check: Routing inference, 45-degree bend generation, visual overlays, accepted edits, and schematic support remain separate slices.
