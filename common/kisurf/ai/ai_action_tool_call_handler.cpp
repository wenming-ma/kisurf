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
                               { "status", "denied" },
                               { "error_code", toUtf8String( aResult.m_ErrorCode ) },
                               { "message", toUtf8String( aResult.m_Message ) } };

    return wxString::FromUTF8( payload.dump().c_str() );
}


wxString fromJson( const nlohmann::json& aJson )
{
    return wxString::FromUTF8( aJson.dump().c_str() );
}


wxString actionPreviewArgumentsJson( const AI_TOOL_CALL_RECORD& aToolCall,
                                     const wxString& aActionName,
                                     const wxString& aArgumentsJson )
{
    nlohmann::json payload = {
        { "operation", "action_preview" },
        { "action", toUtf8String( aActionName ) },
        { "tool_call_id", toUtf8String( aToolCall.m_ToolCallId ) }
    };

    if( !aArgumentsJson.IsEmpty() )
        payload["arguments_json"] = toUtf8String( aArgumentsJson );

    return fromJson( payload );
}


AI_SUGGESTION_RECORD actionPreviewSuggestion(
        const AI_PROVIDER_REQUEST& aRequest,
        const AI_TOOL_CALL_RECORD& aToolCall,
        const AI_ACTION_DESCRIPTOR& aDescriptor,
        const wxString& aArgumentsJson )
{
    AI_CONTEXT_VERSION version = aRequest.m_ContextVersion.IsValid()
                                 ? aRequest.m_ContextVersion
                                 : aRequest.m_ContextSnapshot.m_Version;

    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_EditorKind = aRequest.m_EditorKind;
    suggestion.m_Kind = AI_SUGGESTION_KIND::Preview;
    suggestion.m_ContextVersion = version;
    suggestion.m_Title = wxS( "Preview action" );
    suggestion.m_Body = aDescriptor.m_FriendlyName.IsEmpty()
                        ? aDescriptor.m_Name
                        : aDescriptor.m_FriendlyName;
    suggestion.m_ContextKind = AiDynamicContextKind( aRequest.m_ContextSnapshot );
    suggestion.m_ContextDetailsJson =
            AiDynamicContextDetailsJson( aRequest.m_ContextSnapshot,
                                         wxS( "action_preview" ) );
    suggestion.m_ArgumentsJson =
            actionPreviewArgumentsJson( aToolCall, aDescriptor.m_Name, aArgumentsJson );
    suggestion.m_Fingerprint = wxS( "action|" ) + version.AsString() + wxS( "|" )
                               + aDescriptor.m_Name + wxS( "|" ) + aArgumentsJson;
    return suggestion;
}


wxString actionPreviewResultJson(
        const AI_TOOL_INVOCATION_RESULT& aResult,
        const std::optional<AI_SUGGESTION_RECORD>& aStoredSuggestion )
{
    nlohmann::json payload = {
        { "action", toUtf8String( aResult.m_ActionName ) },
        { "allowed", aResult.m_Allowed },
        { "executed", aResult.m_Executed },
        { "dry_run", true },
        { "status", "preview_ready" },
        { "preview_required", true },
        { "error_code", toUtf8String( aResult.m_ErrorCode ) },
        { "message", toUtf8String( aResult.m_Message ) }
    };

    if( aStoredSuggestion )
        payload["suggestion_id"] = aStoredSuggestion->m_Id;

    return fromJson( payload );
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

    // Model-originated action calls are preview-first. Materialization is handled
    // through explicit user-accepted edit paths rather than model arguments.
    if( aToolCall.m_ToolName == wxS( "kisurf_check_action" )
        || aToolCall.m_ToolName == wxS( "kisurf_run_action" ) )
    {
        dryRun = true;
    }

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

    if( result.m_Allowed && request.m_DryRun && m_SuggestionSink
        && aToolCall.m_ToolName == wxS( "kisurf_run_action" ) )
    {
        std::optional<AI_SUGGESTION_RECORD> stored =
                m_SuggestionSink( actionPreviewSuggestion(
                        aRequest, aToolCall, *descriptor, argumentsJson ) );

        if( stored )
        {
            result.m_Message = wxS( "Action preview suggestion created." );
            result.m_ResultJson = actionPreviewResultJson( result, stored );
        }
    }

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
