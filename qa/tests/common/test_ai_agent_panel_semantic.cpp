#include <boost/test/unit_test.hpp>

#include <json_common.h>
#include <kisurf/ai/ai_agent_panel_semantic.h>
#include <kisurf/ai/ai_panel_state_wx_adapter.h>
#include <kisurf/ai/ai_structured_surface_apply_adapter.h>

#include <utility>

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


class RECORDING_PANEL_GRID_IO : public AI_STRUCTURED_SURFACE_GRID_IO
{
public:
    explicit RECORDING_PANEL_GRID_IO( std::vector<std::vector<wxString>> aCells ) :
            m_Cells( std::move( aCells ) )
    {
        m_RowLabels.resize( m_Cells.size() );
        m_ColumnLabels.resize( m_Cells.empty() ? 0 : m_Cells.front().size() );
    }

    int RowCount() const override
    {
        return static_cast<int>( m_Cells.size() );
    }

    int ColumnCount() const override
    {
        return m_Cells.empty() ? 0 : static_cast<int>( m_Cells.front().size() );
    }

    wxString RowLabel( int aRow ) const override
    {
        return m_RowLabels.at( aRow );
    }

    wxString ColumnLabel( int aColumn ) const override
    {
        return m_ColumnLabels.at( aColumn );
    }

    wxString CellValue( int aRow, int aColumn ) const override
    {
        return m_Cells.at( aRow ).at( aColumn );
    }

    void SetCellValue( int aRow, int aColumn, const wxString& aValue ) override
    {
        m_Cells.at( aRow ).at( aColumn ) = aValue;
    }

    std::vector<std::pair<int, int>> SelectedCells() const override
    {
        return m_SelectedCells;
    }

    std::vector<std::vector<wxString>> m_Cells;
    std::vector<wxString>              m_RowLabels;
    std::vector<wxString>              m_ColumnLabels;
    std::vector<std::pair<int, int>>   m_SelectedCells;
};
} // namespace

BOOST_AUTO_TEST_SUITE( AiAgentPanelSemantic )


BOOST_AUTO_TEST_CASE( EmitsStableAgentPaneNodeIds )
{
    AI_AGENT_PANEL_SEMANTIC_VIEW view;
    view.m_HasProviderRecovery = true;

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
        wxS( "agent.reject" ),
        wxS( "agent.recovery.execute" )
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


BOOST_AUTO_TEST_CASE( ProviderRecoveryNodeRequiresConfirmationAndReflectsAvailability )
{
    AI_AGENT_PANEL_SEMANTIC_VIEW view;
    view.m_HasProviderRecovery = false;
    view.m_CanExecuteProviderRecovery = true;

    AI_SEMANTIC_UI_TREE tree = AiAgentPanelSemanticTree( view );

    BOOST_CHECK( !tree.FindNode( wxS( "agent.recovery.execute" ) ) );

    view.m_HasProviderRecovery = true;
    view.m_CanExecuteProviderRecovery = false;
    tree = AiAgentPanelSemanticTree( view );

    const AI_SEMANTIC_UI_NODE* disabledRecovery =
            tree.FindNode( wxS( "agent.recovery.execute" ) );

    BOOST_REQUIRE( disabledRecovery );
    BOOST_CHECK_EQUAL( disabledRecovery->m_ParentNodeId,
                       wxString( wxS( "agent.tabs.log" ) ) );
    BOOST_CHECK_EQUAL( disabledRecovery->m_Role, wxString( wxS( "button" ) ) );
    BOOST_CHECK_EQUAL( disabledRecovery->m_Label,
                       wxString( wxS( "Recover" ) ) );
    BOOST_CHECK( !disabledRecovery->m_Enabled );
    BOOST_CHECK_EQUAL( disabledRecovery->m_ActionName,
                       wxString( wxS( "invoke" ) ) );
    BOOST_CHECK( disabledRecovery->m_RequiresUserConfirmation );

    view.m_CanExecuteProviderRecovery = true;
    tree = AiAgentPanelSemanticTree( view );

    const AI_SEMANTIC_UI_NODE* enabledRecovery =
            tree.FindNode( wxS( "agent.recovery.execute" ) );

    BOOST_REQUIRE( enabledRecovery );
    BOOST_CHECK( enabledRecovery->m_Enabled );
    BOOST_CHECK( enabledRecovery->m_RequiresUserConfirmation );
}


BOOST_AUTO_TEST_CASE( ProviderRecoveryEpisodeIsProjectedAsReviewText )
{
    AI_AGENT_PANEL_SEMANTIC_VIEW view;
    view.m_HasProviderRecovery = true;
    view.m_CanExecuteProviderRecovery = true;
    view.m_ProviderRecoveryEpisodeJson =
            wxS( "{\"schema\":{\"name\":\"kisurf.ai.provider_recovery_episode\"},"
                 "\"status\":\"ready_for_user_review\","
                 "\"automatic_execution_allowed\":false,"
                 "\"user_review_required\":true,"
                 "\"preflight_result\":{\"allowed\":true},"
                 "\"replay_request\":{\"status\":\"ready_for_user_review\"}}" );

    AI_SEMANTIC_UI_TREE tree = AiAgentPanelSemanticTree( view );
    const AI_SEMANTIC_UI_NODE* review =
            tree.FindNode( wxS( "agent.recovery.review" ) );

    BOOST_REQUIRE( review );
    BOOST_CHECK_EQUAL( review->m_ParentNodeId,
                       wxString( wxS( "agent.tabs.log" ) ) );
    BOOST_CHECK_EQUAL( review->m_Role, wxString( wxS( "text" ) ) );
    BOOST_CHECK_EQUAL( review->m_Label,
                       wxString( wxS( "Recovery review" ) ) );
    BOOST_CHECK( review->m_Enabled );
    BOOST_CHECK( review->m_ActionName.IsEmpty() );
    BOOST_CHECK( !review->m_RequiresUserConfirmation );
    BOOST_CHECK( review->m_TextPolicy == AI_SEMANTIC_UI_TEXT_POLICY::Plain );
    BOOST_CHECK( review->m_TextValue.Contains(
            wxS( "kisurf.ai.provider_recovery_episode" ) ) );
    BOOST_CHECK( review->m_TextValue.Contains(
            wxS( "\"status\":\"ready_for_user_review\"" ) ) );
    BOOST_CHECK( review->m_TextValue.Contains(
            wxS( "\"automatic_execution_allowed\":false" ) ) );
    BOOST_CHECK( review->m_TextValue.Contains( wxS( "replay_request" ) ) );
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


BOOST_AUTO_TEST_CASE( GridIoPanelStateProjectsTablesForNextAction )
{
    RECORDING_PANEL_GRID_IO grid(
            { { wxS( "Default" ), wxS( "0.20 mm" ) },
              { wxS( "Power" ), wxEmptyString },
              { wxS( "Signal" ), wxEmptyString } } );
    grid.m_RowLabels[0] = wxS( "Default" );
    grid.m_RowLabels[1] = wxS( "Power" );
    grid.m_RowLabels[2] = wxS( "Signal" );
    grid.m_ColumnLabels[0] = wxS( "Class" );
    grid.m_ColumnLabels[1] = wxS( "Clearance" );
    grid.m_SelectedCells = { { 0, 1 } };

    AI_PANEL_STATE_RECORD record = AiPanelStateRecordFromGridIo(
            wxS( "board_setup.clearance" ), wxS( "Board Setup" ),
            wxS( "clearance.rules" ), wxS( "Clearance rules" ), grid );

    BOOST_CHECK_EQUAL( record.m_Id, wxString( wxS( "board_setup.clearance" ) ) );
    BOOST_CHECK_EQUAL( record.m_Title, wxString( wxS( "Board Setup" ) ) );
    BOOST_CHECK_EQUAL( record.m_FocusedControlId,
                       wxString( wxS( "clearance.rules.r0.c1" ) ) );
    BOOST_CHECK_EQUAL( record.m_FocusedControlLabel,
                       wxString( wxS( "Clearance" ) ) );

    nlohmann::json state = nlohmann::json::parse( record.m_StateJson.ToStdString() );
    BOOST_REQUIRE( state.contains( "tables" ) );
    BOOST_REQUIRE_EQUAL( state["tables"].size(), 1 );

    const nlohmann::json& table = state["tables"][0];
    BOOST_CHECK_EQUAL( table["id"].get<std::string>(), "clearance.rules" );
    BOOST_CHECK_EQUAL( table["title"].get<std::string>(), "Clearance rules" );
    BOOST_CHECK_EQUAL( table["focused_cell"]["row_id"].get<std::string>(), "r0" );
    BOOST_CHECK_EQUAL( table["focused_cell"]["column_id"].get<std::string>(), "c1" );
    BOOST_CHECK_EQUAL( table["columns"][1]["label"].get<std::string>(), "Clearance" );
    BOOST_CHECK_EQUAL( table["rows"][0]["label"].get<std::string>(), "Default" );
    BOOST_CHECK_EQUAL( table["rows"][0]["cells"]["c1"].get<std::string>(), "0.20 mm" );
    BOOST_CHECK_EQUAL( table["rows"][1]["cells"]["c1"].get<std::string>(), "" );
    BOOST_CHECK_EQUAL( table["rows"][2]["cells"]["c1"].get<std::string>(), "" );
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
