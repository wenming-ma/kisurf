# AI Runtime Tool Call Loop Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Parse OpenAI-compatible model tool calls, preserve them in provider/runtime traces, and optionally route them through a policy-gated runtime handler.

**Architecture:** Provider code remains editor-independent and only parses raw tool-call records. Runtime owns activity recording and an optional non-owning handler interface. Editor-specific action resolution stays outside this phase.

**Tech Stack:** C++17, wxWidgets strings, nlohmann/json, Boost.Test, existing `qa_common` target.

---

## File Structure

- Modify: `include/kisurf/ai/ai_types.h`
  - Extend `AI_TOOL_CALL_RECORD`.
  - Add `std::vector<AI_TOOL_CALL_RECORD> m_ToolCalls` to `AI_PROVIDER_RESPONSE`.
- Modify: `common/kisurf/ai/ai_provider.cpp`
  - Add minimal `tools` and `parallel_tool_calls:false` to chat-completions requests.
  - Parse `choices[0].message.tool_calls[]`.
- Modify: `include/kisurf/ai/ai_runtime.h`
  - Add `AI_TOOL_CALL_HANDLER`.
  - Add runtime handler setter and activity-record accessor.
- Modify: `common/kisurf/ai/ai_runtime.cpp`
  - Record model tool requests.
  - Call the optional handler and copy result fields back to response records.
- Modify: `qa/tests/common/test_ai_provider.cpp`
  - Add provider request/tool-call parsing tests.
- Modify: `qa/tests/common/test_ai_runtime.cpp`
  - Add runtime trace/activity/handler tests.

## Verification Command Template

Use the Visual Studio developer environment for all build and test commands on
this Windows workspace:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_common -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiNativeProvider,AiNativeRuntime --log_level=test_suite"
```

Expected final result: exit code `0`, targeted suites run, and Boost reports no
errors. A schema warning about `qa/tests/schemas/api.v1.schema.json` is known and
acceptable when the exit code is `0`.

### Task 1: Provider Type Tests

**Files:**
- Modify: `qa/tests/common/test_ai_provider.cpp`
- Modify: `include/kisurf/ai/ai_types.h`

- [ ] **Step 1: Write failing provider tests**

Add these includes near the top of `qa/tests/common/test_ai_provider.cpp`:

```cpp
#include <nlohmann/json.hpp>
```

Add these tests before `BOOST_AUTO_TEST_SUITE_END()`:

```cpp
BOOST_AUTO_TEST_CASE( OpenAiProviderDeclaresSingleKiSurfTool )
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

                BOOST_REQUIRE( body.contains( "tools" ) );
                BOOST_REQUIRE( body["tools"].is_array() );
                BOOST_REQUIRE_EQUAL( body["tools"].size(), 1 );
                BOOST_CHECK_EQUAL( body["tools"].front()["type"].get<std::string>(), "function" );
                BOOST_CHECK_EQUAL( body["tools"].front()["function"]["name"].get<std::string>(),
                                   "kisurf_run_action" );
                BOOST_REQUIRE( body.contains( "parallel_tool_calls" ) );
                BOOST_CHECK( !body["parallel_tool_calls"].get<bool>() );

                aResponse.m_StatusCode = 200;
                aResponse.m_Body = wxS( "{\"choices\":[{\"message\":{\"content\":\"ready\"}}]}" );
                return true;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 21;
    request.m_UserText = wxS( "inspect" );

    AI_PROVIDER_RESPONSE response = provider.Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 21 );
    BOOST_CHECK_EQUAL( response.m_ToolCalls.size(), 0 );
}


BOOST_AUTO_TEST_CASE( OpenAiProviderParsesFunctionToolCalls )
{
    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxS( "https://sub2api.wenming-dev.org/v1" );
    settings.m_ApiKey = wxS( "unit-test-key" );
    settings.m_Model = wxS( "unit-model" );

    AI_OPENAI_COMPAT_PROVIDER provider(
            settings,
            []( const AI_HTTP_REQUEST&, AI_HTTP_RESPONSE& aResponse, wxString& aError )
            {
                wxUnusedVar( aError );

                aResponse.m_StatusCode = 200;
                aResponse.m_Body =
                        wxS( "{\"choices\":[{\"message\":{\"content\":null,"
                             "\"tool_calls\":[{\"id\":\"call_123\",\"type\":\"function\","
                             "\"function\":{\"name\":\"kisurf_run_action\","
                             "\"arguments\":\"{\\\"action\\\":\\\"pcbnew.InteractiveSelectionTool.selectionClear\\\"}\"}}]}}]}" );
                return true;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 22;
    request.m_UserText = wxS( "clear selection" );

    AI_PROVIDER_RESPONSE response = provider.Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 22 );
    BOOST_CHECK_EQUAL( response.m_Body, wxString( wxS( "Tool call requested." ) ) );
    BOOST_REQUIRE_EQUAL( response.m_ToolCalls.size(), 1 );
    BOOST_CHECK_EQUAL( response.m_ToolCalls.front().m_RequestId, 22 );
    BOOST_CHECK_EQUAL( response.m_ToolCalls.front().m_ToolCallId,
                       wxString( wxS( "call_123" ) ) );
    BOOST_CHECK_EQUAL( response.m_ToolCalls.front().m_ToolName,
                       wxString( wxS( "kisurf_run_action" ) ) );
    BOOST_CHECK( response.m_ToolCalls.front().m_ArgumentsJson.Contains(
            wxS( "pcbnew.InteractiveSelectionTool.selectionClear" ) ) );
}


BOOST_AUTO_TEST_CASE( OpenAiProviderRejectsMalformedToolCalls )
{
    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxS( "https://sub2api.wenming-dev.org/v1" );
    settings.m_ApiKey = wxS( "unit-test-key" );
    settings.m_Model = wxS( "unit-model" );

    AI_OPENAI_COMPAT_PROVIDER provider(
            settings,
            []( const AI_HTTP_REQUEST&, AI_HTTP_RESPONSE& aResponse, wxString& aError )
            {
                wxUnusedVar( aError );

                aResponse.m_StatusCode = 200;
                aResponse.m_Body =
                        wxS( "{\"choices\":[{\"message\":{\"content\":null,"
                             "\"tool_calls\":[{\"id\":\"call_bad\",\"type\":\"function\","
                             "\"function\":{\"arguments\":\"{}\"}}]}}]}" );
                return true;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 23;
    request.m_UserText = wxS( "bad call" );

    AI_PROVIDER_RESPONSE response = provider.Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 23 );
    BOOST_CHECK( response.m_Body.Contains( wxS( "malformed tool call" ) ) );
    BOOST_CHECK_EQUAL( response.m_ToolCalls.size(), 0 );
}
```

- [ ] **Step 2: Run tests to verify RED**

Run the verification command with `--run_test=AiNativeProvider`.

Expected: build fails because `AI_PROVIDER_RESPONSE` has no `m_ToolCalls` member.

- [ ] **Step 3: Add minimal data fields**

In `include/kisurf/ai/ai_types.h`, change `AI_PROVIDER_RESPONSE` and
`AI_TOOL_CALL_RECORD` to:

```cpp
struct KICOMMON_API AI_PROVIDER_RESPONSE
{
    uint64_t                         m_RequestId = 0;
    AI_SUGGESTION_KIND               m_Kind = AI_SUGGESTION_KIND::Chat;
    wxString                         m_Title;
    wxString                         m_Body;
    std::vector<AI_TOOL_CALL_RECORD> m_ToolCalls;
};
```

Because `AI_TOOL_CALL_RECORD` is used by `AI_PROVIDER_RESPONSE`, move the
`AI_TOOL_CALL_RECORD` definition above `AI_PROVIDER_RESPONSE` and make it:

```cpp
struct KICOMMON_API AI_TOOL_CALL_RECORD
{
    uint64_t m_RequestId = 0;
    wxString m_ToolCallId;
    wxString m_ToolName;
    wxString m_ArgumentsJson;
    wxString m_ResultJson;
    bool     m_Allowed = false;
    bool     m_Executed = false;
    wxString m_ErrorCode;
    wxString m_Message;
};
```

- [ ] **Step 4: Run tests to verify remaining RED**

Run the verification command with `--run_test=AiNativeProvider`.

Expected: build succeeds and tests fail because the provider does not declare
tools or parse `tool_calls`.

### Task 2: Provider Request And Tool-Call Parser

**Files:**
- Modify: `common/kisurf/ai/ai_provider.cpp`
- Test: `qa/tests/common/test_ai_provider.cpp`

- [ ] **Step 1: Add provider request tools**

In `AI_OPENAI_COMPAT_PROVIDER::Generate(...)`, after `body["messages"] = ...`,
add:

```cpp
    body["tools"] = nlohmann::json::array(
            { { { "type", "function" },
                { "function",
                  { { "name", "kisurf_run_action" },
                    { "description",
                      "Request a KiSurf editor action by native action name. Local KiSurf "
                      "policy decides whether the action can run." },
                    { "parameters",
                      { { "type", "object" },
                        { "properties",
                          { { "action",
                              { { "type", "string" },
                                { "description",
                                  "Native KiCad/KiSurf action name from the current action catalog." } } },
                            { "arguments",
                              { { "type", "object" },
                                { "description", "Optional action-specific arguments." },
                                { "additionalProperties", true } } },
                            { "dry_run",
                              { { "type", "boolean" },
                                { "description",
                                  "When true, check policy and preview feasibility without executing." } } } } },
                        { "required", nlohmann::json::array( { "action" } ) },
                        { "additionalProperties", false } } } } } } } );
    body["parallel_tool_calls"] = false;
```

- [ ] **Step 2: Add parser helpers**

In the anonymous namespace in `common/kisurf/ai/ai_provider.cpp`, add:

```cpp
bool parseFunctionToolCall( const nlohmann::json& aToolCall, uint64_t aRequestId,
                            AI_TOOL_CALL_RECORD& aRecord, wxString& aError )
{
    if( aToolCall.contains( "type" ) && aToolCall["type"].is_string()
        && aToolCall["type"].get<std::string>() != "function" )
    {
        return false;
    }

    if( !aToolCall.contains( "id" ) || !aToolCall["id"].is_string()
        || !aToolCall.contains( "function" ) || !aToolCall["function"].is_object() )
    {
        aError = wxS( "AI provider returned a malformed tool call." );
        return false;
    }

    const nlohmann::json& function = aToolCall["function"];

    if( !function.contains( "name" ) || !function["name"].is_string()
        || !function.contains( "arguments" ) )
    {
        aError = wxS( "AI provider returned a malformed tool call." );
        return false;
    }

    aRecord.m_RequestId = aRequestId;
    aRecord.m_ToolCallId =
            wxString::FromUTF8( aToolCall["id"].get_ref<const std::string&>().c_str() );
    aRecord.m_ToolName =
            wxString::FromUTF8( function["name"].get_ref<const std::string&>().c_str() );

    if( function["arguments"].is_string() )
    {
        aRecord.m_ArgumentsJson = wxString::FromUTF8(
                function["arguments"].get_ref<const std::string&>().c_str() );
    }
    else if( function["arguments"].is_object() )
    {
        aRecord.m_ArgumentsJson = wxString::FromUTF8( function["arguments"].dump().c_str() );
    }
    else
    {
        aError = wxS( "AI provider returned a malformed tool call." );
        return false;
    }

    return true;
}


bool parseToolCalls( const nlohmann::json& aMessage, uint64_t aRequestId,
                     std::vector<AI_TOOL_CALL_RECORD>& aToolCalls, wxString& aError )
{
    if( !aMessage.contains( "tool_calls" ) || aMessage["tool_calls"].is_null() )
        return true;

    if( !aMessage["tool_calls"].is_array() )
    {
        aError = wxS( "AI provider returned malformed tool calls." );
        return false;
    }

    for( const nlohmann::json& toolCallJson : aMessage["tool_calls"] )
    {
        AI_TOOL_CALL_RECORD record;
        wxString            localError;

        if( !parseFunctionToolCall( toolCallJson, aRequestId, record, localError ) )
        {
            if( !localError.IsEmpty() )
            {
                aError = localError;
                return false;
            }

            continue;
        }

        aToolCalls.push_back( record );
    }

    return true;
}
```

- [ ] **Step 3: Use parser in response handling**

Replace the current `content` extraction block with code that stores
`std::vector<AI_TOOL_CALL_RECORD> toolCalls`, parses `message.content` when it is
a string, calls `parseToolCalls(...)`, and accepts tool-call-only responses:

```cpp
        wxString                         content;
        std::vector<AI_TOOL_CALL_RECORD> toolCalls;

        if( parsed.contains( "choices" ) && parsed["choices"].is_array()
            && !parsed["choices"].empty() )
        {
            const nlohmann::json& first = parsed["choices"].front();

            if( first.contains( "message" ) && first["message"].is_object() )
            {
                const nlohmann::json& message = first["message"];

                if( message.contains( "content" ) && message["content"].is_string() )
                {
                    content = wxString::FromUTF8(
                            message["content"].get_ref<const std::string&>().c_str() );
                }

                wxString toolError;

                if( !parseToolCalls( message, aRequest.m_RequestId, toolCalls, toolError ) )
                    return providerError( aRequest, toolError );
            }
        }

        if( content.IsEmpty() && toolCalls.empty() )
            return providerError( aRequest, wxS( "AI provider returned no message content." ) );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Kind = AI_SUGGESTION_KIND::Chat;
        response.m_Title = wxS( "AI Provider" );
        response.m_Body = content.IsEmpty() ? wxString( wxS( "Tool call requested." ) ) : content;
        response.m_ToolCalls = std::move( toolCalls );
        return response;
```

- [ ] **Step 4: Run provider tests to verify GREEN**

Run the verification command with `--run_test=AiNativeProvider`.

Expected: provider suite passes.

### Task 3: Runtime Handler Tests

**Files:**
- Modify: `qa/tests/common/test_ai_runtime.cpp`
- Modify: `include/kisurf/ai/ai_runtime.h`
- Modify: `common/kisurf/ai/ai_runtime.cpp`

- [ ] **Step 1: Write failing runtime tests**

Add these helper classes after the includes in `qa/tests/common/test_ai_runtime.cpp`:

```cpp
namespace
{
class TOOL_CALL_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Kind = AI_SUGGESTION_KIND::Chat;
        response.m_Title = wxS( "Tool Provider" );
        response.m_Body = wxS( "Tool call requested." );

        AI_TOOL_CALL_RECORD call;
        call.m_RequestId = aRequest.m_RequestId;
        call.m_ToolCallId = wxS( "call_runtime" );
        call.m_ToolName = wxS( "kisurf_run_action" );
        call.m_ArgumentsJson = wxS( "{\"action\":\"pcbnew.InteractiveSelectionTool.selectionClear\"}" );
        response.m_ToolCalls.push_back( call );

        return response;
    }
};

class FAKE_TOOL_CALL_HANDLER : public AI_TOOL_CALL_HANDLER
{
public:
    AI_TOOL_INVOCATION_RESULT HandleToolCall( const AI_PROVIDER_REQUEST& aRequest,
                                              const AI_TOOL_CALL_RECORD& aToolCall ) override
    {
        ++m_CallCount;
        m_LastRequestId = aRequest.m_RequestId;
        m_LastToolCallId = aToolCall.m_ToolCallId;

        AI_TOOL_INVOCATION_RESULT result;
        result.m_RequestId = aRequest.m_RequestId;
        result.m_ToolCallId = aToolCall.m_ToolCallId;
        result.m_ActionName = wxS( "pcbnew.InteractiveSelectionTool.selectionClear" );
        result.m_Allowed = true;
        result.m_Executed = true;
        result.m_Message = wxS( "fake executed" );
        result.m_ResultJson = wxS( "{\"status\":\"executed\"}" );
        return result;
    }

    int      m_CallCount = 0;
    uint64_t m_LastRequestId = 0;
    wxString m_LastToolCallId;
};
} // namespace
```

Add these tests before `BOOST_AUTO_TEST_SUITE_END()`:

```cpp
BOOST_AUTO_TEST_CASE( RuntimeStoresProviderToolCallsInTrace )
{
    AI_RUNTIME runtime( std::make_unique<TOOL_CALL_PROVIDER>() );

    AI_PROVIDER_REQUEST request;
    request.m_UserText = wxS( "use a tool" );

    AI_PROVIDER_RESPONSE response = runtime.Submit( request );

    BOOST_REQUIRE_EQUAL( response.m_ToolCalls.size(), 1 );
    BOOST_CHECK_EQUAL( runtime.TraceRecords().size(), 1 );
    BOOST_REQUIRE_EQUAL( runtime.TraceRecords().front().m_Response.m_ToolCalls.size(), 1 );
    BOOST_CHECK_EQUAL( runtime.TraceRecords().front().m_Response.m_ToolCalls.front().m_ToolCallId,
                       wxString( wxS( "call_runtime" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeRecordsToolCallActivityWithoutHandler )
{
    AI_RUNTIME runtime( std::make_unique<TOOL_CALL_PROVIDER>() );

    AI_PROVIDER_REQUEST request;
    request.m_UserText = wxS( "use a tool" );

    AI_PROVIDER_RESPONSE response = runtime.Submit( request );
    std::vector<AI_ACTIVITY_RECORD> records = runtime.ActivityRecords();

    BOOST_REQUIRE_EQUAL( response.m_ToolCalls.size(), 1 );
    BOOST_CHECK( !response.m_ToolCalls.front().m_Allowed );
    BOOST_CHECK( !response.m_ToolCalls.front().m_Executed );
    BOOST_CHECK( response.m_ToolCalls.front().m_Message.Contains( wxS( "No tool handler" ) ) );
    BOOST_REQUIRE_EQUAL( records.size(), 1 );
    BOOST_CHECK( records.front().m_Kind == AI_ACTIVITY_KIND::ModelToolRequest );
    BOOST_CHECK_EQUAL( records.front().m_ToolCallId, wxString( wxS( "call_runtime" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeCopiesHandlerResultToToolCallRecord )
{
    AI_RUNTIME             runtime( std::make_unique<TOOL_CALL_PROVIDER>() );
    FAKE_TOOL_CALL_HANDLER handler;
    runtime.SetToolCallHandler( &handler );

    AI_PROVIDER_REQUEST request;
    request.m_UserText = wxS( "use a tool" );

    AI_PROVIDER_RESPONSE response = runtime.Submit( request );
    std::vector<AI_ACTIVITY_RECORD> records = runtime.ActivityRecords();

    BOOST_CHECK_EQUAL( handler.m_CallCount, 1 );
    BOOST_CHECK_EQUAL( handler.m_LastRequestId, response.m_RequestId );
    BOOST_CHECK_EQUAL( handler.m_LastToolCallId, wxString( wxS( "call_runtime" ) ) );
    BOOST_REQUIRE_EQUAL( response.m_ToolCalls.size(), 1 );
    BOOST_CHECK( response.m_ToolCalls.front().m_Allowed );
    BOOST_CHECK( response.m_ToolCalls.front().m_Executed );
    BOOST_CHECK_EQUAL( response.m_ToolCalls.front().m_ResultJson,
                       wxString( wxS( "{\"status\":\"executed\"}" ) ) );
    BOOST_REQUIRE_EQUAL( records.size(), 2 );
    BOOST_CHECK( records.front().m_Kind == AI_ACTIVITY_KIND::ModelToolRequest );
    BOOST_CHECK( records.back().m_Kind == AI_ACTIVITY_KIND::ToolResult );
    BOOST_CHECK( records.back().m_Executed );
}
```

- [ ] **Step 2: Run runtime tests to verify RED**

Run the verification command with `--run_test=AiNativeRuntime`.

Expected: build fails because `AI_TOOL_CALL_HANDLER`, `SetToolCallHandler`, and
`ActivityRecords` do not exist.

### Task 4: Runtime Handler Implementation

**Files:**
- Modify: `include/kisurf/ai/ai_runtime.h`
- Modify: `common/kisurf/ai/ai_runtime.cpp`
- Test: `qa/tests/common/test_ai_runtime.cpp`

- [ ] **Step 1: Extend runtime header**

Change `include/kisurf/ai/ai_runtime.h` to include the activity log and define
the handler:

```cpp
#include <kisurf/ai/ai_activity_log.h>
```

Add before `class AI_RUNTIME`:

```cpp
class KICOMMON_API AI_TOOL_CALL_HANDLER
{
public:
    virtual ~AI_TOOL_CALL_HANDLER() = default;

    virtual AI_TOOL_INVOCATION_RESULT HandleToolCall(
            const AI_PROVIDER_REQUEST& aRequest,
            const AI_TOOL_CALL_RECORD& aToolCall ) = 0;
};
```

Add public methods:

```cpp
    void SetToolCallHandler( AI_TOOL_CALL_HANDLER* aHandler );
    std::vector<AI_ACTIVITY_RECORD> ActivityRecords() const;
```

Add private fields:

```cpp
    AI_ACTIVITY_LOG           m_ActivityLog;
    AI_TOOL_CALL_HANDLER*     m_ToolCallHandler = nullptr;
```

- [ ] **Step 2: Implement runtime tool-call processing**

In `common/kisurf/ai/ai_runtime.cpp`, add anonymous-namespace helpers:

```cpp
namespace
{
void recordModelToolCall( AI_ACTIVITY_LOG& aActivityLog, const AI_PROVIDER_REQUEST& aRequest,
                          const AI_TOOL_CALL_RECORD& aToolCall )
{
    AI_ACTIVITY_RECORD record;
    record.m_RequestId = aRequest.m_RequestId;
    record.m_ToolCallId = aToolCall.m_ToolCallId;
    record.m_Kind = AI_ACTIVITY_KIND::ModelToolRequest;
    record.m_EditorKind = aRequest.m_EditorKind;
    record.m_ActionName = aToolCall.m_ToolName;
    record.m_ArgumentsJson = aToolCall.m_ArgumentsJson;
    record.m_Message = wxS( "Provider tool call requested." );
    aActivityLog.Append( record );
}


void copyToolResult( AI_TOOL_CALL_RECORD& aToolCall,
                     const AI_TOOL_INVOCATION_RESULT& aResult )
{
    aToolCall.m_Allowed = aResult.m_Allowed;
    aToolCall.m_Executed = aResult.m_Executed;
    aToolCall.m_ErrorCode = aResult.m_ErrorCode;
    aToolCall.m_Message = aResult.m_Message;
    aToolCall.m_ResultJson = aResult.m_ResultJson;
}


void recordToolResult( AI_ACTIVITY_LOG& aActivityLog,
                       const AI_TOOL_INVOCATION_RESULT& aResult )
{
    AI_ACTIVITY_RECORD record;
    record.m_RequestId = aResult.m_RequestId;
    record.m_ToolCallId = aResult.m_ToolCallId;
    record.m_Kind = AI_ACTIVITY_KIND::ToolResult;
    record.m_ActionName = aResult.m_ActionName;
    record.m_ResultJson = aResult.m_ResultJson;
    record.m_Allowed = aResult.m_Allowed;
    record.m_Executed = aResult.m_Executed;
    record.m_Message = aResult.m_Message;
    aActivityLog.Append( record );
}
} // namespace
```

Initialize `m_ActivityLog` in the constructor:

```cpp
        m_NextRequestId( 1 ),
        m_ActivityLog( 256 )
```

In `Submit(...)`, after the provider returns and before building
`AI_TRACE_RECORD`, add:

```cpp
    AI_TOOL_CALL_HANDLER* handler = nullptr;

    {
        std::lock_guard<std::mutex> lock( m_Mutex );
        handler = m_ToolCallHandler;
    }

    for( AI_TOOL_CALL_RECORD& toolCall : response.m_ToolCalls )
    {
        toolCall.m_RequestId = aRequest.m_RequestId;
        recordModelToolCall( m_ActivityLog, aRequest, toolCall );

        if( handler )
        {
            AI_TOOL_INVOCATION_RESULT result = handler->HandleToolCall( aRequest, toolCall );
            copyToolResult( toolCall, result );
            recordToolResult( m_ActivityLog, result );
        }
        else
        {
            toolCall.m_Allowed = false;
            toolCall.m_Executed = false;
            toolCall.m_Message = wxS( "No tool handler installed." );
        }
    }
```

Add method implementations:

```cpp
void AI_RUNTIME::SetToolCallHandler( AI_TOOL_CALL_HANDLER* aHandler )
{
    std::lock_guard<std::mutex> lock( m_Mutex );
    m_ToolCallHandler = aHandler;
}


std::vector<AI_ACTIVITY_RECORD> AI_RUNTIME::ActivityRecords() const
{
    return m_ActivityLog.Records();
}
```

- [ ] **Step 3: Run runtime tests to verify GREEN**

Run the verification command with `--run_test=AiNativeRuntime`.

Expected: runtime suite passes.

### Task 5: Full Targeted Verification And Commit

**Files:**
- Verify: `include/kisurf/ai/ai_types.h`
- Verify: `include/kisurf/ai/ai_runtime.h`
- Verify: `common/kisurf/ai/ai_provider.cpp`
- Verify: `common/kisurf/ai/ai_runtime.cpp`
- Verify: `qa/tests/common/test_ai_provider.cpp`
- Verify: `qa/tests/common/test_ai_runtime.cpp`

- [ ] **Step 1: Run targeted verification**

Run the verification command with:

```text
--run_test=AiNativeProvider,AiNativeRuntime,AiToolExecution,AiActivityLog,AiNativeTypes
```

Expected: exit code `0` and no Boost test errors.

- [ ] **Step 2: Run diff check**

Run:

```powershell
git diff --check
```

Expected: no whitespace errors. LF/CRLF warnings are acceptable.

- [ ] **Step 3: Review worktree**

Run:

```powershell
git status --short
git diff --stat
```

Expected: only the six implementation/test files and this plan are changed.

- [ ] **Step 4: Commit**

Run:

```powershell
git add docs\superpowers\plans\2026-06-16-ai-runtime-tool-call-loop-implementation.md include\kisurf\ai\ai_types.h include\kisurf\ai\ai_runtime.h common\kisurf\ai\ai_provider.cpp common\kisurf\ai\ai_runtime.cpp qa\tests\common\test_ai_provider.cpp qa\tests\common\test_ai_runtime.cpp
git commit -m "feat: add ai runtime tool call loop"
```

Expected: commit succeeds on the current `codex/ai-native-first-slice` branch.

## Plan Self-Review

- Spec coverage: every Phase 4 goal maps to a task: provider parsing and tool
  declarations in Tasks 1-2, runtime tracing/activity in Tasks 3-4, and safety
  verification in Task 5.
- Placeholder scan: no TBD/TODO/fill-in text remains.
- Type consistency: `AI_TOOL_CALL_RECORD`, `AI_PROVIDER_RESPONSE::m_ToolCalls`,
  `AI_TOOL_CALL_HANDLER`, and runtime method names are used consistently across
  tests and implementation steps.
- Scope check: multi-turn tool-result follow-up, visual snapshots, editor action
  adapters, and preview materialization remain outside this plan.
