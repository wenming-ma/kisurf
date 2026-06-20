#include <boost/test/unit_test.hpp>
#include <kisurf/ai/ai_agent_panel_model.h>
#include <kisurf/ai/ai_provider.h>
#include <kisurf/ai/ai_suggestion_operations.h>
#include <kisurf/ai/ai_suggestion_orchestrator.h>

#include <memory>
#include <wx/arrstr.h> // for MSVC to see std::vector<wxString> is exported from wx

namespace
{
class CAPTURING_AI_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        m_LastRequest = aRequest;

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "captured" );
        response.m_Body = aRequest.m_ContextSnapshot.AsPromptText();
        return response;
    }

    AI_PROVIDER_REQUEST m_LastRequest;
};


class TOOL_CALL_AI_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "tool call" );
        response.m_Body = wxS( "Tool call requested." );

        AI_TOOL_CALL_RECORD call;
        call.m_RequestId = aRequest.m_RequestId;
        call.m_ToolCallId = wxS( "call_panel" );
        call.m_ToolName = wxS( "kisurf_run_action" );
        call.m_ArgumentsJson = wxS( "{\"action\":\"common.Control.showAgentPanel\"}" );
        response.m_ToolCalls.push_back( call );

        return response;
    }
};


class RUNTIME_ACTIVITY_CAPTURE_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "runtime activity capture" );

        if( m_CallCount == 1 )
        {
            response.m_Body = wxS( "Tool call requested." );

            AI_TOOL_CALL_RECORD call;
            call.m_RequestId = aRequest.m_RequestId;
            call.m_ToolCallId = wxS( "call_panel" );
            call.m_ToolName = wxS( "kisurf_run_action" );
            call.m_ArgumentsJson = wxS( "{\"action\":\"common.Control.showAgentPanel\"}" );
            response.m_ToolCalls.push_back( call );
            return response;
        }

        if( !aRequest.m_ToolResults.empty() )
        {
            response.m_Body = wxS( "Tool result received." );
            return response;
        }

        m_LastRequest = aRequest;
        response.m_Body = wxS( "Captured later request." );
        return response;
    }

    int                 m_CallCount = 0;
    AI_PROVIDER_REQUEST m_LastRequest;
};


class FAKE_PANEL_TOOL_CALL_HANDLER : public AI_TOOL_CALL_HANDLER
{
public:
    AI_TOOL_INVOCATION_RESULT HandleToolCall( const AI_PROVIDER_REQUEST& aRequest,
                                              const AI_TOOL_CALL_RECORD& aToolCall ) override
    {
        ++m_CallCount;

        AI_TOOL_INVOCATION_RESULT result;
        result.m_RequestId = aRequest.m_RequestId;
        result.m_ToolCallId = aToolCall.m_ToolCallId;
        result.m_ActionName = wxS( "common.Control.showAgentPanel" );
        result.m_Allowed = true;
        result.m_Executed = true;
        result.m_Message = wxS( "panel handler executed" );
        result.m_ResultJson = wxS( "{\"status\":\"panel-executed\"}" );
        return result;
    }

    int m_CallCount = 0;
};


class FAKE_SUGGESTION_PROVIDER : public AI_SUGGESTION_PROVIDER
{
public:
    std::optional<AI_SUGGESTION_RECORD> Suggest(
            const AI_SUGGESTION_TRIGGER& aTrigger ) override
    {
        ++m_CallCount;
        m_LastTrigger = aTrigger;

        if( !m_NextSuggestion )
            return std::nullopt;

        AI_SUGGESTION_RECORD suggestion = *m_NextSuggestion;
        m_NextSuggestion.reset();
        return suggestion;
    }

    int                                m_CallCount = 0;
    AI_SUGGESTION_TRIGGER              m_LastTrigger;
    std::optional<AI_SUGGESTION_RECORD> m_NextSuggestion;
};


class FAKE_PREVIEW_ADAPTER : public AI_PREVIEW_ADAPTER
{
public:
    void BeginPreview( uint64_t aPreviewId ) override { m_BeginId = aPreviewId; }

    void ShowObject( uint64_t, const AI_OBJECT_REF& aObject ) override
    {
        m_Shown.push_back( aObject.m_Label );
    }

    void ShowOperation( uint64_t, const AI_SUGGESTION_OPERATION& aOperation ) override
    {
        m_Operations.push_back( aOperation );
    }

    void ClearPreview( uint64_t ) override {}

    uint64_t                             m_BeginId = 0;
    std::vector<wxString>                m_Shown;
    std::vector<AI_SUGGESTION_OPERATION> m_Operations;
};


class FAKE_EDIT_ADAPTER : public AI_EDIT_ADAPTER
{
public:
    bool ApplyObject( const AI_OBJECT_REF& aObject ) override
    {
        m_Applied.push_back( aObject.m_Label );
        return true;
    }

    std::vector<wxString> m_Applied;
};


AI_CONTEXT_SNAPSHOT makeSuggestionContext( uint64_t aDocRevision = 1 )
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_Version.m_DocumentRevision = aDocRevision;
    snapshot.m_SelectedObjects.push_back( AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ) ) );
    return snapshot;
}


wxString viaDetails( int aX, int aY, const wxString& aNetName,
                     int aDiameter = 600000 )
{
    wxString details;
    details << wxS( "{\"kind\":\"via\",\"position\":{\"x\":" ) << aX
            << wxS( ",\"y\":" ) << aY << wxS( "},\"diameter\":" )
            << aDiameter << wxS( ",\"net_name\":\"" ) << aNetName << wxS( "\"}" );
    return details;
}


AI_OBJECT_REF viaRef( int aX, int aY, const wxString& aNetName = wxS( "GND" ) )
{
    return AI_OBJECT_REF( KIID(), PCB_VIA_T,
                          wxString::Format( wxS( "via:%d,%d" ), aX, aY ),
                          viaDetails( aX, aY, aNetName ) );
}


AI_CONTEXT_SNAPSHOT makeViaNextActionContext()
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_Version.m_DocumentRevision = 9;
    snapshot.m_Version.m_ViewRevision = 1;
    snapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::PlacingVia;
    snapshot.m_ToolState.m_ContextVersion = snapshot.m_Version;
    snapshot.m_VisibleObjects.push_back( viaRef( 100, 50 ) );
    snapshot.m_VisibleObjects.push_back( viaRef( 200, 50 ) );
    snapshot.m_VisibleObjects.push_back( viaRef( 300, 50 ) );
    return snapshot;
}


wxString panelTableStateJson()
{
    return wxS( "{\"tables\":[{\"id\":\"clearance.rules\","
                "\"title\":\"Clearance rules\","
                "\"focused_cell\":{\"row_id\":\"row.default\","
                "\"column_id\":\"clearance\"},"
                "\"columns\":[{\"id\":\"clearance\","
                "\"label\":\"Clearance\"}],\"rows\":["
                "{\"id\":\"row.default\",\"label\":\"Default\","
                "\"cells\":{\"clearance\":{\"value\":\"0.20 mm\"}}},"
                "{\"id\":\"row.power\",\"label\":\"Power\","
                "\"cells\":{\"clearance\":\"\"}},"
                "{\"id\":\"row.signal\",\"label\":\"Signal\","
                "\"cells\":{\"clearance\":\"\"}}]}]}" );
}


AI_CONTEXT_SNAPSHOT makePanelTableContext()
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_Version.m_DocumentRevision = 14;
    snapshot.m_Version.m_ViewRevision = 2;
    snapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::Unknown;

    AI_PANEL_STATE_RECORD panel;
    panel.m_Id = wxS( "board_setup.clearance" );
    panel.m_Title = wxS( "Board Setup" );
    panel.m_FocusedControlId = wxS( "clearance.rules.row.default.clearance" );
    panel.m_FocusedControlLabel = wxS( "Clearance" );
    panel.m_StateJson = panelTableStateJson();
    snapshot.m_PanelStates.push_back( panel );
    return snapshot;
}


AI_ACTIVITY_RECORD makeSuggestionActivity( uint64_t aSequence = 1 )
{
    AI_ACTIVITY_RECORD activity;
    activity.m_Sequence = aSequence;
    activity.m_ActionName = wxS( "common.Interactive.selected" );
    return activity;
}


AI_SUGGESTION_RECORD makeModelSuggestion( const wxString& aTitle = wxS( "Review U1.1" ) )
{
    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_Title = aTitle;
    suggestion.m_Body = wxS( "Preview before edit." );
    suggestion.m_PreviewObjects.push_back( AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ) ) );
    suggestion.m_EditObjects.push_back( AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ) ) );
    return suggestion;
}


AI_SUGGESTION_RECORD makePanelFillOperationSuggestion()
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


AI_SUGGESTION_RECORD makeAnchorFocusOperationSuggestion()
{
    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_EditorKind = AI_EDITOR_KIND::Pcb;
    suggestion.m_Kind = AI_SUGGESTION_KIND::Preview;
    suggestion.m_Title = wxS( "Preview anchor focus" );
    suggestion.m_Body = wxS( "Preview semantic anchor focus before routing." );
    suggestion.m_ContextKind = wxS( "routing" );
    suggestion.m_ArgumentsJson = wxS(
            "{\"operation\":\"anchor_focus_preview\","
            "\"anchor_id\":\"tool.routing.orthogonal.horizontal\","
            "\"position\":{\"x\":500,\"y\":200},"
            "\"focus_layer\":\"F.Cu\","
            "\"focus_net\":\"/GPIO\","
            "\"dim_unfocused_layers\":true}" );
    return suggestion;
}


AI_SUGGESTION_RECORD makeActionPreviewSuggestion()
{
    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_EditorKind = AI_EDITOR_KIND::Pcb;
    suggestion.m_Kind = AI_SUGGESTION_KIND::Preview;
    suggestion.m_Title = wxS( "Preview action" );
    suggestion.m_Body = wxS( "Run this action only after acceptance." );
    suggestion.m_ArgumentsJson = wxS(
            "{\"operation\":\"action_preview\","
            "\"action\":\"common.Control.showAgentPanel\"}" );
    return suggestion;
}
} // namespace

BOOST_AUTO_TEST_SUITE( AiAgentPanelModel )


BOOST_AUTO_TEST_CASE( EmptyInputCannotSend )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    BOOST_CHECK( !model.CanSend( wxEmptyString ) );
    BOOST_CHECK( !model.CanSend( wxS( "   " ) ) );
    BOOST_CHECK( model.CanSend( wxS( "inspect board" ) ) );
}


BOOST_AUTO_TEST_CASE( SendAppendsUserAndAgentMessages )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    AI_PROVIDER_RESPONSE response = model.SendUserText( wxS( "inspect board" ), AI_EDITOR_KIND::Pcb );

    BOOST_CHECK_EQUAL( response.m_RequestId, 1 );
    BOOST_REQUIRE_EQUAL( model.Messages().size(), 2 );
    BOOST_CHECK_EQUAL( model.Messages().at( 0 ).m_Role, wxString( wxS( "user" ) ) );
    BOOST_CHECK_EQUAL( model.Messages().at( 0 ).m_Text, wxString( wxS( "inspect board" ) ) );
    BOOST_CHECK_EQUAL( model.Messages().at( 1 ).m_Role, wxString( wxS( "assistant" ) ) );
    BOOST_CHECK( model.Messages().at( 1 ).m_Text.Contains( wxS( "inspect board" ) ) );
}


BOOST_AUTO_TEST_CASE( ReloadDefaultProvidersPreservesPanelModelState )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>(), nullptr );

    model.SendUserText( wxS( "inspect board" ), AI_EDITOR_KIND::Pcb );

    AI_ACTIVITY_RECORD activity;
    activity.m_ActionName = wxS( "common.Interactive.selected" );
    AI_ACTIVITY_RECORD recorded = model.RecordActivity( activity );

    AI_AGENT_WORKSPACE_CONTEXT_STATE routingState;
    routingState.m_ContextKind = AI_AGENT_WORKSPACE_CONTEXT_KIND::Routing;
    routingState.m_Title = wxS( "Routing" );
    routingState.m_StateJson = wxS( "{\"net\":\"GND\"}" );
    routingState.m_LastActivitySequence = recorded.m_Sequence;
    model.SaveWorkspaceContextState( routingState );
    model.SetActiveWorkspaceContext( AI_AGENT_WORKSPACE_CONTEXT_KIND::Routing );

    model.ReloadDefaultProviders();

    BOOST_REQUIRE_EQUAL( model.Messages().size(), 2 );
    BOOST_REQUIRE_EQUAL( model.ActivityRecords().size(), 1 );
    BOOST_CHECK_EQUAL( model.ActivityRecords().front().m_ActionName,
                       wxString( wxS( "common.Interactive.selected" ) ) );
    BOOST_CHECK( model.ActiveWorkspaceContext() == AI_AGENT_WORKSPACE_CONTEXT_KIND::Routing );
    BOOST_CHECK_EQUAL( model.ActiveWorkspaceContextState().m_StateJson,
                       wxString( wxS( "{\"net\":\"GND\"}" ) ) );
}


BOOST_AUTO_TEST_CASE( RecordActivityReturnsSequencedRecord )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>(), nullptr );

    AI_ACTIVITY_RECORD activity;
    activity.m_ActionName = wxS( "common.Interactive.selected" );

    AI_ACTIVITY_RECORD recorded = model.RecordActivity( activity );

    BOOST_CHECK_EQUAL( recorded.m_Sequence, 1 );
    BOOST_CHECK_EQUAL( recorded.m_ActionName,
                       wxString( wxS( "common.Interactive.selected" ) ) );

    std::vector<AI_ACTIVITY_RECORD> records = model.ActivityRecords();
    BOOST_REQUIRE_EQUAL( records.size(), 1 );
    BOOST_CHECK_EQUAL( records.front().m_Sequence, recorded.m_Sequence );
    BOOST_CHECK_EQUAL( records.front().m_ActionName, recorded.m_ActionName );
}


BOOST_AUTO_TEST_CASE( BackgroundAgentIsDisabledByDefault )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>(), nullptr );

    BOOST_CHECK( !model.BackgroundAgentEnabled() );
}


BOOST_AUTO_TEST_CASE( BackgroundAgentDisabledSuppressesAutomaticSuggestions )
{
    auto* suggestionProvider = new FAKE_SUGGESTION_PROVIDER();
    suggestionProvider->m_NextSuggestion = makeModelSuggestion();

    AI_AGENT_PANEL_MODEL model(
            std::make_unique<AI_STUB_PROVIDER>(),
            std::unique_ptr<AI_SUGGESTION_PROVIDER>( suggestionProvider ) );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.UpdateSuggestionsIfBackgroundEnabled(
                    makeSuggestionContext(), makeSuggestionActivity(), wxS( "activity" ) );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_CHECK_EQUAL( suggestionProvider->m_CallCount, 0 );
}


BOOST_AUTO_TEST_CASE( BackgroundAgentEnabledDelegatesToSuggestionProvider )
{
    auto* suggestionProvider = new FAKE_SUGGESTION_PROVIDER();
    suggestionProvider->m_NextSuggestion = makeModelSuggestion();

    AI_AGENT_PANEL_MODEL model(
            std::make_unique<AI_STUB_PROVIDER>(),
            std::unique_ptr<AI_SUGGESTION_PROVIDER>( suggestionProvider ) );
    model.SetBackgroundAgentEnabled( true );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.UpdateSuggestionsIfBackgroundEnabled(
                    makeSuggestionContext(), makeSuggestionActivity(), wxS( "activity" ) );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK_EQUAL( suggestionProvider->m_CallCount, 1 );
}


BOOST_AUTO_TEST_CASE( BackgroundAgentSuggestionsArePreviewOnly )
{
    auto* suggestionProvider = new FAKE_SUGGESTION_PROVIDER();
    suggestionProvider->m_NextSuggestion = makeModelSuggestion();

    AI_AGENT_PANEL_MODEL model(
            std::make_unique<AI_STUB_PROVIDER>(),
            std::unique_ptr<AI_SUGGESTION_PROVIDER>( suggestionProvider ) );
    model.SetBackgroundAgentEnabled( true );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.UpdateSuggestionsIfBackgroundEnabled(
                    makeSuggestionContext(), makeSuggestionActivity(), wxS( "activity" ) );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK( suggestion->m_EditObjects.empty() );
    BOOST_CHECK( model.CanPreviewSuggestion( suggestion->m_Id ) );
    BOOST_CHECK( !model.CanAcceptSuggestion( suggestion->m_Id ) );

    std::optional<AI_SUGGESTION_RECORD> stored = model.FindSuggestion( suggestion->m_Id );
    BOOST_REQUIRE( stored.has_value() );
    BOOST_CHECK( stored->m_EditObjects.empty() );
}


BOOST_AUTO_TEST_CASE( ModelCanMarkActionPreviewSuggestionAccepted )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.AddSuggestion( makeActionPreviewSuggestion() );
    BOOST_REQUIRE( suggestion.has_value() );

    BOOST_CHECK( model.CanAcceptSuggestion( suggestion->m_Id ) );
    BOOST_CHECK( model.MarkSuggestionAccepted( suggestion->m_Id ) );

    std::optional<AI_SUGGESTION_RECORD> updated =
            model.FindSuggestion( suggestion->m_Id );
    BOOST_REQUIRE( updated.has_value() );
    BOOST_CHECK( updated->m_Status == AI_SUGGESTION_STATUS::Accepted );
}


BOOST_AUTO_TEST_CASE( WorkspaceContextStatePreservesIndependentContextState )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>(), nullptr );

    BOOST_CHECK( model.ActiveWorkspaceContext() == AI_AGENT_WORKSPACE_CONTEXT_KIND::General );

    AI_AGENT_WORKSPACE_CONTEXT_STATE routingState;
    routingState.m_ContextKind = AI_AGENT_WORKSPACE_CONTEXT_KIND::Routing;
    routingState.m_Title = wxS( "Routing" );
    routingState.m_StateJson = wxS( "{\"draft\":\"finish GND trace\"}" );
    routingState.m_LastActivitySequence = 7;

    model.SaveWorkspaceContextState( routingState );

    AI_AGENT_WORKSPACE_CONTEXT_STATE viaState;
    viaState.m_ContextKind = AI_AGENT_WORKSPACE_CONTEXT_KIND::ViaPlacement;
    viaState.m_Title = wxS( "Via placement" );
    viaState.m_StateJson = wxS( "{\"last_spacing\":2500000}" );
    viaState.m_LastActivitySequence = 11;

    model.SetActiveWorkspaceContext( AI_AGENT_WORKSPACE_CONTEXT_KIND::ViaPlacement );
    model.SaveWorkspaceContextState( viaState );
    model.SetActiveWorkspaceContext( AI_AGENT_WORKSPACE_CONTEXT_KIND::Routing );

    AI_AGENT_WORKSPACE_CONTEXT_STATE activeState = model.ActiveWorkspaceContextState();

    BOOST_CHECK( activeState.m_ContextKind == AI_AGENT_WORKSPACE_CONTEXT_KIND::Routing );
    BOOST_CHECK_EQUAL( activeState.m_Title, wxString( wxS( "Routing" ) ) );
    BOOST_CHECK_EQUAL( activeState.m_StateJson,
                       wxString( wxS( "{\"draft\":\"finish GND trace\"}" ) ) );
    BOOST_CHECK_EQUAL( activeState.m_LastActivitySequence, 7 );

    std::optional<AI_AGENT_WORKSPACE_CONTEXT_STATE> savedViaState =
            model.WorkspaceContextState( AI_AGENT_WORKSPACE_CONTEXT_KIND::ViaPlacement );

    BOOST_REQUIRE( savedViaState );
    BOOST_CHECK_EQUAL( savedViaState->m_StateJson,
                       wxString( wxS( "{\"last_spacing\":2500000}" ) ) );
}


BOOST_AUTO_TEST_CASE( StopMarksLastRequestCancelled )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    model.SendUserText( wxS( "cancel after response" ), AI_EDITOR_KIND::Schematic );

    BOOST_CHECK_EQUAL( model.LastRequestId(), 1 );
    BOOST_CHECK( !model.LastRequestCancelled() );
    BOOST_CHECK( model.CancelLastRequest() );
    BOOST_CHECK( model.LastRequestCancelled() );
    BOOST_CHECK( !model.CancelRequest( 999 ) );
}


BOOST_AUTO_TEST_CASE( SendPassesContextSnapshotToProvider )
{
    auto* provider = new CAPTURING_AI_PROVIDER();
    AI_AGENT_PANEL_MODEL model{ std::unique_ptr<AI_PROVIDER>( provider ) };

    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Schematic;
    snapshot.m_Version.m_DocumentRevision = 3;
    snapshot.m_SelectedObjects.push_back( AI_OBJECT_REF( KIID(), SCH_SYMBOL_T, wxS( "U3" ) ) );
    snapshot.m_Actions.push_back( { wxS( "common.Control.zoomFitScreen" ),
                                    wxS( "Zoom Fit" ),
                                    wxS( "Zoom to fit" ),
                                    AI_EDITOR_KIND::Schematic,
                                    AI_ACTION_SAFETY::ReadOnly,
                                    true } );
    snapshot.m_Visual.m_Source = wxS( "test.image" );
    snapshot.m_Visual.m_MimeType = wxS( "image/png" );
    snapshot.m_Visual.m_DataUri = wxS( "data:image/png;base64,abc" );
    snapshot.m_Visual.m_WidthPx = 4;
    snapshot.m_Visual.m_HeightPx = 2;
    snapshot.m_Visual.m_ByteSize = 12;

    AI_ACTIVITY_RECORD activity;
    activity.m_ActionName = wxS( "common.Interactive.selected" );
    activity.m_Message = wxS( "selection changed" );
    activity.m_Allowed = true;
    activity.m_Executed = true;
    model.RecordActivity( activity );

    AI_PROVIDER_RESPONSE response =
            model.SendUserText( wxS( "what am I selecting?" ), AI_EDITOR_KIND::Schematic, snapshot );

    BOOST_CHECK_EQUAL( response.m_RequestId, 1 );
    BOOST_CHECK_EQUAL( provider->m_LastRequest.m_UserText,
                       wxString( wxS( "what am I selecting?" ) ) );
    BOOST_CHECK( provider->m_LastRequest.m_ContextSnapshot.m_EditorKind == AI_EDITOR_KIND::Schematic );
    BOOST_CHECK_EQUAL( provider->m_LastRequest.m_ContextSnapshot.m_Version.m_DocumentRevision, 3 );
    BOOST_CHECK_EQUAL( provider->m_LastRequest.m_ContextSnapshot.m_Visual.m_Source,
                       wxString( wxS( "test.image" ) ) );
    BOOST_REQUIRE_EQUAL( provider->m_LastRequest.m_ContextSnapshot.m_RecentActivity.size(), 1 );
    BOOST_CHECK_EQUAL(
            provider->m_LastRequest.m_ContextSnapshot.m_RecentActivity.at( 0 ).m_ActionName,
            wxString( wxS( "common.Interactive.selected" ) ) );
    BOOST_REQUIRE_EQUAL( provider->m_LastRequest.m_ContextSnapshot.m_SelectedObjects.size(), 1 );
    BOOST_REQUIRE_EQUAL( provider->m_LastRequest.m_ContextSnapshot.m_Actions.size(), 1 );
    BOOST_CHECK( response.m_Body.Contains( wxS( "common.Control.zoomFitScreen" ) ) );
    BOOST_CHECK( response.m_Body.Contains( wxS( "visual: test.image image/png pixels=yes" ) ) );
    BOOST_CHECK( response.m_Body.Contains( wxS( "recent activity: 1" ) ) );
    BOOST_CHECK( response.m_Body.Contains( wxS( "common.Interactive.selected" ) ) );
}


BOOST_AUTO_TEST_CASE( SendUserTextUsesInstalledToolCallHandler )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<TOOL_CALL_AI_PROVIDER>() );
    FAKE_PANEL_TOOL_CALL_HANDLER handler;

    model.SetToolCallHandler( &handler );

    AI_PROVIDER_RESPONSE response = model.SendUserText( wxS( "show agent" ), AI_EDITOR_KIND::Pcb );

    BOOST_CHECK_EQUAL( handler.m_CallCount, 1 );
    BOOST_REQUIRE_EQUAL( response.m_ToolCalls.size(), 1 );
    BOOST_CHECK( response.m_ToolCalls.front().m_Allowed );
    BOOST_CHECK( response.m_ToolCalls.front().m_Executed );
    BOOST_CHECK_EQUAL( response.m_ToolCalls.front().m_ResultJson,
                       wxString( wxS( "{\"status\":\"panel-executed\"}" ) ) );
}


BOOST_AUTO_TEST_CASE( ObservabilityEntriesExposeRuntimeTraceAndActivity )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<TOOL_CALL_AI_PROVIDER>(), nullptr );
    FAKE_PANEL_TOOL_CALL_HANDLER handler;
    model.SetToolCallHandler( &handler );

    model.SendUserText( wxS( "show agent" ), AI_EDITOR_KIND::Pcb );

    std::vector<AI_AGENT_OBSERVABILITY_ENTRY> entries =
            model.ObservabilityEntries( 16 );

    BOOST_REQUIRE_GE( entries.size(), 4 );

    bool sawInput = false;
    bool sawToolCall = false;
    bool sawToolResult = false;
    bool sawOutput = false;

    for( const AI_AGENT_OBSERVABILITY_ENTRY& entry : entries )
    {
        sawInput |= entry.m_Kind == AI_AGENT_OBSERVABILITY_KIND::ModelInput;
        sawToolCall |= entry.m_Kind == AI_AGENT_OBSERVABILITY_KIND::ModelToolCall;
        sawToolResult |= entry.m_Kind == AI_AGENT_OBSERVABILITY_KIND::ToolResult;
        sawOutput |= entry.m_Kind == AI_AGENT_OBSERVABILITY_KIND::ModelOutput;
    }

    BOOST_CHECK( sawInput );
    BOOST_CHECK( sawToolCall );
    BOOST_CHECK( sawToolResult );
    BOOST_CHECK( sawOutput );
}


BOOST_AUTO_TEST_CASE( SendUserTextIncludesPriorRuntimeActivity )
{
    auto* provider = new RUNTIME_ACTIVITY_CAPTURE_PROVIDER();
    AI_AGENT_PANEL_MODEL model( std::unique_ptr<AI_PROVIDER>( provider ), nullptr );
    FAKE_PANEL_TOOL_CALL_HANDLER handler;
    model.SetToolCallHandler( &handler );

    model.SendUserText( wxS( "show agent" ), AI_EDITOR_KIND::Pcb );
    model.SendUserText( wxS( "what happened?" ), AI_EDITOR_KIND::Pcb );

    BOOST_CHECK_EQUAL( provider->m_CallCount, 3 );

    const std::vector<AI_ACTIVITY_RECORD>& activity =
            provider->m_LastRequest.m_ContextSnapshot.m_RecentActivity;

    BOOST_REQUIRE_GE( activity.size(), 2 );

    bool sawToolRequest = false;
    bool sawToolResult = false;

    for( const AI_ACTIVITY_RECORD& record : activity )
    {
        if( record.m_ToolCallId != wxS( "call_panel" ) )
            continue;

        if( record.m_Kind == AI_ACTIVITY_KIND::ModelToolRequest )
            sawToolRequest = true;

        if( record.m_Kind == AI_ACTIVITY_KIND::ToolResult
            && record.m_ResultJson.Contains( wxS( "panel-executed" ) ) )
        {
            sawToolResult = true;
        }
    }

    BOOST_CHECK( sawToolRequest );
    BOOST_CHECK( sawToolResult );
}


BOOST_AUTO_TEST_CASE( ActivityRecordsShareSequenceAcrossUserAndRuntimeRecords )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<TOOL_CALL_AI_PROVIDER>(), nullptr );

    AI_ACTIVITY_RECORD userActivity;
    userActivity.m_ActionName = wxS( "common.Interactive.selected" );
    userActivity.m_Kind = AI_ACTIVITY_KIND::UserAction;

    AI_ACTIVITY_RECORD recorded = model.RecordActivity( userActivity );
    BOOST_CHECK_EQUAL( recorded.m_Sequence, 1 );

    model.SendUserText( wxS( "show agent" ), AI_EDITOR_KIND::Pcb );

    std::vector<AI_ACTIVITY_RECORD> records = model.ActivityRecords();

    BOOST_REQUIRE_EQUAL( records.size(), 3 );
    BOOST_CHECK_EQUAL( records.at( 0 ).m_Sequence, 1 );
    BOOST_CHECK( records.at( 0 ).m_Kind == AI_ACTIVITY_KIND::UserAction );
    BOOST_CHECK_EQUAL( records.at( 1 ).m_Sequence, 2 );
    BOOST_CHECK( records.at( 1 ).m_Kind == AI_ACTIVITY_KIND::ModelToolRequest );
    BOOST_CHECK_EQUAL( records.at( 2 ).m_Sequence, 3 );
    BOOST_CHECK( records.at( 2 ).m_Kind == AI_ACTIVITY_KIND::ToolResult );
}


BOOST_AUTO_TEST_CASE( UpdateSuggestionsStoresProviderSuggestion )
{
    auto* suggestionProvider = new FAKE_SUGGESTION_PROVIDER();
    suggestionProvider->m_NextSuggestion = makeModelSuggestion();

    AI_AGENT_PANEL_MODEL model(
            std::make_unique<AI_STUB_PROVIDER>(),
            std::unique_ptr<AI_SUGGESTION_PROVIDER>( suggestionProvider ) );

    std::optional<AI_SUGGESTION_RECORD> suggestion = model.UpdateSuggestions(
            makeSuggestionContext(), makeSuggestionActivity(), wxS( "activity" ) );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK_EQUAL( suggestionProvider->m_CallCount, 1 );
    BOOST_CHECK_EQUAL( suggestion->m_Id, 1 );
    BOOST_REQUIRE_EQUAL( model.Suggestions().size(), 1 );
    BOOST_CHECK_EQUAL( suggestionProvider->m_LastTrigger.m_Reason,
                       wxString( wxS( "activity" ) ) );
}


BOOST_AUTO_TEST_CASE( AddSuggestionStoresToolGeneratedSuggestion )
{
    AI_AGENT_PANEL_MODEL model(
            std::make_unique<AI_STUB_PROVIDER>(),
            std::make_unique<FAKE_SUGGESTION_PROVIDER>() );

    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_EditorKind = AI_EDITOR_KIND::Pcb;
    suggestion.m_ContextVersion.m_DocumentRevision = 3;
    suggestion.m_Title = wxS( "Preview moving selection" );
    suggestion.m_ArgumentsJson = wxS( "{\"operation\":\"move_selected\",\"dx\":10,\"dy\":20}" );
    suggestion.m_PreviewObjects.push_back(
            AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ) ) );
    suggestion.m_EditObjects = suggestion.m_PreviewObjects;

    std::optional<AI_SUGGESTION_RECORD> stored =
            model.AddSuggestion( std::move( suggestion ) );

    BOOST_REQUIRE( stored.has_value() );
    BOOST_CHECK_EQUAL( stored->m_Id, 1 );
    BOOST_CHECK_EQUAL( model.LatestActiveSuggestionId().value_or( 0 ), 1 );
    BOOST_REQUIRE_EQUAL( model.Suggestions().size(), 1 );
    BOOST_CHECK_EQUAL( model.Suggestions().front().m_Title,
                       wxString( wxS( "Preview moving selection" ) ) );
}


BOOST_AUTO_TEST_CASE( DefaultModelIncludesNativeNextActionSuggestions )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    std::optional<AI_SUGGESTION_RECORD> suggestion = model.UpdateSuggestions(
            makeViaNextActionContext(), makeSuggestionActivity( 9 ), wxS( "activity" ) );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK( suggestion->m_Title.Contains( wxS( "next via" ) ) );

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( suggestion->m_ArgumentsJson );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsPlaceViaPreview() );
    BOOST_CHECK_EQUAL( operation->m_Position.x, 400 );
    BOOST_CHECK_EQUAL( operation->m_Position.y, 50 );
}


BOOST_AUTO_TEST_CASE( DefaultModelIncludesPanelTableSuggestions )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    std::optional<AI_SUGGESTION_RECORD> suggestion = model.UpdateSuggestions(
            makePanelTableContext(), makeSuggestionActivity( 10 ), wxS( "panel edit" ) );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK( suggestion->m_Title.Contains( wxS( "Fill" ) ) );
    BOOST_CHECK_EQUAL( suggestion->m_ContextKind, wxString( wxS( "panel" ) ) );
    BOOST_CHECK( suggestion->m_ArgumentsJson.Contains(
            wxS( "panel_fill_column_preview" ) ) );
    BOOST_REQUIRE_EQUAL( model.Suggestions().size(), 1 );
}


BOOST_AUTO_TEST_CASE( DuplicateActiveSuggestionsAreSuppressed )
{
    auto* suggestionProvider = new FAKE_SUGGESTION_PROVIDER();
    AI_SUGGESTION_RECORD record = makeModelSuggestion();
    record.m_Fingerprint = wxS( "same" );
    suggestionProvider->m_NextSuggestion = record;

    AI_AGENT_PANEL_MODEL model(
            std::make_unique<AI_STUB_PROVIDER>(),
            std::unique_ptr<AI_SUGGESTION_PROVIDER>( suggestionProvider ) );

    BOOST_REQUIRE( model.UpdateSuggestions( makeSuggestionContext(), makeSuggestionActivity(),
                                            wxS( "activity" ) )
                           .has_value() );

    suggestionProvider->m_NextSuggestion = record;
    BOOST_CHECK( !model.UpdateSuggestions( makeSuggestionContext(), makeSuggestionActivity( 2 ),
                                           wxS( "activity" ) )
                          .has_value() );
    BOOST_REQUIRE_EQUAL( model.Suggestions().size(), 1 );
}


BOOST_AUTO_TEST_CASE( SuggestionLifecycleDelegatesToOrchestrator )
{
    auto* suggestionProvider = new FAKE_SUGGESTION_PROVIDER();
    suggestionProvider->m_NextSuggestion = makeModelSuggestion();

    AI_AGENT_PANEL_MODEL model(
            std::make_unique<AI_STUB_PROVIDER>(),
            std::unique_ptr<AI_SUGGESTION_PROVIDER>( suggestionProvider ) );

    std::optional<AI_SUGGESTION_RECORD> suggestion = model.UpdateSuggestions(
            makeSuggestionContext(), makeSuggestionActivity(), wxS( "activity" ) );
    BOOST_REQUIRE( suggestion.has_value() );

    FAKE_PREVIEW_ADAPTER previewAdapter;
    AI_PREVIEW_MANAGER   preview( previewAdapter );
    BOOST_CHECK( model.PreviewSuggestion( suggestion->m_Id, preview ) );
    BOOST_CHECK_EQUAL( previewAdapter.m_BeginId, 1 );
    BOOST_REQUIRE_EQUAL( previewAdapter.m_Shown.size(), 1 );
    BOOST_CHECK( model.FindSuggestion( suggestion->m_Id )->m_Status
                 == AI_SUGGESTION_STATUS::Previewing );

    FAKE_EDIT_ADAPTER editAdapter;
    AI_EDIT_SESSION   edit( editAdapter );
    BOOST_CHECK( model.AcceptSuggestion( suggestion->m_Id, edit ) );
    BOOST_REQUIRE_EQUAL( editAdapter.m_Applied.size(), 1 );
    BOOST_CHECK( model.FindSuggestion( suggestion->m_Id )->m_Status
                 == AI_SUGGESTION_STATUS::Accepted );
}


BOOST_AUTO_TEST_CASE( OperationOnlySuggestionExposesPreviewOnlyCapability )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.AddSuggestion( makePanelFillOperationSuggestion() );
    BOOST_REQUIRE( suggestion.has_value() );

    BOOST_CHECK( model.CanPreviewSuggestion( suggestion->m_Id ) );
    BOOST_CHECK( !model.CanAcceptSuggestion( suggestion->m_Id ) );

    FAKE_PREVIEW_ADAPTER previewAdapter;
    AI_PREVIEW_MANAGER   preview( previewAdapter );
    BOOST_CHECK( model.PreviewSuggestion( suggestion->m_Id, preview ) );
    BOOST_CHECK_EQUAL( previewAdapter.m_BeginId, suggestion->m_Id );
    BOOST_CHECK( previewAdapter.m_Shown.empty() );
    BOOST_REQUIRE_EQUAL( previewAdapter.m_Operations.size(), 1 );
    BOOST_CHECK( previewAdapter.m_Operations.front().IsPanelFillColumnPreview() );
    BOOST_CHECK( model.FindSuggestion( suggestion->m_Id )->m_Status
                 == AI_SUGGESTION_STATUS::Previewing );

    FAKE_EDIT_ADAPTER editAdapter;
    AI_EDIT_SESSION   edit( editAdapter );
    BOOST_CHECK( !model.AcceptSuggestion( suggestion->m_Id, edit ) );
    BOOST_CHECK( editAdapter.m_Applied.empty() );
}


BOOST_AUTO_TEST_CASE( AnchorFocusSuggestionExposesPreviewOnlyCapability )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>() );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.AddSuggestion( makeAnchorFocusOperationSuggestion() );
    BOOST_REQUIRE( suggestion.has_value() );

    BOOST_CHECK( model.CanPreviewSuggestion( suggestion->m_Id ) );
    BOOST_CHECK( !model.CanAcceptSuggestion( suggestion->m_Id ) );

    FAKE_PREVIEW_ADAPTER previewAdapter;
    AI_PREVIEW_MANAGER   preview( previewAdapter );
    BOOST_CHECK( model.PreviewSuggestion( suggestion->m_Id, preview ) );
    BOOST_CHECK_EQUAL( previewAdapter.m_BeginId, suggestion->m_Id );
    BOOST_CHECK( previewAdapter.m_Shown.empty() );
    BOOST_REQUIRE_EQUAL( previewAdapter.m_Operations.size(), 1 );
    BOOST_CHECK( previewAdapter.m_Operations.front().IsAnchorFocusPreview() );
    BOOST_CHECK_EQUAL( previewAdapter.m_Operations.front().m_AnchorId,
                       wxString( wxS( "tool.routing.orthogonal.horizontal" ) ) );
}


BOOST_AUTO_TEST_CASE( LatestActiveSuggestionReturnsNewestPendingOrPreviewingRecord )
{
    auto* suggestionProvider = new FAKE_SUGGESTION_PROVIDER();
    AI_AGENT_PANEL_MODEL model(
            std::make_unique<AI_STUB_PROVIDER>(),
            std::unique_ptr<AI_SUGGESTION_PROVIDER>( suggestionProvider ) );

    BOOST_CHECK( !model.LatestActiveSuggestionId().has_value() );

    suggestionProvider->m_NextSuggestion = makeModelSuggestion( wxS( "First" ) );
    std::optional<AI_SUGGESTION_RECORD> first = model.UpdateSuggestions(
            makeSuggestionContext( 1 ), makeSuggestionActivity( 1 ), wxS( "activity" ) );
    BOOST_REQUIRE( first.has_value() );

    suggestionProvider->m_NextSuggestion = makeModelSuggestion( wxS( "Second" ) );
    std::optional<AI_SUGGESTION_RECORD> second = model.UpdateSuggestions(
            makeSuggestionContext( 1 ), makeSuggestionActivity( 2 ), wxS( "activity" ) );
    BOOST_REQUIRE( second.has_value() );

    BOOST_REQUIRE( model.LatestActiveSuggestionId().has_value() );
    BOOST_CHECK_EQUAL( *model.LatestActiveSuggestionId(), second->m_Id );

    BOOST_CHECK( model.RejectSuggestion( second->m_Id ) );
    BOOST_REQUIRE( model.LatestActiveSuggestionId().has_value() );
    BOOST_CHECK_EQUAL( *model.LatestActiveSuggestionId(), first->m_Id );

    BOOST_CHECK( model.RejectSuggestion( first->m_Id ) );
    BOOST_CHECK( !model.LatestActiveSuggestionId().has_value() );
}


BOOST_AUTO_TEST_CASE( ExpireSuggestionsMarksOnlyStaleActiveRecords )
{
    auto* suggestionProvider = new FAKE_SUGGESTION_PROVIDER();
    suggestionProvider->m_NextSuggestion = makeModelSuggestion();

    AI_AGENT_PANEL_MODEL model(
            std::make_unique<AI_STUB_PROVIDER>(),
            std::unique_ptr<AI_SUGGESTION_PROVIDER>( suggestionProvider ) );

    std::optional<AI_SUGGESTION_RECORD> suggestion = model.UpdateSuggestions(
            makeSuggestionContext( 1 ), makeSuggestionActivity(), wxS( "activity" ) );
    BOOST_REQUIRE( suggestion.has_value() );

    AI_CONTEXT_VERSION current;
    current.m_DocumentRevision = 2;
    BOOST_CHECK_EQUAL( model.ExpireSuggestions( current ), 1 );
    BOOST_CHECK( model.FindSuggestion( suggestion->m_Id )->m_Status
                 == AI_SUGGESTION_STATUS::Expired );
}


BOOST_AUTO_TEST_SUITE_END()
