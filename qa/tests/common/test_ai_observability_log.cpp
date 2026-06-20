#include <boost/test/unit_test.hpp>

#include <json_common.h>
#include <kisurf/ai/ai_observability_log.h>

BOOST_AUTO_TEST_SUITE( AiAgentObservabilityLog )

BOOST_AUTO_TEST_CASE( TraceProducesModelInputAndOutputEntries )
{
    AI_TRACE_RECORD trace;
    trace.m_RequestId = 42;
    trace.m_Request.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trace.m_Request.m_UserText = wxS( "route the selected net" );
    trace.m_Request.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trace.m_Request.m_ContextSnapshot.m_Summary = wxS( "2 selected pads" );
    trace.m_Response.m_RequestId = 42;
    trace.m_Response.m_Title = wxS( "Routing assistant" );
    trace.m_Response.m_Body = wxS( "I can preview the next segment." );

    AI_AGENT_OBSERVABILITY_LOG log;
    std::vector<AI_AGENT_OBSERVABILITY_ENTRY> entries =
            log.Build( { trace }, {}, {}, 16 );

    BOOST_REQUIRE_EQUAL( entries.size(), 2 );
    BOOST_CHECK( entries.at( 0 ).m_Kind == AI_AGENT_OBSERVABILITY_KIND::ModelInput );
    BOOST_CHECK_EQUAL( entries.at( 0 ).m_RequestId, 42 );
    BOOST_CHECK( entries.at( 0 ).m_Summary.Contains( wxS( "route the selected net" ) ) );
    BOOST_CHECK( entries.at( 0 ).m_DetailsJson.Contains( wxS( "2 selected pads" ) ) );
    BOOST_CHECK( entries.at( 1 ).m_Kind == AI_AGENT_OBSERVABILITY_KIND::ModelOutput );
    BOOST_CHECK( entries.at( 1 ).m_Summary.Contains( wxS( "preview the next segment" ) ) );
}


BOOST_AUTO_TEST_CASE( ModelInputDetailsExposeTypedContextSummary )
{
    AI_TRACE_RECORD trace;
    trace.m_RequestId = 42;
    trace.m_Request.m_RequestId = 42;
    trace.m_Request.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trace.m_Request.m_UserText = wxS( "route selected net" );
    trace.m_Request.m_ContextVersion.m_DocumentRevision = 3;
    trace.m_Request.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trace.m_Request.m_ContextSnapshot.m_Summary = wxS( "2 selected pads" );
    trace.m_Request.m_ContextSnapshot.m_Version.m_DocumentRevision = 3;
    trace.m_Request.m_ContextSnapshot.m_Version.m_SelectionRevision = 2;
    trace.m_Request.m_ContextSnapshot.m_Version.m_ViewRevision = 9;
    trace.m_Request.m_ContextSnapshot.m_SelectedObjects.push_back(
            AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ) ) );
    trace.m_Request.m_ContextSnapshot.m_SelectedObjects.push_back(
            AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.2" ) ) );
    trace.m_Request.m_ContextSnapshot.m_VisibleObjects.push_back(
            AI_OBJECT_REF( KIID(), PCB_FOOTPRINT_T, wxS( "U1" ) ) );

    AI_ACTION_DESCRIPTOR action;
    action.m_Name = wxS( "pcbnew.InteractiveRoute" );
    action.m_EditorKind = AI_EDITOR_KIND::Pcb;
    action.m_Enabled = true;
    trace.m_Request.m_ContextSnapshot.m_Actions.push_back( action );

    AI_ACTIVITY_RECORD activity;
    activity.m_Sequence = 1;
    activity.m_ActionName = wxS( "common.Interactive.selected" );
    trace.m_Request.m_ContextSnapshot.m_RecentActivity.push_back( activity );

    AI_CONTEXT_ANCHOR anchor;
    anchor.m_Id = wxS( "route.start" );
    anchor.m_Kind = AI_CONTEXT_ANCHOR_KIND::RouteStart;
    anchor.m_Label = wxS( "Route start" );
    trace.m_Request.m_ContextSnapshot.m_Anchors.push_back( anchor );

    AI_PANEL_STATE_RECORD panel;
    panel.m_Id = wxS( "agent.panel" );
    panel.m_Title = wxS( "Agent" );
    panel.m_Summary = wxS( "mode=Routing" );
    trace.m_Request.m_ContextSnapshot.m_PanelStates.push_back( panel );

    trace.m_Request.m_ContextSnapshot.m_ToolState.m_Kind =
            AI_TOOL_STATE_KIND::RoutingTrack;
    trace.m_Request.m_ContextSnapshot.m_Visual.m_Source = wxS( "pcbnew.canvas" );
    trace.m_Request.m_ContextSnapshot.m_Visual.m_MimeType = wxS( "image/png" );
    trace.m_Request.m_ContextSnapshot.m_Visual.m_DataUri =
            wxS( "data:image/png;base64,SHOULD_NOT_APPEAR" );
    trace.m_Request.m_ContextSnapshot.m_Visual.m_WidthPx = 1280;
    trace.m_Request.m_ContextSnapshot.m_Visual.m_HeightPx = 720;
    trace.m_Request.m_ContextSnapshot.m_Visual.m_ByteSize = 2048;

    AI_TOOL_CALL_RECORD toolResult;
    toolResult.m_ToolCallId = wxS( "call_1" );
    toolResult.m_ToolName = wxS( "kisurf_get_workspace_view" );
    toolResult.m_ResultJson = wxS( "{\"status\":\"workspace_view_ready\"}" );
    trace.m_Request.m_ToolResults.push_back( toolResult );
    trace.m_Response.m_Body = wxS( "ok" );

    AI_AGENT_OBSERVABILITY_LOG log;
    std::vector<AI_AGENT_OBSERVABILITY_ENTRY> entries =
            log.Build( { trace }, {}, {}, 16 );

    BOOST_REQUIRE_EQUAL( entries.size(), 2 );
    BOOST_REQUIRE( entries.at( 0 ).m_Kind == AI_AGENT_OBSERVABILITY_KIND::ModelInput );

    nlohmann::json details =
            nlohmann::json::parse( entries.at( 0 ).m_DetailsJson.ToStdString() );

    BOOST_CHECK_EQUAL( details["request_id"].get<int>(), 42 );
    BOOST_CHECK_EQUAL( details["editor"].get<std::string>(), "pcb" );
    BOOST_CHECK_EQUAL( details["user_text"].get<std::string>(),
                       "route selected net" );
    BOOST_CHECK_EQUAL( details["tool_results_count"].get<int>(), 1 );

    const nlohmann::json& context = details["context"];
    BOOST_CHECK_EQUAL( context["summary"].get<std::string>(), "2 selected pads" );
    BOOST_CHECK_EQUAL( context["version"]["document"].get<int>(), 3 );
    BOOST_CHECK_EQUAL( context["version"]["selection"].get<int>(), 2 );
    BOOST_CHECK_EQUAL( context["version"]["view"].get<int>(), 9 );
    BOOST_CHECK_EQUAL( context["selected_count"].get<int>(), 2 );
    BOOST_CHECK_EQUAL( context["visible_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( context["action_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( context["recent_activity_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( context["anchor_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( context["panel_state_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( context["tool_state_kind"].get<std::string>(),
                       "routing_track" );
    BOOST_CHECK( context["visual"]["has_pixels"].get<bool>() );
    BOOST_CHECK( !context["visual"].contains( "data_uri" ) );
    BOOST_CHECK( !entries.at( 0 ).m_DetailsJson.Contains(
            wxS( "SHOULD_NOT_APPEAR" ) ) );
}


BOOST_AUTO_TEST_CASE( ModelOutputDetailsExposeHandledToolCallSummary )
{
    AI_TRACE_RECORD trace;
    trace.m_RequestId = 43;
    trace.m_Request.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trace.m_Request.m_UserText = wxS( "inspect workspace" );
    trace.m_Response.m_RequestId = 43;
    trace.m_Response.m_Title = wxS( "Workspace inspection" );
    trace.m_Response.m_Body = wxS( "Context returned." );

    AI_TOOL_CALL_RECORD call;
    call.m_ToolCallId = wxS( "call_1" );
    call.m_ToolName = wxS( "kisurf_get_workspace_view" );
    call.m_ArgumentsJson = wxS( "{\"views\":[\"visual\",\"panels\"]}" );
    call.m_Allowed = true;
    call.m_Executed = false;
    call.m_Message = wxS( "context returned" );
    trace.m_Response.m_ToolCalls.push_back( call );

    AI_AGENT_OBSERVABILITY_LOG log;
    std::vector<AI_AGENT_OBSERVABILITY_ENTRY> entries =
            log.Build( { trace }, {}, {}, 16 );

    BOOST_REQUIRE_EQUAL( entries.size(), 2 );
    BOOST_REQUIRE( entries.at( 1 ).m_Kind == AI_AGENT_OBSERVABILITY_KIND::ModelOutput );

    nlohmann::json details =
            nlohmann::json::parse( entries.at( 1 ).m_DetailsJson.ToStdString() );

    BOOST_CHECK_EQUAL( details["request_id"].get<int>(), 43 );
    BOOST_CHECK_EQUAL( details["title"].get<std::string>(),
                       "Workspace inspection" );
    BOOST_CHECK_EQUAL( details["body"].get<std::string>(), "Context returned." );
    BOOST_CHECK_EQUAL( details["body_length"].get<int>(), 17 );
    BOOST_CHECK_EQUAL( details["tool_call_count"].get<int>(), 1 );
    BOOST_CHECK( !details["cancelled"].get<bool>() );
    BOOST_REQUIRE_EQUAL( details["tool_calls"].size(), 1 );
    BOOST_CHECK_EQUAL( details["tool_calls"][0]["id"].get<std::string>(),
                       "call_1" );
    BOOST_CHECK_EQUAL( details["tool_calls"][0]["name"].get<std::string>(),
                       "kisurf_get_workspace_view" );
    BOOST_CHECK_EQUAL( details["tool_calls"][0]["arguments_json"].get<std::string>(),
                       "{\"views\":[\"visual\",\"panels\"]}" );
    BOOST_CHECK( details["tool_calls"][0]["allowed"].get<bool>() );
}


BOOST_AUTO_TEST_CASE( ActivityAddsToolCallAndResultEntries )
{
    AI_ACTIVITY_RECORD toolCall;
    toolCall.m_Sequence = 7;
    toolCall.m_RequestId = 42;
    toolCall.m_ToolCallId = wxS( "call_1" );
    toolCall.m_Kind = AI_ACTIVITY_KIND::ModelToolRequest;
    toolCall.m_EditorKind = AI_EDITOR_KIND::Pcb;
    toolCall.m_ActionName = wxS( "kisurf_run_action" );
    toolCall.m_ArgumentsJson = wxS( "{\"action\":\"common.Control.zoomFitScreen\"}" );

    AI_ACTIVITY_RECORD toolResult;
    toolResult.m_Sequence = 8;
    toolResult.m_RequestId = 42;
    toolResult.m_ToolCallId = wxS( "call_1" );
    toolResult.m_Kind = AI_ACTIVITY_KIND::ToolResult;
    toolResult.m_EditorKind = AI_EDITOR_KIND::Pcb;
    toolResult.m_ActionName = wxS( "common.Control.zoomFitScreen" );
    toolResult.m_ResultJson = wxS( "{\"status\":\"executed\"}" );
    toolResult.m_Allowed = true;
    toolResult.m_Executed = true;

    AI_AGENT_OBSERVABILITY_LOG log;
    std::vector<AI_AGENT_OBSERVABILITY_ENTRY> entries =
            log.Build( {}, { toolCall, toolResult }, {}, 16 );

    BOOST_REQUIRE_EQUAL( entries.size(), 2 );
    BOOST_CHECK( entries.at( 0 ).m_Kind == AI_AGENT_OBSERVABILITY_KIND::ModelToolCall );
    BOOST_CHECK_EQUAL( entries.at( 0 ).m_ToolCallId, wxString( wxS( "call_1" ) ) );
    BOOST_CHECK( entries.at( 0 ).m_DetailsJson.Contains( wxS( "zoomFitScreen" ) ) );
    BOOST_CHECK( entries.at( 1 ).m_Kind == AI_AGENT_OBSERVABILITY_KIND::ToolResult );
    BOOST_CHECK( entries.at( 1 ).m_Allowed );
    BOOST_CHECK( entries.at( 1 ).m_Executed );
}


BOOST_AUTO_TEST_CASE( SuggestionsProduceLifecycleEntries )
{
    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_Id = 3;
    suggestion.m_Sequence = 11;
    suggestion.m_Title = wxS( "Preview next via" );
    suggestion.m_Body = wxS( "Detected equal via spacing." );
    suggestion.m_Status = AI_SUGGESTION_STATUS::Previewing;
    suggestion.m_ArgumentsJson = wxS( "{\"operation\":\"place_via_preview\"}" );
    suggestion.m_ContextKind = wxS( "layout" );
    suggestion.m_ContextDetailsJson =
            wxS( "{\"source\":\"tool_state\",\"tool_state_kind\":\"placing_via\","
                 "\"reason\":\"via_pattern\"}" );

    AI_AGENT_OBSERVABILITY_LOG log;
    std::vector<AI_AGENT_OBSERVABILITY_ENTRY> entries =
            log.Build( {}, {}, { suggestion }, 16 );

    BOOST_REQUIRE_EQUAL( entries.size(), 1 );
    BOOST_CHECK( entries.front().m_Kind == AI_AGENT_OBSERVABILITY_KIND::Suggestion );
    BOOST_CHECK( entries.front().m_Summary.Contains( wxS( "Previewing" ) ) );
    BOOST_CHECK( entries.front().m_DetailsJson.Contains( wxS( "place_via_preview" ) ) );

    nlohmann::json details =
            nlohmann::json::parse( entries.front().m_DetailsJson.ToStdString() );
    BOOST_CHECK_EQUAL( details["context_kind"].get<std::string>(), "layout" );
    BOOST_CHECK_EQUAL( details["context_details"]["reason"].get<std::string>(),
                       "via_pattern" );
    BOOST_CHECK_EQUAL(
            details["context_details"]["tool_state_kind"].get<std::string>(),
            "placing_via" );
}


BOOST_AUTO_TEST_CASE( BuildReturnsMostRecentEntriesWithinLimit )
{
    std::vector<AI_ACTIVITY_RECORD> activity;

    for( int ii = 0; ii < 6; ++ii )
    {
        AI_ACTIVITY_RECORD record;
        record.m_Sequence = static_cast<uint64_t>( ii + 1 );
        record.m_Kind = AI_ACTIVITY_KIND::UserAction;
        record.m_ActionName = wxString::Format( wxS( "action.%d" ), ii + 1 );
        activity.push_back( record );
    }

    AI_AGENT_OBSERVABILITY_LOG log;
    std::vector<AI_AGENT_OBSERVABILITY_ENTRY> entries = log.Build( {}, activity, {}, 3 );

    BOOST_REQUIRE_EQUAL( entries.size(), 3 );
    BOOST_CHECK( entries.at( 0 ).m_Summary.Contains( wxS( "action.4" ) ) );
    BOOST_CHECK( entries.at( 2 ).m_Summary.Contains( wxS( "action.6" ) ) );
}


BOOST_AUTO_TEST_CASE( DetailsRedactSecretsAndDoNotCopyRawVisualData )
{
    AI_TRACE_RECORD trace;
    trace.m_RequestId = 9;
    trace.m_Request.m_UserText = wxS( "inspect" );
    trace.m_Request.m_ContextSnapshot.m_Visual.m_Source = wxS( "canvas" );
    trace.m_Request.m_ContextSnapshot.m_Visual.m_MimeType = wxS( "image/png" );
    trace.m_Request.m_ContextSnapshot.m_Visual.m_DataUri =
            wxS( "data:image/png;base64,AAAASECRETIMAGEPAYLOAD" );
    trace.m_Request.m_ContextSnapshot.m_Visual.m_WidthPx = 100;
    trace.m_Request.m_ContextSnapshot.m_Visual.m_HeightPx = 50;
    trace.m_Request.m_ContextSnapshot.m_Visual.m_ByteSize = 24;
    trace.m_Request.m_ContextSnapshot.m_Summary =
            wxS( "provider credential: [test-token-value]" );
    trace.m_Response.m_Body = wxS( "ok" );

    AI_TOOL_CALL_RECORD call;
    call.m_ToolCallId = wxS( "call_visual" );
    call.m_ToolName = wxS( "kisurf_get_workspace_view" );
    call.m_ResultJson =
            wxS( "{\"visual\":{\"data_uri\":\"data:image/png;base64,AAAASECRETTOOLPAYLOAD\"}}" );
    trace.m_Response.m_ToolCalls.push_back( call );

    AI_AGENT_OBSERVABILITY_LOG log;
    std::vector<AI_AGENT_OBSERVABILITY_ENTRY> entries = log.Build( { trace }, {}, {}, 16 );

    BOOST_REQUIRE_GE( entries.size(), 2 );
    BOOST_CHECK( !entries.front().m_DetailsJson.Contains( wxS( "[test-token-value]" ) ) );
    BOOST_CHECK( !entries.front().m_DetailsJson.Contains( wxS( "AAAASECRETIMAGEPAYLOAD" ) ) );
    BOOST_CHECK( entries.front().m_DetailsJson.Contains( wxS( "\"has_pixels\":true" ) ) );
    BOOST_CHECK( !entries.at( 1 ).m_DetailsJson.Contains( wxS( "AAAASECRETTOOLPAYLOAD" ) ) );
    BOOST_CHECK( entries.at( 1 ).m_DetailsJson.Contains( wxS( "[visual-data-redacted]" ) ) );
}

BOOST_AUTO_TEST_SUITE_END()
