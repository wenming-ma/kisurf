#include <kisurf/ai/ai_tool_execution.h>

#include <nlohmann/json.hpp>
#include <string>

namespace
{
std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


wxString resultJson( const AI_TOOL_INVOCATION_RESULT& aResult, bool aDryRun,
                     const char* aStatus )
{
    nlohmann::json payload = { { "action", toUtf8String( aResult.m_ActionName ) },
                               { "allowed", aResult.m_Allowed },
                               { "executed", aResult.m_Executed },
                               { "dry_run", aDryRun },
                               { "status", aStatus },
                               { "error_code", toUtf8String( aResult.m_ErrorCode ) },
                               { "message", toUtf8String( aResult.m_Message ) } };

    return wxString::FromUTF8( payload.dump().c_str() );
}


AI_TOOL_INVOCATION_RESULT makeResult( const AI_TOOL_INVOCATION_REQUEST& aRequest )
{
    AI_TOOL_INVOCATION_RESULT result;
    result.m_RequestId = aRequest.m_RequestId;
    result.m_ToolCallId = aRequest.m_ToolCallId;
    result.m_ActionName = aRequest.m_Action.m_Name;
    return result;
}


void deny( AI_TOOL_INVOCATION_RESULT& aResult, const wxString& aCode,
           const wxString& aMessage )
{
    aResult.m_Allowed = false;
    aResult.m_Executed = false;
    aResult.m_ErrorCode = aCode;
    aResult.m_Message = aMessage;
}
} // namespace


void AI_TOOL_EXECUTION_POLICY::AllowAction( const wxString& aActionName )
{
    if( !aActionName.IsEmpty() )
        m_Allowlist.insert( aActionName );
}


bool AI_TOOL_EXECUTION_POLICY::IsAllowlisted( const wxString& aActionName ) const
{
    return m_Allowlist.find( aActionName ) != m_Allowlist.end();
}


AI_TOOL_INVOCATION_RESULT AI_TOOL_EXECUTION_POLICY::Evaluate(
        const AI_TOOL_INVOCATION_REQUEST& aRequest ) const
{
    AI_TOOL_INVOCATION_RESULT result = makeResult( aRequest );

    if( !aRequest.m_Action.IsValid() )
    {
        deny( result, wxS( "unknown_action" ), wxS( "Action descriptor is not valid." ) );
        return result;
    }

    if( !aRequest.m_Action.m_Enabled )
    {
        deny( result, wxS( "disabled_action" ), wxS( "Action is not currently enabled." ) );
        return result;
    }

    if( aRequest.m_Action.m_Safety == AI_ACTION_SAFETY::Destructive )
    {
        deny( result, wxS( "destructive_denied" ),
              wxS( "Destructive actions cannot be executed by model output." ) );
        return result;
    }

    if( aRequest.m_Action.m_Safety == AI_ACTION_SAFETY::Modifying )
    {
        deny( result, wxS( "requires_preview" ),
              wxS( "Modifying actions require preview and materialization policy." ) );
        return result;
    }

    if( aRequest.m_Action.m_Safety != AI_ACTION_SAFETY::ReadOnly
        && !IsAllowlisted( aRequest.m_Action.m_Name ) )
    {
        deny( result, wxS( "not_allowlisted" ), wxS( "Action is not on the AI allowlist." ) );
        return result;
    }

    result.m_Allowed = true;
    result.m_Executed = false;
    result.m_Message = wxS( "Action is allowed." );
    return result;
}


AI_TOOL_EXECUTOR::AI_TOOL_EXECUTOR( const AI_TOOL_EXECUTION_POLICY& aPolicy,
                                    AI_ACTION_RUNNER& aRunner,
                                    AI_ACTIVITY_LOG& aActivityLog ) :
        m_Policy( aPolicy ),
        m_Runner( aRunner ),
        m_ActivityLog( aActivityLog )
{
}


AI_TOOL_INVOCATION_RESULT AI_TOOL_EXECUTOR::Invoke( const AI_TOOL_INVOCATION_REQUEST& aRequest )
{
    recordRequest( aRequest );

    AI_TOOL_INVOCATION_RESULT result = m_Policy.Evaluate( aRequest );
    recordResult( AI_ACTIVITY_KIND::PolicyDecision, result );

    if( !result.m_Allowed )
    {
        result.m_ResultJson = resultJson( result, false, "denied" );
        recordResult( AI_ACTIVITY_KIND::ToolResult, result );
        return result;
    }

    if( aRequest.m_DryRun )
    {
        result.m_Executed = false;
        result.m_Message = wxS( "Dry run allowed." );
        result.m_ResultJson = resultJson( result, true, "allowed" );
        recordResult( AI_ACTIVITY_KIND::ToolResult, result );
        return result;
    }

    wxString error;

    if( m_Runner.RunActionByName( aRequest.m_Action.m_Name, error ) )
    {
        result.m_Executed = true;
        result.m_Message = wxS( "Action executed." );
        result.m_ResultJson = resultJson( result, false, "executed" );
    }
    else
    {
        result.m_Executed = false;
        result.m_ErrorCode = wxS( "runner_failed" );
        result.m_Message = error.IsEmpty() ? wxString( wxS( "Runner failed." ) ) : error;
        result.m_ResultJson = resultJson( result, false, "failed" );
    }

    recordResult( AI_ACTIVITY_KIND::ToolResult, result );
    return result;
}


void AI_TOOL_EXECUTOR::recordRequest( const AI_TOOL_INVOCATION_REQUEST& aRequest )
{
    AI_ACTIVITY_RECORD record;
    record.m_RequestId = aRequest.m_RequestId;
    record.m_ToolCallId = aRequest.m_ToolCallId;
    record.m_Kind = AI_ACTIVITY_KIND::ModelToolRequest;
    record.m_EditorKind = aRequest.m_EditorKind;
    record.m_ActionName = aRequest.m_Action.m_Name;
    record.m_ArgumentsJson = aRequest.m_ArgumentsJson;
    record.m_Message = aRequest.m_DryRun ? wxS( "dry run" ) : wxS( "execute" );
    m_ActivityLog.Append( record );
}


void AI_TOOL_EXECUTOR::recordResult( AI_ACTIVITY_KIND aKind,
                                     const AI_TOOL_INVOCATION_RESULT& aResult )
{
    AI_ACTIVITY_RECORD record;
    record.m_RequestId = aResult.m_RequestId;
    record.m_ToolCallId = aResult.m_ToolCallId;
    record.m_Kind = aKind;
    record.m_ActionName = aResult.m_ActionName;
    record.m_ResultJson = aResult.m_ResultJson;
    record.m_ErrorCode = aResult.m_ErrorCode;
    record.m_Allowed = aResult.m_Allowed;
    record.m_Executed = aResult.m_Executed;
    record.m_Message = aResult.m_Message;
    m_ActivityLog.Append( record );
}
