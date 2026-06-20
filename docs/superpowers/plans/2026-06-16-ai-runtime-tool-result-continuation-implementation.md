# AI Runtime Tool Result Continuation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add one bounded provider continuation turn so the model can see native tool results and produce a final response.

**Architecture:** Extend `AI_PROVIDER_REQUEST` with handled tool results. The OpenAI-compatible provider serializes those results as assistant `tool_calls` plus `tool` messages, while `AI_RUNTIME` performs exactly one continuation after first-round tool handling and returns the continuation body with the original handled tool records preserved.

**Tech Stack:** C++17, nlohmann JSON, wxString, KiCad common AI runtime/provider, Boost.Test.

---

## File Structure

- Modify: `include/kisurf/ai/ai_types.h`
  - Add continuation tool-result records to `AI_PROVIDER_REQUEST`.
- Modify: `common/kisurf/ai/ai_provider.cpp`
  - Add OpenAI-compatible continuation message construction.
- Modify: `common/kisurf/ai/ai_runtime.cpp`
  - Add one bounded continuation after first-round tool calls are handled.
- Modify: `qa/tests/common/test_ai_provider.cpp`
  - Add provider JSON test for continuation messages.
- Modify: `qa/tests/common/test_ai_runtime.cpp`
  - Add runtime test for tool result continuation.
- Modify: `docs/superpowers/specs/2026-06-16-kisurf-ai-native-spec-index.md`
  - Register the spec and implementation phase.

## Verification Commands

Common targeted verification:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_common -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiNativeProvider,AiNativeRuntime,AiAgentPanelModel --log_level=test_suite
```

The known missing schema warning is acceptable only when Boost reports no errors.

## Task 1: Provider Continuation Request Test

**Files:**
- Modify: `qa/tests/common/test_ai_provider.cpp`

- [ ] **Step 1: Write the failing provider test**

Add this test after `OpenAiProviderParsesFunctionToolCalls`:

```cpp
BOOST_AUTO_TEST_CASE( OpenAiProviderSendsToolResultContinuationMessages )
{
    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxS( "https://sub2api.wenming-dev.org/v1" );
    settings.m_ApiKey = wxS( "unit-test-key" );
    settings.m_Model = wxS( "unit-model" );

    AI_OPENAI_COMPAT_PROVIDER provider(
            settings,
            []( const AI_HTTP_REQUEST& aRequest, AI_HTTP_RESPONSE& aResponse, wxString& aError )
            {
                wxUnusedVar( aError );

                nlohmann::json body = nlohmann::json::parse( aRequest.m_Body.ToStdString() );
                const nlohmann::json& messages = body["messages"];

                BOOST_REQUIRE_EQUAL( messages.size(), 4 );
                BOOST_CHECK_EQUAL( messages.at( 0 )["role"].get<std::string>(), "system" );
                BOOST_CHECK_EQUAL( messages.at( 1 )["role"].get<std::string>(), "user" );
                BOOST_CHECK_EQUAL( messages.at( 2 )["role"].get<std::string>(), "assistant" );
                BOOST_CHECK( messages.at( 2 )["content"].is_null() );

                const nlohmann::json& toolCall = messages.at( 2 )["tool_calls"].at( 0 );
                BOOST_CHECK_EQUAL( toolCall["id"].get<std::string>(), "call_456" );
                BOOST_CHECK_EQUAL( toolCall["type"].get<std::string>(), "function" );
                BOOST_CHECK_EQUAL( toolCall["function"]["name"].get<std::string>(),
                                   "kisurf_check_action" );
                BOOST_CHECK_EQUAL( toolCall["function"]["arguments"].get<std::string>(),
                                   "{\"action\":\"common.Control.showAgentPanel\"}" );

                BOOST_CHECK_EQUAL( messages.at( 3 )["role"].get<std::string>(), "tool" );
                BOOST_CHECK_EQUAL( messages.at( 3 )["tool_call_id"].get<std::string>(),
                                   "call_456" );
                BOOST_CHECK_EQUAL( messages.at( 3 )["content"].get<std::string>(),
                                   "{\"allowed\":true,\"executed\":false}" );

                aResponse.m_StatusCode = 200;
                aResponse.m_Body = wxS( "{\"choices\":[{\"message\":{\"content\":\"checked\"}}]}" );
                return true;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 23;
    request.m_UserText = wxS( "check action" );

    AI_TOOL_CALL_RECORD result;
    result.m_RequestId = 23;
    result.m_ToolCallId = wxS( "call_456" );
    result.m_ToolName = wxS( "kisurf_check_action" );
    result.m_ArgumentsJson = wxS( "{\"action\":\"common.Control.showAgentPanel\"}" );
    result.m_ResultJson = wxS( "{\"allowed\":true,\"executed\":false}" );
    request.m_ToolResults.push_back( result );

    AI_PROVIDER_RESPONSE response = provider.Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 23 );
    BOOST_CHECK_EQUAL( response.m_Body, wxString( wxS( "checked" ) ) );
}
```

- [ ] **Step 2: Run RED verification**

Run the common targeted verification command.

Expected: compile fails because `AI_PROVIDER_REQUEST` has no `m_ToolResults`.

## Task 2: Provider Continuation Messages

**Files:**
- Modify: `include/kisurf/ai/ai_types.h`
- Modify: `common/kisurf/ai/ai_provider.cpp`

- [ ] **Step 1: Add the request field**

In `AI_PROVIDER_REQUEST`, add:

```cpp
std::vector<AI_TOOL_CALL_RECORD> m_ToolResults;
```

- [ ] **Step 2: Add provider helpers**

Add helpers to the anonymous namespace in `common/kisurf/ai/ai_provider.cpp`:

```cpp
std::string toolResultContent( const AI_TOOL_CALL_RECORD& aToolCall )
{
    if( !aToolCall.m_ResultJson.IsEmpty() )
        return toUtf8String( aToolCall.m_ResultJson );

    nlohmann::json result = {
        { "allowed", aToolCall.m_Allowed },
        { "executed", aToolCall.m_Executed },
        { "error_code", toUtf8String( aToolCall.m_ErrorCode ) },
        { "message", toUtf8String( aToolCall.m_Message ) } };

    return result.dump();
}

void appendToolResultMessages( nlohmann::json& aMessages,
                               const std::vector<AI_TOOL_CALL_RECORD>& aToolResults )
{
    if( aToolResults.empty() )
        return;

    nlohmann::json toolCalls = nlohmann::json::array();

    for( const AI_TOOL_CALL_RECORD& toolResult : aToolResults )
    {
        std::string arguments = toUtf8String( toolResult.m_ArgumentsJson );

        if( arguments.empty() )
            arguments = "{}";

        toolCalls.push_back( {
                { "id", toUtf8String( toolResult.m_ToolCallId ) },
                { "type", "function" },
                { "function",
                  { { "name", toUtf8String( toolResult.m_ToolName ) },
                    { "arguments", arguments } } } } );
    }

    aMessages.push_back( { { "role", "assistant" },
                           { "content", nullptr },
                           { "tool_calls", toolCalls } } );

    for( const AI_TOOL_CALL_RECORD& toolResult : aToolResults )
    {
        aMessages.push_back( { { "role", "tool" },
                               { "tool_call_id", toUtf8String( toolResult.m_ToolCallId ) },
                               { "content", toolResultContent( toolResult ) } } );
    }
}
```

- [ ] **Step 3: Append continuation messages**

Replace the inline `body["messages"] = ...` assignment with a local `messages`
array, then call:

```cpp
appendToolResultMessages( messages, aRequest.m_ToolResults );
body["messages"] = std::move( messages );
```

- [ ] **Step 4: Run provider GREEN verification**

Run the common targeted verification command.

Expected: provider continuation test passes; runtime continuation test is not
added yet.

## Task 3: Runtime Continuation Test

**Files:**
- Modify: `qa/tests/common/test_ai_runtime.cpp`

- [ ] **Step 1: Add a continuation-aware provider fixture**

Add this class in the anonymous namespace:

```cpp
class CONTINUATION_TOOL_CALL_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_LastRequest = aRequest;

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Kind = AI_SUGGESTION_KIND::Chat;
        response.m_Title = wxS( "Continuation Provider" );

        if( !aRequest.m_ToolResults.empty() )
        {
            response.m_Body = wxS( "Tool result received." );
            return response;
        }

        response.m_Body = wxS( "Tool call requested." );

        AI_TOOL_CALL_RECORD call;
        call.m_RequestId = aRequest.m_RequestId;
        call.m_ToolCallId = wxS( "call_runtime" );
        call.m_ToolName = wxS( "kisurf_run_action" );
        call.m_ArgumentsJson =
                wxS( "{\"action\":\"pcbnew.InteractiveSelectionTool.selectionClear\"}" );
        response.m_ToolCalls.push_back( call );

        return response;
    }

    int                 m_CallCount = 0;
    AI_PROVIDER_REQUEST m_LastRequest;
};
```

- [ ] **Step 2: Add the failing runtime test**

Add this test before `RuntimeCopiesHandlerResultToToolCallRecord`:

```cpp
BOOST_AUTO_TEST_CASE( RuntimeContinuesAfterHandledToolResults )
{
    auto* provider = new CONTINUATION_TOOL_CALL_PROVIDER();
    AI_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ) };
    FAKE_TOOL_CALL_HANDLER handler;
    runtime.SetToolCallHandler( &handler );

    AI_PROVIDER_REQUEST request;
    request.m_UserText = wxS( "use a tool" );

    AI_PROVIDER_RESPONSE response = runtime.Submit( request );

    BOOST_CHECK_EQUAL( provider->m_CallCount, 2 );
    BOOST_CHECK_EQUAL( handler.m_CallCount, 1 );
    BOOST_CHECK_EQUAL( response.m_Body, wxString( wxS( "Tool result received." ) ) );
    BOOST_REQUIRE_EQUAL( response.m_ToolCalls.size(), 1 );
    BOOST_CHECK( response.m_ToolCalls.front().m_Executed );
    BOOST_CHECK_EQUAL( response.m_ToolCalls.front().m_ResultJson,
                       wxString( wxS( "{\"status\":\"executed\"}" ) ) );
    BOOST_REQUIRE_EQUAL( provider->m_LastRequest.m_ToolResults.size(), 1 );
    BOOST_CHECK_EQUAL( provider->m_LastRequest.m_ToolResults.front().m_ToolCallId,
                       wxString( wxS( "call_runtime" ) ) );
    BOOST_REQUIRE_EQUAL( runtime.TraceRecords().size(), 1 );
    BOOST_CHECK_EQUAL( runtime.TraceRecords().front().m_Response.m_Body,
                       wxString( wxS( "Tool result received." ) ) );
    BOOST_REQUIRE_EQUAL( runtime.TraceRecords().front().m_Response.m_ToolCalls.size(), 1 );
}
```

- [ ] **Step 3: Run RED verification**

Run the common targeted verification command.

Expected: runtime test fails because provider call count is still one and the
body remains `Tool call requested.`

## Task 4: Runtime One-Turn Continuation

**Files:**
- Modify: `common/kisurf/ai/ai_runtime.cpp`

- [ ] **Step 1: Add bounded continuation**

After first-round tool calls have been handled, add:

```cpp
if( !response.m_ToolCalls.empty() )
{
    std::vector<AI_TOOL_CALL_RECORD> handledToolCalls = response.m_ToolCalls;

    AI_PROVIDER_REQUEST continuationRequest = aRequest;
    continuationRequest.m_ToolResults = handledToolCalls;

    AI_PROVIDER_RESPONSE continuationResponse = m_Provider->Generate( continuationRequest );
    continuationResponse.m_RequestId = aRequest.m_RequestId;
    continuationResponse.m_ToolCalls = std::move( handledToolCalls );
    response = std::move( continuationResponse );
}
```

This block must run after the handler/no-handler loop and before the trace record
is created.

- [ ] **Step 2: Run runtime/provider GREEN verification**

Run the common targeted verification command.

Expected: provider, runtime, and agent panel model targeted suites pass.

## Task 5: Spec Index, Hygiene, And Commit

**Files:**
- Modify: `docs/superpowers/specs/2026-06-16-kisurf-ai-native-spec-index.md`

- [ ] **Step 1: Update spec index**

Add:

```markdown
20. [AI Runtime Tool Result Continuation](./2026-06-16-ai-runtime-tool-result-continuation-design.md)
   - Defines one bounded provider continuation turn after native tool handling.
```

Add implementation order:

```markdown
24. Phase 17 runtime tool-result continuation that returns model-visible final text after native tool handling.
```

- [ ] **Step 2: Run diff hygiene checks**

Run:

```powershell
git diff --check
git status --short
git diff --stat
```

Expected: no whitespace errors and only files from this plan changed.

- [ ] **Step 3: Commit**

```powershell
git add include/kisurf/ai/ai_types.h common/kisurf/ai/ai_provider.cpp common/kisurf/ai/ai_runtime.cpp qa/tests/common/test_ai_provider.cpp qa/tests/common/test_ai_runtime.cpp docs/superpowers/specs/2026-06-16-ai-runtime-tool-result-continuation-design.md docs/superpowers/specs/2026-06-16-kisurf-ai-native-spec-index.md docs/superpowers/plans/2026-06-16-ai-runtime-tool-result-continuation-implementation.md
git commit -m "feat: continue ai tool results"
```

## Plan Self-Review

- Spec coverage: tasks cover request contract, provider messages, runtime
  continuation, tests, index update, and verification.
- Open-marker scan: no unresolved placeholders remain.
- Type consistency: field names, helper names, and test fixture names match the
  planned C++ code.
