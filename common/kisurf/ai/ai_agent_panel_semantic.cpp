#include <kisurf/ai/ai_agent_panel_semantic.h>

#include <nlohmann/json.hpp>

#include <string>
#include <utility>

namespace
{
void addNode( AI_SEMANTIC_UI_TREE& aTree, wxString aId, wxString aParent,
              wxString aRole, wxString aLabel, bool aEnabled = true,
              wxString aAction = wxEmptyString,
              AI_SEMANTIC_UI_TEXT_POLICY aTextPolicy = AI_SEMANTIC_UI_TEXT_POLICY::None,
              wxString aTextValue = wxEmptyString,
              bool aRequiresUserConfirmation = false )
{
    AI_SEMANTIC_UI_NODE node;
    node.m_NodeId = std::move( aId );
    node.m_ParentNodeId = std::move( aParent );
    node.m_Role = std::move( aRole );
    node.m_Label = std::move( aLabel );
    node.m_Enabled = aEnabled;
    node.m_ActionName = std::move( aAction );
    node.m_RequiresUserConfirmation = aRequiresUserConfirmation;
    node.m_TextPolicy = aTextPolicy;
    node.m_TextValue = RedactSemanticUiText( aTextValue );
    aTree.m_Nodes.push_back( std::move( node ) );
}


std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() )
                         : std::string();
}


std::string textPolicyName( AI_SEMANTIC_UI_TEXT_POLICY aPolicy )
{
    switch( aPolicy )
    {
    case AI_SEMANTIC_UI_TEXT_POLICY::Plain:
        return "plain";

    case AI_SEMANTIC_UI_TEXT_POLICY::Redacted:
        return "redacted";

    case AI_SEMANTIC_UI_TEXT_POLICY::None:
    default:
        return "none";
    }
}


nlohmann::json semanticNodeJson( const AI_SEMANTIC_UI_NODE& aNode )
{
    nlohmann::json node = {
        { "id", toUtf8String( aNode.m_NodeId ) },
        { "parent_id", toUtf8String( aNode.m_ParentNodeId ) },
        { "role", toUtf8String( aNode.m_Role ) },
        { "label", toUtf8String( aNode.m_Label ) },
        { "enabled", aNode.m_Enabled },
        { "visible", aNode.m_Visible },
        { "focused", aNode.m_Focused },
        { "action", toUtf8String( aNode.m_ActionName ) },
        { "tool_action_id", toUtf8String( aNode.m_ToolActionId ) },
        { "requires_user_confirmation", aNode.m_RequiresUserConfirmation },
        { "text_policy", textPolicyName( aNode.m_TextPolicy ) },
        { "bounds_available", aNode.m_Bounds.m_Available }
    };

    if( aNode.m_TextPolicy == AI_SEMANTIC_UI_TEXT_POLICY::Plain )
        node["text_value"] = toUtf8String( RedactSemanticUiText( aNode.m_TextValue ) );

    if( aNode.m_Bounds.m_Available )
    {
        node["bounds"] = { { "x", aNode.m_Bounds.m_X },
                           { "y", aNode.m_Bounds.m_Y },
                           { "width", aNode.m_Bounds.m_Width },
                           { "height", aNode.m_Bounds.m_Height } };
    }

    return node;
}


const AI_SEMANTIC_UI_NODE* fallbackFocusedNode( const AI_SEMANTIC_UI_TREE& aTree )
{
    for( const AI_SEMANTIC_UI_NODE& node : aTree.m_Nodes )
    {
        if( node.m_Focused )
            return &node;
    }

    if( const AI_SEMANTIC_UI_NODE* input = aTree.FindNode( wxS( "agent.input" ) ) )
        return input;

    return nullptr;
}


wxString nodeTextValue( const AI_SEMANTIC_UI_TREE& aTree, const wxString& aNodeId )
{
    const AI_SEMANTIC_UI_NODE* node = aTree.FindNode( aNodeId );

    if( !node )
        return wxEmptyString;

    return node->m_TextValue;
}


wxString agentPanelSummary( const AI_SEMANTIC_UI_TREE& aTree )
{
    const AI_SEMANTIC_UI_NODE* background =
            aTree.FindNode( wxS( "agent.background.toggle" ) );
    const AI_SEMANTIC_UI_NODE* send = aTree.FindNode( wxS( "agent.send" ) );
    const AI_SEMANTIC_UI_NODE* reject = aTree.FindNode( wxS( "agent.reject" ) );

    wxString summary;
    summary << wxS( "background=" )
            << ( background && background->m_Label.Contains( wxS( " on" ) )
                         ? wxS( "on" )
                         : wxS( "off" ) )
            << wxS( "; messages=" )
            << nodeTextValue( aTree, wxS( "agent.chat.transcript" ) )
            << wxS( "; logs=" )
            << nodeTextValue( aTree, wxS( "agent.log.entries" ) )
            << wxS( "; composer_status=" )
            << nodeTextValue( aTree, wxS( "agent.composer.status" ) )
            << wxS( "; send=" )
            << ( send && send->m_Enabled ? wxS( "enabled" )
                                          : wxS( "disabled" ) )
            << wxS( "; active_suggestion=" )
            << ( reject && reject->m_Enabled ? wxS( "yes" ) : wxS( "no" ) );

    return summary;
}
} // namespace


AI_SEMANTIC_UI_TREE AiAgentPanelSemanticTree(
        const AI_AGENT_PANEL_SEMANTIC_VIEW& aView )
{
    AI_SEMANTIC_UI_TREE tree;
    tree.m_FrameId = wxS( "agent" );
    tree.m_Title = wxS( "Agent" );
    tree.m_ScreenshotAvailable = false;
    tree.m_ScreenshotUnavailableReason =
            wxS( "Agent pane semantic self-test exposes controls; canvas pixels come from visual frame tools." );

    addNode( tree, wxS( "agent.root" ), wxEmptyString, wxS( "panel" ),
             wxS( "Agent" ) );
    addNode( tree, wxS( "agent.background.toggle" ), wxS( "agent.root" ),
             wxS( "checkbox" ),
             aView.m_BackgroundAgentEnabled ? wxS( "Background Agent on" )
                                            : wxS( "Background Agent off" ),
             true, wxS( "toggle" ) );
    addNode( tree, wxS( "agent.model.settings" ), wxS( "agent.root" ),
             wxS( "button" ), wxS( "Model Settings" ), true, wxS( "invoke" ) );

    addNode( tree, wxS( "agent.tabs.chat" ), wxS( "agent.root" ), wxS( "tab" ),
             wxS( "Chat" ), true, wxS( "select" ) );
    addNode( tree, wxS( "agent.tabs.log" ), wxS( "agent.root" ), wxS( "tab" ),
             wxS( "Log" ), true, wxS( "select" ) );

    addNode( tree, wxS( "agent.chat.transcript" ), wxS( "agent.tabs.chat" ),
             wxS( "text" ), wxS( "Transcript" ), true, wxEmptyString,
             AI_SEMANTIC_UI_TEXT_POLICY::Plain,
             wxString::Format( wxS( "%zu messages" ), aView.m_MessageCount ) );
    const wxString logText = aView.m_LogSummary.IsEmpty()
                                     ? wxString::Format( wxS( "%zu entries" ),
                                                         aView.m_LogEntryCount )
                                     : aView.m_LogSummary;

    addNode( tree, wxS( "agent.log.entries" ), wxS( "agent.tabs.log" ),
             wxS( "text" ), wxS( "Log entries" ), true, wxEmptyString,
             AI_SEMANTIC_UI_TEXT_POLICY::Plain, logText );

    addNode( tree, wxS( "agent.input" ), wxS( "agent.root" ), wxS( "textbox" ),
             wxS( "Input" ), true, wxS( "set_text" ),
             AI_SEMANTIC_UI_TEXT_POLICY::Redacted );
    addNode( tree, wxS( "agent.composer.status" ), wxS( "agent.root" ),
             wxS( "text" ), wxS( "Composer status" ), true, wxEmptyString,
             AI_SEMANTIC_UI_TEXT_POLICY::Plain, aView.m_ComposerStatusText );
    addNode( tree, wxS( "agent.send" ), wxS( "agent.root" ), wxS( "button" ),
             wxS( "Send" ), aView.m_InputHasText, wxS( "invoke" ) );
    addNode( tree, wxS( "agent.stop" ), wxS( "agent.root" ), wxS( "button" ),
             wxS( "Stop" ), true, wxS( "invoke" ) );
    addNode( tree, wxS( "agent.preview.invoke" ), wxS( "agent.tabs.chat" ),
             wxS( "button" ), wxS( "Preview" ),
             aView.m_HasActiveSuggestion && aView.m_CanPreviewSuggestion,
             wxS( "invoke" ) );
    addNode( tree, wxS( "agent.accept" ), wxS( "agent.tabs.chat" ),
             wxS( "button" ), wxS( "Accept" ),
             aView.m_HasActiveSuggestion && aView.m_CanAcceptSuggestion,
             wxS( "invoke" ), AI_SEMANTIC_UI_TEXT_POLICY::None, wxEmptyString, true );
    addNode( tree, wxS( "agent.reject" ), wxS( "agent.tabs.chat" ),
             wxS( "button" ), wxS( "Reject" ), aView.m_HasActiveSuggestion,
             wxS( "invoke" ) );

    return tree;
}


AI_PANEL_STATE_RECORD AiAgentPanelSemanticStateRecord(
        const AI_AGENT_PANEL_SEMANTIC_VIEW& aView )
{
    return AiAgentPanelSemanticStateRecord( AiAgentPanelSemanticTree( aView ) );
}


AI_PANEL_STATE_RECORD AiAgentPanelSemanticStateRecord(
        const AI_SEMANTIC_UI_TREE& aTree )
{
    AI_PANEL_STATE_RECORD record;
    record.m_Id = wxS( "agent.panel" );
    record.m_Title = aTree.m_Title.IsEmpty() ? wxString( wxS( "Agent" ) )
                                             : aTree.m_Title;

    if( const AI_SEMANTIC_UI_NODE* focused = fallbackFocusedNode( aTree ) )
    {
        record.m_FocusedControlId = focused->m_NodeId;
        record.m_FocusedControlLabel = focused->m_Label;
    }

    record.m_Summary = agentPanelSummary( aTree );

    nlohmann::json nodes = nlohmann::json::array();

    for( const AI_SEMANTIC_UI_NODE& node : aTree.m_Nodes )
        nodes.push_back( semanticNodeJson( node ) );

    nlohmann::json state = {
        { "frame_id", toUtf8String( aTree.m_FrameId ) },
        { "title", toUtf8String( aTree.m_Title ) },
        { "screenshot_available", aTree.m_ScreenshotAvailable },
        { "screenshot_unavailable_reason",
          toUtf8String( aTree.m_ScreenshotUnavailableReason ) },
        { "nodes", std::move( nodes ) }
    };

    record.m_StateJson = wxString::FromUTF8( state.dump().c_str() );
    return record;
}


void AiUpsertPanelStateRecord( AI_CONTEXT_SNAPSHOT& aSnapshot,
                               AI_PANEL_STATE_RECORD aRecord )
{
    if( !aRecord.HasState() )
        return;

    if( !aRecord.m_Id.IsEmpty() )
    {
        for( AI_PANEL_STATE_RECORD& existing : aSnapshot.m_PanelStates )
        {
            if( existing.m_Id == aRecord.m_Id )
            {
                existing = std::move( aRecord );
                return;
            }
        }
    }

    aSnapshot.m_PanelStates.push_back( std::move( aRecord ) );
}
