#include <kisurf/ai/ai_agent_panel.h>
#include <kisurf/ai/ai_action_tool_call_handler.h>
#include <kisurf/ai/ai_agent_panel_semantic.h>
#include <kisurf/ai/ai_marshaled_tool_call_handler.h>
#include <kisurf/ai/ai_model_config.h>
#include <kisurf/ai/ai_model_settings_dialog_base.h>
#include <kisurf/ai/ai_provider.h>
#include <kisurf/ai/ai_python_local_worker.h>
#include <kisurf/ai/ai_semantic_tool_call_handler.h>
#include <kisurf/ai/ai_session_tool_call_handler.h>
#include <kisurf/ai/ai_suggestion_operations.h>

#include <json_common.h>

#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <wx/app.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/defs.h>
#include <wx/dirdlg.h>
#include <wx/msgdlg.h>
#include <wx/notebook.h>
#include <wx/textctrl.h>
#include <wx/thread.h>

namespace
{
wxString suggestionStatusText( AI_SUGGESTION_STATUS aStatus )
{
    switch( aStatus )
    {
    case AI_SUGGESTION_STATUS::Pending:
        return wxS( "Pending" );

    case AI_SUGGESTION_STATUS::Previewing:
        return wxS( "Previewing" );

    case AI_SUGGESTION_STATUS::Accepted:
        return wxS( "Accepted" );

    case AI_SUGGESTION_STATUS::Rejected:
        return wxS( "Rejected" );

    case AI_SUGGESTION_STATUS::Expired:
        return wxS( "Expired" );

    case AI_SUGGESTION_STATUS::Superseded:
        return wxS( "Superseded" );

    case AI_SUGGESTION_STATUS::Abandoned:
        return wxS( "Abandoned" );

    case AI_SUGGESTION_STATUS::Cancelled:
        return wxS( "Cancelled" );
    }

    return wxS( "Unknown" );
}


wxString observabilityKindText( AI_AGENT_OBSERVABILITY_KIND aKind )
{
    switch( aKind )
    {
    case AI_AGENT_OBSERVABILITY_KIND::UserInput:
        return wxS( "User" );

    case AI_AGENT_OBSERVABILITY_KIND::ModelInput:
        return wxS( "Input" );

    case AI_AGENT_OBSERVABILITY_KIND::ModelToolCall:
        return wxS( "Tool" );

    case AI_AGENT_OBSERVABILITY_KIND::ToolResult:
        return wxS( "Result" );

    case AI_AGENT_OBSERVABILITY_KIND::ModelOutput:
        return wxS( "Output" );

    case AI_AGENT_OBSERVABILITY_KIND::Suggestion:
        return wxS( "Suggestion" );

    case AI_AGENT_OBSERVABILITY_KIND::NextActionReplay:
        return wxS( "Replay" );

    case AI_AGENT_OBSERVABILITY_KIND::System:
        return wxS( "System" );
    }

    return wxS( "System" );
}


wxString htmlEscape( const wxString& aText )
{
    wxString escaped;

    for( wxUniChar ch : aText )
    {
        switch( ch.GetValue() )
        {
        case '&':
            escaped << wxS( "&amp;" );
            break;

        case '<':
            escaped << wxS( "&lt;" );
            break;

        case '>':
            escaped << wxS( "&gt;" );
            break;

        case '"':
            escaped << wxS( "&quot;" );
            break;

        case '\'':
            escaped << wxS( "&#39;" );
            break;

        case '\n':
            escaped << wxS( "<br>" );
            break;

        case '\r':
            break;

        default:
            escaped << ch;
            break;
        }
    }

    return escaped;
}


wxString transcriptRoleLabel( const wxString& aRole )
{
    if( aRole.CmpNoCase( wxS( "user" ) ) == 0 )
        return wxS( "You" );

    if( aRole.CmpNoCase( wxS( "system" ) ) == 0 )
        return wxS( "System" );

    return wxS( "Agent" );
}


wxString transcriptCardColor( const wxString& aRole )
{
    if( aRole.CmpNoCase( wxS( "user" ) ) == 0 )
        return wxS( "#f3f6fb" );

    if( aRole.CmpNoCase( wxS( "system" ) ) == 0 )
        return wxS( "#f7f7f7" );

    return wxS( "#eef7f1" );
}


std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


wxString fromUtf8String( const std::string& aText )
{
    return wxString::FromUTF8( aText.c_str() );
}


std::optional<nlohmann::json> parseEntryDetails( const wxString& aDetailsJson )
{
    if( aDetailsJson.IsEmpty() )
        return std::nullopt;

    nlohmann::json details =
            nlohmann::json::parse( toUtf8String( aDetailsJson ), nullptr, false );

    if( details.is_discarded() || !details.is_object() )
        return std::nullopt;

    return details;
}


std::optional<wxString> actionPreviewActionName( const AI_SUGGESTION_RECORD& aSuggestion )
{
    if( aSuggestion.m_ArgumentsJson.IsEmpty() )
        return std::nullopt;

    nlohmann::json args = nlohmann::json::parse(
            toUtf8String( aSuggestion.m_ArgumentsJson ), nullptr, false );

    if( args.is_discarded() || !args.is_object()
        || !args.contains( "operation" ) || !args["operation"].is_string()
        || args["operation"].get<std::string>() != "action_preview"
        || !args.contains( "action" ) || !args["action"].is_string() )
    {
        return std::nullopt;
    }

    wxString action = fromUtf8String( args["action"].get<std::string>() );
    action.Trim( true ).Trim( false );

    if( action.IsEmpty() )
        return std::nullopt;

    return action;
}


wxString jsonStringField( const nlohmann::json& aJson, const char* aName )
{
    auto it = aJson.find( aName );
    return it != aJson.end() && it->is_string()
           ? fromUtf8String( it->get<std::string>() )
           : wxString();
}


wxString jsonIntFieldText( const nlohmann::json& aJson, const char* aName )
{
    auto it = aJson.find( aName );
    return it != aJson.end() && it->is_number_integer()
           ? wxString::Format( wxS( "%lld" ), it->get<long long>() )
           : wxString();
}


wxString jsonBoolFieldText( const nlohmann::json& aJson, const char* aName )
{
    auto it = aJson.find( aName );

    if( it == aJson.end() || !it->is_boolean() )
        return wxString();

    return it->get<bool>() ? wxString( wxS( "true" ) )
                           : wxString( wxS( "false" ) );
}


wxString compactJsonText( const wxString& aText )
{
    if( aText.IsEmpty() )
        return wxString();

    nlohmann::json value = nlohmann::json::parse( toUtf8String( aText ), nullptr, false );

    if( value.is_discarded() )
        return aText;

    return fromUtf8String( value.dump() );
}


void appendDetailLine( wxString& aText, const wxString& aLine )
{
    if( aLine.IsEmpty() )
        return;

    if( !aText.IsEmpty() )
        aText << wxS( "\n" );

    aText << aLine;
}


void appendDetailField( wxString& aLine, const wxString& aName,
                        const wxString& aValue )
{
    if( aValue.IsEmpty() )
        return;

    aLine << wxS( " " ) << aName << wxS( "=" ) << aValue;
}


void appendPayloadLine( wxString& aText, const wxString& aName,
                        const wxString& aValue )
{
    if( aValue.IsEmpty() )
        return;

    appendDetailLine( aText, aName + wxS( ": " ) + compactJsonText( aValue ) );
}


wxString modelInputDetailLine( const nlohmann::json& aDetails )
{
    wxString line = wxS( "details:" );
    appendDetailField( line, wxS( "request" ),
                       jsonIntFieldText( aDetails, "request_id" ) );
    appendDetailField( line, wxS( "editor" ),
                       jsonStringField( aDetails, "editor" ) );

    auto contextIt = aDetails.find( "context" );

    if( contextIt != aDetails.end() && contextIt->is_object() )
    {
        appendDetailField( line, wxS( "selected" ),
                           jsonIntFieldText( *contextIt, "selected_count" ) );
        appendDetailField( line, wxS( "visible" ),
                           jsonIntFieldText( *contextIt, "visible_count" ) );
        appendDetailField( line, wxS( "anchors" ),
                           jsonIntFieldText( *contextIt, "anchor_count" ) );
        appendDetailField( line, wxS( "panels" ),
                           jsonIntFieldText( *contextIt, "panel_state_count" ) );
        appendDetailField( line, wxS( "tool_state" ),
                           jsonStringField( *contextIt, "tool_state_kind" ) );

        auto visualIt = contextIt->find( "visual" );

        if( visualIt != contextIt->end() && visualIt->is_object() )
        {
            wxString visual = jsonStringField( *visualIt, "source" );
            wxString width = jsonIntFieldText( *visualIt, "width_px" );
            wxString height = jsonIntFieldText( *visualIt, "height_px" );

            if( !visual.IsEmpty() && !width.IsEmpty() && !height.IsEmpty() )
                visual << wxS( " " ) << width << wxS( "x" ) << height;

            appendDetailField( line, wxS( "visual" ), visual );
        }
    }

    appendDetailField( line, wxS( "tool_results" ),
                       jsonIntFieldText( aDetails, "tool_results_count" ) );
    return line == wxS( "details:" ) ? wxString() : line;
}


wxString activityDetailLine( const nlohmann::json& aDetails )
{
    wxString line = wxS( "details:" );
    appendDetailField( line, wxS( "request" ),
                       jsonIntFieldText( aDetails, "request_id" ) );
    appendDetailField( line, wxS( "kind" ),
                       jsonStringField( aDetails, "kind" ) );
    appendDetailField( line, wxS( "action" ),
                       jsonStringField( aDetails, "action" ) );
    appendDetailField( line, wxS( "allowed" ),
                       jsonBoolFieldText( aDetails, "allowed" ) );
    appendDetailField( line, wxS( "executed" ),
                       jsonBoolFieldText( aDetails, "executed" ) );
    appendDetailField( line, wxS( "error" ),
                       jsonStringField( aDetails, "error_code" ) );
    return line == wxS( "details:" ) ? wxString() : line;
}


wxString activityPayloadLines( const nlohmann::json& aDetails )
{
    wxString text;
    appendPayloadLine( text, wxS( "arguments" ),
                       jsonStringField( aDetails, "arguments_json" ) );
    appendPayloadLine( text, wxS( "result" ),
                       jsonStringField( aDetails, "result_json" ) );
    appendPayloadLine( text, wxS( "message" ),
                       jsonStringField( aDetails, "message" ) );
    return text;
}


wxString toolCallDebugLine( const nlohmann::json& aToolCall )
{
    wxString line = wxS( "tool call:" );
    wxString id = jsonStringField( aToolCall, "id" );
    wxString name = jsonStringField( aToolCall, "name" );

    if( !id.IsEmpty() )
        line << wxS( " " ) << id;

    if( !name.IsEmpty() )
        line << wxS( " " ) << name;

    appendDetailField( line, wxS( "allowed" ),
                       jsonBoolFieldText( aToolCall, "allowed" ) );
    appendDetailField( line, wxS( "executed" ),
                       jsonBoolFieldText( aToolCall, "executed" ) );
    return line == wxS( "tool call:" ) ? wxString() : line;
}


void appendToolCallDebugLines( wxString& aText, const nlohmann::json& aToolCall )
{
    appendDetailLine( aText, toolCallDebugLine( aToolCall ) );
    appendPayloadLine( aText, wxS( "arguments" ),
                       jsonStringField( aToolCall, "arguments_json" ) );
    appendPayloadLine( aText, wxS( "result" ),
                       jsonStringField( aToolCall, "result_json" ) );
    appendPayloadLine( aText, wxS( "message" ),
                       jsonStringField( aToolCall, "message" ) );
    appendPayloadLine( aText, wxS( "error" ),
                       jsonStringField( aToolCall, "error_code" ) );
}


wxString modelOutputDetailLine( const nlohmann::json& aDetails )
{
    wxString line = wxS( "details:" );
    appendDetailField( line, wxS( "request" ),
                       jsonIntFieldText( aDetails, "request_id" ) );
    appendDetailField( line, wxS( "body_length" ),
                       jsonIntFieldText( aDetails, "body_length" ) );
    appendDetailField( line, wxS( "tool_calls" ),
                       jsonIntFieldText( aDetails, "tool_call_count" ) );
    appendDetailField( line, wxS( "cancelled" ),
                       jsonBoolFieldText( aDetails, "cancelled" ) );
    return line == wxS( "details:" ) ? wxString() : line;
}


wxString modelOutputPayloadLines( const nlohmann::json& aDetails )
{
    wxString text;
    appendPayloadLine( text, wxS( "body" ), jsonStringField( aDetails, "body" ) );

    auto toolCallsIt = aDetails.find( "tool_calls" );

    if( toolCallsIt != aDetails.end() && toolCallsIt->is_array() )
    {
        for( const nlohmann::json& toolCall : *toolCallsIt )
        {
            if( toolCall.is_object() )
                appendToolCallDebugLines( text, toolCall );
        }
    }

    return text;
}


wxString observabilityDetailLine( const AI_AGENT_OBSERVABILITY_ENTRY& aEntry )
{
    std::optional<nlohmann::json> details = parseEntryDetails( aEntry.m_DetailsJson );

    if( !details )
        return wxString();

    wxString text;

    switch( aEntry.m_Kind )
    {
    case AI_AGENT_OBSERVABILITY_KIND::ModelInput:
        return modelInputDetailLine( *details );

    case AI_AGENT_OBSERVABILITY_KIND::ModelOutput:
        appendDetailLine( text, modelOutputDetailLine( *details ) );
        appendDetailLine( text, modelOutputPayloadLines( *details ) );
        return text;

    case AI_AGENT_OBSERVABILITY_KIND::UserInput:
    case AI_AGENT_OBSERVABILITY_KIND::ModelToolCall:
    case AI_AGENT_OBSERVABILITY_KIND::ToolResult:
    case AI_AGENT_OBSERVABILITY_KIND::System:
        appendDetailLine( text, activityDetailLine( *details ) );
        appendDetailLine( text, activityPayloadLines( *details ) );
        return text;

    case AI_AGENT_OBSERVABILITY_KIND::Suggestion:
        return wxString();

    case AI_AGENT_OBSERVABILITY_KIND::NextActionReplay:
        return aEntry.m_DetailsJson;
    }

    return wxString();
}


wxString compactObservabilitySummary(
        const std::vector<AI_AGENT_OBSERVABILITY_ENTRY>& aEntries,
        size_t aMaxLines )
{
    wxString summary;

    if( aEntries.empty() || aMaxLines == 0 )
        return summary;

    const size_t start = aEntries.size() > aMaxLines ? aEntries.size() - aMaxLines
                                                     : 0;

    for( size_t ii = start; ii < aEntries.size(); ++ii )
    {
        const AI_AGENT_OBSERVABILITY_ENTRY& entry = aEntries[ii];

        if( !summary.IsEmpty() )
            summary << wxS( "\n" );

        summary << wxS( "#" ) << entry.m_Sequence << wxS( " " )
                << observabilityKindText( entry.m_Kind ) << wxS( ": " )
                << entry.m_Title;

        if( !entry.m_Summary.IsEmpty() )
            summary << wxS( " - " ) << entry.m_Summary;
    }

    return summary;
}


AI_SEMANTIC_UI_ACTION_RESULT semanticActionError( wxString aCode, wxString aMessage )
{
    AI_SEMANTIC_UI_ACTION_RESULT result;
    result.m_Success = false;
    result.m_ErrorCode = std::move( aCode );
    result.m_Message = RedactSemanticUiText( aMessage );
    return result;
}


AI_SEMANTIC_UI_ACTION_RESULT semanticActionOk( wxString aFocusedNode = wxEmptyString )
{
    AI_SEMANTIC_UI_ACTION_RESULT result;
    result.m_Success = true;
    result.m_FocusedNodeId = std::move( aFocusedNode );
    return result;
}


constexpr std::array<AI_MODEL_PROVIDER_KIND, 2> MODEL_PROVIDER_ORDER = {
    { AI_MODEL_PROVIDER_KIND::OpenAiCompatible,
      AI_MODEL_PROVIDER_KIND::AnthropicCompatible }
};


int selectionForProviderKind( AI_MODEL_PROVIDER_KIND aKind )
{
    for( size_t ii = 0; ii < MODEL_PROVIDER_ORDER.size(); ++ii )
    {
        if( MODEL_PROVIDER_ORDER[ii] == aKind )
            return static_cast<int>( ii );
    }

    return 0;
}


AI_MODEL_PROVIDER_KIND providerKindForSelection( int aSelection )
{
    if( aSelection >= 0
        && static_cast<size_t>( aSelection ) < MODEL_PROVIDER_ORDER.size() )
    {
        return MODEL_PROVIDER_ORDER[static_cast<size_t>( aSelection )];
    }

    return AI_MODEL_PROVIDER_KIND::OpenAiCompatible;
}


class AI_MODEL_SETTINGS_DIALOG : public AI_MODEL_SETTINGS_DIALOG_BASE
{
public:
    AI_MODEL_SETTINGS_DIALOG( wxWindow* aParent, AI_MODEL_CONFIG aConfig ) :
            AI_MODEL_SETTINGS_DIALOG_BASE( aParent ),
            m_Config( std::move( aConfig ) )
    {
        m_Config.Normalize();

        for( AI_MODEL_PROVIDER_KIND kind : MODEL_PROVIDER_ORDER )
            m_ProviderChoice->Append( AiModelProviderKindLabel( kind ) );

        m_ProviderChoice->SetSelection( selectionForProviderKind( m_Config.m_ProviderKind ) );
        m_BaseUrl->SetValue( m_Config.m_BaseUrl );
        m_Model->SetValue( m_Config.m_Model );
        m_ApiKey->SetValue( m_Config.m_ApiKey );
        m_ResearchFolder->SetValue( m_Config.m_ResearchFolder );

        m_ProviderChoice->Bind( wxEVT_CHOICE, [this]( wxCommandEvent& )
        {
            const AI_MODEL_PROVIDER_KIND selected =
                    providerKindForSelection( m_ProviderChoice->GetSelection() );

            if( selected == m_Config.m_ProviderKind )
                m_ApiKey->SetValue( m_Config.m_ApiKey );
            else
                m_ApiKey->Clear();
        } );

        m_ResearchFolderBrowse->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& )
        {
            wxDirDialog dialog( this, _( "Select research folder" ),
                                m_ResearchFolder->GetValue(),
                                wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST );

            if( dialog.ShowModal() == wxID_OK )
                m_ResearchFolder->SetValue( dialog.GetPath() );
        } );

        SetMinSize( FromDIP( wxSize( 520, -1 ) ) );
        GetSizer()->Fit( this );
        Layout();
    }

    AI_MODEL_CONFIG Config() const
    {
        AI_MODEL_CONFIG config;
        config.m_ProviderKind = providerKindForSelection( m_ProviderChoice->GetSelection() );
        config.m_BaseUrl = m_BaseUrl->GetValue();
        config.m_Model = m_Model->GetValue();
        config.m_ApiKey = m_ApiKey->GetValue();
        config.m_ResearchFolder = m_ResearchFolder->GetValue();
        config.Normalize();
        return config;
    }

private:
    AI_MODEL_CONFIG m_Config;
};
} // namespace


wxString AiAgentWorkspaceContextTitle( AI_AGENT_WORKSPACE_CONTEXT_KIND aMode )
{
    switch( aMode )
    {
    case AI_AGENT_WORKSPACE_CONTEXT_KIND::General:
        return wxS( "General" );

    case AI_AGENT_WORKSPACE_CONTEXT_KIND::Routing:
        return wxS( "Routing" );

    case AI_AGENT_WORKSPACE_CONTEXT_KIND::ViaPlacement:
        return wxS( "Via placement" );

    case AI_AGENT_WORKSPACE_CONTEXT_KIND::FootprintPlacement:
        return wxS( "Footprint placement" );

    case AI_AGENT_WORKSPACE_CONTEXT_KIND::ZoneCreation:
        return wxS( "Zone creation" );

    case AI_AGENT_WORKSPACE_CONTEXT_KIND::SelectionEdit:
        return wxS( "Selection edit" );
    }

    return wxS( "General" );
}


AI_AGENT_WORKSPACE_CONTEXT_KIND AiAgentWorkspaceContextForToolState( AI_TOOL_STATE_KIND aKind )
{
    switch( aKind )
    {
    case AI_TOOL_STATE_KIND::RoutingTrack:
        return AI_AGENT_WORKSPACE_CONTEXT_KIND::Routing;

    case AI_TOOL_STATE_KIND::PlacingVia:
        return AI_AGENT_WORKSPACE_CONTEXT_KIND::ViaPlacement;

    case AI_TOOL_STATE_KIND::PlacingFootprint:
        return AI_AGENT_WORKSPACE_CONTEXT_KIND::FootprintPlacement;

    case AI_TOOL_STATE_KIND::DrawingZone:
        return AI_AGENT_WORKSPACE_CONTEXT_KIND::ZoneCreation;

    case AI_TOOL_STATE_KIND::MovingSelection:
        return AI_AGENT_WORKSPACE_CONTEXT_KIND::SelectionEdit;

    case AI_TOOL_STATE_KIND::Unknown:
    case AI_TOOL_STATE_KIND::Idle:
    case AI_TOOL_STATE_KIND::Selecting:
        return AI_AGENT_WORKSPACE_CONTEXT_KIND::General;
    }

    return AI_AGENT_WORKSPACE_CONTEXT_KIND::General;
}


wxString runtimeStreamEventPendingText( const AI_RUNTIME_STREAM_EVENT& aEvent )
{
    switch( aEvent.m_Kind )
    {
    case AI_RUNTIME_STREAM_EVENT_KIND::RequestStarted:
        return wxS( "Agent thinking..." );

    case AI_RUNTIME_STREAM_EVENT_KIND::ProviderResponse:
        if( !aEvent.m_Response.m_ToolCalls.empty() )
        {
            return wxString::Format(
                    wxS( "Planning %llu tool call(s)..." ),
                    static_cast<unsigned long long>(
                            aEvent.m_Response.m_ToolCalls.size() ) );
        }

        return wxS( "Reading model response..." );

    case AI_RUNTIME_STREAM_EVENT_KIND::TextDelta:
        return aEvent.m_TextDelta;

    case AI_RUNTIME_STREAM_EVENT_KIND::ToolCallStarted:
        if( !aEvent.m_ToolCall.m_ToolName.IsEmpty() )
        {
            return wxString::Format( wxS( "Executing tool: %s" ),
                                     aEvent.m_ToolCall.m_ToolName );
        }

        return wxS( "Executing tool..." );

    case AI_RUNTIME_STREAM_EVENT_KIND::ToolCallFinished:
        if( !aEvent.m_ToolCall.m_ToolName.IsEmpty() )
        {
            wxString text = wxString::Format(
                    wxS( "Finished tool: %s" ), aEvent.m_ToolCall.m_ToolName );

            if( !aEvent.m_ToolResult.m_Message.IsEmpty() )
                text << wxS( " - " ) << aEvent.m_ToolResult.m_Message;

            return text;
        }

        return wxS( "Finished tool." );

    case AI_RUNTIME_STREAM_EVENT_KIND::FinalResponse:
        if( !aEvent.m_TextDelta.IsEmpty() )
            return aEvent.m_TextDelta;

        return wxS( "Finalizing response..." );
    }

    return wxS( "Agent working..." );
}


wxString AiAgentStreamingAssistantTextAfterEvent(
        const wxString& aCurrentText,
        const AI_RUNTIME_STREAM_EVENT& aEvent )
{
    if( aEvent.m_Kind == AI_RUNTIME_STREAM_EVENT_KIND::TextDelta )
        return aCurrentText + aEvent.m_TextDelta;

    wxString eventText = runtimeStreamEventPendingText( aEvent );

    if( aEvent.m_Kind == AI_RUNTIME_STREAM_EVENT_KIND::FinalResponse
        && aEvent.m_TextDelta.IsEmpty() && !aCurrentText.IsEmpty() )
    {
        return aCurrentText;
    }

    if( aEvent.m_Kind == AI_RUNTIME_STREAM_EVENT_KIND::FinalResponse )
        return eventText;

    const bool appendProgress =
            aEvent.m_Kind == AI_RUNTIME_STREAM_EVENT_KIND::ToolCallStarted
            || aEvent.m_Kind == AI_RUNTIME_STREAM_EVENT_KIND::ToolCallFinished
            || ( aEvent.m_Kind == AI_RUNTIME_STREAM_EVENT_KIND::ProviderResponse
                 && !aEvent.m_Response.m_ToolCalls.empty() );

    if( aCurrentText.IsEmpty() )
        return eventText;

    if( !appendProgress || eventText.IsEmpty() )
        return aCurrentText;

    if( aCurrentText.Contains( eventText ) )
        return aCurrentText;

    wxString combined = aCurrentText;
    combined << wxS( "\n\n" ) << eventText;
    return combined;
}


wxString AiAgentTranscriptHtml( const std::vector<AI_AGENT_MESSAGE>& aMessages )
{
    wxString html;
    html << wxS( "<html><body bgcolor=\"#ffffff\">" );
    html << wxS( "<font face=\"Segoe UI, Arial, sans-serif\" size=\"2\">" );

    if( aMessages.empty() )
    {
        html << wxS( "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"8\">" )
             << wxS( "<tr><td bgcolor=\"#f7f7f7\">" )
             << wxS( "<font color=\"#666666\">" )
             << wxS( "Ask the Agent about the current board or request a previewed edit." )
             << wxS( "</font></td></tr></table>" );
    }

    for( const AI_AGENT_MESSAGE& message : aMessages )
    {
        const wxString label = transcriptRoleLabel( message.m_Role );
        const wxString color = transcriptCardColor( message.m_Role );

        html << wxS( "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"8\">" )
             << wxS( "<tr><td bgcolor=\"" ) << color << wxS( "\">" )
             << wxS( "<b>" ) << htmlEscape( label ) << wxS( "</b><br>" )
             << htmlEscape( message.m_Text )
             << wxS( "</td></tr></table><br>" );
    }

    html << wxS( "</font></body></html>" );
    return html;
}


wxString AiAgentSuggestionSummary( const AI_SUGGESTION_RECORD& aSuggestion )
{
    wxString summary;
    summary << wxS( "#" ) << aSuggestion.m_Id << wxS( " [" )
            << suggestionStatusText( aSuggestion.m_Status ) << wxS( "] " )
            << ( aSuggestion.m_ContextKind.IsEmpty()
                         ? wxString()
                         : wxS( "[" ) + aSuggestion.m_ContextKind + wxS( "] " ) )
            << aSuggestion.m_Title;
    return summary;
}


wxString AiAgentObservabilityEntryText( const AI_AGENT_OBSERVABILITY_ENTRY& aEntry )
{
    wxString text;
    text << wxS( "#" ) << aEntry.m_Sequence << wxS( " " )
         << observabilityKindText( aEntry.m_Kind ) << wxS( ": " )
         << aEntry.m_Title;

    if( !aEntry.m_ToolCallId.IsEmpty() )
        text << wxS( " (" ) << aEntry.m_ToolCallId << wxS( ")" );

    if( aEntry.m_Allowed || aEntry.m_Executed )
    {
        text << wxS( " [" )
             << ( aEntry.m_Allowed ? wxS( "allowed" ) : wxS( "denied" ) )
             << wxS( "/" )
             << ( aEntry.m_Executed ? wxS( "executed" ) : wxS( "not executed" ) )
             << wxS( "]" );
    }

    if( !aEntry.m_Summary.IsEmpty() )
        text << wxS( "\n" ) << aEntry.m_Summary;

    if( !aEntry.m_ErrorCode.IsEmpty() )
        text << wxS( "\nerror: " ) << aEntry.m_ErrorCode;

    wxString detailLine = observabilityDetailLine( aEntry );

    if( !detailLine.IsEmpty() )
        text << wxS( "\n" ) << detailLine;

    return text;
}


wxString AiAgentComposerStatusText( const AI_AGENT_COMPOSER_STATUS_VIEW& aView )
{
    if( aView.m_ChatAgentBusy )
        return wxS( "Agent thinking" );

    if( aView.m_LatestRequestId != 0 && aView.m_LastRequestCancelled )
    {
        return wxString::Format( wxS( "Stopped request #%llu" ),
                                 static_cast<unsigned long long>(
                                         aView.m_LatestRequestId ) );
    }

    if( aView.m_LatestRequestId != 0 )
    {
        return wxString::Format( wxS( "Last response #%llu" ),
                                 static_cast<unsigned long long>(
                                         aView.m_LatestRequestId ) );
    }

    if( aView.m_HasActiveSuggestion )
        return wxS( "Preview ready" );

    if( aView.m_BackgroundAgentBusy )
        return wxS( "Background Agent thinking" );

    if( aView.m_BackgroundAgentEnabled )
        return wxS( "Background Agent on" );

    if( aView.m_InputHasText )
        return wxS( "Ready to send" );

    return wxS( "Ask about the current board" );
}


AI_AGENT_PREVIEW_SHORTCUT_ACTION AiAgentPreviewShortcutAction(
        const AI_AGENT_PREVIEW_SHORTCUT_VIEW& aView, int aKeyCode )
{
    if( !aView.m_BackgroundAgentEnabled || !aView.m_HasActiveSuggestion )
        return AI_AGENT_PREVIEW_SHORTCUT_ACTION::Ignore;

    if( aView.m_FocusInsideAgentPanel || aView.m_HasModifier )
        return AI_AGENT_PREVIEW_SHORTCUT_ACTION::Ignore;

    if( aKeyCode == WXK_TAB || aKeyCode == WXK_RETURN || aKeyCode == WXK_NUMPAD_ENTER )
        return AI_AGENT_PREVIEW_SHORTCUT_ACTION::Accept;

    if( aKeyCode == WXK_ESCAPE )
        return AI_AGENT_PREVIEW_SHORTCUT_ACTION::Reject;

    return AI_AGENT_PREVIEW_SHORTCUT_ACTION::Ignore;
}


AI_AGENT_PREVIEW_SHORTCUT_ACTION AiAgentPreviewPointerAction(
        const AI_AGENT_PREVIEW_SHORTCUT_VIEW& aView )
{
    if( !aView.m_BackgroundAgentEnabled || !aView.m_HasActiveSuggestion )
        return AI_AGENT_PREVIEW_SHORTCUT_ACTION::Ignore;

    if( aView.m_FocusInsideAgentPanel )
        return AI_AGENT_PREVIEW_SHORTCUT_ACTION::Ignore;

    return AI_AGENT_PREVIEW_SHORTCUT_ACTION::Reject;
}


bool AiAgentSuggestionTargetsWorkspacePreview( const AI_SUGGESTION_RECORD& aSuggestion )
{
    if( !aSuggestion.m_PreviewObjects.empty() )
        return true;

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( aSuggestion.m_ArgumentsJson );

    return operation && ( operation->IsAnchorFocusPreview()
                          || operation->IsRouteSegmentPreview()
                          || operation->IsPlaceViaPreview()
                          || operation->IsCreateShapePreview()
                          || operation->IsCreateCopperZonePreview() );
}


bool AiAgentSuggestionTargetsAutomaticPreview( const AI_SUGGESTION_RECORD& aSuggestion )
{
    if( AiAgentSuggestionTargetsWorkspacePreview( aSuggestion ) )
        return true;

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( aSuggestion.m_ArgumentsJson );

    return operation && operation->IsPanelFillColumnPreview();
}


bool AiAgentShouldAutoPreviewBackgroundSuggestion(
        const AI_AGENT_BACKGROUND_PREVIEW_VIEW& aView )
{
    return aView.m_BackgroundAgentEnabled && aView.m_HasNewSuggestion
           && aView.m_HasPreviewHandler && aView.m_CanPreviewSuggestion;
}


AI_AGENT_BACKGROUND_UPDATE_ACTION AiAgentBackgroundUpdateAction(
        const AI_AGENT_BACKGROUND_UPDATE_VIEW& aView )
{
    if( !aView.m_BackgroundAgentEnabled || !aView.m_HasContextProvider )
        return AI_AGENT_BACKGROUND_UPDATE_ACTION::Ignore;

    if( aView.m_UpdateInFlight )
        return AI_AGENT_BACKGROUND_UPDATE_ACTION::DropWhileBusy;

    return AI_AGENT_BACKGROUND_UPDATE_ACTION::QueueAsync;
}


bool AiAgentReviewCommandTargetsChatSession(
        bool aHasActiveSuggestion, bool aHasPendingChatSession )
{
    wxUnusedVar( aHasActiveSuggestion );
    return aHasPendingChatSession;
}


bool activeToolStateNeedsBackgroundTick( AI_TOOL_STATE_KIND aKind )
{
    return aKind == AI_TOOL_STATE_KIND::RoutingTrack
           || aKind == AI_TOOL_STATE_KIND::PlacingVia
           || aKind == AI_TOOL_STATE_KIND::PlacingFootprint
           || aKind == AI_TOOL_STATE_KIND::DrawingZone;
}


bool panelStateHasFocusSignal( const AI_PANEL_STATE_RECORD& aPanel )
{
    return !aPanel.m_FocusedControlId.IsEmpty()
           || !aPanel.m_FocusedControlLabel.IsEmpty()
           || !aPanel.m_SelectedText.IsEmpty();
}


bool snapshotHasFocusedPanel( const AI_CONTEXT_SNAPSHOT& aSnapshot )
{
    for( const AI_PANEL_STATE_RECORD& panel : aSnapshot.m_PanelStates )
    {
        if( panelStateHasFocusSignal( panel ) )
            return true;
    }

    return false;
}


wxString semanticPanelFingerprintJson( const AI_CONTEXT_SNAPSHOT& aSnapshot )
{
    nlohmann::json panels = nlohmann::json::array();

    for( const AI_PANEL_STATE_RECORD& panel : aSnapshot.m_PanelStates )
    {
        if( !panelStateHasFocusSignal( panel ) )
            continue;

        panels.push_back( {
                { "id", toUtf8String( panel.m_Id ) },
                { "title", toUtf8String( panel.m_Title ) },
                { "focused_control_id", toUtf8String( panel.m_FocusedControlId ) },
                { "focused_control_label", toUtf8String( panel.m_FocusedControlLabel ) },
                { "selected_text", toUtf8String( panel.m_SelectedText ) },
                { "summary", toUtf8String( panel.m_Summary ) },
                { "state", toUtf8String( panel.m_StateJson ) }
        } );
    }

    return fromUtf8String( panels.dump() );
}


bool AiAgentSnapshotNeedsBackgroundTick( const AI_CONTEXT_SNAPSHOT& aSnapshot )
{
    if( aSnapshot.m_EditorKind == AI_EDITOR_KIND::Unknown )
        return false;

    if( activeToolStateNeedsBackgroundTick( aSnapshot.m_ToolState.m_Kind ) )
        return true;

    return snapshotHasFocusedPanel( aSnapshot )
           && AiDynamicContextKind( aSnapshot ) == wxS( "panel" );
}


bool AiAgentSnapshotNeedsContinuousBackgroundIdle( const AI_CONTEXT_SNAPSHOT& aSnapshot )
{
    return aSnapshot.m_EditorKind != AI_EDITOR_KIND::Unknown
           && activeToolStateNeedsBackgroundTick( aSnapshot.m_ToolState.m_Kind );
}


wxString AiAgentBackgroundTickFingerprint( const AI_CONTEXT_SNAPSHOT& aSnapshot )
{
    if( !AiAgentSnapshotNeedsBackgroundTick( aSnapshot ) )
        return wxString();

    nlohmann::json fingerprint = {
        { "editor", static_cast<int>( aSnapshot.m_EditorKind ) },
        { "version", toUtf8String( aSnapshot.m_Version.AsString() ) },
        { "dynamic_context", toUtf8String( AiDynamicContextKind( aSnapshot ) ) },
        { "tool_state", toUtf8String( aSnapshot.m_ToolState.KindAsString() ) },
        { "active_action", toUtf8String( aSnapshot.m_ToolState.m_ActiveActionName ) },
        { "shared_context", toUtf8String( aSnapshot.m_ToolState.m_SharedContextJson ) },
        { "mode_context", toUtf8String( aSnapshot.m_ToolState.m_ModeContextJson ) },
        { "panel_states", toUtf8String( semanticPanelFingerprintJson( aSnapshot ) ) }
    };

    return fromUtf8String( fingerprint.dump() );
}


bool AiAgentShouldQueueBackgroundTick(
        const AI_CONTEXT_SNAPSHOT& aSnapshot,
        const wxString& aLastBackgroundTickFingerprint )
{
    const wxString fingerprint = AiAgentBackgroundTickFingerprint( aSnapshot );

    if( fingerprint.IsEmpty() )
        return false;

    if( fingerprint != aLastBackgroundTickFingerprint )
        return true;

    return AiAgentSnapshotNeedsContinuousBackgroundIdle( aSnapshot );
}


AI_AGENT_PANEL::AI_AGENT_PANEL( wxWindow* aParent, AI_EDITOR_KIND aEditorKind,
                                CONTEXT_PROVIDER aContextProvider ) :
        AI_AGENT_PANEL_BASE( aParent ),
        m_EditorKind( aEditorKind ),
        m_ContextProvider( std::move( aContextProvider ) ),
        m_Model( std::make_shared<AI_AGENT_PANEL_MODEL>( MakeDefaultAiProvider() ) ),
        m_BackgroundUpdateState( std::make_shared<BACKGROUND_UPDATE_STATE>() ),
        m_ChatSendState( std::make_shared<CHAT_SEND_STATE>() )
{
    m_BackgroundAgentToggle->SetValue( m_Model->BackgroundAgentEnabled() );

    loadConfiguredResearchFolder( AI_MODEL_CONFIG_STORE::LoadUserConfig(), false );

    updateModeControls();
    RefreshTranscript();
    RefreshSuggestions();
    RefreshLog();
    updateComposerStatus();
}


AI_AGENT_PANEL::~AI_AGENT_PANEL()
{
    if( m_BackgroundUpdateState )
        m_BackgroundUpdateState->m_Alive.store( false );

    if( m_ChatSendState )
        m_ChatSendState->m_Alive.store( false );
}


void AI_AGENT_PANEL::OnModelSettings( wxCommandEvent& aEvent )
{
    aEvent.Skip( false );
    ShowModelSettingsDialog();
}


void AI_AGENT_PANEL::OnNewChat( wxCommandEvent& aEvent )
{
    aEvent.Skip( false );
    StartNewChat();
}


void AI_AGENT_PANEL::OnBackgroundAgentToggled( wxCommandEvent& aEvent )
{
    aEvent.Skip( false );
    SetBackgroundAgentEnabled( m_BackgroundAgentToggle->GetValue() );
}


void AI_AGENT_PANEL::OnPromptTextChanged( wxCommandEvent& aEvent )
{
    aEvent.Skip();
    updateComposerStatus();
}


void AI_AGENT_PANEL::OnPromptEnter( wxCommandEvent& aEvent )
{
    aEvent.Skip( false );
    SendCurrentText();
}


void AI_AGENT_PANEL::OnSend( wxCommandEvent& aEvent )
{
    aEvent.Skip( false );
    SendCurrentText();
}


void AI_AGENT_PANEL::OnStop( wxCommandEvent& aEvent )
{
    aEvent.Skip( false );
    m_Model->CancelLastRequest();
    RefreshLog();
    updateComposerStatus();
}


void AI_AGENT_PANEL::OnPreviewSuggestion( wxCommandEvent& aEvent )
{
    aEvent.Skip( false );
    PreviewLatestSuggestion();
}


void AI_AGENT_PANEL::OnAcceptSuggestion( wxCommandEvent& aEvent )
{
    aEvent.Skip( false );
    AcceptLatestSuggestion();
}


void AI_AGENT_PANEL::OnRejectSuggestion( wxCommandEvent& aEvent )
{
    aEvent.Skip( false );
    RejectLatestSuggestion();
}


void AI_AGENT_PANEL::StartNewChat()
{
    if( m_ChatSendInFlight )
        return;

    if( HasPendingChatSessionPreview() )
        rejectActiveChatSession();

    m_Model->StartNewChat();

    if( m_Input )
        m_Input->Clear();

    m_PendingUserText.clear();
    m_StreamingAssistantText.clear();
    RefreshTranscript();
    RefreshSuggestions();
    RefreshLog();
    updateComposerStatus();
}


void AI_AGENT_PANEL::SendCurrentText()
{
    if( m_ChatSendInFlight )
        return;

    const wxString text = m_Input->GetValue();

    if( !m_Model->CanSend( text ) )
        return;

    m_ChatSendInFlight = true;
    m_PendingUserText = text;
    m_StreamingAssistantText.clear();

    const uint64_t generation =
            m_ChatSendState ? m_ChatSendState->m_Generation.fetch_add( 1 ) + 1 : 0;

    renderPendingChatRequest();
    updateComposerStatus();
    RefreshLog();

    if( wxTheApp )
    {
        wxTheApp->CallAfter(
                [this, text, generation]()
                {
                    prepareAndRunPendingChatRequest( text, generation );
                } );
        return;
    }

    prepareAndRunPendingChatRequest( text, generation );
}


void AI_AGENT_PANEL::prepareAndRunPendingChatRequest(
        wxString aText, uint64_t aGeneration )
{
    if( !m_ChatSendInFlight || !m_ChatSendState
        || !m_ChatSendState->m_Alive.load()
        || m_ChatSendState->m_Generation.load() != aGeneration )
    {
        return;
    }

    AI_CONTEXT_SNAPSHOT snapshot = contextSnapshotWithPanelState();
    AI_CHAT_REQUEST_STATE requestState =
            m_Model->PrepareUserTextRequest( aText, m_EditorKind, std::move( snapshot ) );

    if( !m_ChatSendState->m_Alive.load()
        || m_ChatSendState->m_Generation.load() != aGeneration )
    {
        return;
    }

    m_PendingUserText.clear();
    renderPendingChatRequest();
    RefreshLog();

    std::shared_ptr<AI_AGENT_PANEL_MODEL> model = m_Model;
    std::shared_ptr<CHAT_SEND_STATE> chatState = m_ChatSendState;

    std::thread(
            [this, model = std::move( model ), chatState = std::move( chatState ),
             state = std::move( requestState ), aGeneration]() mutable
            {
                runPreparedChatRequest( std::move( model ), std::move( chatState ),
                                        std::move( state ), aGeneration );
            } )
            .detach();
}


void AI_AGENT_PANEL::runPreparedChatRequest(
        std::shared_ptr<AI_AGENT_PANEL_MODEL> aModel,
        std::shared_ptr<CHAT_SEND_STATE> aChatState,
        AI_CHAT_REQUEST_STATE aState,
                                             uint64_t aGeneration )
{
    AI_RUNTIME_STREAM_EVENT_SINK eventSink =
            [this, aChatState, aGeneration]( const AI_RUNTIME_STREAM_EVENT& aEvent )
            {
                if( !wxTheApp )
                    return;

                wxTheApp->CallAfter(
                        [this, chatState = aChatState, aGeneration, aEvent]()
                        {
                            if( !chatState || !chatState->m_Alive.load()
                                || chatState->m_Generation.load() != aGeneration )
                            {
                                return;
                            }

                            handlePreparedChatStreamEvent( aEvent, aGeneration );
                        } );
            };

    AI_PROVIDER_RESPONSE response =
            aModel->ExecutePreparedChatRequest( aState, std::move( eventSink ) );

    if( wxTheApp )
    {
        wxTheApp->CallAfter(
                [this, model = std::move( aModel ), chatState = std::move( aChatState ),
                 state = std::move( aState ), response = std::move( response ),
                 aGeneration]() mutable
                {
                    const bool panelAlive =
                            chatState && chatState->m_Alive.load();
                    const bool generationCurrent =
                            chatState
                            && chatState->m_Generation.load() == aGeneration;

                    model->FinishPreparedChatRequest( std::move( state ),
                                                      std::move( response ) );

                    if( !panelAlive || !generationCurrent )
                        return;

                    m_ChatSendInFlight = false;
                    m_PendingUserText.clear();
                    m_StreamingAssistantText.clear();
                    RefreshTranscript();
                    RefreshSuggestions();
                    RefreshLog();
                    updateComposerStatus();
                } );
        return;
    }

    aModel->FinishPreparedChatRequest( std::move( aState ), std::move( response ) );
}


void AI_AGENT_PANEL::ConfigureActionToolCalls(
        std::unique_ptr<AI_ACTION_RUNNER> aRunner,
        const std::vector<wxString>& aAllowlistedActions,
        std::vector<AI_ACTION_DESCRIPTOR> aFallbackActions,
        AI_ACCEPT_APPLY_ADAPTER* aAcceptAdapter,
        AI_SESSION_PREVIEW_SERVICE* aPreviewService,
        AI_SESSION_SHADOW_BOARD_SEEDER* aShadowBoardSeeder,
        AI_SESSION_VALIDATION_SERVICE* aValidationService )
{
    m_Model->SetToolCallHandler( nullptr );
    m_Model->ConfigureNextActionServices( aPreviewService, aValidationService );
    m_Model->ConfigureNextActionCurrentContextSampler(
            [this]()
            {
                AI_CONTEXT_SNAPSHOT snapshot = contextSnapshotWithPanelState();
                uint64_t latestActivitySequence = 0;

                for( const AI_ACTIVITY_RECORD& activity : m_Model->ActivityRecords() )
                    latestActivitySequence =
                            std::max( latestActivitySequence, activity.m_Sequence );

                return AiNextActionContextVersionFromSnapshot(
                        snapshot, latestActivitySequence );
            } );
    m_RuntimeToolCallHandler.reset();
    m_ToolCallHandler.reset();
    m_SessionToolCallHandler = nullptr;
    m_HasSessionPreviewService = aPreviewService != nullptr;
    m_HasSessionAcceptAdapter = aAcceptAdapter != nullptr;
    m_AcceptAdapter = aAcceptAdapter;
    m_ActionRunner = std::move( aRunner );
    m_ToolExecutionPolicy = AI_TOOL_EXECUTION_POLICY();

    auto dispatcher = std::make_unique<AI_TOOL_CALL_DISPATCHER>();
    auto sessionHandler = std::make_unique<AI_SESSION_TOOL_CALL_HANDLER>(
            AI_PYTHON_LOCAL_WORKER::CreateDefault(), aAcceptAdapter,
            aPreviewService, aShadowBoardSeeder, aValidationService );
    m_SessionToolCallHandler = sessionHandler.get();
    dispatcher->AddHandler( std::move( sessionHandler ) );
    dispatcher->AddHandler( std::make_unique<AI_SEMANTIC_TOOL_CALL_HANDLER>(
            [this]()
            {
                return SemanticUiTree();
            },
            [this]( const AI_SEMANTIC_UI_ACTION_REQUEST& aRequest )
            {
                return InvokeSemanticUiAction( aRequest );
            } ) );
    if( m_ActionRunner )
    {
        for( const wxString& actionName : aAllowlistedActions )
            m_ToolExecutionPolicy.AllowAction( actionName );

        auto actionHandler = std::make_unique<AI_ACTION_TOOL_CALL_HANDLER>(
                m_ToolExecutionPolicy, *m_ActionRunner, m_ToolActivityLog,
                [this]( AI_SUGGESTION_RECORD aSuggestion )
                {
                    return m_Model->AddSuggestion( std::move( aSuggestion ) );
                } );
        actionHandler->SetFallbackActions( std::move( aFallbackActions ) );
        dispatcher->AddHandler( std::move( actionHandler ) );
    }

    m_ToolCallHandler = std::move( dispatcher );
    m_RuntimeToolCallHandler =
            std::make_unique<AI_MARSHALLED_TOOL_CALL_HANDLER>(
                    *m_ToolCallHandler,
                    [this]( std::function<void()> aTask )
                    {
                        CallAfter( [this, task = std::move( aTask )]() mutable
                                   {
                                       RefreshLog();
                                       task();
                                       RefreshLog();
                                       updateComposerStatus();
                                   } );
                    },
                    []()
                    {
                        return wxThread::IsMain();
                    } );
    m_Model->SetToolCallHandler( m_RuntimeToolCallHandler.get() );
}


void AI_AGENT_PANEL::ConfigureSuggestionReview(
        SUGGESTION_PREVIEW_HANDLER aPreviewHandler,
        SUGGESTION_ACCEPT_HANDLER aAcceptHandler,
        SUGGESTION_REJECT_HANDLER aRejectHandler )
{
    m_PreviewSuggestionHandler = std::move( aPreviewHandler );
    m_AcceptSuggestionHandler = std::move( aAcceptHandler );
    m_RejectSuggestionHandler = std::move( aRejectHandler );
    updateModeControls();
    updateComposerStatus();
}


void AI_AGENT_PANEL::SetBackgroundAgentEnabled( bool aEnabled )
{
    m_Model->SetBackgroundAgentEnabled( aEnabled );
    m_LastBackgroundTickFingerprint.Clear();

    if( m_BackgroundAgentToggle && m_BackgroundAgentToggle->GetValue() != aEnabled )
        m_BackgroundAgentToggle->SetValue( aEnabled );

    updateComposerStatus();
}


bool AI_AGENT_PANEL::BackgroundAgentEnabled() const
{
    return m_Model->BackgroundAgentEnabled();
}


bool AI_AGENT_PANEL::HasPendingChatSessionPreview() const
{
    return m_SessionToolCallHandler
           && m_SessionToolCallHandler->HasPendingSessionPreview();
}


bool AI_AGENT_PANEL::ShowModelSettingsDialog()
{
    wxString        loadError;
    AI_MODEL_CONFIG config = AI_MODEL_CONFIG_STORE::LoadUserConfig( &loadError );

    if( !loadError.IsEmpty() )
    {
        wxMessageBox( loadError, _( "Model Settings" ), wxOK | wxICON_WARNING, this );
    }

    AI_MODEL_SETTINGS_DIALOG dialog( this, config );

    if( dialog.ShowModal() != wxID_OK )
        return false;

    wxString saveError;
    AI_MODEL_CONFIG_STORE store;

    AI_MODEL_CONFIG savedConfig = dialog.Config();

    if( !store.Save( savedConfig, &saveError ) )
    {
        wxMessageBox( saveError, _( "Model Settings" ), wxOK | wxICON_ERROR, this );
        return false;
    }

    loadConfiguredResearchFolder( savedConfig, true );

    m_Model->ReloadDefaultProviders();
    RefreshLog();
    updateComposerStatus();
    return true;
}


void AI_AGENT_PANEL::loadConfiguredResearchFolder( const AI_MODEL_CONFIG& aConfig,
                                                  bool aReportErrors )
{
    m_Model->SetLocalTextResearchDirectory( aConfig.m_ResearchFolder );

    if( aConfig.m_ResearchFolder.IsEmpty() )
        return;

    AI_CONTEXT_SNAPSHOT snapshot = contextSnapshotWithPanelState();

    if( snapshot.m_ProjectId.IsEmpty() || snapshot.m_DocumentId.IsEmpty() )
        return;

    wxString loadError;

    if( m_Model->LoadLocalTextResearchDirectory( aConfig.m_ResearchFolder,
                                                 snapshot.m_ProjectId,
                                                 snapshot.m_DocumentId,
                                                 loadError ) )
    {
        return;
    }

    if( aReportErrors && !loadError.IsEmpty() )
    {
        wxMessageBox( loadError, _( "Model Settings" ), wxOK | wxICON_WARNING,
                      this );
    }
}


AI_SEMANTIC_UI_TREE AI_AGENT_PANEL::SemanticUiTree() const
{
    AI_AGENT_PANEL_SEMANTIC_VIEW view;
    view.m_BackgroundAgentEnabled = m_Model->BackgroundAgentEnabled();
    view.m_InputHasText = m_Input && m_Model->CanSend( m_Input->GetValue() );
    std::optional<uint64_t> activeSuggestion = m_Model->LatestActiveSuggestionId();
    const bool pendingChatSessionPreview = HasPendingChatSessionPreview();
    view.m_HasActiveSuggestion = activeSuggestion.has_value()
                                 || pendingChatSessionPreview;
    view.m_CanPreviewSuggestion =
            pendingChatSessionPreview
            || ( activeSuggestion && m_PreviewSuggestionHandler
              && m_Model->CanPreviewSuggestion( *activeSuggestion ) );
    view.m_CanAcceptSuggestion =
            ( pendingChatSessionPreview && m_HasSessionAcceptAdapter )
            || ( activeSuggestion && m_AcceptSuggestionHandler
              && m_Model->CanAcceptSuggestion( *activeSuggestion ) );
    view.m_MessageCount = m_Model->Messages().size();
    view.m_SuggestionCount = m_Model->Suggestions().size();
    AI_PROVIDER_RECOVERY_POLICY chatRecovery =
            m_Model->LatestChatProviderRecoveryPolicy();
    AI_PROVIDER_RECOVERY_POLICY nextActionRecovery =
            m_Model->LatestNextActionProviderRecoveryPolicy();
    view.m_HasProviderRecovery =
            chatRecovery.m_Available || nextActionRecovery.m_Available;
    view.m_CanExecuteProviderRecovery =
            view.m_HasProviderRecovery && m_AcceptAdapter != nullptr;

    if( view.m_HasProviderRecovery )
    {
        AI_CONTEXT_SNAPSHOT snapshot = contextSnapshotWithPanelState();
        AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT recoveryContext;
        recoveryContext.m_DocumentRevision = snapshot.m_Version.m_DocumentRevision;

        const bool useNextAction =
                nextActionRecovery.m_Available
                && ( !chatRecovery.m_Available
                     || nextActionRecovery.m_RequestId > chatRecovery.m_RequestId );

        AI_PROVIDER_RECOVERY_EPISODE episode =
                useNextAction
                        ? m_Model->LatestNextActionProviderRecoveryEpisode(
                                  recoveryContext )
                        : m_Model->LatestChatProviderRecoveryEpisode(
                                  recoveryContext );

        view.m_ProviderRecoveryEpisodeJson = episode.m_EpisodeJson;
    }

    AI_AGENT_COMPOSER_STATUS_VIEW status;
    status.m_BackgroundAgentEnabled = view.m_BackgroundAgentEnabled;
    status.m_InputHasText = view.m_InputHasText;
    status.m_HasActiveSuggestion = view.m_HasActiveSuggestion;
    status.m_LatestRequestId = m_Model->LastRequestId();
    status.m_LastRequestCancelled = m_Model->LastRequestCancelled();
    view.m_ComposerStatusText = AiAgentComposerStatusText( status );
    std::vector<AI_AGENT_OBSERVABILITY_ENTRY> entries =
            m_Model->ObservabilityEntries( 128 );
    view.m_LogEntryCount = entries.size();
    view.m_LogSummary = compactObservabilitySummary( entries, 6 );
    return AiAgentPanelSemanticTree( view );
}


AI_PANEL_STATE_RECORD AI_AGENT_PANEL::SemanticPanelStateRecord() const
{
    return AiAgentPanelSemanticStateRecord( SemanticUiTree() );
}


AI_CONTEXT_SNAPSHOT AI_AGENT_PANEL::contextSnapshotWithPanelState() const
{
    AI_CONTEXT_SNAPSHOT snapshot;

    if( m_ContextProvider )
        snapshot = m_ContextProvider();

    if( snapshot.m_EditorKind == AI_EDITOR_KIND::Unknown )
        snapshot.m_EditorKind = m_EditorKind;

    AiUpsertPanelStateRecord( snapshot, SemanticPanelStateRecord() );
    return snapshot;
}


AI_SEMANTIC_UI_ACTION_RESULT AI_AGENT_PANEL::InvokeSemanticUiAction(
        const AI_SEMANTIC_UI_ACTION_REQUEST& aRequest )
{
    const AI_SEMANTIC_UI_TREE tree = SemanticUiTree();
    const AI_SEMANTIC_UI_NODE* node = tree.FindNode( aRequest.m_NodeId );

    if( !node )
        return semanticActionError( wxS( "unknown_node" ), aRequest.m_NodeId );

    if( !node->m_Enabled )
        return semanticActionError( wxS( "disabled_node" ), aRequest.m_NodeId );

    if( aRequest.m_NodeId == wxS( "agent.input" )
        && aRequest.m_Action == wxS( "set_text" ) )
    {
        if( !aRequest.m_HasText )
            return semanticActionError( wxS( "missing_text" ),
                                        wxS( "set_text requires text" ) );

        if( !m_Input )
            return semanticActionError( wxS( "unavailable_node" ), aRequest.m_NodeId );

        m_Input->SetValue( aRequest.m_Text );
        m_Input->SetFocus();
        updateComposerStatus();
        return semanticActionOk( wxS( "agent.input" ) );
    }

    if( aRequest.m_NodeId == wxS( "agent.background.toggle" )
        && aRequest.m_Action == wxS( "toggle" ) )
    {
        const bool enabled = aRequest.m_Checked.value_or( !BackgroundAgentEnabled() );
        SetBackgroundAgentEnabled( enabled );
        return semanticActionOk( wxS( "agent.background.toggle" ) );
    }

    if( aRequest.m_Action != wxS( "invoke" ) )
        return semanticActionError( wxS( "unsupported_action" ), aRequest.m_Action );

    if( aRequest.m_NodeId == wxS( "agent.send" ) )
    {
        SendCurrentText();
        return semanticActionOk( wxS( "agent.input" ) );
    }

    if( aRequest.m_NodeId == wxS( "agent.stop" ) )
    {
        m_Model->CancelLastRequest();
        RefreshLog();
        updateComposerStatus();
        return semanticActionOk();
    }

    if( aRequest.m_NodeId == wxS( "agent.model.settings" ) )
    {
        ShowModelSettingsDialog();
        return semanticActionOk( wxS( "agent.model.settings" ) );
    }

    if( aRequest.m_NodeId == wxS( "agent.preview.invoke" ) )
        return PreviewLatestSuggestion() ? semanticActionOk()
                                         : semanticActionError( wxS( "action_failed" ),
                                                                aRequest.m_NodeId );

    if( aRequest.m_NodeId == wxS( "agent.accept" ) )
    {
        if( !aRequest.m_UserConfirmed )
            return semanticActionError( wxS( "confirmation_required" ),
                                        wxS( "agent.accept requires user confirmation" ) );

        return AcceptLatestSuggestion() ? semanticActionOk()
                                        : semanticActionError( wxS( "action_failed" ),
                                                               aRequest.m_NodeId );
    }

    if( aRequest.m_NodeId == wxS( "agent.reject" ) )
        return RejectLatestSuggestion() ? semanticActionOk()
                                        : semanticActionError( wxS( "action_failed" ),
                                                               aRequest.m_NodeId );

    if( aRequest.m_NodeId == wxS( "agent.recovery.execute" ) )
    {
        if( !aRequest.m_UserConfirmed )
            return semanticActionError(
                    wxS( "confirmation_required" ),
                    wxS( "agent.recovery.execute requires user confirmation" ) );

        return RecoverLatestProviderFailure()
                       ? semanticActionOk()
                       : semanticActionError( wxS( "action_failed" ),
                                              aRequest.m_NodeId );
    }

    return semanticActionError( wxS( "unsupported_action" ), aRequest.m_NodeId );
}


bool AI_AGENT_PANEL::RecoverLatestProviderFailure()
{
    if( !m_AcceptAdapter )
        return false;

    AI_PROVIDER_RECOVERY_POLICY chatRecovery =
            m_Model->LatestChatProviderRecoveryPolicy();
    AI_PROVIDER_RECOVERY_POLICY nextActionRecovery =
            m_Model->LatestNextActionProviderRecoveryPolicy();

    if( !chatRecovery.m_Available && !nextActionRecovery.m_Available )
        return false;

    const bool useNextAction =
            nextActionRecovery.m_Available
            && ( !chatRecovery.m_Available
                 || nextActionRecovery.m_RequestId > chatRecovery.m_RequestId );

    AI_CONTEXT_SNAPSHOT snapshot = contextSnapshotWithPanelState();

    AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT recoveryContext;
    recoveryContext.m_DocumentRevision = snapshot.m_Version.m_DocumentRevision;

    AI_PROVIDER_RECOVERY_REPLAY_EXECUTION_OPTIONS options;
    options.m_UserReviewed = true;
    options.m_Reviewer = wxS( "agent.panel" );
    options.m_ReviewNote = wxS( "confirmed semantic provider recovery action" );

    AI_ACTIVITY_RECORD requestRecord;
    requestRecord.m_Kind = AI_ACTIVITY_KIND::UserAction;
    requestRecord.m_EditorKind = m_EditorKind;
    requestRecord.m_ActionName = wxS( "provider_recovery.replay" );
    requestRecord.m_Allowed = true;
    requestRecord.m_Message =
            useNextAction ? wxS( "Next Action provider recovery replay requested." )
                          : wxS( "Chat provider recovery replay requested." );
    m_Model->RecordActivity( std::move( requestRecord ) );

    AI_PROVIDER_RECOVERY_REPLAY_EXECUTION_RESULT result =
            useNextAction
                    ? m_Model->ExecuteNextActionProviderRecoveryReplayRequest(
                              recoveryContext, options, *m_AcceptAdapter )
                    : m_Model->ExecuteChatProviderRecoveryReplayRequest(
                              recoveryContext, options, *m_AcceptAdapter );

    AI_ACTIVITY_RECORD resultRecord;
    resultRecord.m_Kind = AI_ACTIVITY_KIND::ToolResult;
    resultRecord.m_EditorKind = m_EditorKind;
    resultRecord.m_ActionName = wxS( "provider_recovery.replay" );
    resultRecord.m_ResultJson = result.m_ResultJson;
    resultRecord.m_ErrorCode = result.m_ErrorCode;
    resultRecord.m_Allowed = result.m_Ok;
    resultRecord.m_Executed = result.m_Ok;
    resultRecord.m_Message = result.m_Message;
    m_Model->RecordActivity( std::move( resultRecord ) );

    RefreshLog();
    updateComposerStatus();
    return result.m_Ok;
}


bool AI_AGENT_PANEL::PreviewLatestSuggestion()
{
    std::optional<uint64_t> suggestionId = m_Model->LatestActiveSuggestionId();

    if( AiAgentReviewCommandTargetsChatSession(
                suggestionId.has_value(), HasPendingChatSessionPreview() ) )
        return previewActiveChatSession();

    if( !suggestionId )
        return false;

    if( !m_PreviewSuggestionHandler )
        return false;

    bool handled = m_PreviewSuggestionHandler( *m_Model, *suggestionId );
    RefreshSuggestions();
    RefreshLog();
    updateComposerStatus();
    return handled;
}


bool AI_AGENT_PANEL::acceptActionPreviewSuggestion( uint64_t aSuggestionId )
{
    if( !m_ActionRunner )
        return false;

    if( !m_Model->CanAcceptSuggestion( aSuggestionId ) )
        return false;

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            m_Model->FindSuggestion( aSuggestionId );

    if( !suggestion )
        return false;

    std::optional<wxString> actionName = actionPreviewActionName( *suggestion );

    if( !actionName )
        return false;

    wxString error;

    if( !m_ActionRunner->RunActionByName( *actionName, error ) )
        return false;

    return m_Model->MarkSuggestionAccepted( aSuggestionId );
}


bool AI_AGENT_PANEL::invokeActiveChatSessionTool( const wxString& aToolName,
                                                  const wxString& aArgumentsJson,
                                                  const wxString& aToolCallId )
{
    if( !m_SessionToolCallHandler || !HasPendingChatSessionPreview() )
        return false;

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = m_Model->LastRequestId();
    request.m_EditorKind = m_EditorKind;
    request.m_ContextSnapshot = contextSnapshotWithPanelState();

    if( request.m_ContextSnapshot.m_EditorKind == AI_EDITOR_KIND::Unknown )
        request.m_ContextSnapshot.m_EditorKind = m_EditorKind;

    request.m_ContextVersion = request.m_ContextSnapshot.m_Version;

    AI_TOOL_CALL_RECORD toolCall;
    toolCall.m_RequestId = request.m_RequestId;
    toolCall.m_ToolCallId = aToolCallId;
    toolCall.m_ToolName = aToolName;
    toolCall.m_ArgumentsJson = aArgumentsJson;

    AI_ACTIVITY_RECORD toolRequest;
    toolRequest.m_RequestId = request.m_RequestId;
    toolRequest.m_ToolCallId = toolCall.m_ToolCallId;
    toolRequest.m_Kind = AI_ACTIVITY_KIND::ModelToolRequest;
    toolRequest.m_EditorKind = m_EditorKind;
    toolRequest.m_ActionName = toolCall.m_ToolName;
    toolRequest.m_ArgumentsJson = toolCall.m_ArgumentsJson;
    toolRequest.m_Message = wxS( "Panel session control requested." );
    m_Model->RecordActivity( std::move( toolRequest ) );

    AI_TOOL_INVOCATION_RESULT result =
            m_SessionToolCallHandler->HandleToolCall( request, toolCall );

    AI_ACTIVITY_RECORD resultRecord;
    resultRecord.m_RequestId = result.m_RequestId;
    resultRecord.m_ToolCallId = result.m_ToolCallId;
    resultRecord.m_Kind = AI_ACTIVITY_KIND::ToolResult;
    resultRecord.m_EditorKind = m_EditorKind;
    resultRecord.m_ActionName = result.m_ActionName.IsEmpty()
                                        ? toolCall.m_ToolName
                                        : result.m_ActionName;
    resultRecord.m_ResultJson = result.m_ResultJson;
    resultRecord.m_ErrorCode = result.m_ErrorCode;
    resultRecord.m_Allowed = result.m_Allowed;
    resultRecord.m_Executed = result.m_Executed;
    resultRecord.m_Message = result.m_Message;
    m_Model->RecordActivity( std::move( resultRecord ) );

    return result.m_Allowed && result.m_Executed;
}


bool AI_AGENT_PANEL::previewActiveChatSession()
{
    if( !m_HasSessionPreviewService )
        return false;

    const bool handled = invokeActiveChatSessionTool(
            wxS( "kisurf_render_preview" ), wxS( "{\"mode\":\"native\"}" ),
            wxS( "panel_session_preview" ) );

    RefreshSuggestions();
    RefreshLog();
    updateComposerStatus();
    return handled;
}


bool AI_AGENT_PANEL::acceptActiveChatSession()
{
    if( !m_HasSessionAcceptAdapter )
        return false;

    const bool handled = invokeActiveChatSessionTool(
            wxS( "kisurf_accept_session" ), wxS( "{}" ),
            wxS( "panel_session_accept" ) );

    RefreshSuggestions();
    RefreshLog();
    updateComposerStatus();
    return handled;
}


bool AI_AGENT_PANEL::rejectActiveChatSession()
{
    const bool handled = invokeActiveChatSessionTool(
            wxS( "kisurf_reject_session" ), wxS( "{}" ),
            wxS( "panel_session_reject" ) );

    RefreshSuggestions();
    RefreshLog();
    updateComposerStatus();
    return handled;
}


bool AI_AGENT_PANEL::AcceptLatestSuggestion()
{
    std::optional<uint64_t> suggestionId = m_Model->LatestActiveSuggestionId();

    if( AiAgentReviewCommandTargetsChatSession(
                suggestionId.has_value(), HasPendingChatSessionPreview() ) )
        return acceptActiveChatSession();

    if( !suggestionId )
        return false;

    if( acceptActionPreviewSuggestion( *suggestionId ) )
    {
        m_LastBackgroundTickFingerprint.Clear();
        RefreshSuggestions();
        RefreshLog();
        updateComposerStatus();
        return true;
    }

    if( !m_AcceptSuggestionHandler )
        return false;

    AI_CONTEXT_SNAPSHOT snapshot = contextSnapshotWithPanelState();
    uint64_t latestActivitySequence = 0;

    for( const AI_ACTIVITY_RECORD& activity : m_Model->ActivityRecords() )
        latestActivitySequence = std::max( latestActivitySequence, activity.m_Sequence );

    AI_NEXT_ACTION_CONTEXT_VERSION contextVersion =
            AiNextActionContextVersionFromSnapshot( snapshot, latestActivitySequence );
    bool handled = m_AcceptSuggestionHandler( *m_Model, *suggestionId,
                                              contextVersion );
    if( handled )
        m_LastBackgroundTickFingerprint.Clear();

    RefreshSuggestions();
    RefreshLog();
    updateComposerStatus();
    return handled;
}


bool AI_AGENT_PANEL::RejectLatestSuggestion()
{
    std::optional<uint64_t> suggestionId = m_Model->LatestActiveSuggestionId();

    if( AiAgentReviewCommandTargetsChatSession(
                suggestionId.has_value(), HasPendingChatSessionPreview() ) )
        return rejectActiveChatSession();

    if( !suggestionId )
        return false;

    bool handled = m_RejectSuggestionHandler
                         ? m_RejectSuggestionHandler( *m_Model, *suggestionId )
                         : m_Model->RejectSuggestion( *suggestionId );

    if( handled )
        m_LastBackgroundTickFingerprint.Clear();

    RefreshSuggestions();
    RefreshLog();
    updateComposerStatus();
    return handled;
}


bool AI_AGENT_PANEL::HandlePreviewShortcut( int aKeyCode, bool aHasModifier,
                                            bool aFocusInsideAgentPanel )
{
    AI_AGENT_PREVIEW_SHORTCUT_VIEW view;
    view.m_BackgroundAgentEnabled = BackgroundAgentEnabled();
    view.m_HasActiveSuggestion = m_Model->LatestActiveSuggestionId().has_value();
    view.m_HasModifier = aHasModifier;
    view.m_FocusInsideAgentPanel = aFocusInsideAgentPanel;

    switch( AiAgentPreviewShortcutAction( view, aKeyCode ) )
    {
    case AI_AGENT_PREVIEW_SHORTCUT_ACTION::Accept:
        return AcceptLatestSuggestion();

    case AI_AGENT_PREVIEW_SHORTCUT_ACTION::Reject:
        return RejectLatestSuggestion();

    case AI_AGENT_PREVIEW_SHORTCUT_ACTION::Ignore:
        break;
    }

    return false;
}


bool AI_AGENT_PANEL::HandlePreviewPointer( bool aFocusInsideAgentPanel )
{
    AI_AGENT_PREVIEW_SHORTCUT_VIEW view;
    view.m_BackgroundAgentEnabled = BackgroundAgentEnabled();
    view.m_HasActiveSuggestion = m_Model->LatestActiveSuggestionId().has_value();
    view.m_FocusInsideAgentPanel = aFocusInsideAgentPanel;

    switch( AiAgentPreviewPointerAction( view ) )
    {
    case AI_AGENT_PREVIEW_SHORTCUT_ACTION::Reject:
        return RejectLatestSuggestion();

    case AI_AGENT_PREVIEW_SHORTCUT_ACTION::Accept:
    case AI_AGENT_PREVIEW_SHORTCUT_ACTION::Ignore:
        break;
    }

    return false;
}


bool AI_AGENT_PANEL::PulseBackgroundAgent( const wxString& aReason )
{
    if( !BackgroundAgentEnabled() || !m_ContextProvider
        || backgroundSuggestionUpdateInFlight()
        || m_Model->LatestActiveSuggestionId().has_value() )
    {
        return false;
    }

    AI_CONTEXT_SNAPSHOT snapshot = contextSnapshotWithPanelState();

    if( !AiAgentSnapshotNeedsBackgroundTick( snapshot ) )
        return false;

    const wxString fingerprint = AiAgentBackgroundTickFingerprint( snapshot );

    if( !AiAgentShouldQueueBackgroundTick( snapshot,
                                           m_LastBackgroundTickFingerprint ) )
        return false;

    m_LastBackgroundTickFingerprint = fingerprint;

    nlohmann::json args = {
        { "schema", { { "name", "kisurf.ai.background_semantic_tick" },
                      { "version", 1 } } },
        { "reason", toUtf8String( aReason ) },
        { "fingerprint", toUtf8String( fingerprint ) },
        { "dynamic_context", toUtf8String( AiDynamicContextKind( snapshot ) ) },
        { "tool_state", toUtf8String( snapshot.m_ToolState.KindAsString() ) }
    };

    AI_ACTIVITY_RECORD record;
    record.m_Kind = AI_ACTIVITY_KIND::UserAction;
    record.m_EditorKind = m_EditorKind;
    record.m_ActionName = wxS( "agent.background.semantic_tick" );
    record.m_ArgumentsJson = fromUtf8String( args.dump() );
    record.m_Allowed = true;
    record.m_Executed = true;
    record.m_Message = wxS( "Background Agent semantic tick." );
    RecordActivity( std::move( record ) );
    return true;
}


bool AI_AGENT_PANEL::ShouldContinueBackgroundIdlePulse() const
{
    if( !BackgroundAgentEnabled() || !m_ContextProvider
        || m_Model->LatestActiveSuggestionId().has_value() )
    {
        return false;
    }

    return AiAgentSnapshotNeedsContinuousBackgroundIdle(
            contextSnapshotWithPanelState() );
}


void AI_AGENT_PANEL::RefreshTranscript()
{
    if( !m_Transcript )
        return;

    m_Transcript->SetPage( AiAgentTranscriptHtml( m_Model->Messages() ) );
}


void AI_AGENT_PANEL::renderPendingChatRequest()
{
    if( m_Input )
        m_Input->Clear();

    if( m_Transcript )
    {
        std::vector<AI_AGENT_MESSAGE> pendingMessages = m_Model->Messages();

        if( !m_PendingUserText.IsEmpty() )
            pendingMessages.push_back( { wxS( "user" ), m_PendingUserText } );

        pendingMessages.push_back(
                { wxS( "assistant" ),
                  m_StreamingAssistantText.IsEmpty()
                          ? wxString( wxS( "Agent thinking..." ) )
                          : m_StreamingAssistantText } );
        m_Transcript->SetPage( AiAgentTranscriptHtml( pendingMessages ) );
        m_Transcript->Update();
    }

    Layout();
    Update();
}


void AI_AGENT_PANEL::handlePreparedChatStreamEvent(
        const AI_RUNTIME_STREAM_EVENT& aEvent, uint64_t aGeneration )
{
    if( !m_ChatSendInFlight || !m_ChatSendState
        || m_ChatSendState->m_Generation.load() != aGeneration )
    {
        return;
    }

    m_StreamingAssistantText =
            AiAgentStreamingAssistantTextAfterEvent( m_StreamingAssistantText,
                                                     aEvent );

    renderPendingChatRequest();
    RefreshLog();
    updateComposerStatus();
}


void AI_AGENT_PANEL::RefreshSuggestions()
{
    updateModeControls();
    updateComposerStatus();
}


void AI_AGENT_PANEL::RefreshLog()
{
    if( !m_Log )
        return;

    wxString text;

    for( const AI_AGENT_OBSERVABILITY_ENTRY& entry : m_Model->ObservabilityEntries( 128 ) )
        text << AiAgentObservabilityEntryText( entry ) << wxS( "\n\n" );

    m_Log->SetValue( text );
    m_Log->SetInsertionPointEnd();
    updateComposerStatus();
}


void AI_AGENT_PANEL::RecordActivity( AI_ACTIVITY_RECORD aRecord )
{
    AI_ACTIVITY_RECORD activity = m_Model->RecordActivity( std::move( aRecord ) );

    if( m_ContextProvider )
    {
        AI_CONTEXT_SNAPSHOT snapshot = contextSnapshotWithPanelState();
        saveWorkspaceContextStateFromContext( snapshot, activity );

        AI_NEXT_ACTION_CONTEXT_VERSION currentContext =
                AiNextActionContextVersionFromSnapshot( snapshot,
                                                        activity.m_Sequence );
        m_Model->ExpireSuggestions( currentContext );

        queueBackgroundSuggestionUpdate( std::move( snapshot ),
                                         std::move( activity ),
                                         wxS( "activity" ) );
        RefreshSuggestions();
    }

    RefreshLog();
    updateComposerStatus();
}


void AI_AGENT_PANEL::updateModeControls()
{
    std::optional<uint64_t> activeSuggestion = m_Model->LatestActiveSuggestionId();
    const bool              pendingChatSessionPreview = HasPendingChatSessionPreview();
    const bool              hasActiveSuggestion = activeSuggestion.has_value()
                                                  || pendingChatSessionPreview;
    const bool              canPreviewSuggestion =
            pendingChatSessionPreview
            || ( activeSuggestion && m_PreviewSuggestionHandler
              && m_Model->CanPreviewSuggestion( *activeSuggestion ) );
    const bool              canAcceptSuggestion =
            ( pendingChatSessionPreview && m_HasSessionAcceptAdapter )
            || ( activeSuggestion && m_AcceptSuggestionHandler
              && m_Model->CanAcceptSuggestion( *activeSuggestion ) );

    if( m_PreviewButton )
        m_PreviewButton->Enable( canPreviewSuggestion );

    if( m_AcceptButton )
        m_AcceptButton->Enable( canAcceptSuggestion );

    if( m_RejectButton )
        m_RejectButton->Enable( hasActiveSuggestion );

    updateComposerStatus();
    Layout();
}


void AI_AGENT_PANEL::updateComposerStatus()
{
    if( !m_ComposerStatus )
        return;

    AI_AGENT_COMPOSER_STATUS_VIEW view;
    view.m_BackgroundAgentEnabled = m_Model->BackgroundAgentEnabled();
    view.m_BackgroundAgentBusy = backgroundSuggestionUpdateInFlight();
    view.m_ChatAgentBusy = m_ChatSendInFlight;
    view.m_InputHasText = m_Input && m_Model->CanSend( m_Input->GetValue() );
    view.m_HasActiveSuggestion =
            m_Model->LatestActiveSuggestionId().has_value()
            || HasPendingChatSessionPreview();
    view.m_LatestRequestId = m_Model->LastRequestId();
    view.m_LastRequestCancelled = m_Model->LastRequestCancelled();

    if( m_SendButton )
        m_SendButton->Enable( view.m_InputHasText && !view.m_ChatAgentBusy );

    if( m_StopButton )
        m_StopButton->Enable( view.m_ChatAgentBusy || backgroundSuggestionUpdateInFlight() );

    m_ComposerStatus->SetLabel( AiAgentComposerStatusText( view ) );
    m_ComposerStatus->Wrap( -1 );
    Layout();
}


void AI_AGENT_PANEL::queueBackgroundSuggestionUpdate( AI_CONTEXT_SNAPSHOT aSnapshot,
                                                      AI_ACTIVITY_RECORD aActivity,
                                                      wxString aReason )
{
    if( !m_BackgroundUpdateState )
        return;

    std::shared_ptr<BACKGROUND_UPDATE_STATE> state = m_BackgroundUpdateState;
    const uint64_t generation = state->m_Generation.fetch_add( 1 ) + 1;

    AI_AGENT_BACKGROUND_UPDATE_VIEW view;
    view.m_BackgroundAgentEnabled = BackgroundAgentEnabled();
    view.m_HasContextProvider = static_cast<bool>( m_ContextProvider );
    view.m_UpdateInFlight = state->m_InFlight.load();

    if( AiAgentBackgroundUpdateAction( view )
        != AI_AGENT_BACKGROUND_UPDATE_ACTION::QueueAsync )
    {
        return;
    }

    bool expected = false;

    if( !state->m_InFlight.compare_exchange_strong( expected, true ) )
        return;

    std::shared_ptr<AI_AGENT_PANEL_MODEL> model = m_Model;

    std::thread(
            [state, panel = this, model = std::move( model ), generation,
             snapshot = std::move( aSnapshot ),
             activity = std::move( aActivity ),
             reason = std::move( aReason )]() mutable
            {
                std::optional<AI_SUGGESTION_RECORD> suggestion;

                suggestion = model->UpdateSuggestionsIfBackgroundEnabled(
                        std::move( snapshot ), std::move( activity ), reason );

                if( !state->m_Alive.load() || !wxTheApp )
                {
                    state->m_InFlight.store( false );
                    return;
                }

                wxTheApp->CallAfter(
                        [state, panel, generation,
                         suggestion = std::move( suggestion )]() mutable
                        {
                            state->m_InFlight.store( false );

                            if( !state->m_Alive.load()
                                || generation != state->m_Generation.load() )
                            {
                                return;
                            }

                            panel->finishBackgroundSuggestionUpdate(
                                    generation, std::move( suggestion ) );
                        } );
            } )
            .detach();

    updateComposerStatus();
}


void AI_AGENT_PANEL::finishBackgroundSuggestionUpdate(
        uint64_t aGeneration,
        std::optional<AI_SUGGESTION_RECORD> aSuggestion )
{
    wxUnusedVar( aGeneration );

    std::optional<AI_SUGGESTION_RECORD> stored;

    if( aSuggestion )
    {
        if( aSuggestion->m_Id != 0 )
            stored = m_Model->FindSuggestion( aSuggestion->m_Id );

        if( !stored )
            stored = m_Model->AddSuggestion( std::move( *aSuggestion ) );
    }

    AI_AGENT_BACKGROUND_PREVIEW_VIEW previewView;
    previewView.m_BackgroundAgentEnabled = BackgroundAgentEnabled();
    previewView.m_HasNewSuggestion = stored.has_value();
    previewView.m_HasPreviewHandler = static_cast<bool>( m_PreviewSuggestionHandler );

    if( stored )
    {
        previewView.m_CanPreviewSuggestion =
                m_Model->CanPreviewSuggestion( stored->m_Id );
        previewView.m_TargetsWorkspacePreview =
                AiAgentSuggestionTargetsWorkspacePreview( *stored );
        previewView.m_TargetsAutomaticPreview =
                AiAgentSuggestionTargetsAutomaticPreview( *stored );
    }

    if( AiAgentShouldAutoPreviewBackgroundSuggestion( previewView ) )
        PreviewLatestSuggestion();
    else
        RefreshSuggestions();

    RefreshLog();
    updateComposerStatus();
}


bool AI_AGENT_PANEL::backgroundSuggestionUpdateInFlight() const
{
    return m_BackgroundUpdateState
           && m_BackgroundUpdateState->m_InFlight.load();
}


void AI_AGENT_PANEL::saveWorkspaceContextStateFromContext( const AI_CONTEXT_SNAPSHOT& aSnapshot,
                                                           const AI_ACTIVITY_RECORD& aActivity )
{
    AI_AGENT_WORKSPACE_CONTEXT_KIND mode = AiAgentWorkspaceContextForToolState( aSnapshot.m_ToolState.m_Kind );
    m_Model->SetActiveWorkspaceContext( mode );

    AI_AGENT_WORKSPACE_CONTEXT_STATE state;
    state.m_ContextKind = mode;
    state.m_Title = AiAgentWorkspaceContextTitle( mode );
    state.m_StateJson = aSnapshot.m_ToolState.m_ModeContextJson;
    state.m_LastActivitySequence = aActivity.m_Sequence;
    m_Model->SaveWorkspaceContextState( std::move( state ) );
    updateModeControls();
    updateComposerStatus();
}
