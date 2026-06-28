#include <boost/test/unit_test.hpp>
#include <json_common.h>
#include <kisurf/ai/ai_agent_panel.h>
#include <kisurf/ai/ai_agent_panel_base.h>
#include <kisurf/ai/ai_agent_panel_model.h>
#include <kisurf/ai/ai_session_tool_call_handler.h>

#include <type_traits>
#include <vector>
#include <wx/defs.h>
#include <wx/panel.h>

BOOST_AUTO_TEST_SUITE( AiAgentPanel )


BOOST_AUTO_TEST_CASE( AgentPanelExposesWxPanelSurface )
{
    BOOST_CHECK( ( std::is_base_of_v<wxPanel, AI_AGENT_PANEL> ) );
}


class AI_AGENT_PANEL_BASE_SURFACE_TEST : public AI_AGENT_PANEL_BASE
{
public:
    explicit AI_AGENT_PANEL_BASE_SURFACE_TEST( wxWindow* aParent ) :
            AI_AGENT_PANEL_BASE( aParent )
    {
    }

    static constexpr bool HasTranscriptMember()
    {
        return std::is_member_object_pointer_v<
                decltype( &AI_AGENT_PANEL_BASE_SURFACE_TEST::m_Transcript )>;
    }

    static constexpr bool HasInputMember()
    {
        return std::is_member_object_pointer_v<
                decltype( &AI_AGENT_PANEL_BASE_SURFACE_TEST::m_Input )>;
    }

    static constexpr bool HasComposerPanelMember()
    {
        return std::is_member_object_pointer_v<
                decltype( &AI_AGENT_PANEL_BASE_SURFACE_TEST::m_ComposerPanel )>;
    }

    static constexpr bool HasComposerStatusMember()
    {
        return std::is_member_object_pointer_v<
                decltype( &AI_AGENT_PANEL_BASE_SURFACE_TEST::m_ComposerStatus )>;
    }

    static constexpr bool HasSendButtonMember()
    {
        return std::is_member_object_pointer_v<
                decltype( &AI_AGENT_PANEL_BASE_SURFACE_TEST::m_SendButton )>;
    }

    static constexpr bool HasNewChatButtonMember()
    {
        return std::is_member_object_pointer_v<
                decltype( &AI_AGENT_PANEL_BASE_SURFACE_TEST::m_NewChatButton )>;
    }

    static constexpr bool HasNewChatEvent()
    {
        return std::is_member_function_pointer_v<
                decltype( &AI_AGENT_PANEL_BASE_SURFACE_TEST::OnNewChat )>;
    }

    static constexpr bool HasPromptTextChangedEvent()
    {
        return std::is_member_function_pointer_v<
                decltype( &AI_AGENT_PANEL_BASE_SURFACE_TEST::OnPromptTextChanged )>;
    }

    static constexpr bool HasModeChoiceMember()
    {
        return HasModeChoiceMemberImpl<AI_AGENT_PANEL_BASE_SURFACE_TEST>();
    }

private:
    template<typename T>
    static constexpr bool HasModeChoiceMemberImpl()
    {
        return requires { &T::m_ModeChoice; };
    }
};


template<typename T>
static constexpr bool HasPublicSetActiveWorkspaceContext()
{
    return requires { &T::SetActiveWorkspaceContext; };
}


template<typename T>
static constexpr bool HasPublicActiveWorkspaceContext()
{
    return requires { &T::ActiveWorkspaceContext; };
}


BOOST_AUTO_TEST_CASE( AgentPanelInheritsGeneratedWxFormBuilderBase )
{
    BOOST_CHECK( ( std::is_base_of_v<AI_AGENT_PANEL_BASE, AI_AGENT_PANEL> ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelBaseExposesExpectedControlSurface )
{
    BOOST_CHECK( AI_AGENT_PANEL_BASE_SURFACE_TEST::HasTranscriptMember() );
    BOOST_CHECK( AI_AGENT_PANEL_BASE_SURFACE_TEST::HasComposerPanelMember() );
    BOOST_CHECK( AI_AGENT_PANEL_BASE_SURFACE_TEST::HasComposerStatusMember() );
    BOOST_CHECK( AI_AGENT_PANEL_BASE_SURFACE_TEST::HasInputMember() );
    BOOST_CHECK( AI_AGENT_PANEL_BASE_SURFACE_TEST::HasSendButtonMember() );
    BOOST_CHECK( AI_AGENT_PANEL_BASE_SURFACE_TEST::HasNewChatButtonMember() );
    BOOST_CHECK( AI_AGENT_PANEL_BASE_SURFACE_TEST::HasNewChatEvent() );
    BOOST_CHECK( AI_AGENT_PANEL_BASE_SURFACE_TEST::HasPromptTextChangedEvent() );
    BOOST_CHECK( !AI_AGENT_PANEL_BASE_SURFACE_TEST::HasModeChoiceMember() );
}


BOOST_AUTO_TEST_CASE( AgentPanelExposesNewChatSurface )
{
    BOOST_CHECK( ( std::is_member_function_pointer_v<
            decltype( &AI_AGENT_PANEL::StartNewChat )> ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelDoesNotExposeWorkspaceContextsAsVisibleModes )
{
    BOOST_CHECK( !HasPublicSetActiveWorkspaceContext<AI_AGENT_PANEL>() );
    BOOST_CHECK( !HasPublicActiveWorkspaceContext<AI_AGENT_PANEL>() );
}


BOOST_AUTO_TEST_CASE( AgentPanelTranscriptHtmlEscapesMessageText )
{
    std::vector<AI_AGENT_MESSAGE> messages;

    AI_AGENT_MESSAGE user;
    user.m_Role = wxS( "user" );
    user.m_Text = wxS( "<route & preview>" );
    messages.push_back( user );

    AI_AGENT_MESSAGE assistant;
    assistant.m_Role = wxS( "assistant" );
    assistant.m_Text = wxS( "Use anchor A1" );
    messages.push_back( assistant );

    wxString html = AiAgentTranscriptHtml( messages );

    BOOST_CHECK( html.Contains( wxS( "You" ) ) );
    BOOST_CHECK( html.Contains( wxS( "Agent" ) ) );
    BOOST_CHECK( html.Contains( wxS( "&lt;route &amp; preview&gt;" ) ) );
    BOOST_CHECK( !html.Contains( wxS( "<route & preview>" ) ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelEmptyTranscriptKeepsChatWorkflowNeutral )
{
    wxString html = AiAgentTranscriptHtml( {} );

    BOOST_CHECK( html.Contains( wxS( "Ask the Agent about the current board" ) ) );
    BOOST_CHECK( !html.Contains( wxS( "route" ) ) );
    BOOST_CHECK( !html.Contains( wxS( "place" ) ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelExposesSuggestionRefreshSurface )
{
    BOOST_CHECK( ( std::is_member_function_pointer_v<decltype( &AI_AGENT_PANEL::RefreshSuggestions )> ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelExposesLogRefreshSurface )
{
    BOOST_CHECK( ( std::is_member_function_pointer_v<decltype( &AI_AGENT_PANEL::RefreshLog )> ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelExposesActionToolCallConfigurationSurface )
{
    BOOST_CHECK( ( std::is_member_function_pointer_v<
            decltype( &AI_AGENT_PANEL::ConfigureActionToolCalls )> ) );

    BOOST_CHECK( ( std::is_invocable_v<
            decltype( &AI_AGENT_PANEL::ConfigureActionToolCalls ),
            AI_AGENT_PANEL*, std::unique_ptr<AI_ACTION_RUNNER>,
            const std::vector<wxString>&, std::vector<AI_ACTION_DESCRIPTOR>,
            AI_ACCEPT_APPLY_ADAPTER*, AI_SESSION_PREVIEW_SERVICE*,
            AI_SESSION_SHADOW_BOARD_SEEDER*, AI_SESSION_VALIDATION_SERVICE*> ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelExposesSuggestionReviewConfigurationSurface )
{
    BOOST_CHECK( ( std::is_member_function_pointer_v<
            decltype( &AI_AGENT_PANEL::ConfigureSuggestionReview )> ) );

    using Preview = AI_AGENT_PANEL::SUGGESTION_PREVIEW_HANDLER;
    using Accept = AI_AGENT_PANEL::SUGGESTION_ACCEPT_HANDLER;
    using Reject = AI_AGENT_PANEL::SUGGESTION_REJECT_HANDLER;

    BOOST_CHECK( ( std::is_invocable_v<
            decltype( &AI_AGENT_PANEL::ConfigureSuggestionReview ),
            AI_AGENT_PANEL*, Preview, Accept, Reject> ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelExposesSuggestionReviewCommands )
{
    BOOST_CHECK( ( std::is_member_function_pointer_v<
            decltype( &AI_AGENT_PANEL::PreviewLatestSuggestion )> ) );
    BOOST_CHECK( ( std::is_member_function_pointer_v<
            decltype( &AI_AGENT_PANEL::AcceptLatestSuggestion )> ) );
    BOOST_CHECK( ( std::is_member_function_pointer_v<
            decltype( &AI_AGENT_PANEL::RejectLatestSuggestion )> ) );
    BOOST_CHECK( ( std::is_member_function_pointer_v<
            decltype( &AI_AGENT_PANEL::HasPendingChatSessionPreview )> ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelExposesProviderRecoveryCommand )
{
    BOOST_CHECK( ( std::is_member_function_pointer_v<
            decltype( &AI_AGENT_PANEL::RecoverLatestProviderFailure )> ) );
}


BOOST_AUTO_TEST_CASE( ReviewCommandsPreferPendingChatSession )
{
    BOOST_CHECK( AiAgentReviewCommandTargetsChatSession( false, true ) );
    BOOST_CHECK( AiAgentReviewCommandTargetsChatSession( true, true ) );
    BOOST_CHECK( !AiAgentReviewCommandTargetsChatSession( true, false ) );
    BOOST_CHECK( !AiAgentReviewCommandTargetsChatSession( false, false ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelExposesBackgroundAgentToggleSurface )
{
    BOOST_CHECK( ( std::is_member_function_pointer_v<
            decltype( &AI_AGENT_PANEL::SetBackgroundAgentEnabled )> ) );
    BOOST_CHECK( ( std::is_member_function_pointer_v<
            decltype( &AI_AGENT_PANEL::BackgroundAgentEnabled )> ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelExposesPreviewShortcutSurface )
{
    BOOST_CHECK( ( std::is_member_function_pointer_v<
            decltype( &AI_AGENT_PANEL::HandlePreviewShortcut )> ) );
    BOOST_CHECK( ( std::is_member_function_pointer_v<
            decltype( &AI_AGENT_PANEL::HandlePreviewPointer )> ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelExposesModelSettingsSurface )
{
    BOOST_CHECK( ( std::is_member_function_pointer_v<
            decltype( &AI_AGENT_PANEL::ShowModelSettingsDialog )> ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelExposesSemanticSelfTestSurface )
{
    BOOST_CHECK( ( std::is_member_function_pointer_v<
            decltype( &AI_AGENT_PANEL::SemanticUiTree )> ) );
    BOOST_CHECK( ( std::is_member_function_pointer_v<
            decltype( &AI_AGENT_PANEL::SemanticPanelStateRecord )> ) );
    BOOST_CHECK( ( std::is_member_function_pointer_v<
            decltype( &AI_AGENT_PANEL::InvokeSemanticUiAction )> ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelMapsToolStateToWorkspaceContext )
{
    BOOST_CHECK( AiAgentWorkspaceContextForToolState( AI_TOOL_STATE_KIND::RoutingTrack )
                 == AI_AGENT_WORKSPACE_CONTEXT_KIND::Routing );
    BOOST_CHECK( AiAgentWorkspaceContextForToolState( AI_TOOL_STATE_KIND::PlacingVia )
                 == AI_AGENT_WORKSPACE_CONTEXT_KIND::ViaPlacement );
    BOOST_CHECK( AiAgentWorkspaceContextForToolState( AI_TOOL_STATE_KIND::DrawingZone )
                 == AI_AGENT_WORKSPACE_CONTEXT_KIND::ZoneCreation );
    BOOST_CHECK( AiAgentWorkspaceContextForToolState( AI_TOOL_STATE_KIND::MovingSelection )
                 == AI_AGENT_WORKSPACE_CONTEXT_KIND::SelectionEdit );
    BOOST_CHECK( AiAgentWorkspaceContextForToolState( AI_TOOL_STATE_KIND::Idle )
                 == AI_AGENT_WORKSPACE_CONTEXT_KIND::General );
}


BOOST_AUTO_TEST_CASE( AgentPanelFormatsWorkspaceContextTitlesAndSuggestionSummary )
{
    BOOST_CHECK_EQUAL( AiAgentWorkspaceContextTitle( AI_AGENT_WORKSPACE_CONTEXT_KIND::Routing ),
                       wxString( wxS( "Routing" ) ) );
    BOOST_CHECK_EQUAL( AiAgentWorkspaceContextTitle( AI_AGENT_WORKSPACE_CONTEXT_KIND::ViaPlacement ),
                       wxString( wxS( "Via placement" ) ) );

    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_Id = 7;
    suggestion.m_Status = AI_SUGGESTION_STATUS::Previewing;
    suggestion.m_Title = wxS( "Preview next via" );
    suggestion.m_ContextKind = wxS( "routing" );

    wxString summary = AiAgentSuggestionSummary( suggestion );
    BOOST_CHECK( summary.Contains( wxS( "#7" ) ) );
    BOOST_CHECK( summary.Contains( wxS( "Previewing" ) ) );
    BOOST_CHECK( summary.Contains( wxS( "[routing]" ) ) );
    BOOST_CHECK( summary.Contains( wxS( "Preview next via" ) ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelFormatsComposerStatusLifecycle )
{
    AI_AGENT_COMPOSER_STATUS_VIEW view;
    BOOST_CHECK_EQUAL( AiAgentComposerStatusText( view ),
                       wxString( wxS( "Ask about the current board" ) ) );

    view.m_InputHasText = true;
    BOOST_CHECK_EQUAL( AiAgentComposerStatusText( view ),
                       wxString( wxS( "Ready to send" ) ) );

    view.m_InputHasText = false;
    view.m_BackgroundAgentEnabled = true;
    BOOST_CHECK_EQUAL( AiAgentComposerStatusText( view ),
                       wxString( wxS( "Background Agent on" ) ) );

    view.m_BackgroundAgentBusy = true;
    BOOST_CHECK_EQUAL( AiAgentComposerStatusText( view ),
                       wxString( wxS( "Background Agent thinking" ) ) );

    view.m_BackgroundAgentBusy = false;
    view.m_HasActiveSuggestion = true;
    BOOST_CHECK_EQUAL( AiAgentComposerStatusText( view ),
                       wxString( wxS( "Preview ready" ) ) );

    view.m_LatestRequestId = 7;
    BOOST_CHECK_EQUAL( AiAgentComposerStatusText( view ),
                       wxString( wxS( "Last response #7" ) ) );

    view.m_LastRequestCancelled = true;
    BOOST_CHECK_EQUAL( AiAgentComposerStatusText( view ),
                       wxString( wxS( "Stopped request #7" ) ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelQueuesBackgroundUpdatesAsAsyncWork )
{
    AI_AGENT_BACKGROUND_UPDATE_VIEW view;
    view.m_BackgroundAgentEnabled = true;
    view.m_HasContextProvider = true;

    BOOST_CHECK( AiAgentBackgroundUpdateAction( view )
                 == AI_AGENT_BACKGROUND_UPDATE_ACTION::QueueAsync );

    view.m_UpdateInFlight = true;
    BOOST_CHECK( AiAgentBackgroundUpdateAction( view )
                 == AI_AGENT_BACKGROUND_UPDATE_ACTION::DropWhileBusy );

    view.m_UpdateInFlight = false;
    view.m_BackgroundAgentEnabled = false;
    BOOST_CHECK( AiAgentBackgroundUpdateAction( view )
                 == AI_AGENT_BACKGROUND_UPDATE_ACTION::Ignore );

    view.m_BackgroundAgentEnabled = true;
    view.m_HasContextProvider = false;
    BOOST_CHECK( AiAgentBackgroundUpdateAction( view )
                 == AI_AGENT_BACKGROUND_UPDATE_ACTION::Ignore );
}


BOOST_AUTO_TEST_CASE( AgentPanelMapsBackgroundPreviewShortcuts )
{
    AI_AGENT_PREVIEW_SHORTCUT_VIEW view;
    view.m_BackgroundAgentEnabled = true;
    view.m_HasActiveSuggestion = true;

    BOOST_CHECK( AiAgentPreviewShortcutAction( view, WXK_TAB )
                 == AI_AGENT_PREVIEW_SHORTCUT_ACTION::Accept );
    BOOST_CHECK( AiAgentPreviewShortcutAction( view, WXK_RETURN )
                 == AI_AGENT_PREVIEW_SHORTCUT_ACTION::Accept );
    BOOST_CHECK( AiAgentPreviewShortcutAction( view, WXK_NUMPAD_ENTER )
                 == AI_AGENT_PREVIEW_SHORTCUT_ACTION::Accept );
    BOOST_CHECK( AiAgentPreviewShortcutAction( view, WXK_ESCAPE )
                 == AI_AGENT_PREVIEW_SHORTCUT_ACTION::Reject );
}


BOOST_AUTO_TEST_CASE( AgentPanelIgnoresPreviewShortcutsWithoutBackgroundSuggestion )
{
    AI_AGENT_PREVIEW_SHORTCUT_VIEW view;
    view.m_BackgroundAgentEnabled = true;
    view.m_HasActiveSuggestion = false;

    BOOST_CHECK( AiAgentPreviewShortcutAction( view, WXK_TAB )
                 == AI_AGENT_PREVIEW_SHORTCUT_ACTION::Ignore );
    BOOST_CHECK( AiAgentPreviewShortcutAction( view, WXK_ESCAPE )
                 == AI_AGENT_PREVIEW_SHORTCUT_ACTION::Ignore );

    view.m_BackgroundAgentEnabled = false;
    view.m_HasActiveSuggestion = true;

    BOOST_CHECK( AiAgentPreviewShortcutAction( view, WXK_TAB )
                 == AI_AGENT_PREVIEW_SHORTCUT_ACTION::Ignore );
    BOOST_CHECK( AiAgentPreviewShortcutAction( view, WXK_ESCAPE )
                 == AI_AGENT_PREVIEW_SHORTCUT_ACTION::Ignore );
}


BOOST_AUTO_TEST_CASE( AgentPanelIgnoresUnrelatedPreviewShortcutKeys )
{
    AI_AGENT_PREVIEW_SHORTCUT_VIEW view;
    view.m_BackgroundAgentEnabled = true;
    view.m_HasActiveSuggestion = true;

    BOOST_CHECK( AiAgentPreviewShortcutAction( view, 'A' )
                 == AI_AGENT_PREVIEW_SHORTCUT_ACTION::Ignore );
    BOOST_CHECK( AiAgentPreviewShortcutAction( view, WXK_SPACE )
                 == AI_AGENT_PREVIEW_SHORTCUT_ACTION::Ignore );
}


BOOST_AUTO_TEST_CASE( AgentPanelIgnoresPreviewShortcutsInsideChatPanel )
{
    AI_AGENT_PREVIEW_SHORTCUT_VIEW view;
    view.m_BackgroundAgentEnabled = true;
    view.m_HasActiveSuggestion = true;
    view.m_FocusInsideAgentPanel = true;

    BOOST_CHECK( AiAgentPreviewShortcutAction( view, WXK_TAB )
                 == AI_AGENT_PREVIEW_SHORTCUT_ACTION::Ignore );
    BOOST_CHECK( AiAgentPreviewShortcutAction( view, WXK_ESCAPE )
                 == AI_AGENT_PREVIEW_SHORTCUT_ACTION::Ignore );
}


BOOST_AUTO_TEST_CASE( AgentPanelIgnoresPreviewShortcutsWithModifiers )
{
    AI_AGENT_PREVIEW_SHORTCUT_VIEW view;
    view.m_BackgroundAgentEnabled = true;
    view.m_HasActiveSuggestion = true;
    view.m_HasModifier = true;

    BOOST_CHECK( AiAgentPreviewShortcutAction( view, WXK_TAB )
                 == AI_AGENT_PREVIEW_SHORTCUT_ACTION::Ignore );
    BOOST_CHECK( AiAgentPreviewShortcutAction( view, WXK_RETURN )
                 == AI_AGENT_PREVIEW_SHORTCUT_ACTION::Ignore );
    BOOST_CHECK( AiAgentPreviewShortcutAction( view, WXK_ESCAPE )
                 == AI_AGENT_PREVIEW_SHORTCUT_ACTION::Ignore );
}


BOOST_AUTO_TEST_CASE( AgentPanelRejectsWorkspaceClickForBackgroundPreview )
{
    AI_AGENT_PREVIEW_SHORTCUT_VIEW view;
    view.m_BackgroundAgentEnabled = true;
    view.m_HasActiveSuggestion = true;

    BOOST_CHECK( AiAgentPreviewPointerAction( view )
                 == AI_AGENT_PREVIEW_SHORTCUT_ACTION::Reject );
}


BOOST_AUTO_TEST_CASE( AgentPanelIgnoresWorkspaceClickWithoutBackgroundPreview )
{
    AI_AGENT_PREVIEW_SHORTCUT_VIEW view;
    view.m_BackgroundAgentEnabled = true;
    view.m_HasActiveSuggestion = false;

    BOOST_CHECK( AiAgentPreviewPointerAction( view )
                 == AI_AGENT_PREVIEW_SHORTCUT_ACTION::Ignore );

    view.m_BackgroundAgentEnabled = false;
    view.m_HasActiveSuggestion = true;

    BOOST_CHECK( AiAgentPreviewPointerAction( view )
                 == AI_AGENT_PREVIEW_SHORTCUT_ACTION::Ignore );

    view.m_BackgroundAgentEnabled = true;
    view.m_FocusInsideAgentPanel = true;

    BOOST_CHECK( AiAgentPreviewPointerAction( view )
                 == AI_AGENT_PREVIEW_SHORTCUT_ACTION::Ignore );
}


BOOST_AUTO_TEST_CASE( AgentPanelDetectsWorkspacePreviewSuggestions )
{
    AI_SUGGESTION_RECORD objectPreview;
    objectPreview.m_PreviewObjects.push_back(
            AI_OBJECT_REF( KIID(), PCB_VIA_T, wxS( "preview via" ) ) );
    BOOST_CHECK( AiAgentSuggestionTargetsWorkspacePreview( objectPreview ) );

    AI_SUGGESTION_RECORD anchorPreview;
    anchorPreview.m_ArgumentsJson =
            wxS( "{\"operation\":\"anchor_focus_preview\","
                 "\"anchor_id\":\"tool.routing.orthogonal.horizontal\","
                 "\"position\":{\"x\":100,\"y\":200}}" );
    BOOST_CHECK( AiAgentSuggestionTargetsWorkspacePreview( anchorPreview ) );

    AI_SUGGESTION_RECORD panelPreview;
    panelPreview.m_ArgumentsJson =
            wxS( "{\"operation\":\"panel_fill_column_preview\","
                 "\"panel_id\":\"board_setup.clearance\","
                 "\"table_id\":\"clearance.rules\","
                 "\"column_id\":\"clearance\","
                 "\"source_row_id\":\"row.default\","
                 "\"target_row_ids\":[\"row.power\"]}" );
    BOOST_CHECK( !AiAgentSuggestionTargetsWorkspacePreview( panelPreview ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelAutoPreviewsOnlyNewPreviewableWorkspaceSuggestions )
{
    AI_AGENT_BACKGROUND_PREVIEW_VIEW view;
    view.m_BackgroundAgentEnabled = true;
    view.m_HasNewSuggestion = true;
    view.m_HasPreviewHandler = true;
    view.m_CanPreviewSuggestion = true;
    view.m_TargetsWorkspacePreview = true;

    BOOST_CHECK( AiAgentShouldAutoPreviewBackgroundSuggestion( view ) );

    view.m_TargetsWorkspacePreview = false;
    BOOST_CHECK( !AiAgentShouldAutoPreviewBackgroundSuggestion( view ) );

    view.m_TargetsWorkspacePreview = true;
    view.m_HasPreviewHandler = false;
    BOOST_CHECK( !AiAgentShouldAutoPreviewBackgroundSuggestion( view ) );

    view.m_HasPreviewHandler = true;
    view.m_CanPreviewSuggestion = false;
    BOOST_CHECK( !AiAgentShouldAutoPreviewBackgroundSuggestion( view ) );

    view.m_CanPreviewSuggestion = true;
    view.m_HasNewSuggestion = false;
    BOOST_CHECK( !AiAgentShouldAutoPreviewBackgroundSuggestion( view ) );

    view.m_HasNewSuggestion = true;
    view.m_BackgroundAgentEnabled = false;
    BOOST_CHECK( !AiAgentShouldAutoPreviewBackgroundSuggestion( view ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelFormatsObservabilityEntryText )
{
    AI_AGENT_OBSERVABILITY_ENTRY entry;
    entry.m_Sequence = 4;
    entry.m_Kind = AI_AGENT_OBSERVABILITY_KIND::ModelToolCall;
    entry.m_Title = wxS( "Tool call" );
    entry.m_Summary = wxS( "kisurf_run_action" );
    entry.m_ToolCallId = wxS( "call_7" );
    entry.m_Allowed = true;
    entry.m_Executed = false;

    wxString text = AiAgentObservabilityEntryText( entry );

    BOOST_CHECK( text.Contains( wxS( "#4" ) ) );
    BOOST_CHECK( text.Contains( wxS( "Tool call" ) ) );
    BOOST_CHECK( text.Contains( wxS( "call_7" ) ) );
    BOOST_CHECK( text.Contains( wxS( "allowed" ) ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelFormatsToolCallObservabilityPayloadsForDebugging )
{
    AI_AGENT_OBSERVABILITY_ENTRY entry;
    entry.m_Sequence = 7;
    entry.m_RequestId = 42;
    entry.m_ToolCallId = wxS( "call_7" );
    entry.m_Kind = AI_AGENT_OBSERVABILITY_KIND::ModelToolCall;
    entry.m_Title = wxS( "Tool call" );
    entry.m_Summary = wxS( "kisurf_run_action" );
    entry.m_DetailsJson = wxString::FromUTF8(
            nlohmann::json{
                    { "request_id", 42 },
                    { "kind", "model_tool_request" },
                    { "action", "kisurf_run_action" },
                    { "arguments_json",
                      "{\"action\":\"common.Control.zoomFitScreen\","
                      "\"dry_run\":true}" },
                    { "allowed", true },
                    { "executed", false },
                    { "message", "preview requested" }
            }.dump().c_str() );

    wxString text = AiAgentObservabilityEntryText( entry );

    BOOST_CHECK( text.Contains( wxS( "details:" ) ) );
    BOOST_CHECK( text.Contains( wxS( "request=42" ) ) );
    BOOST_CHECK( text.Contains( wxS( "kind=model_tool_request" ) ) );
    BOOST_CHECK( text.Contains( wxS( "action=kisurf_run_action" ) ) );
    BOOST_CHECK( text.Contains( wxS( "allowed=true" ) ) );
    BOOST_CHECK( text.Contains( wxS( "executed=false" ) ) );
    BOOST_CHECK( text.Contains( wxS( "arguments:" ) ) );
    BOOST_CHECK( text.Contains( wxS( "common.Control.zoomFitScreen" ) ) );
    BOOST_CHECK( text.Contains( wxS( "\"dry_run\":true" ) ) );
    BOOST_CHECK( text.Contains( wxS( "message: preview requested" ) ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelFormatsToolResultObservabilityPayloadsForDebugging )
{
    AI_AGENT_OBSERVABILITY_ENTRY entry;
    entry.m_Sequence = 8;
    entry.m_RequestId = 42;
    entry.m_ToolCallId = wxS( "call_7" );
    entry.m_Kind = AI_AGENT_OBSERVABILITY_KIND::ToolResult;
    entry.m_Title = wxS( "Tool result" );
    entry.m_Summary = wxS( "common.Control.zoomFitScreen" );
    entry.m_DetailsJson = wxString::FromUTF8(
            nlohmann::json{
                    { "request_id", 42 },
                    { "kind", "tool_result" },
                    { "action", "common.Control.zoomFitScreen" },
                    { "result_json", "{\"status\":\"preview_ready\"}" },
                    { "allowed", true },
                    { "executed", true }
            }.dump().c_str() );

    wxString text = AiAgentObservabilityEntryText( entry );

    BOOST_CHECK( text.Contains( wxS( "details:" ) ) );
    BOOST_CHECK( text.Contains( wxS( "request=42" ) ) );
    BOOST_CHECK( text.Contains( wxS( "kind=tool_result" ) ) );
    BOOST_CHECK( text.Contains( wxS( "action=common.Control.zoomFitScreen" ) ) );
    BOOST_CHECK( text.Contains( wxS( "allowed=true" ) ) );
    BOOST_CHECK( text.Contains( wxS( "executed=true" ) ) );
    BOOST_CHECK( text.Contains( wxS( "result:" ) ) );
    BOOST_CHECK( text.Contains( wxS( "\"status\":\"preview_ready\"" ) ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelFormatsModelInputObservabilityDetails )
{
    AI_AGENT_OBSERVABILITY_ENTRY entry;
    entry.m_Sequence = 5;
    entry.m_RequestId = 42;
    entry.m_Kind = AI_AGENT_OBSERVABILITY_KIND::ModelInput;
    entry.m_Title = wxS( "Model input" );
    entry.m_Summary = wxS( "route selected net" );
    entry.m_DetailsJson = wxString::FromUTF8(
            nlohmann::json{
                    { "request_id", 42 },
                    { "editor", "pcb" },
                    { "tool_results_count", 1 },
                    { "context",
                      {
                              { "selected_count", 2 },
                              { "visible_count", 5 },
                              { "anchor_count", 3 },
                              { "panel_state_count", 1 },
                              { "tool_state_kind", "routing_track" },
                              { "visual",
                                {
                                        { "source", "pcbnew.canvas" },
                                        { "width_px", 1280 },
                                        { "height_px", 720 },
                                        { "has_pixels", true }
                                } }
                      } }
            }.dump().c_str() );

    wxString text = AiAgentObservabilityEntryText( entry );

    BOOST_CHECK( text.Contains( wxS( "details:" ) ) );
    BOOST_CHECK( text.Contains( wxS( "request=42" ) ) );
    BOOST_CHECK( text.Contains( wxS( "editor=pcb" ) ) );
    BOOST_CHECK( text.Contains( wxS( "selected=2" ) ) );
    BOOST_CHECK( text.Contains( wxS( "visible=5" ) ) );
    BOOST_CHECK( text.Contains( wxS( "anchors=3" ) ) );
    BOOST_CHECK( text.Contains( wxS( "panels=1" ) ) );
    BOOST_CHECK( text.Contains( wxS( "tool_state=routing_track" ) ) );
    BOOST_CHECK( text.Contains( wxS( "visual=pcbnew.canvas 1280x720" ) ) );
    BOOST_CHECK( text.Contains( wxS( "tool_results=1" ) ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelFormatsModelOutputObservabilityDetails )
{
    AI_AGENT_OBSERVABILITY_ENTRY entry;
    entry.m_Sequence = 6;
    entry.m_RequestId = 42;
    entry.m_Kind = AI_AGENT_OBSERVABILITY_KIND::ModelOutput;
    entry.m_Title = wxS( "Routing assistant" );
    entry.m_Summary = wxS( "I can preview the next segment." );
    entry.m_DetailsJson = wxString::FromUTF8(
            nlohmann::json{
                    { "request_id", 42 },
                    { "body", "I can preview the next segment." },
                    { "body_length", 31 },
                    { "tool_call_count", 2 },
                    { "cancelled", false },
                    { "tool_calls",
                      nlohmann::json::array(
                              { {
                                        { "id", "call_1" },
                                        { "name", "kisurf_get_workspace_view" },
                                        { "arguments_json",
                                          "{\"views\":[\"visual\",\"panels\"]}" },
                                        { "allowed", true },
                                        { "executed", false },
                                        { "message", "workspace view requested" }
                                } } ) }
            }.dump().c_str() );

    wxString text = AiAgentObservabilityEntryText( entry );

    BOOST_CHECK( text.Contains( wxS( "details:" ) ) );
    BOOST_CHECK( text.Contains( wxS( "request=42" ) ) );
    BOOST_CHECK( text.Contains( wxS( "body_length=31" ) ) );
    BOOST_CHECK( text.Contains( wxS( "tool_calls=2" ) ) );
    BOOST_CHECK( text.Contains( wxS( "cancelled=false" ) ) );
    BOOST_CHECK( text.Contains( wxS( "body: I can preview the next segment." ) ) );
    BOOST_CHECK( text.Contains( wxS( "tool call: call_1 kisurf_get_workspace_view" ) ) );
    BOOST_CHECK( text.Contains( wxS( "\"views\":[\"visual\",\"panels\"]" ) ) );
    BOOST_CHECK( text.Contains( wxS( "message: workspace view requested" ) ) );
}


BOOST_AUTO_TEST_SUITE_END()
