#include <boost/test/unit_test.hpp>

#include <json_common.h>
#include <kisurf/ai/ai_agent_panel_semantic.h>
#include <kisurf/ai/ai_semantic_tool_call_handler.h>
#include <kisurf/ai/ai_suggestion_operations.h>

#include <utility>

namespace
{
class FIXED_RESULT_TOOL_HANDLER : public AI_TOOL_CALL_HANDLER
{
public:
    explicit FIXED_RESULT_TOOL_HANDLER( wxString aErrorCode, bool aAllowed = false ) :
            m_ErrorCode( std::move( aErrorCode ) ),
            m_Allowed( aAllowed )
    {
    }

    AI_TOOL_INVOCATION_RESULT HandleToolCall( const AI_PROVIDER_REQUEST& aRequest,
                                              const AI_TOOL_CALL_RECORD& aToolCall ) override
    {
        ++m_CallCount;

        AI_TOOL_INVOCATION_RESULT result;
        result.m_RequestId = aRequest.m_RequestId;
        result.m_ToolCallId = aToolCall.m_ToolCallId;
        result.m_ActionName = aToolCall.m_ToolName;
        result.m_Allowed = m_Allowed;
        result.m_Executed = false;
        result.m_ErrorCode = m_ErrorCode;
        result.m_Message = m_Allowed ? wxS( "handled" ) : wxS( "not handled" );
        result.m_ResultJson = wxS( "{}" );
        return result;
    }

    int m_CallCount = 0;

private:
    wxString m_ErrorCode;
    bool     m_Allowed = false;
};


AI_PROVIDER_REQUEST requestWithSelection()
{
    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 41;
    request.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_ContextSnapshot.m_Version.m_DocumentRevision = 3;
    request.m_ContextSnapshot.m_Version.m_SelectionRevision = 2;
    request.m_ContextSnapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_ContextSnapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::Selecting;
    request.m_ContextSnapshot.m_SelectedObjects.push_back(
            AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ) ) );
    request.m_ContextSnapshot.m_SelectedObjects.push_back(
            AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.2" ) ) );
    return request;
}


AI_CONTEXT_ANCHOR positionedAnchor( const wxString& aId, AI_CONTEXT_ANCHOR_KIND aKind,
                                    const VECTOR2I& aPosition,
                                    const wxString& aDetailsJson = wxEmptyString )
{
    AI_CONTEXT_ANCHOR anchor;
    anchor.m_Id = aId;
    anchor.m_Kind = aKind;
    anchor.m_EditorKind = AI_EDITOR_KIND::Pcb;
    anchor.m_Label = aId;
    anchor.m_Summary = wxS( "test anchor" );
    anchor.m_Position = aPosition;
    anchor.m_HasPosition = true;
    anchor.m_Layer = 0;
    anchor.m_DetailsJson = aDetailsJson;
    anchor.m_Confidence = 1.0;
    return anchor;
}


AI_PROVIDER_REQUEST requestWithRouteAnchors()
{
    AI_PROVIDER_REQUEST request = requestWithSelection();
    request.m_ContextSnapshot.m_SelectedObjects.clear();
    request.m_ContextSnapshot.m_Anchors.push_back(
            positionedAnchor( wxS( "pcb.track.start" ),
                              AI_CONTEXT_ANCHOR_KIND::RouteStart,
                              VECTOR2I( 100, 200 ) ) );
    request.m_ContextSnapshot.m_Anchors.push_back(
            positionedAnchor( wxS( "pcb.pad.target" ),
                              AI_CONTEXT_ANCHOR_KIND::RouteTarget,
                              VECTOR2I( 500, 650 ) ) );
    return request;
}


AI_PROVIDER_REQUEST requestWithPanelTableState()
{
    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 74;
    request.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_ContextSnapshot.m_Version.m_DocumentRevision = 9;
    request.m_ContextSnapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_ContextSnapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::Unknown;

    AI_PANEL_STATE_RECORD panel;
    panel.m_Id = wxS( "board_setup.clearance" );
    panel.m_Title = wxS( "Board Setup" );
    panel.m_FocusedControlId = wxS( "clearance.rules.row.default.clearance" );
    panel.m_FocusedControlLabel = wxS( "Clearance" );
    panel.m_StateJson = wxS(
            "{\"tables\":[{\"id\":\"clearance.rules\","
            "\"title\":\"Clearance rules\","
            "\"columns\":[{\"id\":\"clearance\",\"label\":\"Clearance\"}],"
            "\"rows\":["
            "{\"id\":\"row.default\",\"label\":\"Default\","
            "\"cells\":{\"clearance\":\"0.20 mm\"}},"
            "{\"id\":\"row.power\",\"label\":\"Power\","
            "\"cells\":{\"clearance\":\"\"}},"
            "{\"id\":\"row.signal\",\"label\":\"Signal\","
            "\"cells\":{\"clearance\":\"\"}}]}]}" );
    request.m_ContextSnapshot.m_PanelStates.push_back( panel );
    return request;
}


wxString panelFillColumnArguments(
        const wxString& aPanelId = wxS( "board_setup.clearance" ),
        const wxString& aTableId = wxS( "clearance.rules" ),
        const wxString& aColumnId = wxS( "clearance" ),
        const wxString& aSecondTargetRowId = wxS( "row.signal" ) )
{
    wxString args;
    args << wxS( "{\"panel_id\":\"" ) << aPanelId
         << wxS( "\",\"table_id\":\"" ) << aTableId
         << wxS( "\",\"column_id\":\"" ) << aColumnId
         << wxS( "\",\"value\":\"0.20 mm\","
                  "\"target_row_ids\":[\"row.power\",\"" )
         << aSecondTargetRowId << wxS( "\"]}" );
    return args;
}


AI_PROVIDER_REQUEST requestWithUnifiedContext()
{
    AI_PROVIDER_REQUEST request = requestWithSelection();
    request.m_RequestId = 62;
    request.m_ContextSnapshot.m_Summary = wxS( "unit context summary" );
    request.m_ContextSnapshot.m_Version.m_ViewRevision = 9;
    request.m_ContextSnapshot.m_VisibleObjects.push_back(
            AI_OBJECT_REF( KIID(), PCB_FOOTPRINT_T, wxS( "U1" ),
                           wxS( "{\"ref\":\"U1\"}" ) ) );
    request.m_ContextSnapshot.m_VisibleObjects.push_back(
            AI_OBJECT_REF( KIID(), PCB_TRACE_T, wxS( "Net-(U1-Pad1)" ) ) );
    request.m_ContextSnapshot.m_VisibleObjects.push_back(
            AI_OBJECT_REF( KIID(), PCB_VIA_T, wxS( "via:/GPIO" ) ) );

    AI_ACTION_DESCRIPTOR action;
    action.m_Name = wxS( "pcbnew.InteractiveRoute" );
    action.m_FriendlyName = wxS( "Route Track" );
    action.m_Description = wxS( "Start routing a PCB track." );
    action.m_EditorKind = AI_EDITOR_KIND::Pcb;
    action.m_Safety = AI_ACTION_SAFETY::Interactive;
    action.m_Enabled = true;
    request.m_ContextSnapshot.m_Actions.push_back( action );
    action.m_Name = wxS( "pcbnew.ZoomSelection" );
    action.m_FriendlyName = wxS( "Zoom Selection" );
    action.m_Safety = AI_ACTION_SAFETY::ReadOnly;
    request.m_ContextSnapshot.m_Actions.push_back( action );

    AI_ACTIVITY_RECORD activity;
    activity.m_Sequence = 7;
    activity.m_RequestId = request.m_RequestId;
    activity.m_Kind = AI_ACTIVITY_KIND::UserAction;
    activity.m_EditorKind = AI_EDITOR_KIND::Pcb;
    activity.m_ActionName = wxS( "pcbnew.InteractiveRoute" );
    activity.m_Message = wxS( "routing started" );
    request.m_ContextSnapshot.m_RecentActivity.push_back( activity );
    activity.m_Sequence = 8;
    activity.m_Kind = AI_ACTIVITY_KIND::ModelToolRequest;
    activity.m_ToolCallId = wxS( "call_route" );
    request.m_ContextSnapshot.m_RecentActivity.push_back( activity );

    request.m_ContextSnapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_ContextSnapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::RoutingTrack;
    request.m_ContextSnapshot.m_ToolState.m_ContextVersion =
            request.m_ContextSnapshot.m_Version;
    request.m_ContextSnapshot.m_ToolState.m_ActiveActionName =
            wxS( "pcbnew.InteractiveRoute" );
    request.m_ContextSnapshot.m_ToolState.m_HasCursorBoardPosition = true;
    request.m_ContextSnapshot.m_ToolState.m_CursorBoardPosition = VECTOR2I( 400, 800 );
    request.m_ContextSnapshot.m_ToolState.m_ModeContextJson =
            wxS( "{\"net\":\"/GPIO\",\"layer\":\"F.Cu\",\"width\":150000}" );

    request.m_ContextSnapshot.m_Visual.m_Source = wxS( "pcbnew.canvas" );
    request.m_ContextSnapshot.m_Visual.m_MimeType = wxS( "image/png" );
    request.m_ContextSnapshot.m_Visual.m_DataUri =
            wxS( "data:image/png;base64,dW5pdA==" );
    request.m_ContextSnapshot.m_Visual.m_WidthPx = 1280;
    request.m_ContextSnapshot.m_Visual.m_HeightPx = 720;
    request.m_ContextSnapshot.m_Visual.m_ByteSize = 2048;

    request.m_ContextSnapshot.m_Anchors.push_back(
            positionedAnchor( wxS( "tool.routing.start" ),
                              AI_CONTEXT_ANCHOR_KIND::RouteStart,
                              VECTOR2I( 100, 200 ),
                              wxS( "{\"net\":\"/GPIO\",\"layer\":\"F.Cu\"}" ) ) );
    request.m_ContextSnapshot.m_Anchors.push_back(
            positionedAnchor( wxS( "pcb.pad.target" ),
                              AI_CONTEXT_ANCHOR_KIND::RouteTarget,
                              VECTOR2I( 500, 650 ) ) );

    AI_PANEL_STATE_RECORD panel;
    panel.m_Id = wxS( "agent.review" );
    panel.m_Title = wxS( "Agent Review" );
    panel.m_FocusedControlId = wxS( "accept-preview" );
    panel.m_FocusedControlLabel = wxS( "Accept" );
    panel.m_Summary = wxS( "route preview pending" );
    panel.m_StateJson = wxS( "{\"preview\":\"route\"}" );
    request.m_ContextSnapshot.m_PanelStates.push_back( panel );
    panel.m_Id = wxS( "properties.selection" );
    panel.m_Title = wxS( "Properties" );
    panel.m_FocusedControlId.clear();
    panel.m_FocusedControlLabel.clear();
    panel.m_SelectedText.clear();
    panel.m_Summary.clear();
    panel.m_StateJson.clear();
    request.m_ContextSnapshot.m_PanelStates.push_back( panel );

    return request;
}


AI_PROVIDER_REQUEST requestWithPlacementContext()
{
    AI_PROVIDER_REQUEST request = requestWithUnifiedContext();
    request.m_ContextSnapshot.m_ToolState.m_Kind =
            AI_TOOL_STATE_KIND::PlacingFootprint;
    request.m_ContextSnapshot.m_ToolState.m_ActiveActionName =
            wxS( "pcbnew.placeFootprint" );
    request.m_ContextSnapshot.m_ToolState.m_ModeContextJson =
            wxS( "{\"mode\":\"placing_footprint\","
                 "\"cursor\":{\"x\":1000,\"y\":2000}}" );
    request.m_ContextSnapshot.m_Anchors.clear();
    request.m_ContextSnapshot.m_Anchors.push_back(
            positionedAnchor( wxS( "tool.placement.cursor" ),
                              AI_CONTEXT_ANCHOR_KIND::PlacementCandidate,
                              VECTOR2I( 1000, 2000 ) ) );
    request.m_ContextSnapshot.m_Anchors.push_back(
            positionedAnchor( wxS( "tool.placement.grid.east" ),
                              AI_CONTEXT_ANCHOR_KIND::PlacementCandidate,
                              VECTOR2I( 2000, 2000 ) ) );
    request.m_ContextSnapshot.m_Anchors.push_back(
            positionedAnchor( wxS( "pcb.pad.target" ),
                              AI_CONTEXT_ANCHOR_KIND::RouteTarget,
                              VECTOR2I( 5000, 6000 ) ) );
    return request;
}


AI_PROVIDER_REQUEST requestWithActivityTimeline()
{
    AI_PROVIDER_REQUEST request = requestWithUnifiedContext();
    request.m_ContextSnapshot.m_RecentActivity.clear();

    AI_ACTIVITY_RECORD activity;
    activity.m_Sequence = 1;
    activity.m_Kind = AI_ACTIVITY_KIND::UserAction;
    activity.m_EditorKind = AI_EDITOR_KIND::Pcb;
    activity.m_ActionName = wxS( "common.Interactive.selected" );
    activity.m_ArgumentsJson = wxS( "{\"category\":\"message\",\"action\":\"event\"}" );
    activity.m_Allowed = true;
    activity.m_Executed = true;
    activity.m_Message = wxS( "selection changed" );
    request.m_ContextSnapshot.m_RecentActivity.push_back( activity );

    activity.m_Sequence = 2;
    activity.m_ActionName = wxS( "mouse.click" );
    activity.m_ArgumentsJson = wxS( "{\"category\":\"mouse\",\"action\":\"click\"}" );
    activity.m_Message = wxS( "left click" );
    request.m_ContextSnapshot.m_RecentActivity.push_back( activity );

    activity = AI_ACTIVITY_RECORD();
    activity.m_Sequence = 3;
    activity.m_RequestId = 62;
    activity.m_ToolCallId = wxS( "call_context" );
    activity.m_Kind = AI_ACTIVITY_KIND::ModelToolRequest;
    activity.m_EditorKind = AI_EDITOR_KIND::Pcb;
    activity.m_ActionName = wxS( "kisurf_get_context_snapshot" );
    activity.m_ArgumentsJson = wxS( "{}" );
    activity.m_Message = wxS( "model requested context" );
    request.m_ContextSnapshot.m_RecentActivity.push_back( activity );

    activity.m_Sequence = 4;
    activity.m_Kind = AI_ACTIVITY_KIND::ToolResult;
    activity.m_ResultJson = wxS( "{\"status\":\"context_ready\"}" );
    activity.m_Allowed = true;
    activity.m_Executed = false;
    activity.m_Message = wxS( "context returned" );
    request.m_ContextSnapshot.m_RecentActivity.push_back( activity );

    return request;
}


AI_TOOL_CALL_RECORD toolCall( const wxString& aToolName, const wxString& aArguments )
{
    AI_TOOL_CALL_RECORD call;
    call.m_RequestId = 41;
    call.m_ToolCallId = wxS( "call_semantic" );
    call.m_ToolName = aToolName;
    call.m_ArgumentsJson = aArguments;
    return call;
}


void checkModelRetryFailureContract( const AI_TOOL_INVOCATION_RESULT& aResult )
{
    nlohmann::json payload = nlohmann::json::parse( aResult.m_ResultJson.ToStdString() );
    BOOST_CHECK( !payload["ok"].get<bool>() );
    BOOST_CHECK( payload["retryable"].is_boolean() );
    BOOST_CHECK( payload["retry_hint"].is_string() );
    BOOST_CHECK( !payload["retry_hint"].get<std::string>().empty() );
}


AI_SEMANTIC_UI_NODE semanticUiNode( const wxString& aNodeId,
                                    const wxString& aAction,
                                    bool aEnabled = true,
                                    bool aRequiresConfirmation = false )
{
    AI_SEMANTIC_UI_NODE node;
    node.m_NodeId = aNodeId;
    node.m_Role = wxS( "button" );
    node.m_Label = aNodeId;
    node.m_ActionName = aAction;
    node.m_Enabled = aEnabled;
    node.m_RequiresUserConfirmation = aRequiresConfirmation;
    return node;
}


AI_SEMANTIC_UI_TREE semanticUiActionTree()
{
    AI_SEMANTIC_UI_TREE tree;
    tree.m_FrameId = wxS( "agent" );
    tree.m_Title = wxS( "Agent" );
    tree.m_Nodes.push_back( semanticUiNode( wxS( "agent.input" ),
                                            wxS( "set_text" ) ) );
    tree.m_Nodes.back().m_Role = wxS( "textbox" );
    tree.m_Nodes.push_back( semanticUiNode( wxS( "agent.send" ),
                                            wxS( "invoke" ) ) );
    tree.m_Nodes.push_back( semanticUiNode( wxS( "agent.accept" ),
                                            wxS( "invoke" ), true, true ) );
    tree.m_Nodes.push_back( semanticUiNode( wxS( "agent.disabled" ),
                                            wxS( "invoke" ), false ) );
    return tree;
}
} // namespace


BOOST_AUTO_TEST_SUITE( AiSemanticToolCallHandler )


BOOST_AUTO_TEST_CASE( LegacyPreviewCompositeToolsAreRejected )
{
    AI_SEMANTIC_TOOL_CALL_HANDLER handler;

    const wxString legacyTools[] = {
        wxS( "kisurf_preview_move_selected" ),
        wxS( "kisurf_preview_create_copper_zone" ),
        wxS( "kisurf_preview_anchor_focus" ),
        wxS( "kisurf_preview_panel_fill_column" ),
        wxS( "kisurf_preview_route_to_anchor" ),
    };

    for( const wxString& toolName : legacyTools )
    {
        AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
                requestWithUnifiedContext(), toolCall( toolName, wxS( "{}" ) ) );

        BOOST_TEST_CONTEXT( toolName )
        {
            BOOST_CHECK( !result.m_Allowed );
            BOOST_CHECK( !result.m_Executed );
            BOOST_CHECK_EQUAL( result.m_ErrorCode,
                               wxString( wxS( "unknown_tool" ) ) );
        }
    }
}


BOOST_AUTO_TEST_CASE( LegacyObservationToolsAreRejected )
{
    AI_SEMANTIC_TOOL_CALL_HANDLER handler;

    const wxString legacyTools[] = {
        wxS( "kisurf_get_context_snapshot" ),
        wxS( "kisurf_get_visual_frame" ),
        wxS( "kisurf_get_activity_timeline" ),
    };

    for( const wxString& toolName : legacyTools )
    {
        AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
                requestWithUnifiedContext(), toolCall( toolName, wxS( "{}" ) ) );

        BOOST_TEST_CONTEXT( toolName )
        {
            BOOST_CHECK( !result.m_Allowed );
            BOOST_CHECK( !result.m_Executed );
            BOOST_CHECK_EQUAL( result.m_ErrorCode,
                               wxString( wxS( "unknown_tool" ) ) );
        }
    }
}


BOOST_AUTO_TEST_CASE( SemanticToolFailuresIncludeRetryContract )
{
    AI_SEMANTIC_TOOL_CALL_HANDLER handler;

    AI_TOOL_INVOCATION_RESULT unknown = handler.HandleToolCall(
            requestWithUnifiedContext(),
            toolCall( wxS( "kisurf_get_visual_frame" ), wxS( "{}" ) ) );

    BOOST_CHECK_EQUAL( unknown.m_ErrorCode, wxString( wxS( "unknown_tool" ) ) );
    checkModelRetryFailureContract( unknown );

    AI_TOOL_INVOCATION_RESULT malformed = handler.HandleToolCall(
            requestWithActivityTimeline(),
            toolCall( wxS( "kisurf_get_workspace_view" ),
                      wxS( "{\"views\":[\"layers\"]}" ) ) );

    BOOST_CHECK_EQUAL( malformed.m_ErrorCode,
                       wxString( wxS( "malformed_arguments" ) ) );
    checkModelRetryFailureContract( malformed );
}


BOOST_AUTO_TEST_CASE( WorkspaceViewToolReturnsAllSectionsByDefault )
{
    AI_SEMANTIC_TOOL_CALL_HANDLER handler;

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithActivityTimeline(),
            toolCall( wxS( "kisurf_get_workspace_view" ), wxS( "{}" ) ) );

    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString() );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "workspace_view_ready" );
    BOOST_CHECK_EQUAL( payload["tool"].get<std::string>(), "kisurf_get_workspace_view" );

    const nlohmann::json& view = payload["workspace_view"];
    BOOST_CHECK( view.contains( "context" ) );
    BOOST_CHECK( view.contains( "visual" ) );
    BOOST_CHECK( view.contains( "activity" ) );
    BOOST_CHECK_EQUAL( view["context"]["editor"].get<std::string>(), "pcb" );
    BOOST_CHECK_EQUAL( view["context"]["dynamic_context"]["kind"].get<std::string>(),
                       "routing" );
    BOOST_CHECK_EQUAL( view["visual"]["source"].get<std::string>(), "pcbnew.canvas" );
    BOOST_REQUIRE( view["visual"].contains( "render_directives" ) );
    const nlohmann::json& directives = view["visual"]["render_directives"];
    BOOST_CHECK_EQUAL( directives["focus_layer"].get<std::string>(), "F.Cu" );
    BOOST_CHECK_EQUAL( directives["focus_net"].get<std::string>(), "/GPIO" );
    BOOST_CHECK( directives["dim_unfocused_layers"].get<bool>() );
    BOOST_CHECK_EQUAL( view["activity"]["activity_count"].get<int>(), 4 );
}


BOOST_AUTO_TEST_CASE( WorkspaceViewToolReturnsSummaryHeader )
{
    AI_SEMANTIC_TOOL_CALL_HANDLER handler;

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithActivityTimeline(),
            toolCall( wxS( "kisurf_get_workspace_view" ),
                      wxS( "{\"views\":[\"visual\"]}" ) ) );

    BOOST_CHECK( result.m_Allowed );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    const nlohmann::json& view = payload["workspace_view"];

    BOOST_REQUIRE( view.contains( "summary" ) );
    const nlohmann::json& summary = view["summary"];

    BOOST_REQUIRE_EQUAL( summary["included_views"].size(), 1 );
    BOOST_CHECK_EQUAL( summary["included_views"][0].get<std::string>(), "visual" );
    BOOST_CHECK_EQUAL( summary["editor"].get<std::string>(), "pcb" );
    BOOST_CHECK_EQUAL( summary["dynamic_context_kind"].get<std::string>(),
                       "routing" );
    BOOST_CHECK_EQUAL( summary["dynamic_context_source"].get<std::string>(),
                       "tool_state" );
    BOOST_CHECK_EQUAL( summary["tool_state_kind"].get<std::string>(),
                       "routing_track" );
    BOOST_CHECK_EQUAL( summary["selected_object_count"].get<int>(), 2 );
    BOOST_CHECK_EQUAL( summary["visible_object_count"].get<int>(), 3 );
    BOOST_CHECK_EQUAL( summary["anchor_count"].get<int>(), 2 );
    BOOST_CHECK_EQUAL( summary["panel_state_count"].get<int>(), 2 );
    BOOST_CHECK_EQUAL( summary["recent_activity_count"].get<int>(), 4 );
    BOOST_CHECK_EQUAL( summary["visual_source"].get<std::string>(),
                       "pcbnew.canvas" );
    BOOST_CHECK( summary["visual_has_pixels"].get<bool>() );
    BOOST_CHECK( !summary.contains( "data_uri" ) );
}


BOOST_AUTO_TEST_CASE( WorkspaceViewToolHighlightsPlacementAnchorsByDefault )
{
    AI_SEMANTIC_TOOL_CALL_HANDLER handler;

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithPlacementContext(),
            toolCall( wxS( "kisurf_get_workspace_view" ),
                      wxS( "{\"views\":[\"visual\"]}" ) ) );

    BOOST_CHECK( result.m_Allowed );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    const nlohmann::json& directives =
            payload["workspace_view"]["visual"]["render_directives"];
    BOOST_REQUIRE_EQUAL( directives["highlight_anchor_ids"].size(), 2 );
    BOOST_CHECK_EQUAL( directives["highlight_anchor_ids"][0].get<std::string>(),
                       "tool.placement.cursor" );
    BOOST_CHECK_EQUAL( directives["highlight_anchor_ids"][1].get<std::string>(),
                       "tool.placement.grid.east" );
}


BOOST_AUTO_TEST_CASE( WorkspaceViewToolCarriesAgentSemanticPanelState )
{
    AI_PROVIDER_REQUEST request = requestWithActivityTimeline();
    request.m_ContextSnapshot.m_PanelStates.clear();

    AI_AGENT_PANEL_SEMANTIC_VIEW panelView;
    panelView.m_InputHasText = true;
    panelView.m_MessageCount = 2;
    request.m_ContextSnapshot.m_PanelStates.push_back(
            AiAgentPanelSemanticStateRecord( panelView ) );

    AI_SEMANTIC_TOOL_CALL_HANDLER handler;

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            request,
            toolCall( wxS( "kisurf_get_workspace_view" ),
                      wxS( "{\"views\":[\"context\"],"
                           "\"context\":{\"include_panels\":true}}" ) ) );

    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString() );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    const nlohmann::json& panels =
            payload["workspace_view"]["context"]["panel_states"];

    BOOST_REQUIRE_EQUAL( panels.size(), 1 );
    BOOST_CHECK_EQUAL( panels[0]["id"].get<std::string>(), "agent.panel" );
    BOOST_CHECK_EQUAL( panels[0]["title"].get<std::string>(), "Agent" );

    const nlohmann::json& state = panels[0]["state"];
    BOOST_CHECK_EQUAL( state["frame_id"].get<std::string>(), "agent" );

    bool foundSend = false;

    for( const nlohmann::json& node : state["nodes"] )
    {
        if( node["id"].get<std::string>() == "agent.send" )
        {
            foundSend = true;
            BOOST_CHECK( node["enabled"].get<bool>() );
            BOOST_CHECK_EQUAL( node["action"].get<std::string>(), "invoke" );
        }
    }

    BOOST_CHECK( foundSend );
}


BOOST_AUTO_TEST_CASE( WorkspaceViewToolReturnsPanelsAsFirstClassSection )
{
    AI_SEMANTIC_TOOL_CALL_HANDLER handler;

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithActivityTimeline(),
            toolCall( wxS( "kisurf_get_workspace_view" ),
                      wxS( "{\"views\":[\"panels\"],"
                           "\"panels\":{\"max_panels\":1}}" ) ) );

    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString() );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    const nlohmann::json& view = payload["workspace_view"];

    BOOST_REQUIRE( view.contains( "summary" ) );
    BOOST_CHECK( !view.contains( "context" ) );
    BOOST_CHECK( !view.contains( "visual" ) );
    BOOST_CHECK( !view.contains( "activity" ) );
    BOOST_REQUIRE( view.contains( "panels" ) );

    BOOST_REQUIRE_EQUAL( view["summary"]["included_views"].size(), 1 );
    BOOST_CHECK_EQUAL( view["summary"]["included_views"][0].get<std::string>(),
                       "panels" );
    BOOST_CHECK_EQUAL( view["summary"]["panel_state_count"].get<int>(), 2 );

    const nlohmann::json& panels = view["panels"];
    BOOST_CHECK_EQUAL( panels["panel_state_count"].get<int>(), 2 );
    BOOST_CHECK_EQUAL( panels["matched_panel_count"].get<int>(), 2 );
    BOOST_REQUIRE_EQUAL( panels["records"].size(), 1 );
    BOOST_CHECK_EQUAL( panels["records"][0]["id"].get<std::string>(), "agent.review" );
    BOOST_CHECK_EQUAL( panels["records"][0]["title"].get<std::string>(),
                       "Agent Review" );
    BOOST_CHECK_EQUAL( panels["records"][0]["focused_control_id"].get<std::string>(),
                       "accept-preview" );
    BOOST_CHECK_EQUAL( panels["records"][0]["state"]["preview"].get<std::string>(),
                       "route" );
}


BOOST_AUTO_TEST_CASE( WorkspaceViewToolFiltersPanelsAndCanOmitState )
{
    AI_SEMANTIC_TOOL_CALL_HANDLER handler;

    AI_TOOL_INVOCATION_RESULT byId = handler.HandleToolCall(
            requestWithActivityTimeline(),
            toolCall( wxS( "kisurf_get_workspace_view" ),
                      wxS( "{\"views\":[\"panels\"],"
                           "\"panels\":{\"panel_id\":\"properties.selection\","
                           "\"include_state\":false}}" ) ) );

    BOOST_CHECK( byId.m_Allowed );

    nlohmann::json byIdPayload =
            nlohmann::json::parse( byId.m_ResultJson.ToStdString() );
    const nlohmann::json& byIdPanels = byIdPayload["workspace_view"]["panels"];

    BOOST_CHECK_EQUAL( byIdPanels["panel_state_count"].get<int>(), 2 );
    BOOST_CHECK_EQUAL( byIdPanels["matched_panel_count"].get<int>(), 1 );
    BOOST_REQUIRE_EQUAL( byIdPanels["records"].size(), 1 );
    BOOST_CHECK_EQUAL( byIdPanels["records"][0]["id"].get<std::string>(),
                       "properties.selection" );
    BOOST_CHECK( !byIdPanels["records"][0].contains( "state" ) );
    BOOST_CHECK( !byIdPanels["records"][0].contains( "state_raw" ) );

    AI_TOOL_INVOCATION_RESULT focusedOnly = handler.HandleToolCall(
            requestWithActivityTimeline(),
            toolCall( wxS( "kisurf_get_workspace_view" ),
                      wxS( "{\"views\":[\"panels\"],"
                           "\"panels\":{\"focused_only\":true}}" ) ) );

    BOOST_CHECK( focusedOnly.m_Allowed );

    nlohmann::json focusedPayload =
            nlohmann::json::parse( focusedOnly.m_ResultJson.ToStdString() );
    const nlohmann::json& focusedPanels = focusedPayload["workspace_view"]["panels"];

    BOOST_CHECK_EQUAL( focusedPanels["panel_state_count"].get<int>(), 2 );
    BOOST_CHECK_EQUAL( focusedPanels["matched_panel_count"].get<int>(), 1 );
    BOOST_REQUIRE_EQUAL( focusedPanels["records"].size(), 1 );
    BOOST_CHECK_EQUAL( focusedPanels["records"][0]["id"].get<std::string>(),
                       "agent.review" );
    BOOST_CHECK_EQUAL( focusedPanels["records"][0]["focused_control_id"].get<std::string>(),
                       "accept-preview" );
}


BOOST_AUTO_TEST_CASE( WorkspaceViewToolReturnsOnlyRequestedViewsWithNestedOptions )
{
    AI_SEMANTIC_TOOL_CALL_HANDLER handler;

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithActivityTimeline(),
            toolCall( wxS( "kisurf_get_workspace_view" ),
                      wxS( "{\"views\":[\"visual\",\"activity\",\"visual\"],"
                           "\"visual\":{\"include_pixels\":true,\"max_bytes\":4096},"
                           "\"activity\":{\"kind\":\"tool_result\"}}" ) ) );

    BOOST_CHECK( result.m_Allowed );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    const nlohmann::json& view = payload["workspace_view"];
    BOOST_CHECK( !view.contains( "context" ) );
    BOOST_CHECK( view.contains( "visual" ) );
    BOOST_CHECK( view.contains( "activity" ) );
    BOOST_CHECK_EQUAL( view["visual"]["data_uri"].get<std::string>(),
                       "data:image/png;base64,dW5pdA==" );
    BOOST_CHECK_EQUAL( view["activity"]["activity_count"].get<int>(), 1 );
    BOOST_REQUIRE_EQUAL( view["activity"]["records"].size(), 1 );
    BOOST_CHECK_EQUAL( view["activity"]["records"][0]["kind"].get<std::string>(),
                       "tool_result" );
}


BOOST_AUTO_TEST_CASE( WorkspaceViewToolPassesNestedVisualAnchorOverlayOptions )
{
    AI_SEMANTIC_TOOL_CALL_HANDLER handler;

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithUnifiedContext(),
            toolCall( wxS( "kisurf_get_workspace_view" ),
                      wxS( "{\"views\":[\"visual\"],"
                           "\"visual\":{\"include_anchor_overlays\":false}}" ) ) );

    BOOST_CHECK( result.m_Allowed );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    const nlohmann::json& visual = payload["workspace_view"]["visual"];
    BOOST_CHECK_EQUAL( visual["anchor_overlay_count"].get<int>(), 2 );
    BOOST_CHECK( !visual.contains( "anchor_overlays" ) );
}


BOOST_AUTO_TEST_CASE( WorkspaceViewToolPassesNestedVisualRenderDirectives )
{
    AI_SEMANTIC_TOOL_CALL_HANDLER handler;

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithUnifiedContext(),
            toolCall( wxS( "kisurf_get_workspace_view" ),
                      wxS( "{\"views\":[\"visual\"],"
                           "\"visual\":{\"focus_layer\":\"F.Cu\","
                           "\"highlight_anchor_ids\":[\"tool.routing.start\"]}}" ) ) );

    BOOST_CHECK( result.m_Allowed );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    const nlohmann::json& directives =
            payload["workspace_view"]["visual"]["render_directives"];
    BOOST_CHECK_EQUAL( directives["focus_layer"].get<std::string>(), "F.Cu" );
    BOOST_REQUIRE_EQUAL( directives["highlight_anchor_ids"].size(), 1 );
    BOOST_CHECK_EQUAL( directives["highlight_anchor_ids"][0].get<std::string>(),
                       "tool.routing.start" );
}


BOOST_AUTO_TEST_CASE( WorkspaceViewToolRejectsMalformedArguments )
{
    AI_SEMANTIC_TOOL_CALL_HANDLER handler;

    AI_TOOL_INVOCATION_RESULT badView = handler.HandleToolCall(
            requestWithActivityTimeline(),
            toolCall( wxS( "kisurf_get_workspace_view" ),
                      wxS( "{\"views\":[\"layers\"]}" ) ) );

    BOOST_CHECK( !badView.m_Allowed );
    BOOST_CHECK_EQUAL( badView.m_ErrorCode,
                       wxString( wxS( "malformed_arguments" ) ) );

    AI_TOOL_INVOCATION_RESULT badNested = handler.HandleToolCall(
            requestWithActivityTimeline(),
            toolCall( wxS( "kisurf_get_workspace_view" ),
                      wxS( "{\"activity\":{\"kind\":\"mouse\"}}" ) ) );

    BOOST_CHECK( !badNested.m_Allowed );
    BOOST_CHECK_EQUAL( badNested.m_ErrorCode,
                       wxString( wxS( "malformed_arguments" ) ) );

    AI_TOOL_INVOCATION_RESULT badPanelsObject = handler.HandleToolCall(
            requestWithActivityTimeline(),
            toolCall( wxS( "kisurf_get_workspace_view" ),
                      wxS( "{\"panels\":true}" ) ) );

    BOOST_CHECK( !badPanelsObject.m_Allowed );
    BOOST_CHECK_EQUAL( badPanelsObject.m_ErrorCode,
                       wxString( wxS( "malformed_arguments" ) ) );

    AI_TOOL_INVOCATION_RESULT badPanelsLimit = handler.HandleToolCall(
            requestWithActivityTimeline(),
            toolCall( wxS( "kisurf_get_workspace_view" ),
                      wxS( "{\"views\":[\"panels\"],"
                           "\"panels\":{\"max_panels\":0}}" ) ) );

    BOOST_CHECK( !badPanelsLimit.m_Allowed );
    BOOST_CHECK_EQUAL( badPanelsLimit.m_ErrorCode,
                       wxString( wxS( "malformed_arguments" ) ) );

    AI_TOOL_INVOCATION_RESULT badPanelId = handler.HandleToolCall(
            requestWithActivityTimeline(),
            toolCall( wxS( "kisurf_get_workspace_view" ),
                      wxS( "{\"views\":[\"panels\"],"
                           "\"panels\":{\"panel_id\":\"\"}}" ) ) );

    BOOST_CHECK( !badPanelId.m_Allowed );
    BOOST_CHECK_EQUAL( badPanelId.m_ErrorCode,
                       wxString( wxS( "malformed_arguments" ) ) );

    AI_TOOL_INVOCATION_RESULT badFocusedOnly = handler.HandleToolCall(
            requestWithActivityTimeline(),
            toolCall( wxS( "kisurf_get_workspace_view" ),
                      wxS( "{\"views\":[\"panels\"],"
                           "\"panels\":{\"focused_only\":\"yes\"}}" ) ) );

    BOOST_CHECK( !badFocusedOnly.m_Allowed );
    BOOST_CHECK_EQUAL( badFocusedOnly.m_ErrorCode,
                       wxString( wxS( "malformed_arguments" ) ) );

    AI_TOOL_INVOCATION_RESULT badIncludeState = handler.HandleToolCall(
            requestWithActivityTimeline(),
            toolCall( wxS( "kisurf_get_workspace_view" ),
                      wxS( "{\"views\":[\"panels\"],"
                           "\"panels\":{\"include_state\":\"yes\"}}" ) ) );

    BOOST_CHECK( !badIncludeState.m_Allowed );
    BOOST_CHECK_EQUAL( badIncludeState.m_ErrorCode,
                       wxString( wxS( "malformed_arguments" ) ) );
}


BOOST_AUTO_TEST_CASE( WorkspaceViewToolDeniesOversizeVisualPixels )
{
    AI_SEMANTIC_TOOL_CALL_HANDLER handler;

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithActivityTimeline(),
            toolCall( wxS( "kisurf_get_workspace_view" ),
                      wxS( "{\"views\":[\"visual\"],"
                           "\"visual\":{\"include_pixels\":true,\"max_bytes\":1024}}" ) ) );

    BOOST_CHECK( !result.m_Allowed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString( wxS( "visual_too_large" ) ) );
}


BOOST_AUTO_TEST_CASE( SemanticUiActionToolInvokesAllowedNode )
{
    int                           invokeCount = 0;
    AI_SEMANTIC_UI_ACTION_REQUEST captured;

    AI_SEMANTIC_TOOL_CALL_HANDLER handler(
            []()
            {
                return semanticUiActionTree();
            },
            [&]( const AI_SEMANTIC_UI_ACTION_REQUEST& aRequest )
            {
                ++invokeCount;
                captured = aRequest;

                AI_SEMANTIC_UI_ACTION_RESULT actionResult;
                actionResult.m_Success = true;
                actionResult.m_Message = wxS( "ok" );
                actionResult.m_FocusedNodeId = wxS( "agent.input" );
                return actionResult;
            } );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithSelection(),
            toolCall( wxS( "kisurf_invoke_semantic_ui_action" ),
                      wxS( "{\"node_id\":\"agent.input\","
                           "\"action\":\"set_text\","
                           "\"text\":\"hello\"}" ) ) );

    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString() );
    BOOST_CHECK_EQUAL( invokeCount, 1 );
    BOOST_CHECK_EQUAL( captured.m_NodeId, wxString( wxS( "agent.input" ) ) );
    BOOST_CHECK_EQUAL( captured.m_Action, wxString( wxS( "set_text" ) ) );
    BOOST_CHECK( captured.m_HasText );
    BOOST_CHECK_EQUAL( captured.m_Text, wxString( wxS( "hello" ) ) );
    BOOST_CHECK( !captured.m_Checked.has_value() );
    BOOST_CHECK( !captured.m_UserConfirmed );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "ui_action_executed" );
    BOOST_CHECK_EQUAL( payload["node_id"].get<std::string>(), "agent.input" );
    BOOST_CHECK_EQUAL( payload["action"].get<std::string>(), "set_text" );
    BOOST_CHECK_EQUAL( payload["focused_node_id"].get<std::string>(), "agent.input" );
}


BOOST_AUTO_TEST_CASE( SemanticUiActionToolRefusesConfirmationRequiredNode )
{
    bool invokerCalled = false;

    AI_SEMANTIC_TOOL_CALL_HANDLER handler(
            []()
            {
                return semanticUiActionTree();
            },
            [&]( const AI_SEMANTIC_UI_ACTION_REQUEST& )
            {
                invokerCalled = true;
                return AI_SEMANTIC_UI_ACTION_RESULT();
            } );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithSelection(),
            toolCall( wxS( "kisurf_invoke_semantic_ui_action" ),
                      wxS( "{\"node_id\":\"agent.accept\","
                           "\"action\":\"invoke\"}" ) ) );

    BOOST_CHECK( !result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode,
                       wxString( wxS( "confirmation_required" ) ) );
    BOOST_CHECK( !invokerCalled );
}


BOOST_AUTO_TEST_CASE( SemanticUiActionToolRefusesDisabledAndUnknownNodes )
{
    int invokeCount = 0;

    AI_SEMANTIC_TOOL_CALL_HANDLER handler(
            []()
            {
                return semanticUiActionTree();
            },
            [&]( const AI_SEMANTIC_UI_ACTION_REQUEST& )
            {
                ++invokeCount;
                return AI_SEMANTIC_UI_ACTION_RESULT();
            } );

    AI_TOOL_INVOCATION_RESULT disabled = handler.HandleToolCall(
            requestWithSelection(),
            toolCall( wxS( "kisurf_invoke_semantic_ui_action" ),
                      wxS( "{\"node_id\":\"agent.disabled\","
                           "\"action\":\"invoke\"}" ) ) );

    BOOST_CHECK( !disabled.m_Allowed );
    BOOST_CHECK_EQUAL( disabled.m_ErrorCode,
                       wxString( wxS( "disabled_node" ) ) );

    AI_TOOL_INVOCATION_RESULT unknown = handler.HandleToolCall(
            requestWithSelection(),
            toolCall( wxS( "kisurf_invoke_semantic_ui_action" ),
                      wxS( "{\"node_id\":\"agent.missing\","
                           "\"action\":\"invoke\"}" ) ) );

    BOOST_CHECK( !unknown.m_Allowed );
    BOOST_CHECK_EQUAL( unknown.m_ErrorCode,
                       wxString( wxS( "unknown_node" ) ) );
    BOOST_CHECK_EQUAL( invokeCount, 0 );
}


BOOST_AUTO_TEST_CASE( SemanticUiActionToolRejectsMalformedArgumentsAndMissingCallbacks )
{
    AI_SEMANTIC_TOOL_CALL_HANDLER handler(
            []()
            {
                return semanticUiActionTree();
            },
            []( const AI_SEMANTIC_UI_ACTION_REQUEST& )
            {
                return AI_SEMANTIC_UI_ACTION_RESULT();
            } );

    AI_TOOL_INVOCATION_RESULT missingAction = handler.HandleToolCall(
            requestWithSelection(),
            toolCall( wxS( "kisurf_invoke_semantic_ui_action" ),
                      wxS( "{\"node_id\":\"agent.input\"}" ) ) );

    BOOST_CHECK( !missingAction.m_Allowed );
    BOOST_CHECK_EQUAL( missingAction.m_ErrorCode,
                       wxString( wxS( "malformed_arguments" ) ) );

    AI_TOOL_INVOCATION_RESULT unknownField = handler.HandleToolCall(
            requestWithSelection(),
            toolCall( wxS( "kisurf_invoke_semantic_ui_action" ),
                      wxS( "{\"node_id\":\"agent.input\","
                           "\"action\":\"set_text\","
                           "\"user_confirmed\":true}" ) ) );

    BOOST_CHECK( !unknownField.m_Allowed );
    BOOST_CHECK_EQUAL( unknownField.m_ErrorCode,
                       wxString( wxS( "malformed_arguments" ) ) );

    AI_SEMANTIC_TOOL_CALL_HANDLER missingCallbacks;

    AI_TOOL_INVOCATION_RESULT unconfigured = missingCallbacks.HandleToolCall(
            requestWithSelection(),
            toolCall( wxS( "kisurf_invoke_semantic_ui_action" ),
                      wxS( "{\"node_id\":\"agent.input\","
                           "\"action\":\"set_text\","
                           "\"text\":\"hello\"}" ) ) );

    BOOST_CHECK( !unconfigured.m_Allowed );
    BOOST_CHECK_EQUAL( unconfigured.m_ErrorCode,
                       wxString( wxS( "handler_not_configured" ) ) );
}


BOOST_AUTO_TEST_CASE( SemanticUiActionToolRedactsFailureMessages )
{
    AI_SEMANTIC_TOOL_CALL_HANDLER handler(
            []()
            {
                return semanticUiActionTree();
            },
            []( const AI_SEMANTIC_UI_ACTION_REQUEST& )
            {
                AI_SEMANTIC_UI_ACTION_RESULT actionResult;
                actionResult.m_Success = false;
                actionResult.m_ErrorCode = wxS( "action_failed" );
                actionResult.m_Message =
                        wxS( "failed with OPENAI_API_KEY=unit-secret-value" );
                return actionResult;
            } );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithSelection(),
            toolCall( wxS( "kisurf_invoke_semantic_ui_action" ),
                      wxS( "{\"node_id\":\"agent.input\","
                           "\"action\":\"set_text\","
                           "\"text\":\"hello\"}" ) ) );

    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode,
                       wxString( wxS( "action_failed" ) ) );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "ui_action_failed" );
    BOOST_CHECK( payload["message"].get<std::string>().find( "unit-secret-value" )
                 == std::string::npos );
    BOOST_CHECK( payload["message"].get<std::string>().find( "[redacted]" )
                 != std::string::npos );
}


BOOST_AUTO_TEST_CASE( DispatcherFallsThroughUnknownToolToNextHandler )
{
    auto first = std::make_unique<FIXED_RESULT_TOOL_HANDLER>( wxS( "unknown_tool" ) );
    auto second = std::make_unique<FIXED_RESULT_TOOL_HANDLER>( wxString(), true );
    FIXED_RESULT_TOOL_HANDLER* firstPtr = first.get();
    FIXED_RESULT_TOOL_HANDLER* secondPtr = second.get();

    AI_TOOL_CALL_DISPATCHER dispatcher;
    dispatcher.AddHandler( std::move( first ) );
    dispatcher.AddHandler( std::move( second ) );

    AI_TOOL_INVOCATION_RESULT result = dispatcher.HandleToolCall(
            requestWithSelection(),
            toolCall( wxS( "kisurf_run_action" ),
                      wxS( "{\"action\":\"common.Control.showAgentPanel\"}" ) ) );

    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString() );
    BOOST_CHECK_EQUAL( firstPtr->m_CallCount, 1 );
    BOOST_CHECK_EQUAL( secondPtr->m_CallCount, 1 );
}


BOOST_AUTO_TEST_SUITE_END()
