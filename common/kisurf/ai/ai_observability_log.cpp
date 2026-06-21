#include <kisurf/ai/ai_observability_log.h>

#include <json_common.h>

#include <algorithm>
#include <string>
#include <wx/regex.h>

namespace
{
std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


wxString fromUtf8String( const std::string& aText )
{
    return wxString::FromUTF8( aText.c_str() );
}


wxString redactSensitiveText( const wxString& aText )
{
    wxString text = aText;

    wxRegEx keyPattern( wxS( "sk-[A-Za-z0-9_-]{12,}" ) );
    keyPattern.ReplaceAll( &text, wxS( "sk-[redacted]" ) );

    wxRegEx dataUriPattern(
            wxS( "data:image/[A-Za-z0-9.+_-]+;base64,[A-Za-z0-9+/=_-]{12,}" ) );
    dataUriPattern.ReplaceAll( &text, wxS( "[visual-data-redacted]" ) );

    wxRegEx envPattern( wxS( "(OPENAI_API_KEY|KISURF_AI_API_KEY)[[:space:]]*=[^[:space:]\\\"']+" ) );
    envPattern.ReplaceAll( &text, wxS( "\\1=[redacted]" ) );

    wxRegEx credentialPattern( wxS( "(credential|token|api key|api_key)[[:space:]]*:[^\\n\\r,}\\\"']+" ),
                               wxRE_ADVANCED | wxRE_ICASE );
    credentialPattern.ReplaceAll( &text, wxS( "\\1: [redacted]" ) );

    if( text.length() > 4000 )
        text = text.Left( 4000 ) + wxS( "...[truncated]" );

    return text;
}


std::string sanitizedUtf8String( const wxString& aText )
{
    return toUtf8String( redactSensitiveText( aText ) );
}


nlohmann::json visualSummaryJson( const AI_VISUAL_SNAPSHOT& aVisual )
{
    nlohmann::json visual = {
        { "source", sanitizedUtf8String( aVisual.m_Source ) },
        { "mime_type", sanitizedUtf8String( aVisual.m_MimeType ) },
        { "width_px", aVisual.m_WidthPx },
        { "height_px", aVisual.m_HeightPx },
        { "byte_size", aVisual.m_ByteSize },
        { "has_pixels", aVisual.HasPixels() }
    };

    if( !aVisual.m_UnavailableReason.IsEmpty() )
        visual["unavailable_reason"] = sanitizedUtf8String( aVisual.m_UnavailableReason );

    return visual;
}


std::string editorKindJsonName( AI_EDITOR_KIND aKind )
{
    switch( aKind )
    {
    case AI_EDITOR_KIND::Pcb:
        return "pcb";

    case AI_EDITOR_KIND::Schematic:
        return "schematic";

    case AI_EDITOR_KIND::Unknown:
    default:
        return "unknown";
    }
}


std::string toolStateKindJsonName( AI_TOOL_STATE_KIND aKind )
{
    switch( aKind )
    {
    case AI_TOOL_STATE_KIND::Idle:
        return "idle";

    case AI_TOOL_STATE_KIND::Selecting:
        return "selecting";

    case AI_TOOL_STATE_KIND::RoutingTrack:
        return "routing_track";

    case AI_TOOL_STATE_KIND::PlacingVia:
        return "placing_via";

    case AI_TOOL_STATE_KIND::PlacingFootprint:
        return "placing_footprint";

    case AI_TOOL_STATE_KIND::DrawingZone:
        return "drawing_zone";

    case AI_TOOL_STATE_KIND::MovingSelection:
        return "moving_selection";

    case AI_TOOL_STATE_KIND::Unknown:
    default:
        return "unknown";
    }
}


nlohmann::json versionJson( const AI_CONTEXT_VERSION& aVersion )
{
    return nlohmann::json{
        { "document", aVersion.m_DocumentRevision },
        { "selection", aVersion.m_SelectionRevision },
        { "view", aVersion.m_ViewRevision },
        { "text", sanitizedUtf8String( aVersion.AsString() ) }
    };
}


nlohmann::json contextSummaryJson( const AI_CONTEXT_SNAPSHOT& aSnapshot )
{
    return nlohmann::json{
        { "summary", sanitizedUtf8String( aSnapshot.m_Summary ) },
        { "version", versionJson( aSnapshot.m_Version ) },
        { "selected_count", aSnapshot.m_SelectedObjects.size() },
        { "visible_count", aSnapshot.m_VisibleObjects.size() },
        { "action_count", aSnapshot.m_Actions.size() },
        { "recent_activity_count", aSnapshot.m_RecentActivity.size() },
        { "anchor_count", aSnapshot.m_Anchors.size() },
        { "panel_state_count", aSnapshot.m_PanelStates.size() },
        { "tool_state_kind", toolStateKindJsonName( aSnapshot.m_ToolState.m_Kind ) },
        { "visual", visualSummaryJson( aSnapshot.m_Visual ) }
    };
}


nlohmann::json modelInputDetailsJson( const AI_TRACE_RECORD& aTrace )
{
    const AI_PROVIDER_REQUEST& request = aTrace.m_Request;
    const uint64_t requestId = request.m_RequestId > 0 ? request.m_RequestId
                                                       : aTrace.m_RequestId;

    return nlohmann::json{
        { "request_id", requestId },
        { "editor", editorKindJsonName( request.m_EditorKind ) },
        { "user_text", sanitizedUtf8String( request.m_UserText ) },
        { "context", contextSummaryJson( request.m_ContextSnapshot ) },
        { "tool_results_count", request.m_ToolResults.size() }
    };
}


nlohmann::json toolCallSummaryJson( const AI_TOOL_CALL_RECORD& aToolCall )
{
    return nlohmann::json{
        { "id", sanitizedUtf8String( aToolCall.m_ToolCallId ) },
        { "name", sanitizedUtf8String( aToolCall.m_ToolName ) },
        { "arguments_json", sanitizedUtf8String( aToolCall.m_ArgumentsJson ) },
        { "result_json", sanitizedUtf8String( aToolCall.m_ResultJson ) },
        { "allowed", aToolCall.m_Allowed },
        { "executed", aToolCall.m_Executed },
        { "error_code", sanitizedUtf8String( aToolCall.m_ErrorCode ) },
        { "message", sanitizedUtf8String( aToolCall.m_Message ) }
    };
}


nlohmann::json modelOutputDetailsJson( const AI_TRACE_RECORD& aTrace )
{
    nlohmann::json toolCalls = nlohmann::json::array();

    for( const AI_TOOL_CALL_RECORD& toolCall : aTrace.m_Response.m_ToolCalls )
        toolCalls.push_back( toolCallSummaryJson( toolCall ) );

    return nlohmann::json{
        { "request_id", aTrace.m_RequestId },
        { "title", sanitizedUtf8String( aTrace.m_Response.m_Title ) },
        { "body", sanitizedUtf8String( aTrace.m_Response.m_Body ) },
        { "body_length", aTrace.m_Response.m_Body.length() },
        { "tool_call_count", aTrace.m_Response.m_ToolCalls.size() },
        { "cancelled", aTrace.m_Cancelled },
        { "tool_calls", std::move( toolCalls ) }
    };
}


wxString dumpJson( const nlohmann::json& aJson )
{
    return fromUtf8String( aJson.dump() );
}


uint64_t requestSortBase( uint64_t aRequestId )
{
    return aRequestId > 0 ? aRequestId * 1000000 : 0;
}


wxString activityKindLabel( AI_ACTIVITY_KIND aKind )
{
    switch( aKind )
    {
    case AI_ACTIVITY_KIND::UserAction:
        return wxS( "User action" );

    case AI_ACTIVITY_KIND::ModelToolRequest:
        return wxS( "Tool call" );

    case AI_ACTIVITY_KIND::PolicyDecision:
        return wxS( "Policy decision" );

    case AI_ACTIVITY_KIND::ToolResult:
        return wxS( "Tool result" );
    }

    return wxS( "Activity" );
}


std::string activityKindJsonName( AI_ACTIVITY_KIND aKind )
{
    switch( aKind )
    {
    case AI_ACTIVITY_KIND::UserAction:
        return "user_action";

    case AI_ACTIVITY_KIND::ModelToolRequest:
        return "model_tool_request";

    case AI_ACTIVITY_KIND::PolicyDecision:
        return "policy_decision";

    case AI_ACTIVITY_KIND::ToolResult:
        return "tool_result";
    }

    return "activity";
}


wxString suggestionStatusLabel( AI_SUGGESTION_STATUS aStatus )
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

    return wxS( "Suggestion" );
}


std::string suggestionStatusJsonName( AI_SUGGESTION_STATUS aStatus )
{
    switch( aStatus )
    {
    case AI_SUGGESTION_STATUS::Pending:
        return "pending";

    case AI_SUGGESTION_STATUS::Previewing:
        return "previewing";

    case AI_SUGGESTION_STATUS::Accepted:
        return "accepted";

    case AI_SUGGESTION_STATUS::Rejected:
        return "rejected";

    case AI_SUGGESTION_STATUS::Expired:
        return "expired";

    case AI_SUGGESTION_STATUS::Superseded:
        return "superseded";

    case AI_SUGGESTION_STATUS::Abandoned:
        return "abandoned";

    case AI_SUGGESTION_STATUS::Cancelled:
        return "cancelled";
    }

    return "suggestion";
}


AI_AGENT_OBSERVABILITY_KIND observabilityKind( AI_ACTIVITY_KIND aKind )
{
    switch( aKind )
    {
    case AI_ACTIVITY_KIND::ModelToolRequest:
        return AI_AGENT_OBSERVABILITY_KIND::ModelToolCall;

    case AI_ACTIVITY_KIND::ToolResult:
        return AI_AGENT_OBSERVABILITY_KIND::ToolResult;

    case AI_ACTIVITY_KIND::PolicyDecision:
        return AI_AGENT_OBSERVABILITY_KIND::System;

    case AI_ACTIVITY_KIND::UserAction:
        return AI_AGENT_OBSERVABILITY_KIND::UserInput;
    }

    return AI_AGENT_OBSERVABILITY_KIND::System;
}


wxString activitySummary( const AI_ACTIVITY_RECORD& aRecord )
{
    wxString summary = aRecord.m_ActionName;

    if( summary.IsEmpty() )
        summary = activityKindLabel( aRecord.m_Kind );

    if( !aRecord.m_Message.IsEmpty() )
        summary << wxS( ": " ) << redactSensitiveText( aRecord.m_Message );

    return redactSensitiveText( summary );
}


nlohmann::json activityDetailsJson( const AI_ACTIVITY_RECORD& aRecord )
{
    return nlohmann::json{
        { "sequence", aRecord.m_Sequence },
        { "request_id", aRecord.m_RequestId },
        { "tool_call_id", sanitizedUtf8String( aRecord.m_ToolCallId ) },
        { "kind", activityKindJsonName( aRecord.m_Kind ) },
        { "action", sanitizedUtf8String( aRecord.m_ActionName ) },
        { "arguments_json", sanitizedUtf8String( aRecord.m_ArgumentsJson ) },
        { "result_json", sanitizedUtf8String( aRecord.m_ResultJson ) },
        { "error_code", sanitizedUtf8String( aRecord.m_ErrorCode ) },
        { "allowed", aRecord.m_Allowed },
        { "executed", aRecord.m_Executed },
        { "message", sanitizedUtf8String( aRecord.m_Message ) }
    };
}


nlohmann::json suggestionDetailsJson( const AI_SUGGESTION_RECORD& aSuggestion )
{
    nlohmann::json details = nlohmann::json{
        { "id", aSuggestion.m_Id },
        { "sequence", aSuggestion.m_Sequence },
        { "status", suggestionStatusJsonName( aSuggestion.m_Status ) },
        { "title", sanitizedUtf8String( aSuggestion.m_Title ) },
        { "body", sanitizedUtf8String( aSuggestion.m_Body ) },
        { "arguments_json", sanitizedUtf8String( aSuggestion.m_ArgumentsJson ) },
        { "fingerprint", sanitizedUtf8String( aSuggestion.m_Fingerprint ) },
        { "trigger_activity_sequence", aSuggestion.m_TriggerActivitySequence }
    };

    if( !aSuggestion.m_ContextKind.IsEmpty() )
        details["context_kind"] = sanitizedUtf8String( aSuggestion.m_ContextKind );

    if( !aSuggestion.m_ContextDetailsJson.IsEmpty() )
    {
        nlohmann::json contextDetails =
                nlohmann::json::parse(
                        sanitizedUtf8String( aSuggestion.m_ContextDetailsJson ),
                        nullptr, false );

        if( contextDetails.is_discarded() )
        {
            details["context_details_json"] =
                    sanitizedUtf8String( aSuggestion.m_ContextDetailsJson );
        }
        else
        {
            details["context_details"] = std::move( contextDetails );
        }
    }

    return details;
}


void appendTraceEntries( const AI_TRACE_RECORD& aTrace,
                         std::vector<AI_AGENT_OBSERVABILITY_ENTRY>& aEntries )
{
    AI_AGENT_OBSERVABILITY_ENTRY input;
    input.m_Sequence = requestSortBase( aTrace.m_RequestId ) + 1;
    input.m_RequestId = aTrace.m_RequestId;
    input.m_Kind = AI_AGENT_OBSERVABILITY_KIND::ModelInput;
    input.m_EditorKind = aTrace.m_Request.m_EditorKind;
    input.m_Title = wxS( "Model input" );
    input.m_Summary = redactSensitiveText( aTrace.m_Request.m_UserText );
    input.m_DetailsJson = dumpJson( modelInputDetailsJson( aTrace ) );
    aEntries.push_back( std::move( input ) );

    AI_AGENT_OBSERVABILITY_ENTRY output;
    output.m_Sequence = requestSortBase( aTrace.m_RequestId ) + 900000;
    output.m_RequestId = aTrace.m_RequestId;
    output.m_Kind = AI_AGENT_OBSERVABILITY_KIND::ModelOutput;
    output.m_EditorKind = aTrace.m_Request.m_EditorKind;
    output.m_Title = aTrace.m_Response.m_Title.IsEmpty() ? wxString( wxS( "Model output" ) )
                                                         : aTrace.m_Response.m_Title;
    output.m_Title = redactSensitiveText( output.m_Title );
    output.m_Summary = redactSensitiveText( aTrace.m_Response.m_Body );
    output.m_DetailsJson = dumpJson( modelOutputDetailsJson( aTrace ) );
    aEntries.push_back( std::move( output ) );
}


void appendActivityEntry( const AI_ACTIVITY_RECORD& aRecord,
                          std::vector<AI_AGENT_OBSERVABILITY_ENTRY>& aEntries )
{
    AI_AGENT_OBSERVABILITY_ENTRY entry;
    entry.m_Sequence = requestSortBase( aRecord.m_RequestId ) + 10000 + aRecord.m_Sequence;
    entry.m_RequestId = aRecord.m_RequestId;
    entry.m_ToolCallId = redactSensitiveText( aRecord.m_ToolCallId );
    entry.m_Kind = observabilityKind( aRecord.m_Kind );
    entry.m_EditorKind = aRecord.m_EditorKind;
    entry.m_Title = activityKindLabel( aRecord.m_Kind );
    entry.m_Summary = activitySummary( aRecord );
    entry.m_DetailsJson = dumpJson( activityDetailsJson( aRecord ) );
    entry.m_Allowed = aRecord.m_Allowed;
    entry.m_Executed = aRecord.m_Executed;
    entry.m_ErrorCode = redactSensitiveText( aRecord.m_ErrorCode );
    aEntries.push_back( std::move( entry ) );
}


void appendSuggestionEntry( const AI_SUGGESTION_RECORD& aSuggestion,
                            std::vector<AI_AGENT_OBSERVABILITY_ENTRY>& aEntries )
{
    AI_AGENT_OBSERVABILITY_ENTRY entry;
    entry.m_Sequence = aSuggestion.m_Sequence;
    entry.m_Kind = AI_AGENT_OBSERVABILITY_KIND::Suggestion;
    entry.m_EditorKind = aSuggestion.m_EditorKind;
    entry.m_Title = wxS( "Suggestion" );
    entry.m_Summary = suggestionStatusLabel( aSuggestion.m_Status )
                      + wxS( ": " ) + redactSensitiveText( aSuggestion.m_Title );
    entry.m_DetailsJson = dumpJson( suggestionDetailsJson( aSuggestion ) );
    aEntries.push_back( std::move( entry ) );
}
} // namespace


std::vector<AI_AGENT_OBSERVABILITY_ENTRY> AI_AGENT_OBSERVABILITY_LOG::Build(
        const std::vector<AI_TRACE_RECORD>& aTraces,
        const std::vector<AI_ACTIVITY_RECORD>& aActivity,
        const std::vector<AI_SUGGESTION_RECORD>& aSuggestions,
        size_t aLimit ) const
{
    std::vector<AI_AGENT_OBSERVABILITY_ENTRY> entries;

    for( const AI_TRACE_RECORD& trace : aTraces )
        appendTraceEntries( trace, entries );

    for( const AI_ACTIVITY_RECORD& record : aActivity )
        appendActivityEntry( record, entries );

    for( const AI_SUGGESTION_RECORD& suggestion : aSuggestions )
        appendSuggestionEntry( suggestion, entries );

    std::stable_sort( entries.begin(), entries.end(),
                      []( const AI_AGENT_OBSERVABILITY_ENTRY& aFirst,
                          const AI_AGENT_OBSERVABILITY_ENTRY& aSecond )
                      {
                          return aFirst.m_Sequence < aSecond.m_Sequence;
                      } );

    if( aLimit > 0 && entries.size() > aLimit )
        entries.erase( entries.begin(), entries.end() - static_cast<std::ptrdiff_t>( aLimit ) );

    for( size_t ii = 0; ii < entries.size(); ++ii )
        entries[ii].m_Sequence = ii + 1;

    return entries;
}
