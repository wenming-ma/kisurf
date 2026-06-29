#include <kisurf/ai/ai_marshaled_tool_call_handler.h>

#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <utility>

namespace
{
AI_TOOL_INVOCATION_RESULT failureResult( const AI_PROVIDER_REQUEST& aRequest,
                                         const AI_TOOL_CALL_RECORD& aToolCall,
                                         const wxString& aCode,
                                         const wxString& aMessage )
{
    AI_TOOL_INVOCATION_RESULT result;
    result.m_RequestId = aRequest.m_RequestId;
    result.m_ToolCallId = aToolCall.m_ToolCallId;
    result.m_ActionName = aToolCall.m_ToolName;
    result.m_Allowed = false;
    result.m_Executed = false;
    result.m_ErrorCode = aCode;
    result.m_Message = aMessage;
    result.m_ResultJson =
            wxString::Format( wxS( "{\"status\":\"failed\","
                                   "\"error_code\":\"%s\","
                                   "\"message\":\"%s\"}" ),
                              aCode, aMessage );
    return result;
}


struct MARSHALLED_TOOL_CALL_STATE
{
    std::mutex                m_Mutex;
    std::condition_variable   m_Completed;
    bool                      m_Done = false;
    AI_TOOL_INVOCATION_RESULT m_Result;
};
} // namespace


AI_MARSHALLED_TOOL_CALL_HANDLER::AI_MARSHALLED_TOOL_CALL_HANDLER(
        AI_TOOL_CALL_HANDLER& aTarget,
        DISPATCH_TO_TARGET_THREAD aDispatchToTargetThread,
        IS_TARGET_THREAD aIsTargetThread ) :
        m_Target( aTarget ),
        m_DispatchToTargetThread( std::move( aDispatchToTargetThread ) ),
        m_IsTargetThread( std::move( aIsTargetThread ) )
{
}


AI_TOOL_INVOCATION_RESULT AI_MARSHALLED_TOOL_CALL_HANDLER::HandleToolCall(
        const AI_PROVIDER_REQUEST& aRequest,
        const AI_TOOL_CALL_RECORD& aToolCall )
{
    if( m_IsTargetThread && m_IsTargetThread() )
        return m_Target.HandleToolCall( aRequest, aToolCall );

    if( !m_DispatchToTargetThread )
    {
        return failureResult( aRequest, aToolCall, wxS( "marshal_unavailable" ),
                              wxS( "No target-thread dispatcher is available." ) );
    }

    auto state = std::make_shared<MARSHALLED_TOOL_CALL_STATE>();

    m_DispatchToTargetThread(
            [this, state, aRequest, aToolCall]()
            {
                AI_TOOL_INVOCATION_RESULT result;

                try
                {
                    result = m_Target.HandleToolCall( aRequest, aToolCall );
                }
                catch( const std::exception& e )
                {
                    result = failureResult(
                            aRequest, aToolCall, wxS( "tool_handler_exception" ),
                            wxString::FromUTF8( e.what() ) );
                }
                catch( ... )
                {
                    result = failureResult(
                            aRequest, aToolCall, wxS( "tool_handler_exception" ),
                            wxS( "Tool handler raised an unknown exception." ) );
                }

                {
                    std::lock_guard<std::mutex> lock( state->m_Mutex );
                    state->m_Result = std::move( result );
                    state->m_Done = true;
                }

                state->m_Completed.notify_one();
            } );

    std::unique_lock<std::mutex> lock( state->m_Mutex );
    state->m_Completed.wait( lock, [&]() { return state->m_Done; } );
    return state->m_Result;
}
