#include <boost/test/unit_test.hpp>
#include <json_common.h>
#include <kisurf/ai/ai_agent_panel.h>
#include <kisurf/ai/ai_agent_panel_base.h>
#include <kisurf/ai/ai_agent_panel_model.h>
#include <kisurf/ai/ai_session_tool_call_handler.h>

#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>
#include <wx/defs.h>
#include <wx/panel.h>

#ifndef QA_SRC_ROOT
#error QA_SRC_ROOT must be defined for AI agent panel tests.
#endif

BOOST_AUTO_TEST_SUITE( AiAgentPanel )


static std::string readAiAgentPanelSource()
{
    const std::string path = std::string( QA_SRC_ROOT )
                             + "/common/kisurf/ai/ai_agent_panel.cpp";
    std::ifstream     in( path, std::ios::binary );

    BOOST_REQUIRE_MESSAGE( in.good(), "Unable to read source file: " << path );

    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}


static std::string sourceFunctionBody( const std::string& aSource,
                                       const std::string& aFunctionName,
                                       const std::string& aNextFunctionName )
{
    const size_t start = aSource.find( aFunctionName );
    BOOST_REQUIRE_MESSAGE( start != std::string::npos,
                           "Unable to find function: " << aFunctionName );

    const size_t end = aSource.find( aNextFunctionName, start + aFunctionName.size() );
    BOOST_REQUIRE_MESSAGE( end != std::string::npos,
                           "Unable to find next function: " << aNextFunctionName );

    return aSource.substr( start, end - start );
}


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


BOOST_AUTO_TEST_CASE( AgentPanelStreamingTextShowsToolProgressAfterTextDelta )
{
    AI_RUNTIME_STREAM_EVENT textDelta;
    textDelta.m_Kind = AI_RUNTIME_STREAM_EVENT_KIND::TextDelta;
    textDelta.m_TextDelta = wxS( "I will inspect the board." );

    wxString visible = AiAgentStreamingAssistantTextAfterEvent( wxString(), textDelta );
    BOOST_CHECK_EQUAL( visible, wxString( wxS( "I will inspect the board." ) ) );

    AI_RUNTIME_STREAM_EVENT toolStarted;
    toolStarted.m_Kind = AI_RUNTIME_STREAM_EVENT_KIND::ToolCallStarted;
    toolStarted.m_ToolCall.m_ToolName = wxS( "kisurf_render_preview" );

    visible = AiAgentStreamingAssistantTextAfterEvent( visible, toolStarted );

    BOOST_CHECK( visible.Contains( wxS( "I will inspect the board." ) ) );
    BOOST_CHECK( visible.Contains( wxS( "Executing tool: kisurf_render_preview" ) ) );

    AI_RUNTIME_STREAM_EVENT toolFinished;
    toolFinished.m_Kind = AI_RUNTIME_STREAM_EVENT_KIND::ToolCallFinished;
    toolFinished.m_ToolCall.m_ToolName = wxS( "kisurf_render_preview" );
    toolFinished.m_ToolResult.m_Message = wxS( "preview rendered" );

    visible = AiAgentStreamingAssistantTextAfterEvent( visible, toolFinished );

    BOOST_CHECK( visible.Contains( wxS( "Finished tool: kisurf_render_preview" ) ) );
    BOOST_CHECK( visible.Contains( wxS( "preview rendered" ) ) );

    AI_RUNTIME_STREAM_EVENT finalResponse;
    finalResponse.m_Kind = AI_RUNTIME_STREAM_EVENT_KIND::FinalResponse;
    finalResponse.m_TextDelta = wxS( "Done." );

    visible = AiAgentStreamingAssistantTextAfterEvent( visible, finalResponse );
    BOOST_CHECK_EQUAL( visible, wxString( wxS( "Done." ) ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelStreamingTextKeepsDeltaWhenFinalResponseHasNoText )
{
    AI_RUNTIME_STREAM_EVENT textDelta;
    textDelta.m_Kind = AI_RUNTIME_STREAM_EVENT_KIND::TextDelta;
    textDelta.m_TextDelta = wxS( "Streaming answer" );

    wxString visible = AiAgentStreamingAssistantTextAfterEvent( wxString(), textDelta );

    AI_RUNTIME_STREAM_EVENT finalResponse;
    finalResponse.m_Kind = AI_RUNTIME_STREAM_EVENT_KIND::FinalResponse;

    visible = AiAgentStreamingAssistantTextAfterEvent( visible, finalResponse );

    BOOST_CHECK_EQUAL( visible, wxString( wxS( "Streaming answer" ) ) );
    BOOST_CHECK( !visible.Contains( wxS( "Finalizing response" ) ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelSendCurrentTextRendersPendingUserBeforeContextSampling )
{
    const std::string source = readAiAgentPanelSource();
    const std::string sendBody = sourceFunctionBody(
            source, "void AI_AGENT_PANEL::SendCurrentText()",
            "void AI_AGENT_PANEL::prepareAndRunPendingChatRequest(" );

    const size_t pendingUser = sendBody.find( "m_PendingUserText = text" );
    const size_t pendingRender = sendBody.find( "renderPendingChatRequest()" );
    const size_t deferredWork = sendBody.find( "wxTheApp->CallAfter" );

    BOOST_REQUIRE( pendingUser != std::string::npos );
    BOOST_REQUIRE( pendingRender != std::string::npos );
    BOOST_REQUIRE( deferredWork != std::string::npos );
    BOOST_CHECK_LT( pendingUser, pendingRender );
    BOOST_CHECK_LT( pendingRender, deferredWork );
    BOOST_CHECK_EQUAL( sendBody.find( "contextSnapshotWithPanelState()" ),
                       std::string::npos );
    BOOST_CHECK_EQUAL( sendBody.find( "PrepareUserTextRequest(" ),
                       std::string::npos );

    const std::string renderBody = sourceFunctionBody(
            source, "void AI_AGENT_PANEL::renderPendingChatRequest()",
            "void AI_AGENT_PANEL::handlePreparedChatStreamEvent(" );

    BOOST_CHECK( renderBody.find( "m_PendingUserText" ) != std::string::npos );
}


BOOST_AUTO_TEST_CASE( AgentPanelEmptyTranscriptKeepsChatWorkflowNeutral )
{
    wxString html = AiAgentTranscriptHtml( {} );

    BOOST_CHECK( html.Contains( wxS( "Ask the Agent about the current board" ) ) );
    BOOST_CHECK( !html.Contains( wxS( "previewed edit" ) ) );
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


BOOST_AUTO_TEST_CASE( ReviewCommandsTargetPendingChatSession )
{
    BOOST_CHECK( !AiAgentReviewCommandTargetsChatSession( false, true ) );
    BOOST_CHECK( !AiAgentReviewCommandTargetsChatSession( true, true ) );
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


BOOST_AUTO_TEST_CASE( AgentPanelBackgroundTimerUsesLowFrequencySampling )
{
    const std::string source = readAiAgentPanelSource();
    const std::string body = sourceFunctionBody(
            source, "void AI_AGENT_PANEL::SetBackgroundAgentEnabled( bool aEnabled )",
            "bool AI_AGENT_PANEL::BackgroundAgentEnabled() const" );

    BOOST_CHECK( body.find( "m_BackgroundPulseTimer.Start( 1500 )" )
                 != std::string::npos );
    BOOST_CHECK_EQUAL( body.find( "m_BackgroundPulseTimer.Start( 750 )" ),
                       std::string::npos );
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


BOOST_AUTO_TEST_CASE( AgentPanelBackgroundTickTargetsActiveSemanticStates )
{
    AI_CONTEXT_SNAPSHOT routing;
    routing.m_EditorKind = AI_EDITOR_KIND::Pcb;
    routing.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    routing.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::RoutingTrack;
    BOOST_CHECK( AiAgentSnapshotNeedsBackgroundTick( routing ) );

    AI_CONTEXT_SNAPSHOT placing;
    placing.m_EditorKind = AI_EDITOR_KIND::Pcb;
    placing.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    placing.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::PlacingVia;
    BOOST_CHECK( AiAgentSnapshotNeedsBackgroundTick( placing ) );

    AI_CONTEXT_SNAPSHOT panel;
    panel.m_EditorKind = AI_EDITOR_KIND::Pcb;
    panel.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    panel.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::Idle;
    AI_PANEL_STATE_RECORD panelState;
    panelState.m_Id = wxS( "board_setup.layers" );
    panelState.m_Title = wxS( "Board Setup" );
    panelState.m_FocusedControlId = wxS( "board_setup.layers.grid.r0.c1" );
    panelState.m_FocusedControlLabel = wxS( "Layer name" );
    panel.m_PanelStates.push_back( panelState );
    BOOST_CHECK( AiAgentSnapshotNeedsBackgroundTick( panel ) );

    AI_CONTEXT_SNAPSHOT idle;
    idle.m_EditorKind = AI_EDITOR_KIND::Pcb;
    idle.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    idle.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::Idle;
    BOOST_CHECK( !AiAgentSnapshotNeedsBackgroundTick( idle ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelContinuousBackgroundIdleOnlyTargetsActiveTools )
{
    AI_CONTEXT_SNAPSHOT routing;
    routing.m_EditorKind = AI_EDITOR_KIND::Pcb;
    routing.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    routing.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::RoutingTrack;
    BOOST_CHECK( AiAgentSnapshotNeedsContinuousBackgroundIdle( routing ) );

    AI_CONTEXT_SNAPSHOT panel;
    panel.m_EditorKind = AI_EDITOR_KIND::Pcb;
    panel.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    panel.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::Idle;
    AI_PANEL_STATE_RECORD panelState;
    panelState.m_Id = wxS( "board_setup.layers" );
    panelState.m_FocusedControlId = wxS( "board_setup.layers.grid.r0.c1" );
    panel.m_PanelStates.push_back( panelState );
    BOOST_CHECK( AiAgentSnapshotNeedsBackgroundTick( panel ) );
    BOOST_CHECK( !AiAgentSnapshotNeedsContinuousBackgroundIdle( panel ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelBackgroundTickFingerprintTracksSemanticState )
{
    AI_CONTEXT_SNAPSHOT first;
    first.m_EditorKind = AI_EDITOR_KIND::Pcb;
    first.m_Version.m_DocumentRevision = 10;
    first.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    first.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::RoutingTrack;
    first.m_ToolState.m_ModeContextJson =
            wxS( "{\"mode\":\"routing_track\",\"start\":{\"x\":1,\"y\":2}}" );

    AI_CONTEXT_SNAPSHOT second = first;
    second.m_ToolState.m_ModeContextJson =
            wxS( "{\"mode\":\"routing_track\",\"start\":{\"x\":3,\"y\":4}}" );

    BOOST_CHECK( !AiAgentBackgroundTickFingerprint( first ).IsEmpty() );
    BOOST_CHECK( AiAgentBackgroundTickFingerprint( first )
                 != AiAgentBackgroundTickFingerprint( second ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelQueuesRepeatedActiveToolBackgroundTicks )
{
    AI_CONTEXT_SNAPSHOT routing;
    routing.m_EditorKind = AI_EDITOR_KIND::Pcb;
    routing.m_Version.m_DocumentRevision = 10;
    routing.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    routing.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::RoutingTrack;
    routing.m_ToolState.m_ModeContextJson =
            wxS( "{\"mode\":\"routing_track\",\"start\":{\"x\":1,\"y\":2}}" );

    const wxString routingFingerprint =
            AiAgentBackgroundTickFingerprint( routing );
    BOOST_REQUIRE( !routingFingerprint.IsEmpty() );
    BOOST_CHECK( AiAgentShouldQueueBackgroundTick( routing,
                                                   routingFingerprint ) );

    AI_CONTEXT_SNAPSHOT panel;
    panel.m_EditorKind = AI_EDITOR_KIND::Pcb;
    panel.m_Version.m_DocumentRevision = 10;
    panel.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    panel.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::Idle;
    AI_PANEL_STATE_RECORD panelState;
    panelState.m_Id = wxS( "board_setup.layers" );
    panelState.m_Title = wxS( "Board Setup" );
    panelState.m_FocusedControlId = wxS( "board_setup.layers.grid.r0.c1" );
    panelState.m_FocusedControlLabel = wxS( "Layer name" );
    panel.m_PanelStates.push_back( panelState );

    const wxString panelFingerprint = AiAgentBackgroundTickFingerprint( panel );
    BOOST_REQUIRE( !panelFingerprint.IsEmpty() );
    BOOST_CHECK( !AiAgentShouldQueueBackgroundTick( panel,
                                                    panelFingerprint ) );
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

    view.m_ChatAgentBusy = true;
    view.m_LatestRequestId = 7;
    BOOST_CHECK_EQUAL( AiAgentComposerStatusText( view ),
                       wxString( wxS( "Agent thinking" ) ) );

    view.m_ChatAgentBusy = false;
    view.m_LatestRequestId = 0;
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


BOOST_AUTO_TEST_CASE( AgentPanelBackgroundQueueUsesConfiguredModelRuntime )
{
    const std::string source = readAiAgentPanelSource();

    BOOST_CHECK_EQUAL( source.find( "AI_AGENT_PANEL_MODEL workerModel" ),
                       std::string::npos );
    BOOST_CHECK_EQUAL( source.find( "immediateBackgroundCandidate(" ),
                       std::string::npos );
    BOOST_CHECK( source.find( "model->UpdateSuggestionsIfBackgroundEnabled" )
                 != std::string::npos );
}


BOOST_AUTO_TEST_CASE( AgentPanelResetsBackgroundTickAfterSuggestionReview )
{
    const std::string source = readAiAgentPanelSource();
    const std::string acceptBody = sourceFunctionBody(
            source, "bool AI_AGENT_PANEL::AcceptLatestSuggestion()",
            "bool AI_AGENT_PANEL::RejectLatestSuggestion()" );
    const std::string rejectBody = sourceFunctionBody(
            source, "bool AI_AGENT_PANEL::RejectLatestSuggestion()",
            "bool AI_AGENT_PANEL::HandlePreviewShortcut" );

    BOOST_CHECK( acceptBody.find( "m_LastBackgroundTickFingerprint.Clear()" )
                 != std::string::npos );
    BOOST_CHECK( rejectBody.find( "m_LastBackgroundTickFingerprint.Clear()" )
                 != std::string::npos );
}


BOOST_AUTO_TEST_CASE( AgentPanelAutoAcceptsPendingChatSessionWhenResponseFinishes )
{
    const std::string source = readAiAgentPanelSource();
    const std::string finishBody = sourceFunctionBody(
            source, "void AI_AGENT_PANEL::runPreparedChatRequest(",
            "void AI_AGENT_PANEL::ConfigureActionToolCalls(" );

    BOOST_CHECK( finishBody.find(
            "autoAcceptCompletedChatSession()" ) != std::string::npos );
    BOOST_CHECK( finishBody.find(
            "model->FinishPreparedChatRequest( std::move( state )," )
                 != std::string::npos );
}


BOOST_AUTO_TEST_CASE( AgentPanelSemanticViewDoesNotExposeChatSessionReview )
{
    const std::string source = readAiAgentPanelSource();
    const std::string semanticBody = sourceFunctionBody(
            source, "AI_SEMANTIC_UI_TREE AI_AGENT_PANEL::SemanticUiTree() const",
            "AI_PANEL_STATE_RECORD AI_AGENT_PANEL::SemanticPanelStateRecord() const" );
    const std::string modeBody = sourceFunctionBody(
            source, "void AI_AGENT_PANEL::updateModeControls()",
            "void AI_AGENT_PANEL::updateComposerStatus()" );

    BOOST_CHECK_EQUAL( semanticBody.find( "pendingChatSessionPreview" ),
                       std::string::npos );
    BOOST_CHECK_EQUAL( modeBody.find( "pendingChatSessionPreview" ),
                       std::string::npos );
    BOOST_CHECK( modeBody.find( "m_PreviewButton->Enable( canPreviewSuggestion )" )
                 != std::string::npos );
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

    AI_SUGGESTION_RECORD routePreview;
    routePreview.m_ArgumentsJson =
            wxS( "{\"operation\":\"route_segment_preview\","
                 "\"net\":\"GND\",\"layer\":\"F.Cu\",\"width\":200000,"
                 "\"start\":{\"x\":1000000,\"y\":2000000},"
                 "\"end\":{\"x\":3000000,\"y\":2000000}}" );
    BOOST_CHECK( AiAgentSuggestionTargetsWorkspacePreview( routePreview ) );
    BOOST_CHECK( AiAgentSuggestionTargetsAutomaticPreview( routePreview ) );

    AI_SUGGESTION_RECORD viaPreview;
    viaPreview.m_ArgumentsJson =
            wxS( "{\"operation\":\"place_via_preview\","
                 "\"net\":\"GND\",\"diameter\":600000,\"drill\":300000,"
                 "\"position\":{\"x\":3000000,\"y\":2000000}}" );
    BOOST_CHECK( AiAgentSuggestionTargetsWorkspacePreview( viaPreview ) );
    BOOST_CHECK( AiAgentSuggestionTargetsAutomaticPreview( viaPreview ) );

    AI_SUGGESTION_RECORD shapePreview;
    shapePreview.m_ArgumentsJson =
            wxS( "{\"operation\":\"create_shape_preview\","
                 "\"shape\":\"segment\",\"layer\":\"Dwgs.User\",\"width\":100000,"
                 "\"start\":{\"x\":1000000,\"y\":1000000},"
                 "\"end\":{\"x\":2000000,\"y\":1000000}}" );
    BOOST_CHECK( AiAgentSuggestionTargetsWorkspacePreview( shapePreview ) );
    BOOST_CHECK( AiAgentSuggestionTargetsAutomaticPreview( shapePreview ) );

    AI_SUGGESTION_RECORD zonePreview;
    zonePreview.m_ArgumentsJson =
            wxS( "{\"operation\":\"create_copper_zone_preview\","
                 "\"net\":\"GND\",\"layer\":\"F.Cu\","
                 "\"points\":[{\"x\":1000000,\"y\":1000000},"
                 "{\"x\":3000000,\"y\":1000000},"
                 "{\"x\":3000000,\"y\":3000000},"
                 "{\"x\":1000000,\"y\":3000000}]}" );
    BOOST_CHECK( AiAgentSuggestionTargetsWorkspacePreview( zonePreview ) );
    BOOST_CHECK( AiAgentSuggestionTargetsAutomaticPreview( zonePreview ) );

    AI_SUGGESTION_RECORD panelPreview;
    panelPreview.m_ArgumentsJson =
            wxS( "{\"operation\":\"panel_fill_column_preview\","
                 "\"panel_id\":\"board_setup.clearance\","
                 "\"table_id\":\"clearance.rules\","
                 "\"column_id\":\"clearance\","
                 "\"value\":\"0.20 mm\","
                 "\"source_row_id\":\"row.default\","
                 "\"target_row_ids\":[\"row.power\"]}" );
    BOOST_CHECK( !AiAgentSuggestionTargetsWorkspacePreview( panelPreview ) );
    BOOST_CHECK( AiAgentSuggestionTargetsAutomaticPreview( panelPreview ) );
}


BOOST_AUTO_TEST_CASE( AgentPanelAutoPreviewsOnlyNewPreviewableSuggestions )
{
    AI_AGENT_BACKGROUND_PREVIEW_VIEW view;
    view.m_BackgroundAgentEnabled = true;
    view.m_HasNewSuggestion = true;
    view.m_HasPreviewHandler = true;
    view.m_CanPreviewSuggestion = true;
    view.m_TargetsWorkspacePreview = true;
    view.m_TargetsAutomaticPreview = true;

    BOOST_CHECK( AiAgentShouldAutoPreviewBackgroundSuggestion( view ) );

    view.m_TargetsWorkspacePreview = false;
    BOOST_CHECK( AiAgentShouldAutoPreviewBackgroundSuggestion( view ) );

    view.m_TargetsWorkspacePreview = true;
    view.m_TargetsAutomaticPreview = false;
    BOOST_CHECK( AiAgentShouldAutoPreviewBackgroundSuggestion( view ) );

    view.m_TargetsAutomaticPreview = true;
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
