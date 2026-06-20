/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 */

#include <boost/test/unit_test.hpp>
#include <json_common.h>
#include <kisurf/ai/ai_types.h>

BOOST_AUTO_TEST_SUITE( AiNativeTypes )


nlohmann::json contextJson( const AI_CONTEXT_SNAPSHOT& aSnapshot )
{
    const wxString jsonText = aSnapshot.AsJsonText( 10, 10, 10, 10, 10 );
    nlohmann::json parsed = nlohmann::json::parse( jsonText.ToStdString() );
    return parsed["kisurf_context"];
}


BOOST_AUTO_TEST_CASE( ContextVersionStartsInvalidAndCanAdvance )
{
    AI_CONTEXT_VERSION version;

    BOOST_CHECK( !version.IsValid() );

    version.m_DocumentRevision = 1;
    version.m_SelectionRevision = 2;
    version.m_ViewRevision = 3;

    BOOST_CHECK( version.IsValid() );
    BOOST_CHECK_EQUAL( version.AsString(), wxString( wxS( "doc=1;sel=2;view=3" ) ) );
}


BOOST_AUTO_TEST_CASE( ObjectRefValidityDependsOnUuidAndType )
{
    AI_OBJECT_REF emptyRef;
    BOOST_CHECK( !emptyRef.IsValid() );

    AI_OBJECT_REF padRef( KIID(), PCB_PAD_T, wxS( "U1.1" ) );
    BOOST_CHECK( padRef.IsValid() );
    BOOST_CHECK_EQUAL( padRef.m_Label, wxString( wxS( "U1.1" ) ) );
}


BOOST_AUTO_TEST_CASE( ObjectRefDetailsAreRenderedInPromptAndJson )
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_VisibleObjects.push_back( AI_OBJECT_REF(
            KIID(), PCB_TRACE_T, wxS( "track:0,0->100,200" ),
            wxS( "{\"kind\":\"track\",\"width\":250}" ) ) );

    const wxString prompt = snapshot.AsPromptText( 10, 10 );

    BOOST_CHECK( prompt.Contains( wxS( "details={\"kind\":\"track\",\"width\":250}" ) ) );

    const wxString jsonText = snapshot.AsJsonText( 10, 10, 10 );
    nlohmann::json parsed = nlohmann::json::parse( jsonText.ToStdString() );

    const nlohmann::json& object =
            parsed["kisurf_context"]["visible_objects"].at( 0 );

    BOOST_REQUIRE( object.contains( "details" ) );
    BOOST_CHECK_EQUAL( object["details"]["kind"].get<std::string>(), "track" );
    BOOST_CHECK_EQUAL( object["details"]["width"].get<int>(), 250 );
}


BOOST_AUTO_TEST_CASE( ValidationSummaryFindsWorstSeverity )
{
    AI_VALIDATION_SUMMARY summary;
    BOOST_CHECK( summary.WorstSeverity() == AI_VALIDATION_SEVERITY::None );
    BOOST_CHECK( !summary.HasBlockingIssue() );

    summary.m_Issues.push_back( { AI_VALIDATION_SEVERITY::Warning, wxS( "near clearance" ), false } );
    summary.m_Issues.push_back( { AI_VALIDATION_SEVERITY::Error, wxS( "new short" ), true } );

    BOOST_CHECK( summary.WorstSeverity() == AI_VALIDATION_SEVERITY::Error );
    BOOST_CHECK( summary.HasBlockingIssue() );
}


BOOST_AUTO_TEST_CASE( ActionDescriptorValidityUsesStableName )
{
    AI_ACTION_DESCRIPTOR descriptor;
    BOOST_CHECK( !descriptor.IsValid() );

    descriptor.m_Name = wxS( "common.Control.showAgentPanel" );
    descriptor.m_FriendlyName = wxS( "Agent" );
    descriptor.m_EditorKind = AI_EDITOR_KIND::Pcb;
    descriptor.m_Safety = AI_ACTION_SAFETY::ReadOnly;

    BOOST_CHECK( descriptor.IsValid() );
}


BOOST_AUTO_TEST_CASE( ContextSnapshotFormatsPromptText )
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_Version.m_DocumentRevision = 7;
    snapshot.m_VisibleObjects.push_back( AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ) ) );
    snapshot.m_SelectedObjects.push_back( AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ) ) );
    snapshot.m_Actions.push_back( { wxS( "common.Control.showAgentPanel" ),
                                    wxS( "Agent" ),
                                    wxS( "Show or hide the Agent panel" ),
                                    AI_EDITOR_KIND::Pcb,
                                    AI_ACTION_SAFETY::ReadOnly,
                                    true } );
    snapshot.m_Visual.m_Source = wxS( "canvas" );
    snapshot.m_Visual.m_MimeType = wxS( "image/png" );
    snapshot.m_Visual.m_UnavailableReason = wxS( "invalid_image" );

    BOOST_CHECK( snapshot.HasContext() );

    const wxString prompt = snapshot.AsPromptText( 10, 10 );

    BOOST_CHECK( prompt.Contains( wxS( "editor: PCB" ) ) );
    BOOST_CHECK( prompt.Contains( wxS( "version: doc=7;sel=0;view=0" ) ) );
    BOOST_CHECK( prompt.Contains( wxS( "visible objects: 1" ) ) );
    BOOST_CHECK( prompt.Contains( wxS( "selected objects: 1" ) ) );
    BOOST_CHECK( prompt.Contains( wxS( "common.Control.showAgentPanel" ) ) );
    BOOST_CHECK( prompt.Contains(
            wxS( "visual: canvas image/png pixels=no unavailable_reason=invalid_image" ) ) );
}


BOOST_AUTO_TEST_CASE( ContextSnapshotJsonCarriesVisualUnavailableReason )
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_Visual.m_Source = wxS( "canvas.opengl" );
    snapshot.m_Visual.m_UnavailableReason = wxS( "invalid_image" );

    BOOST_CHECK( snapshot.HasContext() );

    nlohmann::json context = contextJson( snapshot );

    BOOST_CHECK_EQUAL( context["visual"]["source"].get<std::string>(), "canvas.opengl" );
    BOOST_CHECK( !context["visual"]["has_pixels"].get<bool>() );
    BOOST_CHECK_EQUAL( context["visual"]["unavailable_reason"].get<std::string>(),
                       "invalid_image" );
}


BOOST_AUTO_TEST_CASE( ContextSnapshotFormatsRecentActivity )
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;

    AI_ACTIVITY_RECORD record;
    record.m_Sequence = 7;
    record.m_Kind = AI_ACTIVITY_KIND::UserAction;
    record.m_ActionName = wxS( "common.Interactive.selected" );
    record.m_Allowed = true;
    record.m_Executed = true;
    record.m_Message = wxS( "selection changed" );
    snapshot.m_RecentActivity.push_back( record );

    BOOST_CHECK( snapshot.HasContext() );

    const wxString prompt = snapshot.AsPromptText( 10, 10 );

    BOOST_CHECK( prompt.Contains( wxS( "recent activity: 1" ) ) );
    BOOST_CHECK( prompt.Contains( wxS( "#7 user-action common.Interactive.selected" ) ) );
    BOOST_CHECK( prompt.Contains( wxS( "allowed=yes executed=yes" ) ) );
    BOOST_CHECK( prompt.Contains( wxS( "message=selection changed" ) ) );
}


BOOST_AUTO_TEST_CASE( ContextSnapshotFormatsStructuredJson )
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_Version.m_DocumentRevision = 7;
    snapshot.m_Version.m_SelectionRevision = 2;
    snapshot.m_Version.m_ViewRevision = 5;
    snapshot.m_Summary = wxS( "selected pad near crowded routing" );
    snapshot.m_VisibleObjects.push_back( AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ) ) );
    snapshot.m_SelectedObjects.push_back( AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ) ) );
    snapshot.m_Actions.push_back( { wxS( "common.Control.showAgentPanel" ),
                                    wxS( "Agent" ),
                                    wxS( "Show or hide the Agent panel" ),
                                    AI_EDITOR_KIND::Pcb,
                                    AI_ACTION_SAFETY::ReadOnly,
                                    true } );

    AI_ACTIVITY_RECORD activity;
    activity.m_Sequence = 12;
    activity.m_Kind = AI_ACTIVITY_KIND::UserAction;
    activity.m_EditorKind = AI_EDITOR_KIND::Pcb;
    activity.m_ActionName = wxS( "common.Interactive.selected" );
    activity.m_ArgumentsJson = wxS( "{\"x\":10,\"y\":20}" );
    activity.m_Allowed = true;
    activity.m_Executed = true;
    activity.m_Message = wxS( "selection changed" );
    snapshot.m_RecentActivity.push_back( activity );

    snapshot.m_Visual.m_Source = wxS( "canvas" );
    snapshot.m_Visual.m_MimeType = wxS( "image/png" );
    snapshot.m_Visual.m_DataUri = wxS( "data:image/png;base64,dW5pdA==" );
    snapshot.m_Visual.m_WidthPx = 640;
    snapshot.m_Visual.m_HeightPx = 480;
    snapshot.m_Visual.m_ByteSize = 1024;

    const wxString jsonText = snapshot.AsJsonText( 10, 10, 10 );
    nlohmann::json parsed = nlohmann::json::parse( jsonText.ToStdString() );

    BOOST_REQUIRE( parsed.contains( "kisurf_context" ) );
    const nlohmann::json& context = parsed["kisurf_context"];

    BOOST_CHECK_EQUAL( context["editor"].get<std::string>(), "pcb" );
    BOOST_CHECK_EQUAL( context["version"]["document"].get<int>(), 7 );
    BOOST_CHECK_EQUAL( context["version"]["selection"].get<int>(), 2 );
    BOOST_CHECK_EQUAL( context["version"]["view"].get<int>(), 5 );
    BOOST_CHECK_EQUAL( context["summary"].get<std::string>(),
                       "selected pad near crowded routing" );
    BOOST_CHECK_EQUAL( context["visible_object_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( context["visible_objects"].at( 0 )["label"].get<std::string>(), "U1.1" );
    BOOST_CHECK_EQUAL( context["selected_objects"].at( 0 )["label"].get<std::string>(), "U1.1" );
    BOOST_CHECK_EQUAL( context["action_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( context["actions"].at( 0 )["name"].get<std::string>(),
                       "common.Control.showAgentPanel" );
    BOOST_CHECK_EQUAL( context["actions"].at( 0 )["safety"].get<std::string>(), "readonly" );
    BOOST_CHECK( context["actions"].at( 0 )["enabled"].get<bool>() );
    BOOST_CHECK_EQUAL( context["recent_activity"].at( 0 )["kind"].get<std::string>(),
                       "user-action" );
    BOOST_CHECK_EQUAL( context["recent_activity"].at( 0 )["arguments_json"].get<std::string>(),
                       "{\"x\":10,\"y\":20}" );
    BOOST_CHECK_EQUAL( context["visual"]["source"].get<std::string>(), "canvas" );
    BOOST_CHECK( context["visual"]["has_pixels"].get<bool>() );
    BOOST_CHECK( !context["visual"].contains( "data_uri" ) );
    BOOST_CHECK( !context["visual"].contains( "unavailable_reason" ) );
}


BOOST_AUTO_TEST_CASE( ContextSnapshotIncludesToolStateInPromptAndJson )
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::PlacingVia;
    snapshot.m_ToolState.m_ActiveActionName = wxS( "pcbnew.InteractiveDrawing.drawVia" );
    snapshot.m_ToolState.m_ModeContextJson = wxS( "{\"net\":\"GND\"}" );

    BOOST_CHECK( snapshot.HasContext() );

    const wxString prompt = snapshot.AsPromptText( 10, 10 );

    BOOST_CHECK( prompt.Contains( wxS( "current tool state:" ) ) );
    BOOST_CHECK( prompt.Contains( wxS( "tool state: placing_via" ) ) );
    BOOST_CHECK( prompt.Contains( wxS( "active action: pcbnew.InteractiveDrawing.drawVia" ) ) );

    const wxString jsonText = snapshot.AsJsonText( 10, 10, 10 );
    nlohmann::json parsed = nlohmann::json::parse( jsonText.ToStdString() );

    const nlohmann::json& context = parsed["kisurf_context"];
    BOOST_REQUIRE( context.contains( "tool_state" ) );
    BOOST_CHECK_EQUAL( context["tool_state"]["kind"].get<std::string>(), "placing_via" );
    BOOST_CHECK_EQUAL( context["tool_state"]["mode_context"]["net"].get<std::string>(), "GND" );
}


BOOST_AUTO_TEST_CASE( ContextSnapshotProjectsRoutingDynamicContext )
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::RoutingTrack;
    snapshot.m_ToolState.m_ActiveActionName = wxS( "pcbnew.InteractiveRoute" );

    const wxString prompt = snapshot.AsPromptText( 10, 10, 10, 10 );
    BOOST_CHECK( prompt.Contains( wxS( "dynamic context: routing" ) ) );

    nlohmann::json context = contextJson( snapshot );
    const nlohmann::json& dynamicContext = context["dynamic_context"];
    BOOST_CHECK_EQUAL( dynamicContext["kind"].get<std::string>(), "routing" );
    BOOST_CHECK_EQUAL( dynamicContext["source"].get<std::string>(), "tool_state" );
    BOOST_CHECK_EQUAL( dynamicContext["tool_state_kind"].get<std::string>(),
                       "routing_track" );
    BOOST_CHECK_EQUAL( dynamicContext["active_action"].get<std::string>(),
                       "pcbnew.InteractiveRoute" );
}


BOOST_AUTO_TEST_CASE( ContextSnapshotProjectsLayoutDynamicContext )
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::PlacingFootprint;

    nlohmann::json context = contextJson( snapshot );
    BOOST_CHECK_EQUAL( context["dynamic_context"]["kind"].get<std::string>(),
                       "layout" );
    BOOST_CHECK_EQUAL( context["dynamic_context"]["source"].get<std::string>(),
                       "tool_state" );
}


BOOST_AUTO_TEST_CASE( ContextSnapshotProjectsPanelDynamicContext )
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;

    AI_PANEL_STATE_RECORD panel;
    panel.m_Id = wxS( "pcb.board_setup.clearance" );
    panel.m_Title = wxS( "Board Setup" );
    panel.m_FocusedControlId = wxS( "clearance.grid.row3.col2" );
    panel.m_FocusedControlLabel = wxS( "Minimum clearance" );
    snapshot.m_PanelStates.push_back( panel );

    nlohmann::json context = contextJson( snapshot );
    const nlohmann::json& dynamicContext = context["dynamic_context"];
    BOOST_CHECK_EQUAL( dynamicContext["kind"].get<std::string>(), "panel" );
    BOOST_CHECK_EQUAL( dynamicContext["source"].get<std::string>(),
                       "panel_state" );
    BOOST_CHECK_EQUAL( dynamicContext["tool_state_kind"].get<std::string>(),
                       "unknown" );
    BOOST_CHECK_EQUAL( dynamicContext["focused_panel_id"].get<std::string>(),
                       "pcb.board_setup.clearance" );
    BOOST_CHECK_EQUAL( dynamicContext["focused_panel_title"].get<std::string>(),
                       "Board Setup" );
    BOOST_CHECK_EQUAL( dynamicContext["focused_control_id"].get<std::string>(),
                       "clearance.grid.row3.col2" );
    BOOST_CHECK_EQUAL( dynamicContext["focused_control_label"].get<std::string>(),
                       "Minimum clearance" );
}


BOOST_AUTO_TEST_CASE( ContextSnapshotProjectsGeneralAndIdleDynamicContexts )
{
    AI_CONTEXT_SNAPSHOT selecting;
    selecting.m_EditorKind = AI_EDITOR_KIND::Pcb;
    selecting.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::Selecting;

    nlohmann::json selectingContext = contextJson( selecting );
    BOOST_CHECK_EQUAL( selectingContext["dynamic_context"]["kind"].get<std::string>(),
                       "general" );

    AI_CONTEXT_SNAPSHOT idle;
    idle.m_EditorKind = AI_EDITOR_KIND::Pcb;
    idle.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::Idle;

    nlohmann::json idleContext = contextJson( idle );
    BOOST_CHECK_EQUAL( idleContext["dynamic_context"]["kind"].get<std::string>(),
                       "idle" );
}


BOOST_AUTO_TEST_CASE( ToolStateSnapshotFormatsPromptAndJson )
{
    AI_TOOL_STATE_SNAPSHOT state;
    BOOST_CHECK( !state.HasToolState() );

    state.m_EditorKind = AI_EDITOR_KIND::Pcb;
    state.m_Kind = AI_TOOL_STATE_KIND::RoutingTrack;
    state.m_ContextVersion.m_DocumentRevision = 9;
    state.m_ActiveActionName = wxS( "pcbnew.InteractiveRouter.SingleTrack" );
    state.m_CursorBoardPosition = VECTOR2I( 100, 200 );
    state.m_HasCursorBoardPosition = true;
    state.m_SharedContextJson = wxS( "{\"layer\":\"F.Cu\"}" );
    state.m_ModeContextJson = wxS( "{\"net\":\"GND\",\"width\":150000}" );

    BOOST_CHECK( state.HasToolState() );

    const wxString prompt = state.AsPromptText();

    BOOST_CHECK( prompt.Contains( wxS( "tool state: routing_track" ) ) );
    BOOST_CHECK( prompt.Contains( wxS( "active action: pcbnew.InteractiveRouter.SingleTrack" ) ) );
    BOOST_CHECK( prompt.Contains( wxS( "cursor: x=100 y=200" ) ) );
    BOOST_CHECK( prompt.Contains( wxS( "shared context: {\"layer\":\"F.Cu\"}" ) ) );
    BOOST_CHECK( prompt.Contains( wxS( "mode context: {\"net\":\"GND\",\"width\":150000}" ) ) );

    const wxString jsonText = state.AsJsonText();
    nlohmann::json parsed = nlohmann::json::parse( jsonText.ToStdString() );

    BOOST_REQUIRE( parsed.contains( "kisurf_tool_state" ) );
    const nlohmann::json& toolState = parsed["kisurf_tool_state"];

    BOOST_CHECK_EQUAL( toolState["editor"].get<std::string>(), "pcb" );
    BOOST_CHECK_EQUAL( toolState["kind"].get<std::string>(), "routing_track" );
    BOOST_CHECK_EQUAL( toolState["active_action"].get<std::string>(),
                       "pcbnew.InteractiveRouter.SingleTrack" );
    BOOST_CHECK_EQUAL( toolState["cursor"]["x"].get<int>(), 100 );
    BOOST_CHECK_EQUAL( toolState["cursor"]["y"].get<int>(), 200 );
    BOOST_CHECK_EQUAL( toolState["shared_context"]["layer"].get<std::string>(), "F.Cu" );
    BOOST_CHECK_EQUAL( toolState["mode_context"]["net"].get<std::string>(), "GND" );
}


BOOST_AUTO_TEST_CASE( AgentWorkspaceContextStateFormatsContextAndTracksStoredState )
{
    AI_AGENT_WORKSPACE_CONTEXT_STATE state;
    BOOST_CHECK( !state.HasState() );
    BOOST_CHECK_EQUAL( state.ContextAsString(), wxString( wxS( "general" ) ) );

    state.m_ContextKind = AI_AGENT_WORKSPACE_CONTEXT_KIND::Routing;
    state.m_Title = wxS( "Routing" );
    state.m_StateJson = wxS( "{\"draft\":\"continue GND\"}" );
    state.m_LastActivitySequence = 42;

    BOOST_CHECK( state.HasState() );
    BOOST_CHECK_EQUAL( state.ContextAsString(), wxString( wxS( "routing" ) ) );
}


BOOST_AUTO_TEST_CASE( ToolInvocationResultFormatsStatus )
{
    AI_TOOL_INVOCATION_RESULT result;
    result.m_RequestId = 7;
    result.m_ToolCallId = wxS( "call_1" );
    result.m_ActionName = wxS( "common.Control.showAgentPanel" );
    result.m_Allowed = true;
    result.m_Executed = true;
    result.m_Message = wxS( "ran" );

    wxString text = result.AsTraceText();

    BOOST_CHECK( text.Contains( wxS( "request=7" ) ) );
    BOOST_CHECK( text.Contains( wxS( "tool_call=call_1" ) ) );
    BOOST_CHECK( text.Contains( wxS( "allowed=yes" ) ) );
    BOOST_CHECK( text.Contains( wxS( "executed=yes" ) ) );
}


BOOST_AUTO_TEST_CASE( ContextAnchorValidityAndKindNames )
{
    AI_CONTEXT_ANCHOR anchor;
    BOOST_CHECK( !anchor.IsValid() );
    BOOST_CHECK_EQUAL( anchor.KindAsString(), wxString( wxS( "unknown" ) ) );

    anchor.m_Id = wxS( "route.candidate.1" );
    anchor.m_Kind = AI_CONTEXT_ANCHOR_KIND::FortyFiveIntersection;
    anchor.m_EditorKind = AI_EDITOR_KIND::Pcb;
    anchor.m_Label = wxS( "45-degree bend" );
    anchor.m_Position = VECTOR2I( 1000, 2000 );
    anchor.m_HasPosition = true;
    anchor.m_Layer = 0;
    anchor.m_Confidence = 0.75;

    BOOST_CHECK( anchor.IsValid() );
    BOOST_CHECK_EQUAL( anchor.KindAsString(),
                       wxString( wxS( "forty_five_intersection" ) ) );
}


BOOST_AUTO_TEST_CASE( PanelStateRecordDetectsVisibleState )
{
    AI_PANEL_STATE_RECORD panel;
    BOOST_CHECK( !panel.HasState() );

    panel.m_Id = wxS( "pcb.board_setup.clearance" );
    panel.m_Title = wxS( "Board Setup" );
    panel.m_FocusedControlId = wxS( "clearance.grid.row3.col2" );
    panel.m_FocusedControlLabel = wxS( "Minimum clearance" );
    panel.m_SelectedText = wxS( "0.20 mm" );
    panel.m_StateJson = wxS( "{\"row\":3,\"column\":\"clearance\"}" );

    BOOST_CHECK( panel.HasState() );
}


BOOST_AUTO_TEST_CASE( ContextSnapshotFormatsAnchorsAndPanelState )
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;

    AI_CONTEXT_ANCHOR anchor;
    anchor.m_Id = wxS( "route.candidate.2" );
    anchor.m_Kind = AI_CONTEXT_ANCHOR_KIND::OrthogonalBreakout;
    anchor.m_Label = wxS( "Horizontal breakout" );
    anchor.m_Summary = wxS( "First horizontal segment from current cursor" );
    anchor.m_Position = VECTOR2I( 1200, 3400 );
    anchor.m_HasPosition = true;
    anchor.m_Layer = 0;
    anchor.m_DetailsJson = wxS( "{\"net\":\"GND\",\"width\":150000}" );
    anchor.m_Confidence = 0.9;
    snapshot.m_Anchors.push_back( anchor );

    AI_PANEL_STATE_RECORD panel;
    panel.m_Id = wxS( "pcb.board_setup.clearance" );
    panel.m_Title = wxS( "Board Setup" );
    panel.m_FocusedControlLabel = wxS( "Minimum clearance" );
    panel.m_SelectedText = wxS( "0.20 mm" );
    panel.m_StateJson = wxS( "{\"row\":3,\"column\":\"clearance\"}" );
    snapshot.m_PanelStates.push_back( panel );

    BOOST_CHECK( snapshot.HasContext() );

    const wxString prompt = snapshot.AsPromptText( 10, 10, 10, 10 );

    BOOST_CHECK( prompt.Contains( wxS( "semantic anchors: 1" ) ) );
    BOOST_CHECK( prompt.Contains( wxS( "route.candidate.2" ) ) );
    BOOST_CHECK( prompt.Contains( wxS( "orthogonal_breakout" ) ) );
    BOOST_CHECK( prompt.Contains( wxS( "x=1200 y=3400" ) ) );
    BOOST_CHECK( prompt.Contains( wxS( "panel states: 1" ) ) );
    BOOST_CHECK( prompt.Contains( wxS( "pcb.board_setup.clearance" ) ) );
    BOOST_CHECK( prompt.Contains( wxS( "Minimum clearance" ) ) );
}


BOOST_AUTO_TEST_CASE( ContextSnapshotJsonIncludesAnchorsAndPanelState )
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;

    AI_CONTEXT_ANCHOR anchor;
    anchor.m_Id = wxS( "route.target.pad" );
    anchor.m_Kind = AI_CONTEXT_ANCHOR_KIND::RouteTarget;
    anchor.m_Label = wxS( "U1.1 pad" );
    anchor.m_DetailsJson = wxS( "{\"pad\":\"U1.1\"}" );
    snapshot.m_Anchors.push_back( anchor );

    AI_CONTEXT_ANCHOR rawAnchor;
    rawAnchor.m_Id = wxS( "route.raw" );
    rawAnchor.m_Kind = AI_CONTEXT_ANCHOR_KIND::General;
    rawAnchor.m_DetailsJson = wxS( "not-json" );
    snapshot.m_Anchors.push_back( rawAnchor );

    AI_PANEL_STATE_RECORD panel;
    panel.m_Id = wxS( "pcb.panel" );
    panel.m_StateJson = wxS( "{\"focused\":\"cell\"}" );
    snapshot.m_PanelStates.push_back( panel );

    const wxString jsonText = snapshot.AsJsonText( 10, 10, 10, 10, 10 );
    nlohmann::json parsed = nlohmann::json::parse( jsonText.ToStdString() );
    const nlohmann::json& context = parsed["kisurf_context"];

    BOOST_CHECK_EQUAL( context["anchor_count"].get<int>(), 2 );
    BOOST_CHECK_EQUAL( context["anchors"].at( 0 )["id"].get<std::string>(),
                       "route.target.pad" );
    BOOST_CHECK_EQUAL( context["anchors"].at( 0 )["kind"].get<std::string>(),
                       "route_target" );
    BOOST_CHECK_EQUAL( context["anchors"].at( 0 )["details"]["pad"].get<std::string>(),
                       "U1.1" );
    BOOST_CHECK_EQUAL( context["anchors"].at( 1 )["details_raw"].get<std::string>(),
                       "not-json" );
    BOOST_CHECK_EQUAL( context["panel_state_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( context["panel_states"].at( 0 )["state"]["focused"].get<std::string>(),
                       "cell" );
}


BOOST_AUTO_TEST_SUITE_END()
