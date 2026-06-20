#include <boost/test/unit_test.hpp>
#include <json_common.h>
#include <kisurf/ai/ai_provider.h>
#include <kisurf/ai/ai_runtime.h>

#include <memory>
#include <vector>

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
