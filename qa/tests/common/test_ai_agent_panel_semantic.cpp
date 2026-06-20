#include <boost/test/unit_test.hpp>

#include <json_common.h>
#include <kisurf/ai/ai_agent_panel_semantic.h>

namespace
{
const nlohmann::json* findNodeJson( const nlohmann::json& aNodes,
                                    const std::string& aNodeId )
{
    for( const nlohmann::json& node : aNodes )
    {
        if( node["id"].get<std::string>() == aNodeId )
            return &node;
    }

    return nullptr;
}
} // namespace

BOOST_AUTO_TEST_SUITE( AiAgentPanelSemantic )


BOOST_AUTO_TEST_CASE( EmitsStableAgentPaneNodeIds )
{
    AI_AGENT_PANEL_SEMANTIC_VIEW view;

    AI_SEMANTIC_UI_TREE tree = AiAgentPanelSemanticTree( view );

    const wxString ids[] = {
        wxS( "agent.root" ),
        wxS( "agent.background.toggle" ),
        wxS( "agent.model.settings" ),
        wxS( "agent.tabs.chat" ),
        wxS( "agent.tabs.log" ),
        wxS( "agent.chat.transcript" ),
        wxS( "agent.log.entries" ),
        wxS( "agent.input" ),
        wxS( "agent.composer.status" ),
        wxS( "agent.send" ),
        wxS( "agent.stop" ),
        wxS( "agent.preview.invoke" ),
        wxS( "agent.accept" ),
        wxS( "agent.reject" )
    };

    for( const wxString& id : ids )
        BOOST_CHECK_MESSAGE( tree.FindNode( id ), id.ToStdString() );

    BOOST_CHECK( !tree.FindNode( wxS( "agent.mode.choice" ) ) );
    BOOST_CHECK( !tree.FindNode( wxS( "agent.tabs.preview" ) ) );
    BOOST_CHECK( !tree.FindNode( wxS( "agent.preview.suggestions" ) ) );
    BOOST_CHECK_EQUAL( tree.FindNode( wxS( "agent.preview.invoke" ) )->m_ParentNodeId,
                       wxString( wxS( "agent.tabs.chat" ) ) );
    BOOST_CHECK_EQUAL( tree.FindNode( wxS( "agent.accept" ) )->m_ParentNodeId,
                       wxString( wxS( "agent.tabs.chat" ) ) );
    BOOST_CHECK_EQUAL( tree.FindNode( wxS( "agent.reject" ) )->m_ParentNodeId,
                       wxString( wxS( "agent.tabs.chat" ) ) );
}


BOOST_AUTO_TEST_CASE( ComposerStatusIsProjectedIntoSemanticTreeAndPanelState )
{
    AI_AGENT_PANEL_SEMANTIC_VIEW view;
    view.m_ComposerStatusText = wxS( "Background Agent on" );

    AI_SEMANTIC_UI_TREE tree = AiAgentPanelSemanticTree( view );
    const AI_SEMANTIC_UI_NODE* statusNode =
            tree.FindNode( wxS( "agent.composer.status" ) );

    BOOST_REQUIRE( statusNode );
    BOOST_CHECK_EQUAL( statusNode->m_Role, wxString( wxS( "text" ) ) );
    BOOST_CHECK( statusNode->m_TextPolicy == AI_SEMANTIC_UI_TEXT_POLICY::Plain );
    BOOST_CHECK_EQUAL( statusNode->m_TextValue,
                       wxString( wxS( "Background Agent on" ) ) );

    AI_PANEL_STATE_RECORD record = AiAgentPanelSemanticStateRecord( tree );
    BOOST_CHECK( record.m_Summary.Contains(
            wxS( "composer_status=Background Agent on" ) ) );
}


BOOST_AUTO_TEST_CASE( ModelSettingsNodeIsInvokableButton )
{
    AI_AGENT_PANEL_SEMANTIC_VIEW view;

    AI_SEMANTIC_UI_TREE tree = AiAgentPanelSemanticTree( view );
    const AI_SEMANTIC_UI_NODE* modelSettings =
            tree.FindNode( wxS( "agent.model.settings" ) );

    BOOST_REQUIRE( modelSettings );
    BOOST_CHECK_EQUAL( modelSettings->m_ParentNodeId,
                       wxString( wxS( "agent.root" ) ) );
    BOOST_CHECK_EQUAL( modelSettings->m_Role, wxString( wxS( "button" ) ) );
    BOOST_CHECK_EQUAL( modelSettings->m_Label,
                       wxString( wxS( "Model Settings" ) ) );
    BOOST_CHECK( modelSettings->m_Enabled );
    BOOST_CHECK_EQUAL( modelSettings->m_ActionName,
                       wxString( wxS( "invoke" ) ) );
    BOOST_CHECK( !modelSettings->m_RequiresUserConfirmation );
    BOOST_CHECK( modelSettings->m_TextPolicy == AI_SEMANTIC_UI_TEXT_POLICY::None );
    BOOST_CHECK( modelSettings->m_TextValue.IsEmpty() );
}


BOOST_AUTO_TEST_CASE( SendNodeReflectsInputState )
{
    AI_AGENT_PANEL_SEMANTIC_VIEW view;
    view.m_InputHasText = false;

    AI_SEMANTIC_UI_TREE tree = AiAgentPanelSemanticTree( view );
    BOOST_REQUIRE( tree.FindNode( wxS( "agent.send" ) ) );
    BOOST_CHECK( !tree.FindNode( wxS( "agent.send" ) )->m_Enabled );

    view.m_InputHasText = true;
    tree = AiAgentPanelSemanticTree( view );
    BOOST_REQUIRE( tree.FindNode( wxS( "agent.send" ) ) );
    BOOST_CHECK( tree.FindNode( wxS( "agent.send" ) )->m_Enabled );
}


BOOST_AUTO_TEST_CASE( SuggestionControlsReflectHandlersAndActiveSuggestion )
{
    AI_AGENT_PANEL_SEMANTIC_VIEW view;
    view.m_HasActiveSuggestion = false;
    view.m_CanPreviewSuggestion = true;
    view.m_CanAcceptSuggestion = true;

    AI_SEMANTIC_UI_TREE tree = AiAgentPanelSemanticTree( view );
    BOOST_CHECK( !tree.FindNode( wxS( "agent.preview.invoke" ) )->m_Enabled );
    BOOST_CHECK( !tree.FindNode( wxS( "agent.accept" ) )->m_Enabled );
    BOOST_CHECK( !tree.FindNode( wxS( "agent.reject" ) )->m_Enabled );

    view.m_HasActiveSuggestion = true;
    tree = AiAgentPanelSemanticTree( view );
    BOOST_CHECK( tree.FindNode( wxS( "agent.preview.invoke" ) )->m_Enabled );
    BOOST_CHECK( tree.FindNode( wxS( "agent.accept" ) )->m_Enabled );
    BOOST_CHECK( tree.FindNode( wxS( "agent.reject" ) )->m_Enabled );
}


BOOST_AUTO_TEST_CASE( AcceptNodeRequiresUserConfirmation )
{
    AI_AGENT_PANEL_SEMANTIC_VIEW view;
    view.m_HasActiveSuggestion = true;
    view.m_CanPreviewSuggestion = true;
    view.m_CanAcceptSuggestion = true;

    AI_SEMANTIC_UI_TREE tree = AiAgentPanelSemanticTree( view );

    const AI_SEMANTIC_UI_NODE* accept = tree.FindNode( wxS( "agent.accept" ) );
    const AI_SEMANTIC_UI_NODE* preview = tree.FindNode( wxS( "agent.preview.invoke" ) );
    const AI_SEMANTIC_UI_NODE* send = tree.FindNode( wxS( "agent.send" ) );

    BOOST_REQUIRE( accept );
    BOOST_REQUIRE( preview );
    BOOST_REQUIRE( send );
    BOOST_CHECK( accept->m_RequiresUserConfirmation );
    BOOST_CHECK( !preview->m_RequiresUserConfirmation );
    BOOST_CHECK( !send->m_RequiresUserConfirmation );
}


BOOST_AUTO_TEST_CASE( SemanticUiActionRequestIsUnconfirmedByDefault )
{
    AI_SEMANTIC_UI_ACTION_REQUEST request;

    BOOST_CHECK( !request.m_UserConfirmed );
}


BOOST_AUTO_TEST_CASE( LogSummaryOverridesCountText )
{
    AI_AGENT_PANEL_SEMANTIC_VIEW view;
    view.m_LogEntryCount = 3;
    view.m_LogSummary =
            wxS( "#1 Input: Model input\n#2 Tool: kisurf_get_workspace_view" );

    AI_SEMANTIC_UI_TREE tree = AiAgentPanelSemanticTree( view );
    const AI_SEMANTIC_UI_NODE* log = tree.FindNode( wxS( "agent.log.entries" ) );

    BOOST_REQUIRE( log );
    BOOST_CHECK( log->m_TextPolicy == AI_SEMANTIC_UI_TEXT_POLICY::Plain );
    BOOST_CHECK_EQUAL( log->m_TextValue,
                       wxString( wxS( "#1 Input: Model input\n"
                                      "#2 Tool: kisurf_get_workspace_view" ) ) );
}


BOOST_AUTO_TEST_CASE( LogSummaryIsRedacted )
{
    AI_AGENT_PANEL_SEMANTIC_VIEW view;
    view.m_LogEntryCount = 1;
    view.m_LogSummary = wxString( wxS( "token: " ) ) + wxS( "secret-value " )
                        + wxS( "sk-" ) + wxS( "12345678901234567890" );

    AI_SEMANTIC_UI_TREE tree = AiAgentPanelSemanticTree( view );
    const AI_SEMANTIC_UI_NODE* log = tree.FindNode( wxS( "agent.log.entries" ) );

    BOOST_REQUIRE( log );
    BOOST_CHECK( !log->m_TextValue.Contains( wxS( "secret-value" ) ) );
    BOOST_CHECK( !log->m_TextValue.Contains( wxS( "12345678901234567890" ) ) );
    BOOST_CHECK( log->m_TextValue.Contains( wxS( "redacted" ) ) );
}


BOOST_AUTO_TEST_CASE( EmptyLogSummaryFallsBackToEntryCount )
{
    AI_AGENT_PANEL_SEMANTIC_VIEW view;
    view.m_LogEntryCount = 5;

    AI_SEMANTIC_UI_TREE tree = AiAgentPanelSemanticTree( view );
    const AI_SEMANTIC_UI_NODE* log = tree.FindNode( wxS( "agent.log.entries" ) );

    BOOST_REQUIRE( log );
    BOOST_CHECK_EQUAL( log->m_TextValue, wxString( wxS( "5 entries" ) ) );
}


BOOST_AUTO_TEST_CASE( PanelStateRecordProjectsSemanticTreeSafely )
{
    AI_AGENT_PANEL_SEMANTIC_VIEW view;
    view.m_BackgroundAgentEnabled = true;
    view.m_InputHasText = true;
    view.m_HasActiveSuggestion = true;
    view.m_CanPreviewSuggestion = true;
    view.m_CanAcceptSuggestion = true;
    view.m_MessageCount = 2;
    view.m_SuggestionCount = 3;
    view.m_LogEntryCount = 4;

    AI_PANEL_STATE_RECORD record = AiAgentPanelSemanticStateRecord( view );

    BOOST_CHECK_EQUAL( record.m_Id, wxString( wxS( "agent.panel" ) ) );
    BOOST_CHECK_EQUAL( record.m_Title, wxString( wxS( "Agent" ) ) );
    BOOST_CHECK_EQUAL( record.m_FocusedControlId, wxString( wxS( "agent.input" ) ) );
    BOOST_CHECK_EQUAL( record.m_FocusedControlLabel, wxString( wxS( "Input" ) ) );
    BOOST_CHECK( record.m_SelectedText.IsEmpty() );
    BOOST_CHECK( !record.m_Summary.Contains( wxS( "mode=" ) ) );
    BOOST_CHECK( record.m_Summary.Contains( wxS( "background=on" ) ) );
    BOOST_CHECK( record.m_Summary.Contains( wxS( "messages=2 messages" ) ) );
    BOOST_CHECK( !record.m_Summary.Contains( wxS( "suggestions=" ) ) );
    BOOST_CHECK( record.m_Summary.Contains( wxS( "logs=4 entries" ) ) );
    BOOST_CHECK( record.m_Summary.Contains( wxS( "send=enabled" ) ) );
    BOOST_CHECK( record.m_Summary.Contains( wxS( "active_suggestion=yes" ) ) );

    nlohmann::json state = nlohmann::json::parse( record.m_StateJson.ToStdString() );
    BOOST_CHECK_EQUAL( state["frame_id"].get<std::string>(), "agent" );
    BOOST_CHECK_EQUAL( state["title"].get<std::string>(), "Agent" );
    BOOST_CHECK( !state["screenshot_available"].get<bool>() );

    const nlohmann::json& nodes = state["nodes"];
    const nlohmann::json* send = findNodeJson( nodes, "agent.send" );
    BOOST_REQUIRE( send );
    BOOST_CHECK_EQUAL( ( *send )["role"].get<std::string>(), "button" );
    BOOST_CHECK_EQUAL( ( *send )["action"].get<std::string>(), "invoke" );
    BOOST_CHECK( ( *send )["enabled"].get<bool>() );
    BOOST_CHECK( !( *send )["requires_user_confirmation"].get<bool>() );

    const nlohmann::json* modelSettings =
            findNodeJson( nodes, "agent.model.settings" );
    BOOST_REQUIRE( modelSettings );
    BOOST_CHECK_EQUAL( ( *modelSettings )["role"].get<std::string>(), "button" );
    BOOST_CHECK_EQUAL( ( *modelSettings )["action"].get<std::string>(), "invoke" );
    BOOST_CHECK( !( *modelSettings )["requires_user_confirmation"].get<bool>() );
    BOOST_CHECK( !( *modelSettings ).contains( "text_value" ) );

    const nlohmann::json* accept = findNodeJson( nodes, "agent.accept" );
    BOOST_REQUIRE( accept );
    BOOST_CHECK( ( *accept )["requires_user_confirmation"].get<bool>() );

    const nlohmann::json* input = findNodeJson( nodes, "agent.input" );
    BOOST_REQUIRE( input );
    BOOST_CHECK_EQUAL( ( *input )["text_policy"].get<std::string>(), "redacted" );
    BOOST_CHECK( !input->contains( "text_value" ) );

    const nlohmann::json* transcript =
            findNodeJson( nodes, "agent.chat.transcript" );
    BOOST_REQUIRE( transcript );
    BOOST_CHECK_EQUAL( ( *transcript )["text_policy"].get<std::string>(),
                       "plain" );
    BOOST_CHECK_EQUAL( ( *transcript )["text_value"].get<std::string>(),
                       "2 messages" );
}


BOOST_AUTO_TEST_CASE( UpsertPanelStateRecordAppendsFirstValidRecord )
{
    AI_CONTEXT_SNAPSHOT snapshot;

    AI_PANEL_STATE_RECORD record;
    record.m_Id = wxS( "agent.panel" );
    record.m_Title = wxS( "Agent" );
    record.m_Summary = wxS( "first" );

    AiUpsertPanelStateRecord( snapshot, record );

    BOOST_REQUIRE_EQUAL( snapshot.m_PanelStates.size(), 1 );
    BOOST_CHECK_EQUAL( snapshot.m_PanelStates[0].m_Id,
                       wxString( wxS( "agent.panel" ) ) );
    BOOST_CHECK_EQUAL( snapshot.m_PanelStates[0].m_Summary,
                       wxString( wxS( "first" ) ) );
}


BOOST_AUTO_TEST_CASE( UpsertPanelStateRecordReplacesMatchingId )
{
    AI_CONTEXT_SNAPSHOT snapshot;

    AI_PANEL_STATE_RECORD first;
    first.m_Id = wxS( "agent.panel" );
    first.m_Title = wxS( "Agent" );
    first.m_Summary = wxS( "first" );
    first.m_StateJson = wxS( "{\"version\":1}" );
    snapshot.m_PanelStates.push_back( first );

    AI_PANEL_STATE_RECORD replacement;
    replacement.m_Id = wxS( "agent.panel" );
    replacement.m_Title = wxS( "Agent" );
    replacement.m_Summary = wxS( "replacement" );
    replacement.m_StateJson = wxS( "{\"version\":2}" );

    AiUpsertPanelStateRecord( snapshot, replacement );

    BOOST_REQUIRE_EQUAL( snapshot.m_PanelStates.size(), 1 );
    BOOST_CHECK_EQUAL( snapshot.m_PanelStates[0].m_Summary,
                       wxString( wxS( "replacement" ) ) );
    BOOST_CHECK_EQUAL( snapshot.m_PanelStates[0].m_StateJson,
                       wxString( wxS( "{\"version\":2}" ) ) );
}


BOOST_AUTO_TEST_CASE( UpsertPanelStateRecordIgnoresEmptyRecord )
{
    AI_CONTEXT_SNAPSHOT snapshot;

    AiUpsertPanelStateRecord( snapshot, AI_PANEL_STATE_RECORD() );

    BOOST_CHECK( snapshot.m_PanelStates.empty() );
}


BOOST_AUTO_TEST_SUITE_END()
