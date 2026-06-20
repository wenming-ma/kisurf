#include <boost/test/unit_test.hpp>

#include <json_common.h>
#include <kisurf/ai/ai_agent_panel_model.h>
#include <kisurf/ai/ai_agent_panel_semantic.h>
#include <kisurf/ai/ai_provider.h>
#include <kisurf/ai/ai_semantic_tool_call_handler.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include <wx/arrstr.h> // for MSVC to see std::vector<wxString> is exported from wx
#include <wx/utils.h>

namespace
{
class ENV_GUARD
{
public:
    explicit ENV_GUARD( wxString aName ) :
            m_Name( std::move( aName ) ),
            m_HadValue( wxGetEnv( m_Name, &m_Value ) )
    {
    }

    ~ENV_GUARD()
    {
        if( m_HadValue )
            wxSetEnv( m_Name, m_Value );
        else
            wxUnsetEnv( m_Name );
    }

private:
    wxString m_Name;
    wxString m_Value;
    bool     m_HadValue = false;
};


AI_TOOL_CALL_RECORD toolCall( const wxString& aToolName, const wxString& aArguments )
{
    AI_TOOL_CALL_RECORD call;
    call.m_RequestId = 901;
    call.m_ToolCallId = wxS( "call_direct_use" );
    call.m_ToolName = aToolName;
    call.m_ArgumentsJson = aArguments;
    return call;
}


AI_CONTEXT_ANCHOR positionedRouteAnchor( const wxString& aId,
                                         AI_CONTEXT_ANCHOR_KIND aKind,
                                         const VECTOR2I& aPosition )
{
    AI_CONTEXT_ANCHOR anchor;
    anchor.m_Id = aId;
    anchor.m_Kind = aKind;
    anchor.m_EditorKind = AI_EDITOR_KIND::Pcb;
    anchor.m_Label = aId;
    anchor.m_Summary = wxS( "direct-use smoke anchor" );
    anchor.m_Position = aPosition;
    anchor.m_HasPosition = true;
    anchor.m_Layer = 0;
    anchor.m_Confidence = 1.0;
    return anchor;
}


AI_PROVIDER_REQUEST directUseWorkspaceRequest()
{
    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 901;
    request.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_UserText = wxS( "show me what I can do now" );

    AI_CONTEXT_SNAPSHOT& snapshot = request.m_ContextSnapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_Summary = wxS( "direct-use smoke workspace" );
    snapshot.m_Version.m_DocumentRevision = 11;
    snapshot.m_Version.m_SelectionRevision = 2;
    snapshot.m_Version.m_ViewRevision = 5;
    snapshot.m_SelectedObjects.push_back(
            AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ) ) );
    snapshot.m_VisibleObjects.push_back(
            AI_OBJECT_REF( KIID(), PCB_FOOTPRINT_T, wxS( "U1" ),
                           wxS( "{\"ref\":\"U1\"}" ) ) );

    AI_ACTION_DESCRIPTOR routeAction;
    routeAction.m_Name = wxS( "pcbnew.InteractiveRoute" );
    routeAction.m_FriendlyName = wxS( "Route Track" );
    routeAction.m_Description = wxS( "Start routing a PCB track." );
    routeAction.m_EditorKind = AI_EDITOR_KIND::Pcb;
    routeAction.m_Safety = AI_ACTION_SAFETY::Interactive;
    routeAction.m_Enabled = true;
    snapshot.m_Actions.push_back( routeAction );

    AI_ACTIVITY_RECORD activity;
    activity.m_Sequence = 1;
    activity.m_Kind = AI_ACTIVITY_KIND::UserAction;
    activity.m_EditorKind = AI_EDITOR_KIND::Pcb;
    activity.m_ActionName = wxS( "pcbnew.InteractiveRoute" );
    activity.m_Message = wxS( "routing started" );
    activity.m_Allowed = true;
    activity.m_Executed = true;
    snapshot.m_RecentActivity.push_back( activity );

    activity.m_Sequence = 2;
    activity.m_Kind = AI_ACTIVITY_KIND::ModelToolRequest;
    activity.m_ToolCallId = wxS( "call_workspace" );
    activity.m_ActionName = wxS( "kisurf_get_workspace_view" );
    activity.m_Message = wxS( "model requested workspace view" );
    snapshot.m_RecentActivity.push_back( activity );

    snapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::RoutingTrack;
    snapshot.m_ToolState.m_ContextVersion = snapshot.m_Version;
    snapshot.m_ToolState.m_ActiveActionName = wxS( "pcbnew.InteractiveRoute" );
    snapshot.m_ToolState.m_HasCursorBoardPosition = true;
    snapshot.m_ToolState.m_CursorBoardPosition = VECTOR2I( 400, 800 );
    snapshot.m_ToolState.m_ModeContextJson =
            wxS( "{\"net\":\"/GPIO\",\"layer\":\"F.Cu\",\"width\":150000}" );

    snapshot.m_Visual.m_Source = wxS( "pcbnew.canvas" );
    snapshot.m_Visual.m_MimeType = wxS( "image/png" );
    snapshot.m_Visual.m_DataUri = wxS( "data:image/png;base64,dW5pdA==" );
    snapshot.m_Visual.m_WidthPx = 1280;
    snapshot.m_Visual.m_HeightPx = 720;
    snapshot.m_Visual.m_ByteSize = 2048;

    snapshot.m_Anchors.push_back(
            positionedRouteAnchor( wxS( "tool.routing.start" ),
                                   AI_CONTEXT_ANCHOR_KIND::RouteStart,
                                   VECTOR2I( 100, 200 ) ) );
    snapshot.m_Anchors.push_back(
            positionedRouteAnchor( wxS( "pcb.pad.target" ),
                                   AI_CONTEXT_ANCHOR_KIND::RouteTarget,
                                   VECTOR2I( 500, 650 ) ) );

    AI_AGENT_PANEL_SEMANTIC_VIEW panelView;
    panelView.m_InputHasText = true;
    panelView.m_HasActiveSuggestion = true;
    panelView.m_CanPreviewSuggestion = true;
    panelView.m_CanAcceptSuggestion = false;
    panelView.m_MessageCount = 2;
    panelView.m_SuggestionCount = 1;
    panelView.m_LogEntryCount = 3;
    panelView.m_LogSummary = wxS( "model requested workspace view" );
    snapshot.m_PanelStates.push_back( AiAgentPanelSemanticStateRecord( panelView ) );

    return request;
}


AI_SUGGESTION_RECORD operationOnlyPanelFillSuggestion()
{
    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_EditorKind = AI_EDITOR_KIND::Pcb;
    suggestion.m_Kind = AI_SUGGESTION_KIND::Preview;
    suggestion.m_Title = wxS( "Fill clearance column" );
    suggestion.m_Body = wxS( "Preview panel table fill before committing it." );
    suggestion.m_ContextKind = wxS( "panel" );
    suggestion.m_ArgumentsJson = wxS(
            "{\"operation\":\"panel_fill_column_preview\","
            "\"panel_id\":\"board_setup.clearance\","
            "\"table_id\":\"clearance.rules\","
            "\"column_id\":\"clearance\","
            "\"value\":\"0.20 mm\","
            "\"target_row_ids\":[\"row.power\",\"row.signal\"]}" );
    return suggestion;
}
} // namespace


BOOST_AUTO_TEST_SUITE( AiDirectUseSmoke )


BOOST_AUTO_TEST_CASE( EnvironmentLowercaseBaseUrlAliasConfiguresProvider )
{
    ENV_GUARD kisurfBaseGuard( wxS( "KISURF_AI_BASE_URL" ) );
    ENV_GUARD openaiBaseGuard( wxS( "OPENAI_BASE_URL" ) );
    ENV_GUARD lowerBaseGuard( wxS( "base_url" ) );

    wxUnsetEnv( wxS( "KISURF_AI_BASE_URL" ) );
    wxUnsetEnv( wxS( "OPENAI_BASE_URL" ) );
    wxSetEnv( wxS( "base_url" ), wxS( "https://direct-use.example.test/v1/" ) );

    AI_PROVIDER_SETTINGS settings = AI_PROVIDER_SETTINGS::FromEnvironment();

    BOOST_CHECK_EQUAL( settings.m_BaseUrl,
                       wxString( wxS( "https://direct-use.example.test/v1" ) ) );
}


BOOST_AUTO_TEST_CASE( ProviderAdvertisesDirectUseToolSurface )
{
    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxS( "https://sub2api.wenming-dev.org/v1" );
    settings.m_ApiKey = wxS( "unit-test-key" );
    settings.m_Model = wxS( "unit-model" );

    AI_OPENAI_COMPAT_PROVIDER provider(
            settings,
            []( const AI_HTTP_REQUEST& aRequest, AI_HTTP_RESPONSE& aResponse,
                wxString& aError )
            {
                wxUnusedVar( aError );

                nlohmann::json body = nlohmann::json::parse( aRequest.m_Body.ToStdString() );
                BOOST_REQUIRE( body.contains( "tools" ) );
                BOOST_REQUIRE( body["tools"].is_array() );

                std::vector<std::string> toolNames;

                for( const nlohmann::json& tool : body["tools"] )
                    toolNames.push_back( tool["function"]["name"].get<std::string>() );

                const std::vector<std::string> requiredTools = {
                    "kisurf_run_action",
                    "kisurf_check_action",
                    "kisurf_get_context_snapshot",
                    "kisurf_get_visual_frame",
                    "kisurf_get_activity_timeline",
                    "kisurf_get_workspace_view",
                    "kisurf_invoke_semantic_ui_action",
                    "kisurf_open_session",
                    "kisurf_run_cell",
                    "kisurf_checkpoint",
                    "kisurf_rollback_to",
                    "kisurf_render_preview",
                    "kisurf_accept_session",
                };

                for( const std::string& requiredTool : requiredTools )
                {
                    BOOST_CHECK_MESSAGE(
                            std::find( toolNames.begin(), toolNames.end(), requiredTool )
                                    != toolNames.end(),
                            "missing AI direct-use tool: " << requiredTool );
                }

                BOOST_REQUIRE( body.contains( "parallel_tool_calls" ) );
                BOOST_CHECK( !body["parallel_tool_calls"].get<bool>() );

                aResponse.m_StatusCode = 200;
                aResponse.m_Body =
                        wxS( "{\"choices\":[{\"message\":{\"content\":\"ready\"}}]}" );
                return true;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 902;
    request.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_UserText = wxS( "inspect current workspace" );

    AI_PROVIDER_RESPONSE response = provider.Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 902 );
    BOOST_CHECK_EQUAL( response.m_Body, wxString( wxS( "ready" ) ) );
}


BOOST_AUTO_TEST_CASE( WorkspaceViewToolReturnsDirectUseStateBundle )
{
    AI_SEMANTIC_TOOL_CALL_HANDLER handler;

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            directUseWorkspaceRequest(),
            toolCall( wxS( "kisurf_get_workspace_view" ),
                      wxS( "{\"views\":[\"context\",\"visual\",\"activity\"],"
                           "\"context\":{\"include_panels\":true},"
                           "\"visual\":{\"include_pixels\":true,\"max_bytes\":4096},"
                           "\"activity\":{\"max_activity\":4}}" ) ) );

    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString() );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "workspace_view_ready" );

    const nlohmann::json& workspaceView = payload["workspace_view"];
    BOOST_REQUIRE( workspaceView.contains( "context" ) );
    BOOST_REQUIRE( workspaceView.contains( "visual" ) );
    BOOST_REQUIRE( workspaceView.contains( "activity" ) );

    BOOST_CHECK_EQUAL( workspaceView["context"]["editor"].get<std::string>(), "pcb" );
    BOOST_CHECK_EQUAL(
            workspaceView["context"]["dynamic_context"]["kind"].get<std::string>(),
            "routing" );
    BOOST_REQUIRE_EQUAL( workspaceView["context"]["panel_states"].size(), 1 );
    BOOST_CHECK_EQUAL( workspaceView["context"]["panel_states"][0]["id"].get<std::string>(),
                       "agent.panel" );

    BOOST_CHECK_EQUAL( workspaceView["visual"]["source"].get<std::string>(),
                       "pcbnew.canvas" );
    BOOST_CHECK_EQUAL( workspaceView["visual"]["data_uri"].get<std::string>(),
                       "data:image/png;base64,dW5pdA==" );
    BOOST_CHECK_EQUAL( workspaceView["visual"]["anchor_overlay_count"].get<int>(), 2 );

    BOOST_CHECK_EQUAL( workspaceView["activity"]["activity_count"].get<int>(), 2 );
    BOOST_REQUIRE_EQUAL( workspaceView["activity"]["records"].size(), 2 );
    BOOST_CHECK_EQUAL( workspaceView["activity"]["records"][0]["action"].get<std::string>(),
                       "pcbnew.InteractiveRoute" );
}


BOOST_AUTO_TEST_CASE( OperationOnlySuggestionIsPreviewableButNotAcceptable )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.AddSuggestion( operationOnlyPanelFillSuggestion() );
    BOOST_REQUIRE( suggestion.has_value() );

    BOOST_CHECK( model.CanPreviewSuggestion( suggestion->m_Id ) );
    BOOST_CHECK( !model.CanAcceptSuggestion( suggestion->m_Id ) );
}


BOOST_AUTO_TEST_SUITE_END()
