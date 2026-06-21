#include <kisurf/ai/ai_runtime.h>

#include <nlohmann/json.hpp>
#include <iterator>
#include <string>
#include <utility>

namespace
{
std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


wxString deniedToolResultJson( const AI_TOOL_INVOCATION_RESULT& aResult )
{
    nlohmann::json payload = { { "action", toUtf8String( aResult.m_ActionName ) },
                               { "allowed", aResult.m_Allowed },
                               { "executed", aResult.m_Executed },
                               { "dry_run", false },
                               { "status", "denied" },
                               { "error_code", toUtf8String( aResult.m_ErrorCode ) },
                               { "message", toUtf8String( aResult.m_Message ) } };

    return wxString::FromUTF8( payload.dump().c_str() );
}


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
    record.m_ErrorCode = aResult.m_ErrorCode;
    record.m_Allowed = aResult.m_Allowed;
    record.m_Executed = aResult.m_Executed;
    record.m_Message = aResult.m_Message;
    aActivityLog.Append( record );
}


wxString fromUtf8String( const std::string& aText )
{
    return wxString::FromUTF8( aText.c_str() );
}


std::string jsonStringValue( const nlohmann::json& aJson, const char* aKey )
{
    if( !aJson.is_object() || !aJson.contains( aKey ) || !aJson[aKey].is_string() )
        return {};

    return aJson[aKey].get<std::string>();
}


void recordPythonWorkerEvents( AI_ACTIVITY_LOG& aActivityLog,
                               const AI_TOOL_INVOCATION_RESULT& aResult )
{
    if( aResult.m_ResultJson.IsEmpty() )
        return;

    nlohmann::json payload = nlohmann::json::parse( toUtf8String( aResult.m_ResultJson ),
                                                    nullptr, false );

    if( payload.is_discarded() || !payload.is_object()
        || !payload.contains( "recorded_events" )
        || !payload["recorded_events"].is_array() )
    {
        return;
    }

    for( const nlohmann::json& event : payload["recorded_events"] )
    {
        if( !event.is_object() )
            continue;

        const std::string eventKind = jsonStringValue( event, "kind" );
        const std::string actionSuffix = eventKind.empty() ? "event" : eventKind;

        AI_ACTIVITY_RECORD record;
        record.m_RequestId = aResult.m_RequestId;
        record.m_ToolCallId = aResult.m_ToolCallId;
        record.m_Kind = AI_ACTIVITY_KIND::ToolResult;
        record.m_ActionName = aResult.m_ActionName;

        if( record.m_ActionName.IsEmpty() )
            record.m_ActionName = wxS( "python" );

        record.m_ActionName << wxS( "." ) << fromUtf8String( actionSuffix );
        record.m_ResultJson = fromUtf8String( event.dump() );
        record.m_ErrorCode = aResult.m_ErrorCode;
        record.m_Allowed = aResult.m_Allowed;
        record.m_Executed = aResult.m_Executed;
        record.m_Message = fromUtf8String( jsonStringValue( event, "message" ) );
        aActivityLog.Append( record );
    }
}
} // namespace


AI_RUNTIME::AI_RUNTIME( std::unique_ptr<AI_PROVIDER> aProvider ) :
        m_Provider( std::move( aProvider ) ),
        m_NextRequestId( 1 ),
        m_OwnedActivityLog( 256 ),
        m_ActivityLog( &m_OwnedActivityLog )
{
}


AI_RUNTIME::AI_RUNTIME( std::unique_ptr<AI_PROVIDER> aProvider,
                        AI_ACTIVITY_LOG& aActivityLog ) :
        m_Provider( std::move( aProvider ) ),
        m_NextRequestId( 1 ),
        m_OwnedActivityLog( 0 ),
        m_ActivityLog( &aActivityLog )
{
}


AI_PROVIDER_RESPONSE AI_RUNTIME::Submit( AI_PROVIDER_REQUEST aRequest )
{
    aRequest.m_RequestId = m_NextRequestId.fetch_add( 1 );

    AI_PROVIDER_RESPONSE response = m_Provider->Generate( aRequest );

    AI_TOOL_CALL_HANDLER* handler = nullptr;

    {
        std::lock_guard<std::mutex> lock( m_Mutex );
        handler = m_ToolCallHandler;
    }

    std::vector<AI_TOOL_CALL_RECORD> handledToolCalls;
    size_t                           toolRounds = 0;

    while( !response.m_ToolCalls.empty() && toolRounds < aRequest.m_MaxToolRounds )
    {
        std::vector<AI_TOOL_CALL_RECORD> roundToolCalls = std::move( response.m_ToolCalls );

        for( AI_TOOL_CALL_RECORD& toolCall : roundToolCalls )
        {
            toolCall.m_RequestId = aRequest.m_RequestId;
            recordModelToolCall( *m_ActivityLog, aRequest, toolCall );

            if( handler )
            {
                AI_TOOL_INVOCATION_RESULT result = handler->HandleToolCall( aRequest, toolCall );
                copyToolResult( toolCall, result );
                recordToolResult( *m_ActivityLog, result );
                recordPythonWorkerEvents( *m_ActivityLog, result );
            }
            else
            {
                AI_TOOL_INVOCATION_RESULT result;
                result.m_RequestId = aRequest.m_RequestId;
                result.m_ToolCallId = toolCall.m_ToolCallId;
                result.m_ActionName = toolCall.m_ToolName;
                result.m_Allowed = false;
                result.m_Executed = false;
                result.m_ErrorCode = wxS( "no_tool_handler" );
                result.m_Message = wxS( "No tool handler installed." );
                result.m_ResultJson = deniedToolResultJson( result );
                copyToolResult( toolCall, result );
                recordToolResult( *m_ActivityLog, result );
                recordPythonWorkerEvents( *m_ActivityLog, result );
            }
        }

        handledToolCalls.insert( handledToolCalls.end(),
                                 std::make_move_iterator( roundToolCalls.begin() ),
                                 std::make_move_iterator( roundToolCalls.end() ) );
        ++toolRounds;

        AI_PROVIDER_REQUEST continuationRequest = aRequest;
        continuationRequest.m_ToolResults = handledToolCalls;

        AI_PROVIDER_RESPONSE continuationResponse = m_Provider->Generate( continuationRequest );
        continuationResponse.m_RequestId = aRequest.m_RequestId;
        response = std::move( continuationResponse );
    }

    if( !handledToolCalls.empty() )
        response.m_ToolCalls = std::move( handledToolCalls );

    AI_TRACE_RECORD record;
    record.m_RequestId = aRequest.m_RequestId;
    record.m_Request = aRequest;
    record.m_Response = response;

    {
        std::lock_guard<std::mutex> lock( m_Mutex );
        m_TraceRecords.push_back( record );
    }

    return response;
}


bool AI_RUNTIME::Cancel( uint64_t aRequestId )
{
    std::lock_guard<std::mutex> lock( m_Mutex );

    for( AI_TRACE_RECORD& record : m_TraceRecords )
    {
        if( record.m_RequestId == aRequestId )
        {
            record.m_Cancelled = true;
            return true;
        }
    }

    return false;
}


void AI_RUNTIME::SetProvider( std::unique_ptr<AI_PROVIDER> aProvider )
{
    if( !aProvider )
        return;

    std::lock_guard<std::mutex> lock( m_Mutex );
    m_Provider = std::move( aProvider );
}


void AI_RUNTIME::SetToolCallHandler( AI_TOOL_CALL_HANDLER* aHandler )
{
    std::lock_guard<std::mutex> lock( m_Mutex );
    m_ToolCallHandler = aHandler;
}


std::vector<AI_TRACE_RECORD> AI_RUNTIME::TraceRecords() const
{
    std::lock_guard<std::mutex> lock( m_Mutex );
    return m_TraceRecords;
}


std::vector<AI_ACTIVITY_RECORD> AI_RUNTIME::ActivityRecords() const
{
    return m_ActivityLog->Records();
}
