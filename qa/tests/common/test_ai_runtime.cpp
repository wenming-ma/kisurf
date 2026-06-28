#include <boost/test/unit_test.hpp>
#include <json_common.h>
#include <kisurf/ai/ai_artifact_store.h>
#include <kisurf/ai/ai_prompt_trace_store.h>
#include <kisurf/ai/ai_provider.h>
#include <kisurf/ai/ai_runtime.h>

#include <memory>
#include <vector>

#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/utils.h>

namespace
{
wxString uniqueRuntimePromptTracePath( const wxString& aSuffix )
{
    wxString base = wxFileName::CreateTempFileName( wxS( "kst" ) );

    if( wxFileExists( base ) )
        wxRemoveFile( base );

    wxFileName path( base );
    path.SetFullName( wxString::Format( wxS( "kisurf_runtime_prompt_trace_%s_%lu.jsonl" ),
                                        aSuffix,
                                        static_cast<unsigned long>( wxGetProcessId() ) ) );

    if( wxFileExists( path.GetFullPath() ) )
        wxRemoveFile( path.GetFullPath() );

    return path.GetFullPath();
}


wxString uniqueRuntimeArtifactManifestPath( const wxString& aSuffix )
{
    wxString base = wxFileName::CreateTempFileName( wxS( "ksa" ) );

    if( wxFileExists( base ) )
        wxRemoveFile( base );

    wxFileName path( base );
    path.SetFullName( wxString::Format( wxS( "kisurf_runtime_artifacts_%s_%lu.jsonl" ),
                                        aSuffix,
                                        static_cast<unsigned long>( wxGetProcessId() ) ) );

    if( wxFileExists( path.GetFullPath() ) )
        wxRemoveFile( path.GetFullPath() );

    return path.GetFullPath();
}


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
        call.m_ArgumentsJson =
                wxS( "{\"action\":\"pcbnew.InteractiveSelectionTool.selectionClear\"}" );
        response.m_ToolCalls.push_back( call );

        return response;
    }
};


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


class SCRIPT_CONTINUATION_TOOL_CALL_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_LastRequest = aRequest;

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Kind = AI_SUGGESTION_KIND::Chat;
        response.m_Title = wxS( "Script Continuation Provider" );

        if( !aRequest.m_ToolResults.empty() )
        {
            response.m_Body = wxS( "Script result received." );
            return response;
        }

        response.m_Body = wxS( "Script call requested." );

        AI_TOOL_CALL_RECORD call;
        call.m_RequestId = aRequest.m_RequestId;
        call.m_ToolCallId = wxS( "call_script_runtime" );
        call.m_ToolName = wxS( "kisurf_run_cell" );
        call.m_ArgumentsJson =
                wxS( "{\"cell_id\":\"cell-large\",\"cell_text\":\"print('x')\"}" );
        response.m_ToolCalls.push_back( call );

        return response;
    }

    int                 m_CallCount = 0;
    AI_PROVIDER_REQUEST m_LastRequest;
};


class MULTI_ROUND_TOOL_CALL_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_LastRequest = aRequest;

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Kind = AI_SUGGESTION_KIND::Chat;
        response.m_Title = wxS( "Multi Round Provider" );

        if( aRequest.m_ToolResults.size() >= 2 )
        {
            response.m_Body = wxS( "All tool results received." );
            return response;
        }

        const uint64_t round = static_cast<uint64_t>( aRequest.m_ToolResults.size() + 1 );
        response.m_Body = wxString::Format( wxS( "Tool round %llu requested." ),
                                            static_cast<unsigned long long>( round ) );

        AI_TOOL_CALL_RECORD call;
        call.m_RequestId = aRequest.m_RequestId;
        call.m_ToolCallId = wxString::Format( wxS( "call_round_%llu" ),
                                              static_cast<unsigned long long>( round ) );
        call.m_ToolName = wxS( "kisurf_run_action" );
        call.m_ArgumentsJson =
                wxS( "{\"action\":\"pcbnew.InteractiveSelectionTool.selectionClear\"}" );
        response.m_ToolCalls.push_back( call );

        return response;
    }

    int                 m_CallCount = 0;
    AI_PROVIDER_REQUEST m_LastRequest;
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


class RECOVERABLE_SESSION_TOOL_CALL_HANDLER : public AI_TOOL_CALL_HANDLER
{
public:
    AI_TOOL_INVOCATION_RESULT HandleToolCall( const AI_PROVIDER_REQUEST& aRequest,
                                              const AI_TOOL_CALL_RECORD& aToolCall ) override
    {
        ++m_CallCount;

        AI_TOOL_INVOCATION_RESULT result;
        result.m_RequestId = aRequest.m_RequestId;
        result.m_ToolCallId = aToolCall.m_ToolCallId;
        result.m_ActionName = aToolCall.m_ToolName;
        result.m_Allowed = true;
        result.m_Executed = true;
        result.m_Message = wxS( "fake session mutation executed" );
        result.m_ResultJson =
                wxS( "{\"status\":\"executed\","
                     "\"session_id\":\"chat-session-7\","
                     "\"checkpoint_id\":42,"
                     "\"session_journal\":{\"operations\":["
                     "{\"kind\":\"pcb.create_via\"},"
                     "{\"kind\":\"session.render_preview\"}]}}" );
        return result;
    }

    int m_CallCount = 0;
};


class PYTHON_EVENT_TOOL_CALL_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Kind = AI_SUGGESTION_KIND::Chat;
        response.m_Title = wxS( "Python Event Provider" );

        if( !aRequest.m_ToolResults.empty() )
        {
            response.m_Body = wxS( "Python events received." );
            return response;
        }

        response.m_Body = wxS( "Run a Python cell." );

        AI_TOOL_CALL_RECORD call;
        call.m_RequestId = aRequest.m_RequestId;
        call.m_ToolCallId = wxS( "call_run_cell" );
        call.m_ToolName = wxS( "kisurf_run_cell" );
        call.m_ArgumentsJson = wxS( "{\"cell_id\":\"cell-a\",\"cell_text\":\"run()\"}" );
        response.m_ToolCalls.push_back( call );

        return response;
    }
};


class PYTHON_EVENT_TOOL_CALL_HANDLER : public AI_TOOL_CALL_HANDLER
{
public:
    AI_TOOL_INVOCATION_RESULT HandleToolCall( const AI_PROVIDER_REQUEST& aRequest,
                                              const AI_TOOL_CALL_RECORD& aToolCall ) override
    {
        AI_TOOL_INVOCATION_RESULT result;
        result.m_RequestId = aRequest.m_RequestId;
        result.m_ToolCallId = aToolCall.m_ToolCallId;
        result.m_ActionName = wxS( "kisurf_run_cell" );
        result.m_Allowed = true;
        result.m_Executed = true;
        result.m_Message = wxS( "cell executed" );
        result.m_ResultJson =
                wxS( "{\"status\":\"cell_executed\",\"recorded_events\":["
                     "{\"sequence\":1,\"cell_id\":\"cell-a\",\"source\":\"stream\","
                     "\"kind\":\"progress\",\"message\":\"routed first segment\","
                     "\"payload\":{\"segment\":1}},"
                     "{\"sequence\":2,\"cell_id\":\"cell-a\",\"source\":\"cell_result\","
                     "\"kind\":\"inspection\",\"message\":\"needs clearance review\","
                     "\"payload\":{\"severity\":\"warning\"}}]}" );
        return result;
    }
};


class LARGE_RESULT_TOOL_CALL_HANDLER : public AI_TOOL_CALL_HANDLER
{
public:
    AI_TOOL_INVOCATION_RESULT HandleToolCall( const AI_PROVIDER_REQUEST& aRequest,
                                              const AI_TOOL_CALL_RECORD& aToolCall ) override
    {
        AI_TOOL_INVOCATION_RESULT result;
        result.m_RequestId = aRequest.m_RequestId;
        result.m_ToolCallId = aToolCall.m_ToolCallId;
        result.m_ActionName = aToolCall.m_ToolName;
        result.m_Allowed = true;
        result.m_Executed = true;
        result.m_Message = wxS( "large result executed" );

        wxString payload;

        for( int i = 0; i < 6000; ++i )
            payload << wxS( "r" );

        result.m_ResultJson = wxS( "{\"stdout\":\"" ) + payload + wxS( "\"}" );
        m_LastResultJson = result.m_ResultJson;
        return result;
    }

    wxString m_LastResultJson;
};


class ERROR_RESPONSE_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Kind = AI_SUGGESTION_KIND::Chat;
        response.m_Title = wxS( "AI Provider Error" );
        response.m_Body = wxS( "AI provider request failed with HTTP 502." );
        return response;
    }
};


class PROVIDER_TRACE_RESPONSE_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Kind = AI_SUGGESTION_KIND::Chat;
        response.m_Title = wxS( "AI Provider" );
        response.m_Body = wxS( "response after retry" );
        response.m_ProviderTraceJson =
                wxS( "{\"retry_history\":[{\"reason\":\"context_limit\","
                     "\"action\":\"shrunk_retry\"}]}" );
        return response;
    }
};


class CONTINUATION_ERROR_AFTER_TOOL_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Kind = AI_SUGGESTION_KIND::Chat;

        if( !aRequest.m_ToolResults.empty() )
        {
            response.m_Title = wxS( "AI Provider Error" );
            response.m_Body = wxS( "AI provider request failed after tool execution." );
            response.m_ProviderTraceJson =
                    wxS( "{\"retry_history\":[{\"reason\":\"transient_gateway\","
                         "\"action\":\"failed_after_retry\"}]}" );
            return response;
        }

        response.m_Title = wxS( "Continuation Provider" );
        response.m_Body = wxS( "Tool call requested before provider failure." );

        AI_TOOL_CALL_RECORD call;
        call.m_RequestId = aRequest.m_RequestId;
        call.m_ToolCallId = wxS( "call_before_failure" );
        call.m_ToolName = wxS( "kisurf_run_action" );
        call.m_ArgumentsJson =
                wxS( "{\"action\":\"pcbnew.InteractiveSelectionTool.selectionClear\"}" );
        response.m_ToolCalls.push_back( call );
        return response;
    }

    int m_CallCount = 0;
};


class RECORDING_CHAT_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_LastRequest = aRequest;

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Kind = AI_SUGGESTION_KIND::Chat;
        response.m_Title = wxS( "Recording Provider" );
        response.m_Body = wxS( "recorded" );
        return response;
    }

    int                 m_CallCount = 0;
    AI_PROVIDER_REQUEST m_LastRequest;
};
} // namespace

BOOST_AUTO_TEST_SUITE( AiNativeRuntime )


BOOST_AUTO_TEST_CASE( RuntimeAssignsRequestIdsAndStoresTrace )
{
    AI_RUNTIME runtime( std::make_unique<AI_STUB_PROVIDER>() );

    AI_PROVIDER_REQUEST request;
    request.m_UserText = wxS( "hello" );

    AI_PROVIDER_RESPONSE response = runtime.Submit( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 1 );
    BOOST_CHECK_EQUAL( runtime.TraceRecords().size(), 1 );
    BOOST_CHECK_EQUAL( runtime.TraceRecords().front().m_Response.m_Body, response.m_Body );
}


BOOST_AUTO_TEST_CASE( RuntimeWritesPromptTraceForSubmittedRequest )
{
    wxString path = uniqueRuntimePromptTracePath( wxS( "submit" ) );

    AI_PROMPT_TRACE_STORE traceStore( path );
    AI_RUNTIME            runtime( std::make_unique<AI_STUB_PROVIDER>() );
    runtime.SetPromptTraceStore( &traceStore );

    AI_PROVIDER_REQUEST request;
    request.m_UserText = wxS( "hello trace store" );

    AI_PROVIDER_RESPONSE response = runtime.Submit( request );

    wxString error;
    std::vector<AI_PROMPT_TRACE_ENTRY> entries = traceStore.LoadAll( error );

    BOOST_CHECK_EQUAL( response.m_RequestId, 1 );
    BOOST_REQUIRE_EQUAL( entries.size(), 1 );
    BOOST_CHECK_EQUAL( entries.front().m_RequestId, 1 );
    BOOST_CHECK_EQUAL( entries.front().m_ProviderStatus,
                       wxString( wxS( "provider_response" ) ) );
    BOOST_CHECK( entries.front().m_PromptTraceJson.Contains( wxS( "user.request" ) ) );

    if( wxFileExists( path ) )
        wxRemoveFile( path );
}


BOOST_AUTO_TEST_CASE( RuntimeAppliesRequestKindBudgetBeforeProviderAndPromptTrace )
{
    wxString path = uniqueRuntimePromptTracePath( wxS( "budgeted_submit" ) );

    auto* provider = new RECORDING_CHAT_PROVIDER();
    AI_PROMPT_TRACE_STORE traceStore( path );
    AI_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ) };
    runtime.SetPromptTraceStore( &traceStore );

    AI_PROVIDER_REQUEST request;
    request.m_RequestKind = AI_PROVIDER_REQUEST_KIND::NextActionDecision;
    request.m_UserText = wxS( "bounded runtime provider input" );
    request.m_MaxProviderInputChars = 50000;
    request.m_MaxContextActivityRecords = 50;
    request.m_MaxToolResultChars = 50000;
    request.m_MaxRetrievedMemoryChars = 20000;

    for( int i = 0; i < 40; ++i )
    {
        AI_ACTIVITY_RECORD activity;
        activity.m_Sequence = static_cast<uint64_t>( i + 1 );
        activity.m_ActionName = wxString::Format( wxS( "activity-%02d" ), i );
        activity.m_Message = wxS( "runtime next action activity" );
        request.m_ContextSnapshot.m_RecentActivity.push_back( activity );
    }

    AI_TOOL_CALL_RECORD result;
    result.m_RequestId = 1;
    result.m_ToolCallId = wxS( "call_runtime_large" );
    result.m_ToolName = wxS( "observation.read" );

    wxString rawPayload;
    for( int i = 0; i < 25000; ++i )
        rawPayload << wxS( "r" );

    result.m_ResultJson = wxS( "{\"observation\":\"" ) + rawPayload
                          + wxS( "RUNTIME_RAW_TOOL_RESULT_NEEDLE\"}" );
    request.m_ToolResults.push_back( result );

    runtime.Submit( request );

    BOOST_REQUIRE_EQUAL( provider->m_CallCount, 1 );
    BOOST_CHECK( provider->m_LastRequest.m_ContextCompiled );
    BOOST_CHECK_EQUAL( provider->m_LastRequest.m_MaxContextActivityRecords, 8 );
    BOOST_CHECK_EQUAL( provider->m_LastRequest.m_MaxToolResultChars, 16384 );
    BOOST_CHECK( !provider->m_LastRequest.m_CompiledUserMessageText.Contains(
            wxS( "activity-00" ) ) );
    BOOST_CHECK( provider->m_LastRequest.m_CompiledUserMessageText.Contains(
            wxS( "activity-39" ) ) );
    BOOST_REQUIRE_EQUAL( provider->m_LastRequest.m_ToolResults.size(), 1 );
    BOOST_CHECK( provider->m_LastRequest.m_ToolResults.front().m_ResultJson.Contains(
            wxS( "compressed_tool_result" ) ) );
    BOOST_CHECK( !provider->m_LastRequest.m_ToolResults.front().m_ResultJson.Contains(
            wxS( "RUNTIME_RAW_TOOL_RESULT_NEEDLE" ) ) );

    wxString error;
    std::vector<AI_PROMPT_TRACE_ENTRY> entries = traceStore.LoadAll( error );

    BOOST_REQUIRE_EQUAL( entries.size(), 1 );
    BOOST_CHECK( entries.front().m_PromptTraceJson.Contains(
            wxS( "older_activity_omitted" ) ) );
    BOOST_CHECK( entries.front().m_PromptTraceJson.Contains(
            wxS( "compressed" ) ) );

    if( wxFileExists( path ) )
        wxRemoveFile( path );
}


BOOST_AUTO_TEST_CASE( RuntimeCanCancelKnownRequest )
{
    AI_RUNTIME runtime( std::make_unique<AI_STUB_PROVIDER>() );

    AI_PROVIDER_REQUEST request;
    request.m_UserText = wxS( "cancel me" );

    AI_PROVIDER_RESPONSE response = runtime.Submit( request );

    BOOST_CHECK( runtime.Cancel( response.m_RequestId ) );
    BOOST_CHECK( runtime.TraceRecords().front().m_Cancelled );
    BOOST_CHECK( !runtime.Cancel( 999 ) );
}


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
    BOOST_CHECK_EQUAL( response.m_ToolCalls.front().m_ErrorCode,
                       wxString( wxS( "no_tool_handler" ) ) );
    BOOST_CHECK( response.m_ToolCalls.front().m_Message.Contains( wxS( "No tool handler" ) ) );
    nlohmann::json resultJson =
            nlohmann::json::parse( response.m_ToolCalls.front().m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( resultJson["action"].get<std::string>(), "kisurf_run_action" );
    BOOST_CHECK( !resultJson["allowed"].get<bool>() );
    BOOST_CHECK( !resultJson["executed"].get<bool>() );
    BOOST_CHECK_EQUAL( resultJson["status"].get<std::string>(), "denied" );
    BOOST_CHECK_EQUAL( resultJson["error_code"].get<std::string>(), "no_tool_handler" );
    BOOST_REQUIRE_EQUAL( records.size(), 2 );
    BOOST_CHECK( records.front().m_Kind == AI_ACTIVITY_KIND::ModelToolRequest );
    BOOST_CHECK_EQUAL( records.front().m_ToolCallId, wxString( wxS( "call_runtime" ) ) );
    BOOST_CHECK( records.back().m_Kind == AI_ACTIVITY_KIND::ToolResult );
    BOOST_CHECK_EQUAL( records.back().m_ToolCallId, wxString( wxS( "call_runtime" ) ) );
    BOOST_CHECK_EQUAL( records.back().m_ErrorCode, wxString( wxS( "no_tool_handler" ) ) );
    BOOST_CHECK( !records.back().m_Allowed );
    BOOST_CHECK( !records.back().m_Executed );
}


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


BOOST_AUTO_TEST_CASE( RuntimeWritesPromptTraceForToolContinuationRequest )
{
    wxString path = uniqueRuntimePromptTracePath( wxS( "continuation" ) );

    auto* provider = new CONTINUATION_TOOL_CALL_PROVIDER();
    AI_PROMPT_TRACE_STORE traceStore( path );
    AI_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ) };
    FAKE_TOOL_CALL_HANDLER handler;
    runtime.SetToolCallHandler( &handler );
    runtime.SetPromptTraceStore( &traceStore );

    AI_PROVIDER_REQUEST request;
    request.m_UserText = wxS( "use a tool and continue" );

    runtime.Submit( request );

    wxString error;
    std::vector<AI_PROMPT_TRACE_ENTRY> entries = traceStore.LoadAll( error );

    BOOST_REQUIRE_EQUAL( entries.size(), 2 );
    BOOST_CHECK_EQUAL( entries.at( 0 ).m_RequestId, 1 );
    BOOST_CHECK_EQUAL( entries.at( 1 ).m_RequestId, 1 );
    BOOST_CHECK( entries.at( 1 ).m_PromptTraceJson.Contains( wxS( "tool_result" ) ) );
    BOOST_CHECK_EQUAL( provider->m_CallCount, 2 );

    if( wxFileExists( path ) )
        wxRemoveFile( path );
}


BOOST_AUTO_TEST_CASE( RuntimeMarksProviderFailureAfterExecutedToolAsAmbiguous )
{
    wxString path = uniqueRuntimePromptTracePath( wxS( "post_side_effect" ) );

    auto* provider = new CONTINUATION_ERROR_AFTER_TOOL_PROVIDER();
    AI_PROMPT_TRACE_STORE traceStore( path );
    AI_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ) };
    FAKE_TOOL_CALL_HANDLER handler;
    runtime.SetToolCallHandler( &handler );
    runtime.SetPromptTraceStore( &traceStore );

    AI_PROVIDER_REQUEST request;
    request.m_UserText = wxS( "run tool then provider fails" );

    AI_PROVIDER_RESPONSE response = runtime.Submit( request );

    BOOST_CHECK_EQUAL( provider->m_CallCount, 2 );
    BOOST_CHECK_EQUAL( handler.m_CallCount, 1 );
    BOOST_CHECK_EQUAL( response.m_Title, wxString( wxS( "AI Provider Error" ) ) );
    BOOST_REQUIRE_EQUAL( response.m_ToolCalls.size(), 1 );
    BOOST_CHECK( response.m_ToolCalls.front().m_Executed );
    BOOST_CHECK( response.m_ProviderTraceJson.Contains(
            wxS( "post_side_effect_ambiguity" ) ) );
    BOOST_CHECK( response.m_ProviderTraceJson.Contains( wxS( "call_before_failure" ) ) );

    wxString error;
    std::vector<AI_PROMPT_TRACE_ENTRY> entries = traceStore.LoadAll( error );

    BOOST_REQUIRE_EQUAL( entries.size(), 2 );
    BOOST_CHECK_EQUAL( entries.back().m_ProviderStatus,
                       wxString( wxS( "provider_error" ) ) );
    BOOST_CHECK( entries.back().m_ProviderTraceJson.Contains(
            wxS( "post_side_effect_ambiguity" ) ) );

    if( wxFileExists( path ) )
        wxRemoveFile( path );
}


BOOST_AUTO_TEST_CASE( RuntimeProviderFailureAfterExecutedToolRecordsRecoveryBasis )
{
    auto* provider = new CONTINUATION_ERROR_AFTER_TOOL_PROVIDER();
    AI_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ) };
    RECOVERABLE_SESSION_TOOL_CALL_HANDLER handler;
    runtime.SetToolCallHandler( &handler );

    AI_PROVIDER_REQUEST request;
    request.m_UserText = wxS( "run recoverable session tool then provider fails" );
    request.m_ContextVersion.m_DocumentRevision = 12;
    request.m_ContextVersion.m_SelectionRevision = 3;
    request.m_ContextVersion.m_ViewRevision = 5;
    request.m_ContextSnapshot.m_Version = request.m_ContextVersion;

    AI_PROVIDER_RESPONSE response = runtime.Submit( request );

    BOOST_CHECK_EQUAL( provider->m_CallCount, 2 );
    BOOST_CHECK_EQUAL( handler.m_CallCount, 1 );

    nlohmann::json trace = nlohmann::json::parse(
            response.m_ProviderTraceJson.ToStdString() );
    BOOST_REQUIRE( trace.contains( "runtime_guard" ) );
    const nlohmann::json& guard = trace["runtime_guard"];
    BOOST_REQUIRE( guard.contains( "recovery_basis" ) );
    const nlohmann::json& recovery = guard["recovery_basis"];

    BOOST_CHECK( recovery["requires_checkpoint_resume"].get<bool>() );
    BOOST_CHECK_EQUAL( recovery["executed_tool_result_count"].get<int>(), 1 );
    BOOST_REQUIRE_EQUAL( recovery["tool_results"].size(), 1 );
    BOOST_CHECK_EQUAL( recovery["tool_results"].at( 0 )["tool_call_id"].get<std::string>(),
                       "call_before_failure" );
    BOOST_CHECK_EQUAL( recovery["tool_results"].at( 0 )["session_id"].get<std::string>(),
                       "chat-session-7" );
    BOOST_CHECK_EQUAL( recovery["tool_results"].at( 0 )["checkpoint_id"].get<int>(), 42 );
    BOOST_CHECK_EQUAL( recovery["tool_results"].at( 0 )["journal_operation_count"].get<int>(),
                       2 );
    BOOST_CHECK_EQUAL( recovery["board_state_version"]["document_revision"].get<int>(), 12 );
    BOOST_CHECK_EQUAL( recovery["board_state_version"]["selection_revision"].get<int>(), 3 );
    BOOST_CHECK_EQUAL( recovery["board_state_version"]["view_revision"].get<int>(), 5 );
}


BOOST_AUTO_TEST_CASE( RuntimeProviderFailureAfterExecutedToolArchivesRecoveryArtifact )
{
    wxString manifestPath = uniqueRuntimeArtifactManifestPath( wxS( "provider_recovery" ) );

    auto* provider = new CONTINUATION_ERROR_AFTER_TOOL_PROVIDER();
    AI_ARTIFACT_STORE artifactStore( manifestPath );
    AI_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ) };
    RECOVERABLE_SESSION_TOOL_CALL_HANDLER handler;
    runtime.SetToolCallHandler( &handler );
    runtime.SetArtifactStore( &artifactStore );

    AI_PROVIDER_REQUEST request;
    request.m_UserText = wxS( "run recoverable session tool then provider fails" );
    request.m_ContextSnapshot.m_ProjectId = wxS( "project-a" );
    request.m_ContextSnapshot.m_DocumentId = wxS( "board-1" );
    request.m_ContextVersion.m_DocumentRevision = 12;
    request.m_ContextSnapshot.m_Version = request.m_ContextVersion;

    AI_PROVIDER_RESPONSE response = runtime.Submit( request );

    nlohmann::json trace = nlohmann::json::parse(
            response.m_ProviderTraceJson.ToStdString() );
    BOOST_REQUIRE( trace.contains( "runtime_guard" ) );
    BOOST_REQUIRE( trace["runtime_guard"].contains( "recovery_artifact_ref" ) );

    wxString error;
    AI_ARTIFACT_QUERY query;
    query.m_Kind = wxS( "provider_recovery" );
    std::vector<AI_ARTIFACT_RECORD> artifacts =
            artifactStore.Query( query, error );

    BOOST_REQUIRE_EQUAL( artifacts.size(), 1 );
    BOOST_CHECK_EQUAL( artifacts.front().m_ProjectId, wxString( wxS( "project-a" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_DocumentId, wxString( wxS( "board-1" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_AgentKind, wxString( wxS( "chat" ) ) );
    BOOST_CHECK( artifacts.front().m_MetadataJson.Contains(
            wxS( "post_side_effect_ambiguity" ) ) );

    wxString archivedPayload;
    BOOST_REQUIRE( artifactStore.ReadPayload( artifacts.front().m_Uri,
                                              archivedPayload, error ) );
    BOOST_CHECK( archivedPayload.Contains( wxS( "provider_recovery" ) ) );
    BOOST_CHECK( archivedPayload.Contains( wxS( "recovery_basis" ) ) );
    BOOST_CHECK( archivedPayload.Contains( wxS( "checkpoint_id" ) ) );
    BOOST_CHECK( archivedPayload.Contains( wxS( "chat-session-7" ) ) );

    wxRemoveFile( manifestPath );
    wxRemoveFile( artifacts.front().m_BlobPath );
}


BOOST_AUTO_TEST_CASE( RuntimeArchivesLargeToolResultsBeforeContinuation )
{
    wxString manifestPath = uniqueRuntimeArtifactManifestPath( wxS( "continuation" ) );

    auto* provider = new CONTINUATION_TOOL_CALL_PROVIDER();
    AI_ARTIFACT_STORE artifactStore( manifestPath );
    AI_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ) };
    LARGE_RESULT_TOOL_CALL_HANDLER handler;
    runtime.SetToolCallHandler( &handler );
    runtime.SetArtifactStore( &artifactStore );

    AI_PROVIDER_REQUEST request;
    request.m_UserText = wxS( "use a verbose tool" );
    request.m_MaxToolResultChars = 200;
    request.m_ContextSnapshot.m_ProjectId = wxS( "project-a" );
    request.m_ContextSnapshot.m_DocumentId = wxS( "board-1" );

    runtime.Submit( request );

    wxString error;
    std::vector<AI_ARTIFACT_RECORD> artifacts = artifactStore.LoadAll( error );

    BOOST_REQUIRE_EQUAL( artifacts.size(), 1 );
    BOOST_CHECK_EQUAL( artifacts.front().m_Kind, wxString( wxS( "tool_result" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_ProjectId, wxString( wxS( "project-a" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_DocumentId, wxString( wxS( "board-1" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_AgentKind, wxString( wxS( "chat" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_RetentionClass, wxString( wxS( "trace" ) ) );

    BOOST_REQUIRE_EQUAL( provider->m_LastRequest.m_ToolResults.size(), 1 );
    nlohmann::json compressed = nlohmann::json::parse(
            provider->m_LastRequest.m_ToolResults.front().m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( compressed["status"].get<std::string>(),
                       "compressed_tool_result" );
    BOOST_CHECK_EQUAL( compressed["artifact_ref"]["kind"].get<std::string>(),
                       "tool_result" );
    BOOST_CHECK_EQUAL( compressed["artifact_ref"]["uri"].get<std::string>(),
                       artifacts.front().m_Uri.ToStdString() );

    wxString archivedPayload;
    BOOST_REQUIRE( artifactStore.ReadPayload( artifacts.front().m_Uri,
                                              archivedPayload, error ) );
    BOOST_CHECK_EQUAL( archivedPayload, handler.m_LastResultJson );

    wxRemoveFile( manifestPath );
    wxRemoveFile( artifacts.front().m_BlobPath );
}


BOOST_AUTO_TEST_CASE( RuntimeArchivesLargeScriptOutputBeforeContinuation )
{
    wxString manifestPath = uniqueRuntimeArtifactManifestPath( wxS( "script_output" ) );

    auto* provider = new SCRIPT_CONTINUATION_TOOL_CALL_PROVIDER();
    AI_ARTIFACT_STORE artifactStore( manifestPath );
    AI_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ) };
    LARGE_RESULT_TOOL_CALL_HANDLER handler;
    runtime.SetToolCallHandler( &handler );
    runtime.SetArtifactStore( &artifactStore );

    AI_PROVIDER_REQUEST request;
    request.m_UserText = wxS( "run a verbose script cell" );
    request.m_MaxToolResultChars = 200;
    request.m_ContextSnapshot.m_ProjectId = wxS( "project-a" );
    request.m_ContextSnapshot.m_DocumentId = wxS( "board-1" );

    runtime.Submit( request );

    wxString error;
    std::vector<AI_ARTIFACT_RECORD> artifacts = artifactStore.LoadAll( error );

    BOOST_REQUIRE_EQUAL( artifacts.size(), 1 );
    BOOST_CHECK_EQUAL( artifacts.front().m_Kind, wxString( wxS( "script_output" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_ProjectId, wxString( wxS( "project-a" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_DocumentId, wxString( wxS( "board-1" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_AgentKind, wxString( wxS( "chat" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_Source, wxString( wxS( "kisurf_run_cell" ) ) );
    BOOST_CHECK( artifacts.front().m_MetadataJson.Contains( wxS( "cell-large" ) ) );

    BOOST_REQUIRE_EQUAL( provider->m_LastRequest.m_ToolResults.size(), 1 );
    nlohmann::json compressed = nlohmann::json::parse(
            provider->m_LastRequest.m_ToolResults.front().m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( compressed["status"].get<std::string>(),
                       "compressed_tool_result" );
    BOOST_CHECK_EQUAL( compressed["artifact_ref"]["kind"].get<std::string>(),
                       "script_output" );
    BOOST_CHECK_EQUAL( compressed["artifact_ref"]["uri"].get<std::string>(),
                       artifacts.front().m_Uri.ToStdString() );

    wxString archivedPayload;
    BOOST_REQUIRE( artifactStore.ReadPayload( artifacts.front().m_Uri,
                                              archivedPayload, error ) );
    BOOST_CHECK_EQUAL( archivedPayload, handler.m_LastResultJson );

    wxRemoveFile( manifestPath );
    wxRemoveFile( artifacts.front().m_BlobPath );
}


BOOST_AUTO_TEST_CASE( RuntimeArchivesOversizedVisualObservationBeforeProviderInput )
{
    wxString manifestPath = uniqueRuntimeArtifactManifestPath( wxS( "visual" ) );

    auto* provider = new RECORDING_CHAT_PROVIDER();
    AI_ARTIFACT_STORE artifactStore( manifestPath );
    AI_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ) };
    runtime.SetArtifactStore( &artifactStore );

    AI_PROVIDER_REQUEST request;
    request.m_UserText = wxS( "inspect the visual frame" );
    request.m_MaxVisualDataUriChars = 32;
    request.m_ContextSnapshot.m_ProjectId = wxS( "project-a" );
    request.m_ContextSnapshot.m_DocumentId = wxS( "board-1" );
    request.m_ContextSnapshot.m_Visual.m_Source = wxS( "pcbnew.canvas.roi" );
    request.m_ContextSnapshot.m_Visual.m_MimeType = wxS( "image/png" );
    request.m_ContextSnapshot.m_Visual.m_DataUri =
            wxS( "data:image/png;base64,abcdefghijklmnopqrstuvwxyz0123456789" );
    request.m_ContextSnapshot.m_Visual.m_FrameId = wxS( "frame-chat-1" );
    request.m_ContextSnapshot.m_Visual.m_FrameKind = wxS( "roi_raw" );
    request.m_ContextSnapshot.m_Visual.m_WidthPx = 640;
    request.m_ContextSnapshot.m_Visual.m_HeightPx = 480;
    request.m_ContextSnapshot.m_Visual.m_ByteSize = 4096;
    request.m_ContextSnapshot.m_Visual.m_SidecarJson =
            wxS( "{\"frame_id\":\"frame-chat-1\",\"frame_kind\":\"roi_raw\","
                 "\"anchors\":[{\"anchor_id\":\"A1\"}]}" );

    runtime.Submit( request );

    wxString error;
    std::vector<AI_ARTIFACT_RECORD> artifacts = artifactStore.LoadAll( error );

    BOOST_REQUIRE_EQUAL( artifacts.size(), 1 );
    BOOST_CHECK_EQUAL( artifacts.front().m_Kind,
                       wxString( wxS( "visual_observation" ) ) );
    BOOST_CHECK_EQUAL( artifacts.front().m_AgentKind, wxString( wxS( "chat" ) ) );

    BOOST_CHECK( !provider->m_LastRequest.m_ContextSnapshot.m_Visual.HasPixels() );
    BOOST_CHECK( provider->m_LastRequest.m_PromptTraceJson.Contains(
            artifacts.front().m_Uri ) );
    BOOST_CHECK( provider->m_LastRequest.m_ContextSnapshot.m_Visual.m_SidecarJson
                         .Contains( artifacts.front().m_Uri ) );

    wxString archivedPayload;
    BOOST_REQUIRE( artifactStore.ReadPayload( artifacts.front().m_Uri,
                                              archivedPayload, error ) );
    BOOST_CHECK( archivedPayload.Contains(
            wxS( "data:image/png;base64,abcdefghijklmnopqrstuvwxyz0123456789" ) ) );
    BOOST_CHECK( archivedPayload.Contains( wxS( "frame-chat-1" ) ) );

    wxRemoveFile( manifestPath );
    wxRemoveFile( artifacts.front().m_BlobPath );
}


BOOST_AUTO_TEST_CASE( RuntimeMarksPromptTraceProviderErrors )
{
    wxString path = uniqueRuntimePromptTracePath( wxS( "provider_error" ) );

    AI_PROMPT_TRACE_STORE traceStore( path );
    AI_RUNTIME runtime( std::make_unique<ERROR_RESPONSE_PROVIDER>() );
    runtime.SetPromptTraceStore( &traceStore );

    AI_PROVIDER_REQUEST request;
    request.m_UserText = wxS( "trigger provider error" );

    runtime.Submit( request );

    wxString error;
    std::vector<AI_PROMPT_TRACE_ENTRY> entries = traceStore.LoadAll( error );

    BOOST_REQUIRE_EQUAL( entries.size(), 1 );
    BOOST_CHECK_EQUAL( entries.front().m_ProviderStatus,
                       wxString( wxS( "provider_error" ) ) );

    if( wxFileExists( path ) )
        wxRemoveFile( path );
}


BOOST_AUTO_TEST_CASE( RuntimeWritesProviderTraceToPromptTraceStore )
{
    wxString path = uniqueRuntimePromptTracePath( wxS( "provider_trace" ) );

    AI_PROMPT_TRACE_STORE traceStore( path );
    AI_RUNTIME runtime( std::make_unique<PROVIDER_TRACE_RESPONSE_PROVIDER>() );
    runtime.SetPromptTraceStore( &traceStore );

    AI_PROVIDER_REQUEST request;
    request.m_UserText = wxS( "trigger provider retry trace" );

    runtime.Submit( request );

    wxString error;
    std::vector<AI_PROMPT_TRACE_ENTRY> entries = traceStore.LoadAll( error );

    BOOST_REQUIRE_EQUAL( entries.size(), 1 );
    BOOST_CHECK( entries.front().m_ProviderTraceJson.Contains( wxS( "retry_history" ) ) );
    BOOST_CHECK( entries.front().m_ProviderTraceJson.Contains( wxS( "shrunk_retry" ) ) );

    if( wxFileExists( path ) )
        wxRemoveFile( path );
}


BOOST_AUTO_TEST_CASE( RuntimeContinuesUntilMaxToolRounds )
{
    auto* provider = new MULTI_ROUND_TOOL_CALL_PROVIDER();
    AI_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ) };
    FAKE_TOOL_CALL_HANDLER handler;
    runtime.SetToolCallHandler( &handler );

    AI_PROVIDER_REQUEST request;
    request.m_UserText = wxS( "use tools until done" );
    request.m_MaxToolRounds = 2;

    AI_PROVIDER_RESPONSE response = runtime.Submit( request );

    BOOST_CHECK_EQUAL( provider->m_CallCount, 3 );
    BOOST_CHECK_EQUAL( handler.m_CallCount, 2 );
    BOOST_CHECK_EQUAL( response.m_Body, wxString( wxS( "All tool results received." ) ) );
    BOOST_REQUIRE_EQUAL( response.m_ToolCalls.size(), 2 );
    BOOST_CHECK_EQUAL( response.m_ToolCalls.at( 0 ).m_ToolCallId,
                       wxString( wxS( "call_round_1" ) ) );
    BOOST_CHECK_EQUAL( response.m_ToolCalls.at( 1 ).m_ToolCallId,
                       wxString( wxS( "call_round_2" ) ) );
    BOOST_REQUIRE_EQUAL( provider->m_LastRequest.m_ToolResults.size(), 2 );
    BOOST_REQUIRE_EQUAL( runtime.TraceRecords().size(), 1 );
    BOOST_REQUIRE_EQUAL( runtime.TraceRecords().front().m_Response.m_ToolCalls.size(), 2 );
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


BOOST_AUTO_TEST_CASE( RuntimeRecordsPythonWorkerEventsAsActivity )
{
    AI_RUNTIME runtime( std::make_unique<PYTHON_EVENT_TOOL_CALL_PROVIDER>() );
    PYTHON_EVENT_TOOL_CALL_HANDLER handler;
    runtime.SetToolCallHandler( &handler );

    AI_PROVIDER_REQUEST request;
    request.m_UserText = wxS( "route this with a script" );

    runtime.Submit( request );

    std::vector<AI_ACTIVITY_RECORD> records = runtime.ActivityRecords();

    BOOST_REQUIRE_EQUAL( records.size(), 4 );
    BOOST_CHECK( records.at( 0 ).m_Kind == AI_ACTIVITY_KIND::ModelToolRequest );
    BOOST_CHECK( records.at( 1 ).m_Kind == AI_ACTIVITY_KIND::ToolResult );

    const AI_ACTIVITY_RECORD& progress = records.at( 2 );
    BOOST_CHECK( progress.m_Kind == AI_ACTIVITY_KIND::ToolResult );
    BOOST_CHECK_EQUAL( progress.m_ToolCallId, wxString( wxS( "call_run_cell" ) ) );
    BOOST_CHECK_EQUAL( progress.m_ActionName, wxString( wxS( "kisurf_run_cell.progress" ) ) );
    BOOST_CHECK_EQUAL( progress.m_Message, wxString( wxS( "routed first segment" ) ) );
    BOOST_CHECK( progress.m_Allowed );
    BOOST_CHECK( progress.m_Executed );

    nlohmann::json progressJson = nlohmann::json::parse( progress.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( progressJson["source"].get<std::string>(), "stream" );
    BOOST_CHECK_EQUAL( progressJson["cell_id"].get<std::string>(), "cell-a" );
    BOOST_CHECK_EQUAL( progressJson["payload"]["segment"].get<int>(), 1 );

    const AI_ACTIVITY_RECORD& inspection = records.at( 3 );
    BOOST_CHECK( inspection.m_Kind == AI_ACTIVITY_KIND::ToolResult );
    BOOST_CHECK_EQUAL( inspection.m_ActionName,
                       wxString( wxS( "kisurf_run_cell.inspection" ) ) );
    BOOST_CHECK_EQUAL( inspection.m_Message,
                       wxString( wxS( "needs clearance review" ) ) );

    nlohmann::json inspectionJson =
            nlohmann::json::parse( inspection.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( inspectionJson["source"].get<std::string>(), "cell_result" );
    BOOST_CHECK_EQUAL( inspectionJson["payload"]["severity"].get<std::string>(), "warning" );
}


BOOST_AUTO_TEST_SUITE_END()
