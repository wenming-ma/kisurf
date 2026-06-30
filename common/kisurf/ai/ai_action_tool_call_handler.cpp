#include <kisurf/ai/ai_action_tool_call_handler.h>

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <utility>

namespace
{
std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


wxString deniedResultJson( const AI_TOOL_INVOCATION_RESULT& aResult )
{
    nlohmann::json payload = { { "action", toUtf8String( aResult.m_ActionName ) },
                               { "allowed", aResult.m_Allowed },
                               { "executed", aResult.m_Executed },
                               { "dry_run", false },
                               { "ok", false },
                               { "status", "denied" },
                               { "error_code", toUtf8String( aResult.m_ErrorCode ) },
                               { "message", toUtf8String( aResult.m_Message ) },
                               { "retryable", true },
                               { "retry_hint", "" },
                               { "valid_tools",
                                 nlohmann::json::array(
                                         { "kisurf_run_action",
                                           "kisurf_check_action" } ) },
                               { "expected_arguments",
                                 { { "type", "object" },
                                   { "required",
                                     nlohmann::json::array( { "action" } ) },
                                   { "properties",
                                     { { "action",
                                         { { "type", "string" },
                                           { "description",
                                             "Name from the current AI action catalog." } } },
                                       { "arguments",
                                         { { "type", "object" },
                                           { "description",
                                             "Optional action-specific arguments." } } },
                                       { "dry_run",
                                         { { "type", "boolean" },
                                           { "description",
                                             "Ignored by kisurf_run_action; use "
                                             "kisurf_check_action when you only need "
                                             "availability/policy facts." } } } } } } } };

    const std::string code = toUtf8String( aResult.m_ErrorCode );

    if( code == "unknown_tool" )
    {
        payload["retry_hint"] =
                "Use kisurf_run_action or kisurf_check_action with a JSON object "
                "containing an action string.";
    }
    else if( code == "malformed_arguments" )
    {
        payload["retry_hint"] =
                "Retry with a valid JSON object such as "
                "{\"action\":\"pcbnew.SomeAction\",\"arguments\":{}}.";
    }
    else if( code == "unknown_action" )
    {
        payload["retry_hint"] =
                "The requested action is not in the current action catalog. "
                "Call kisurf_get_workspace_view to inspect the current workspace "
                "and choose an available action or use current-board atomic/script "
                "tools for board edits.";
    }
    else
    {
        payload["retry_hint"] =
                "Fix the requested action or arguments according to error_code "
                "and retry only if the task still requires this action.";
    }

    return wxString::FromUTF8( payload.dump().c_str() );
}


AI_TOOL_INVOCATION_RESULT deniedResult( const AI_PROVIDER_REQUEST& aRequest,
                                        const AI_TOOL_CALL_RECORD& aToolCall,
                                        const wxString& aActionName,
                                        const wxString& aErrorCode,
                                        const wxString& aMessage )
{
    AI_TOOL_INVOCATION_RESULT result;
    result.m_RequestId = aRequest.m_RequestId;
    result.m_ToolCallId = aToolCall.m_ToolCallId;
    result.m_ActionName = aActionName;
    result.m_Allowed = false;
    result.m_Executed = false;
    result.m_ErrorCode = aErrorCode;
    result.m_Message = aMessage;
    result.m_ResultJson = deniedResultJson( result );
    return result;
}


bool isSupportedTool( const wxString& aToolName )
{
    return aToolName == wxS( "kisurf_run_action" )
           || aToolName == wxS( "kisurf_check_action" );
}


bool parseArguments( const AI_TOOL_CALL_RECORD& aToolCall, wxString& aActionName,
                     wxString& aArgumentsJson, bool& aDryRun, wxString& aError )
{
    try
    {
        nlohmann::json args = nlohmann::json::parse( toUtf8String( aToolCall.m_ArgumentsJson ) );

        if( !args.is_object() || !args.contains( "action" ) || !args["action"].is_string() )
        {
            aError = wxS( "Tool call arguments must include an action string." );
            return false;
        }

        aActionName = wxString::FromUTF8( args["action"].get_ref<const std::string&>().c_str() );

        if( args.contains( "dry_run" ) )
        {
            if( !args["dry_run"].is_boolean() )
            {
                aError = wxS( "Tool call dry_run argument must be a boolean." );
                return false;
            }

            aDryRun = args["dry_run"].get<bool>();
        }

        if( args.contains( "arguments" ) )
        {
            if( !args["arguments"].is_object() )
            {
                aError = wxS( "Tool call arguments field must be an object." );
                return false;
            }

            aArgumentsJson = wxString::FromUTF8( args["arguments"].dump().c_str() );
        }

        return true;
    }
    catch( const std::exception& e )
    {
        aError = wxString::FromUTF8( e.what() );
        return false;
    }
}
} // namespace


AI_ACTION_TOOL_CALL_HANDLER::AI_ACTION_TOOL_CALL_HANDLER(
        const AI_TOOL_EXECUTION_POLICY& aPolicy, AI_ACTION_RUNNER& aRunner,
        AI_ACTIVITY_LOG& aActivityLog,
        AI_ACTION_SUGGESTION_SINK aSuggestionSink ) :
        m_Policy( aPolicy ),
        m_Runner( aRunner ),
        m_ActivityLog( aActivityLog ),
        m_SuggestionSink( std::move( aSuggestionSink ) )
{
}


void AI_ACTION_TOOL_CALL_HANDLER::SetFallbackActions(
        std::vector<AI_ACTION_DESCRIPTOR> aActions )
{
    m_FallbackActions = std::move( aActions );
}


AI_TOOL_INVOCATION_RESULT AI_ACTION_TOOL_CALL_HANDLER::HandleToolCall(
        const AI_PROVIDER_REQUEST& aRequest, const AI_TOOL_CALL_RECORD& aToolCall )
{
    if( !isSupportedTool( aToolCall.m_ToolName ) )
    {
        return deniedResult( aRequest, aToolCall, aToolCall.m_ToolName,
                             wxS( "unknown_tool" ),
                             wxS( "Unsupported KiSurf tool call." ) );
    }

    wxString actionName;
    wxString argumentsJson;
    wxString parseError;
    bool     dryRun = false;

    if( !parseArguments( aToolCall, actionName, argumentsJson, dryRun, parseError ) )
    {
        return deniedResult( aRequest, aToolCall, actionName,
                             wxS( "malformed_arguments" ), parseError );
    }

    if( aToolCall.m_ToolName == wxS( "kisurf_check_action" ) )
        dryRun = true;
    else if( aToolCall.m_ToolName == wxS( "kisurf_run_action" ) )
        dryRun = false;

    const AI_ACTION_DESCRIPTOR* descriptor = findAction( aRequest, actionName );

    if( !descriptor )
    {
        return deniedResult( aRequest, aToolCall, actionName,
                             wxS( "unknown_action" ),
                             wxS( "Action is not present in the AI action catalog." ) );
    }

    AI_TOOL_INVOCATION_REQUEST request;
    request.m_RequestId = aRequest.m_RequestId;
    request.m_ToolCallId = aToolCall.m_ToolCallId;
    request.m_EditorKind = aRequest.m_EditorKind;
    request.m_ContextVersion = aRequest.m_ContextVersion.IsValid()
                               ? aRequest.m_ContextVersion
                               : aRequest.m_ContextSnapshot.m_Version;
    request.m_Action = *descriptor;
    request.m_ArgumentsJson = argumentsJson;
    request.m_DryRun = dryRun;

    AI_TOOL_EXECUTOR executor( m_Policy, m_Runner, m_ActivityLog );
    AI_TOOL_INVOCATION_RESULT result = executor.Invoke( request );

    return result;
}


const AI_ACTION_DESCRIPTOR* AI_ACTION_TOOL_CALL_HANDLER::findAction(
        const AI_PROVIDER_REQUEST& aRequest, const wxString& aActionName ) const
{
    for( const AI_ACTION_DESCRIPTOR& descriptor : aRequest.m_ContextSnapshot.m_Actions )
    {
        if( descriptor.m_Name == aActionName )
            return &descriptor;
    }

    for( const AI_ACTION_DESCRIPTOR& descriptor : m_FallbackActions )
    {
        if( descriptor.m_Name == aActionName )
            return &descriptor;
    }

    return nullptr;
}
