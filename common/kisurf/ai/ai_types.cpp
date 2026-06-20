/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 */

#include <kisurf/ai/ai_types.h>

#include <algorithm>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace
{
std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


wxString editorKindName( AI_EDITOR_KIND aKind )
{
    switch( aKind )
    {
    case AI_EDITOR_KIND::Pcb:
        return wxS( "PCB" );

    case AI_EDITOR_KIND::Schematic:
        return wxS( "Schematic" );

    case AI_EDITOR_KIND::Unknown:
    default:
        return wxS( "Unknown" );
    }
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


std::string agentWorkspaceContextJsonName( AI_AGENT_WORKSPACE_CONTEXT_KIND aMode )
{
    switch( aMode )
    {
    case AI_AGENT_WORKSPACE_CONTEXT_KIND::Routing:
        return "routing";

    case AI_AGENT_WORKSPACE_CONTEXT_KIND::ViaPlacement:
        return "via_placement";

    case AI_AGENT_WORKSPACE_CONTEXT_KIND::FootprintPlacement:
        return "footprint_placement";

    case AI_AGENT_WORKSPACE_CONTEXT_KIND::ZoneCreation:
        return "zone_creation";

    case AI_AGENT_WORKSPACE_CONTEXT_KIND::SelectionEdit:
        return "selection_edit";

    case AI_AGENT_WORKSPACE_CONTEXT_KIND::General:
    default:
        return "general";
    }
}


std::string dynamicContextKindForToolState( AI_TOOL_STATE_KIND aKind )
{
    switch( aKind )
    {
    case AI_TOOL_STATE_KIND::RoutingTrack:
        return "routing";

    case AI_TOOL_STATE_KIND::PlacingVia:
    case AI_TOOL_STATE_KIND::PlacingFootprint:
    case AI_TOOL_STATE_KIND::DrawingZone:
    case AI_TOOL_STATE_KIND::MovingSelection:
        return "layout";

    case AI_TOOL_STATE_KIND::Selecting:
        return "general";

    case AI_TOOL_STATE_KIND::Idle:
        return "idle";

    case AI_TOOL_STATE_KIND::Unknown:
    default:
        return "unknown";
    }
}


bool panelHasFocusSignal( const AI_PANEL_STATE_RECORD& aPanel )
{
    return !aPanel.m_FocusedControlId.IsEmpty()
           || !aPanel.m_FocusedControlLabel.IsEmpty()
           || !aPanel.m_SelectedText.IsEmpty();
}


const AI_PANEL_STATE_RECORD* focusedPanelState(
        const std::vector<AI_PANEL_STATE_RECORD>& aPanelStates )
{
    for( const AI_PANEL_STATE_RECORD& panel : aPanelStates )
    {
        if( panelHasFocusSignal( panel ) )
            return &panel;
    }

    return nullptr;
}


std::string dynamicContextKind( const AI_CONTEXT_SNAPSHOT& aSnapshot,
                                const AI_PANEL_STATE_RECORD* aFocusedPanel )
{
    std::string kind = dynamicContextKindForToolState( aSnapshot.m_ToolState.m_Kind );

    if( kind == "unknown" && aFocusedPanel )
        return "panel";

    return kind;
}


std::string dynamicContextSource( const AI_CONTEXT_SNAPSHOT& aSnapshot,
                                  const AI_PANEL_STATE_RECORD* aFocusedPanel )
{
    if( aSnapshot.m_ToolState.m_Kind != AI_TOOL_STATE_KIND::Unknown )
        return "tool_state";

    if( aFocusedPanel )
        return "panel_state";

    if( aSnapshot.m_ToolState.HasToolState() )
        return "tool_state";

    return "default";
}


wxString wxStringFromUtf8String( const std::string& aText )
{
    return wxString::FromUTF8( aText.c_str() );
}


std::string contextAnchorKindJsonName( AI_CONTEXT_ANCHOR_KIND aKind )
{
    switch( aKind )
    {
    case AI_CONTEXT_ANCHOR_KIND::RouteStart:
        return "route_start";

    case AI_CONTEXT_ANCHOR_KIND::RouteTarget:
        return "route_target";

    case AI_CONTEXT_ANCHOR_KIND::RouteCandidate:
        return "route_candidate";

    case AI_CONTEXT_ANCHOR_KIND::OrthogonalBreakout:
        return "orthogonal_breakout";

    case AI_CONTEXT_ANCHOR_KIND::FortyFiveIntersection:
        return "forty_five_intersection";

    case AI_CONTEXT_ANCHOR_KIND::PlacementCandidate:
        return "placement_candidate";

    case AI_CONTEXT_ANCHOR_KIND::PatternContinuation:
        return "pattern_continuation";

    case AI_CONTEXT_ANCHOR_KIND::ShapeCorner:
        return "shape_corner";

    case AI_CONTEXT_ANCHOR_KIND::ZoneVertex:
        return "zone_vertex";

    case AI_CONTEXT_ANCHOR_KIND::PanelCell:
        return "panel_cell";

    case AI_CONTEXT_ANCHOR_KIND::General:
        return "general";

    case AI_CONTEXT_ANCHOR_KIND::Unknown:
    default:
        return "unknown";
    }
}


wxString activityKindName( AI_ACTIVITY_KIND aKind )
{
    switch( aKind )
    {
    case AI_ACTIVITY_KIND::UserAction:
        return wxS( "user-action" );

    case AI_ACTIVITY_KIND::ModelToolRequest:
        return wxS( "model-tool-request" );

    case AI_ACTIVITY_KIND::PolicyDecision:
        return wxS( "policy-decision" );

    case AI_ACTIVITY_KIND::ToolResult:
        return wxS( "tool-result" );

    default:
        return wxS( "activity" );
    }
}


std::string activityKindJsonName( AI_ACTIVITY_KIND aKind )
{
    switch( aKind )
    {
    case AI_ACTIVITY_KIND::UserAction:
        return "user-action";

    case AI_ACTIVITY_KIND::ModelToolRequest:
        return "model-tool-request";

    case AI_ACTIVITY_KIND::PolicyDecision:
        return "policy-decision";

    case AI_ACTIVITY_KIND::ToolResult:
        return "tool-result";

    default:
        return "activity";
    }
}


nlohmann::json parseContextJsonOrRawText( const wxString& aText )
{
    if( aText.IsEmpty() )
        return nlohmann::json::object();

    const std::string text = toUtf8String( aText );
    nlohmann::json parsed = nlohmann::json::parse( text, nullptr, false );

    if( !parsed.is_discarded() )
        return parsed;

    return { { "raw", text } };
}


void appendObjectList( wxString& aText, const wxString& aTitle,
                       const std::vector<AI_OBJECT_REF>& aObjects, size_t aMaxObjects )
{
    aText << aTitle << wxS( ": " ) << aObjects.size() << wxS( "\n" );

    const size_t count = std::min( aObjects.size(), aMaxObjects );

    for( size_t i = 0; i < count; ++i )
    {
        const AI_OBJECT_REF& ref = aObjects[i];
        aText << wxS( "- " ) << ref.m_Label << wxS( " type=" )
              << static_cast<int>( ref.m_Type ) << wxS( " uuid=" )
              << ref.m_Uuid.AsString();

        if( !ref.m_DetailsJson.IsEmpty() )
            aText << wxS( " details=" ) << ref.m_DetailsJson;

        aText << wxS( "\n" );
    }

    if( aObjects.size() > count )
        aText << wxS( "- ... " ) << ( aObjects.size() - count ) << wxS( " more\n" );
}


nlohmann::json objectRefsJson( const std::vector<AI_OBJECT_REF>& aObjects, size_t aMaxObjects )
{
    nlohmann::json objects = nlohmann::json::array();
    const size_t   count = std::min( aObjects.size(), aMaxObjects );

    for( size_t i = 0; i < count; ++i )
    {
        const AI_OBJECT_REF& ref = aObjects[i];

        nlohmann::json object = { { "label", toUtf8String( ref.m_Label ) },
                                  { "type", static_cast<int>( ref.m_Type ) },
                                  { "uuid", toUtf8String( ref.m_Uuid.AsString() ) } };

        if( !ref.m_DetailsJson.IsEmpty() )
        {
            std::string detailsText = toUtf8String( ref.m_DetailsJson );
            nlohmann::json details =
                    nlohmann::json::parse( detailsText, nullptr, false );

            if( !details.is_discarded() && details.is_object() )
                object["details"] = std::move( details );
            else
                object["details_json"] = detailsText;
        }

        objects.push_back( std::move( object ) );
    }

    return objects;
}


nlohmann::json actionsJson( const std::vector<AI_ACTION_DESCRIPTOR>& aActions,
                            size_t aMaxActions )
{
    nlohmann::json actions = nlohmann::json::array();
    const size_t   count = std::min( aActions.size(), aMaxActions );

    for( size_t i = 0; i < count; ++i )
    {
        const AI_ACTION_DESCRIPTOR& action = aActions[i];

        actions.push_back( { { "name", toUtf8String( action.m_Name ) },
                             { "friendly_name", toUtf8String( action.m_FriendlyName ) },
                             { "description", toUtf8String( action.m_Description ) },
                             { "editor", editorKindJsonName( action.m_EditorKind ) },
                             { "safety", toUtf8String( action.SafetyAsString() ) },
                             { "enabled", action.m_Enabled } } );
    }

    return actions;
}


nlohmann::json activityJson( const std::vector<AI_ACTIVITY_RECORD>& aActivity,
                             size_t aMaxActivity )
{
    nlohmann::json activity = nlohmann::json::array();
    const size_t   count = std::min( aActivity.size(), aMaxActivity );

    for( size_t i = 0; i < count; ++i )
    {
        const AI_ACTIVITY_RECORD& record = aActivity[i];

        activity.push_back(
                { { "sequence", record.m_Sequence },
                  { "request_id", record.m_RequestId },
                  { "tool_call_id", toUtf8String( record.m_ToolCallId ) },
                  { "kind", activityKindJsonName( record.m_Kind ) },
                  { "editor", editorKindJsonName( record.m_EditorKind ) },
                  { "action", toUtf8String( record.m_ActionName ) },
                  { "arguments_json", toUtf8String( record.m_ArgumentsJson ) },
                  { "result_json", toUtf8String( record.m_ResultJson ) },
                  { "error_code", toUtf8String( record.m_ErrorCode ) },
                  { "allowed", record.m_Allowed },
                  { "executed", record.m_Executed },
                  { "message", toUtf8String( record.m_Message ) } } );
    }

    return activity;
}


nlohmann::json anchorsJson( const std::vector<AI_CONTEXT_ANCHOR>& aAnchors,
                            size_t aMaxAnchors )
{
    nlohmann::json anchors = nlohmann::json::array();
    const size_t   count = std::min( aAnchors.size(), aMaxAnchors );

    for( size_t i = 0; i < count; ++i )
    {
        const AI_CONTEXT_ANCHOR& anchor = aAnchors[i];
        nlohmann::json record = {
            { "id", toUtf8String( anchor.m_Id ) },
            { "kind", contextAnchorKindJsonName( anchor.m_Kind ) },
            { "editor", editorKindJsonName( anchor.m_EditorKind ) },
            { "label", toUtf8String( anchor.m_Label ) },
            { "summary", toUtf8String( anchor.m_Summary ) },
            { "has_position", anchor.m_HasPosition },
            { "position", { { "x", anchor.m_Position.x }, { "y", anchor.m_Position.y } } },
            { "layer", anchor.m_Layer },
            { "confidence", anchor.m_Confidence }
        };

        nlohmann::json details = parseContextJsonOrRawText( anchor.m_DetailsJson );

        if( details.contains( "raw" ) )
            record["details_raw"] = details["raw"];
        else
            record["details"] = std::move( details );

        anchors.push_back( std::move( record ) );
    }

    return anchors;
}


nlohmann::json panelStatesJson( const std::vector<AI_PANEL_STATE_RECORD>& aPanelStates,
                                size_t aMaxPanelStates )
{
    nlohmann::json panels = nlohmann::json::array();
    const size_t   count = std::min( aPanelStates.size(), aMaxPanelStates );

    for( size_t i = 0; i < count; ++i )
    {
        const AI_PANEL_STATE_RECORD& panel = aPanelStates[i];
        nlohmann::json record = {
            { "id", toUtf8String( panel.m_Id ) },
            { "title", toUtf8String( panel.m_Title ) },
            { "focused_control_id", toUtf8String( panel.m_FocusedControlId ) },
            { "focused_control_label", toUtf8String( panel.m_FocusedControlLabel ) },
            { "selected_text", toUtf8String( panel.m_SelectedText ) },
            { "summary", toUtf8String( panel.m_Summary ) }
        };

        nlohmann::json state = parseContextJsonOrRawText( panel.m_StateJson );

        if( state.contains( "raw" ) )
            record["state_raw"] = state["raw"];
        else
            record["state"] = std::move( state );

        panels.push_back( std::move( record ) );
    }

    return panels;
}


nlohmann::json toolStateJson( const AI_TOOL_STATE_SNAPSHOT& aToolState )
{
    nlohmann::json toolState;

    toolState["editor"] = editorKindJsonName( aToolState.m_EditorKind );
    toolState["kind"] = toolStateKindJsonName( aToolState.m_Kind );
    toolState["version"] = { { "document", aToolState.m_ContextVersion.m_DocumentRevision },
                             { "selection", aToolState.m_ContextVersion.m_SelectionRevision },
                             { "view", aToolState.m_ContextVersion.m_ViewRevision },
                             { "text", toUtf8String( aToolState.m_ContextVersion.AsString() ) } };
    toolState["active_action"] = toUtf8String( aToolState.m_ActiveActionName );
    toolState["cursor"] = { { "has_position", aToolState.m_HasCursorBoardPosition },
                            { "x", aToolState.m_CursorBoardPosition.x },
                            { "y", aToolState.m_CursorBoardPosition.y } };
    toolState["shared_context"] = parseContextJsonOrRawText( aToolState.m_SharedContextJson );
    toolState["mode_context"] = parseContextJsonOrRawText( aToolState.m_ModeContextJson );

    return toolState;
}


nlohmann::json dynamicContextJson( const AI_CONTEXT_SNAPSHOT& aSnapshot )
{
    const AI_PANEL_STATE_RECORD* panel = focusedPanelState( aSnapshot.m_PanelStates );

    nlohmann::json context =
            { { "kind", dynamicContextKind( aSnapshot, panel ) },
              { "source", dynamicContextSource( aSnapshot, panel ) },
              { "tool_state_kind",
                toolStateKindJsonName( aSnapshot.m_ToolState.m_Kind ) } };

    if( !aSnapshot.m_ToolState.m_ActiveActionName.IsEmpty() )
        context["active_action"] = toUtf8String( aSnapshot.m_ToolState.m_ActiveActionName );

    if( context["kind"].get_ref<const std::string&>() == "panel" && panel )
    {
        context["focused_panel_id"] = toUtf8String( panel->m_Id );
        context["focused_panel_title"] = toUtf8String( panel->m_Title );
        context["focused_control_id"] = toUtf8String( panel->m_FocusedControlId );
        context["focused_control_label"] =
                toUtf8String( panel->m_FocusedControlLabel );
    }

    return context;
}
} // namespace

wxString AiToolStateKindName( AI_TOOL_STATE_KIND aKind )
{
    return wxStringFromUtf8String( toolStateKindJsonName( aKind ) );
}


wxString AiDynamicContextKind( const AI_CONTEXT_SNAPSHOT& aSnapshot )
{
    const AI_PANEL_STATE_RECORD* panel = focusedPanelState( aSnapshot.m_PanelStates );
    return wxStringFromUtf8String( dynamicContextKind( aSnapshot, panel ) );
}


wxString AiDynamicContextDetailsJson( const AI_CONTEXT_SNAPSHOT& aSnapshot,
                                      const wxString& aReason )
{
    nlohmann::json details = dynamicContextJson( aSnapshot );

    if( !aReason.IsEmpty() )
        details["reason"] = toUtf8String( aReason );

    return wxStringFromUtf8String( details.dump() );
}


bool AI_CONTEXT_VERSION::IsValid() const
{
    return m_DocumentRevision > 0 || m_SelectionRevision > 0 || m_ViewRevision > 0;
}


wxString AI_CONTEXT_VERSION::AsString() const
{
    return wxString::Format( wxS( "doc=%llu;sel=%llu;view=%llu" ),
                             static_cast<unsigned long long>( m_DocumentRevision ),
                             static_cast<unsigned long long>( m_SelectionRevision ),
                             static_cast<unsigned long long>( m_ViewRevision ) );
}


AI_OBJECT_REF::AI_OBJECT_REF() :
        m_Uuid( NilUuid() )
{
}


AI_OBJECT_REF::AI_OBJECT_REF( const KIID& aUuid, KICAD_T aType, const wxString& aLabel ) :
        AI_OBJECT_REF( aUuid, aType, aLabel, wxString() )
{
}


AI_OBJECT_REF::AI_OBJECT_REF( const KIID& aUuid, KICAD_T aType, const wxString& aLabel,
                              const wxString& aDetailsJson ) :
        m_Uuid( aUuid ),
        m_Type( aType ),
        m_Label( aLabel ),
        m_DetailsJson( aDetailsJson )
{
}


bool AI_OBJECT_REF::IsValid() const
{
    return m_Uuid != NilUuid() && m_Type != TYPE_NOT_INIT && m_Type != NOT_USED;
}


AI_VALIDATION_SEVERITY AI_VALIDATION_SUMMARY::WorstSeverity() const
{
    AI_VALIDATION_SEVERITY worst = AI_VALIDATION_SEVERITY::None;

    for( const AI_VALIDATION_ISSUE& issue : m_Issues )
    {
        if( static_cast<int>( issue.m_Severity ) > static_cast<int>( worst ) )
            worst = issue.m_Severity;
    }

    return worst;
}


bool AI_VALIDATION_SUMMARY::HasBlockingIssue() const
{
    for( const AI_VALIDATION_ISSUE& issue : m_Issues )
    {
        if( issue.m_IsNew && issue.m_Severity == AI_VALIDATION_SEVERITY::Error )
            return true;
    }

    return false;
}


bool AI_ACTION_DESCRIPTOR::IsValid() const
{
    return !m_Name.IsEmpty();
}


wxString AI_ACTION_DESCRIPTOR::SafetyAsString() const
{
    switch( m_Safety )
    {
    case AI_ACTION_SAFETY::ReadOnly:
        return wxS( "readonly" );

    case AI_ACTION_SAFETY::Interactive:
        return wxS( "interactive" );

    case AI_ACTION_SAFETY::Modifying:
        return wxS( "modifying" );

    case AI_ACTION_SAFETY::Destructive:
        return wxS( "destructive" );

    default:
        return wxS( "interactive" );
    }
}


bool AI_VISUAL_SNAPSHOT::HasPixels() const
{
    return !m_DataUri.IsEmpty();
}


bool AI_TOOL_STATE_SNAPSHOT::HasToolState() const
{
    return m_EditorKind != AI_EDITOR_KIND::Unknown
           || m_Kind != AI_TOOL_STATE_KIND::Unknown
           || m_ContextVersion.IsValid()
           || !m_ActiveActionName.IsEmpty()
           || m_HasCursorBoardPosition
           || !m_SharedContextJson.IsEmpty()
           || !m_ModeContextJson.IsEmpty();
}


wxString AI_TOOL_STATE_SNAPSHOT::KindAsString() const
{
    return wxString::FromUTF8( toolStateKindJsonName( m_Kind ).c_str() );
}


wxString AI_TOOL_STATE_SNAPSHOT::AsPromptText() const
{
    wxString text;

    text << wxS( "editor: " ) << editorKindName( m_EditorKind ) << wxS( "\n" );
    text << wxS( "tool state: " ) << KindAsString() << wxS( "\n" );
    text << wxS( "version: " ) << m_ContextVersion.AsString() << wxS( "\n" );

    if( !m_ActiveActionName.IsEmpty() )
        text << wxS( "active action: " ) << m_ActiveActionName << wxS( "\n" );

    if( m_HasCursorBoardPosition )
    {
        text << wxS( "cursor: x=" ) << m_CursorBoardPosition.x
             << wxS( " y=" ) << m_CursorBoardPosition.y << wxS( "\n" );
    }

    if( !m_SharedContextJson.IsEmpty() )
        text << wxS( "shared context: " ) << m_SharedContextJson << wxS( "\n" );

    if( !m_ModeContextJson.IsEmpty() )
        text << wxS( "mode context: " ) << m_ModeContextJson << wxS( "\n" );

    return text;
}


wxString AI_TOOL_STATE_SNAPSHOT::AsJsonText() const
{
    nlohmann::json root = { { "kisurf_tool_state", toolStateJson( *this ) } };
    return wxString::FromUTF8( root.dump().c_str() );
}


bool AI_AGENT_WORKSPACE_CONTEXT_STATE::HasState() const
{
    return m_ContextKind != AI_AGENT_WORKSPACE_CONTEXT_KIND::General
           || !m_Title.IsEmpty()
           || !m_StateJson.IsEmpty()
           || m_LastActivitySequence > 0;
}


wxString AI_AGENT_WORKSPACE_CONTEXT_STATE::ContextAsString() const
{
    return wxString::FromUTF8( agentWorkspaceContextJsonName( m_ContextKind ).c_str() );
}


bool AI_CONTEXT_ANCHOR::IsValid() const
{
    return !m_Id.IsEmpty()
           && ( m_Kind != AI_CONTEXT_ANCHOR_KIND::Unknown
                || !m_Label.IsEmpty()
                || !m_Summary.IsEmpty()
                || m_HasPosition
                || !m_DetailsJson.IsEmpty() );
}


wxString AI_CONTEXT_ANCHOR::KindAsString() const
{
    return wxString::FromUTF8( contextAnchorKindJsonName( m_Kind ).c_str() );
}


bool AI_PANEL_STATE_RECORD::HasState() const
{
    return !m_Id.IsEmpty()
           || !m_Title.IsEmpty()
           || !m_FocusedControlId.IsEmpty()
           || !m_FocusedControlLabel.IsEmpty()
           || !m_SelectedText.IsEmpty()
           || !m_Summary.IsEmpty()
           || !m_StateJson.IsEmpty();
}


bool AI_CONTEXT_SNAPSHOT::HasContext() const
{
    return m_Version.IsValid() || !m_VisibleObjects.empty() || !m_SelectedObjects.empty()
           || !m_Actions.empty() || !m_RecentActivity.empty() || !m_Summary.IsEmpty()
           || m_ToolState.HasToolState() || !m_Visual.m_Source.IsEmpty()
           || !m_Visual.m_UnavailableReason.IsEmpty() || !m_Anchors.empty()
           || !m_PanelStates.empty();
}


wxString AI_CONTEXT_SNAPSHOT::AsPromptText( size_t aMaxObjects, size_t aMaxActions,
                                            size_t aMaxAnchors,
                                            size_t aMaxPanelStates ) const
{
    wxString text;

    text << wxS( "editor: " ) << editorKindName( m_EditorKind ) << wxS( "\n" );
    text << wxS( "version: " ) << m_Version.AsString() << wxS( "\n" );

    if( !m_Summary.IsEmpty() )
        text << wxS( "summary: " ) << m_Summary << wxS( "\n" );

    nlohmann::json dynamicContext = dynamicContextJson( *this );
    const std::string dynamicKind =
            dynamicContext["kind"].get_ref<const std::string&>();
    const std::string dynamicSource =
            dynamicContext["source"].get_ref<const std::string&>();
    const std::string toolStateKind =
            dynamicContext["tool_state_kind"].get_ref<const std::string&>();

    text << wxS( "dynamic context: " ) << wxStringFromUtf8String( dynamicKind )
         << wxS( " source=" ) << wxStringFromUtf8String( dynamicSource )
         << wxS( " tool_state=" ) << wxStringFromUtf8String( toolStateKind )
         << wxS( "\n" );

    if( m_ToolState.HasToolState() )
        text << wxS( "current tool state:\n" ) << m_ToolState.AsPromptText();

    appendObjectList( text, wxS( "visible objects" ), m_VisibleObjects, aMaxObjects );
    appendObjectList( text, wxS( "selected objects" ), m_SelectedObjects, aMaxObjects );

    text << wxS( "available actions: " ) << m_Actions.size() << wxS( "\n" );

    const size_t actionCount = std::min( m_Actions.size(), aMaxActions );

    for( size_t i = 0; i < actionCount; ++i )
    {
        const AI_ACTION_DESCRIPTOR& action = m_Actions[i];

        text << wxS( "- " ) << action.m_Name;

        if( !action.m_FriendlyName.IsEmpty() )
            text << wxS( " | " ) << action.m_FriendlyName;

        text << wxS( " | safety=" ) << action.SafetyAsString()
             << wxS( " | enabled=" ) << ( action.m_Enabled ? wxS( "yes" ) : wxS( "no" ) )
             << wxS( "\n" );
    }

    if( m_Actions.size() > actionCount )
        text << wxS( "- ... " ) << ( m_Actions.size() - actionCount ) << wxS( " more\n" );

    if( !m_RecentActivity.empty() )
    {
        text << wxS( "recent activity: " ) << m_RecentActivity.size() << wxS( "\n" );

        const size_t activityCount = std::min( m_RecentActivity.size(), aMaxActions );

        for( size_t i = 0; i < activityCount; ++i )
        {
            const AI_ACTIVITY_RECORD& activity = m_RecentActivity[i];

            text << wxS( "- #" ) << activity.m_Sequence << wxS( " " )
                 << activityKindName( activity.m_Kind ) << wxS( " " )
                 << activity.m_ActionName << wxS( " allowed=" )
                 << ( activity.m_Allowed ? wxS( "yes" ) : wxS( "no" ) )
                 << wxS( " executed=" )
                 << ( activity.m_Executed ? wxS( "yes" ) : wxS( "no" ) );

            if( !activity.m_ErrorCode.IsEmpty() )
                text << wxS( " error=" ) << activity.m_ErrorCode;

            if( !activity.m_Message.IsEmpty() )
                text << wxS( " message=" ) << activity.m_Message;

            text << wxS( "\n" );
        }

        if( m_RecentActivity.size() > activityCount )
        {
            text << wxS( "- ... " ) << ( m_RecentActivity.size() - activityCount )
                 << wxS( " more\n" );
        }
    }

    if( !m_Anchors.empty() )
    {
        text << wxS( "semantic anchors: " ) << m_Anchors.size() << wxS( "\n" );
        const size_t anchorCount = std::min( m_Anchors.size(), aMaxAnchors );

        for( size_t i = 0; i < anchorCount; ++i )
        {
            const AI_CONTEXT_ANCHOR& anchor = m_Anchors[i];

            text << wxS( "- " ) << anchor.m_Id << wxS( " | " )
                 << anchor.KindAsString();

            if( !anchor.m_Label.IsEmpty() )
                text << wxS( " | " ) << anchor.m_Label;

            if( anchor.m_HasPosition )
            {
                text << wxS( " | x=" ) << anchor.m_Position.x
                     << wxS( " y=" ) << anchor.m_Position.y;
            }

            if( anchor.m_Layer >= 0 )
                text << wxS( " | layer=" ) << anchor.m_Layer;

            if( !anchor.m_Summary.IsEmpty() )
                text << wxS( " | " ) << anchor.m_Summary;

            text << wxS( "\n" );
        }

        if( m_Anchors.size() > anchorCount )
            text << wxS( "- ... " ) << ( m_Anchors.size() - anchorCount )
                 << wxS( " more\n" );
    }

    if( !m_PanelStates.empty() )
    {
        text << wxS( "panel states: " ) << m_PanelStates.size() << wxS( "\n" );
        const size_t panelCount = std::min( m_PanelStates.size(), aMaxPanelStates );

        for( size_t i = 0; i < panelCount; ++i )
        {
            const AI_PANEL_STATE_RECORD& panel = m_PanelStates[i];

            text << wxS( "- " ) << panel.m_Id;

            if( !panel.m_Title.IsEmpty() )
                text << wxS( " | " ) << panel.m_Title;

            if( !panel.m_FocusedControlLabel.IsEmpty() )
                text << wxS( " | focused=" ) << panel.m_FocusedControlLabel;

            if( !panel.m_SelectedText.IsEmpty() )
                text << wxS( " | selected=" ) << panel.m_SelectedText;

            if( !panel.m_Summary.IsEmpty() )
                text << wxS( " | " ) << panel.m_Summary;

            text << wxS( "\n" );
        }

        if( m_PanelStates.size() > panelCount )
            text << wxS( "- ... " ) << ( m_PanelStates.size() - panelCount )
                 << wxS( " more\n" );
    }

    if( !m_Visual.m_Source.IsEmpty() || !m_Visual.m_MimeType.IsEmpty()
        || !m_Visual.m_UnavailableReason.IsEmpty() )
    {
        text << wxS( "visual: " ) << m_Visual.m_Source << wxS( " " )
             << m_Visual.m_MimeType << wxS( " pixels=" )
             << ( m_Visual.HasPixels() ? wxS( "yes" ) : wxS( "no" ) );

        if( !m_Visual.m_UnavailableReason.IsEmpty() )
            text << wxS( " unavailable_reason=" ) << m_Visual.m_UnavailableReason;

        text << wxS( "\n" );
    }

    return text;
}


wxString AI_CONTEXT_SNAPSHOT::AsJsonText( size_t aMaxObjects, size_t aMaxActions,
                                          size_t aMaxActivity, size_t aMaxAnchors,
                                          size_t aMaxPanelStates ) const
{
    nlohmann::json context;
    context["editor"] = editorKindJsonName( m_EditorKind );
    context["version"] = { { "document", m_Version.m_DocumentRevision },
                           { "selection", m_Version.m_SelectionRevision },
                           { "view", m_Version.m_ViewRevision },
                           { "text", toUtf8String( m_Version.AsString() ) } };
    context["summary"] = toUtf8String( m_Summary );
    context["dynamic_context"] = dynamicContextJson( *this );
    context["visible_object_count"] = m_VisibleObjects.size();
    context["visible_objects"] = objectRefsJson( m_VisibleObjects, aMaxObjects );
    context["selected_object_count"] = m_SelectedObjects.size();
    context["selected_objects"] = objectRefsJson( m_SelectedObjects, aMaxObjects );
    context["action_count"] = m_Actions.size();
    context["actions"] = actionsJson( m_Actions, aMaxActions );
    context["recent_activity_count"] = m_RecentActivity.size();
    context["recent_activity"] = activityJson( m_RecentActivity, aMaxActivity );
    context["tool_state"] = m_ToolState.HasToolState() ? toolStateJson( m_ToolState )
                                                       : nlohmann::json::object();
    context["anchor_count"] = m_Anchors.size();
    context["anchors"] = anchorsJson( m_Anchors, aMaxAnchors );
    context["panel_state_count"] = m_PanelStates.size();
    context["panel_states"] = panelStatesJson( m_PanelStates, aMaxPanelStates );
    context["visual"] = { { "source", toUtf8String( m_Visual.m_Source ) },
                          { "mime_type", toUtf8String( m_Visual.m_MimeType ) },
                          { "width_px", m_Visual.m_WidthPx },
                          { "height_px", m_Visual.m_HeightPx },
                          { "byte_size", m_Visual.m_ByteSize },
                          { "has_pixels", m_Visual.HasPixels() } };

    if( !m_Visual.m_UnavailableReason.IsEmpty() )
    {
        context["visual"]["unavailable_reason"] =
                toUtf8String( m_Visual.m_UnavailableReason );
    }

    nlohmann::json root = { { "kisurf_context", std::move( context ) } };
    return wxString::FromUTF8( root.dump().c_str() );
}


wxString AI_TOOL_INVOCATION_RESULT::AsTraceText() const
{
    return wxString::Format(
            wxS( "request=%llu tool_call=%s action=%s allowed=%s executed=%s error=%s "
                 "message=%s" ),
            static_cast<unsigned long long>( m_RequestId ),
            m_ToolCallId,
            m_ActionName,
            m_Allowed ? wxS( "yes" ) : wxS( "no" ),
            m_Executed ? wxS( "yes" ) : wxS( "no" ),
            m_ErrorCode,
            m_Message );
}
