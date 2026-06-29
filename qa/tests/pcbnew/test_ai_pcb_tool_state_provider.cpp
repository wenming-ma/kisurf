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

#include <kisurf_ai_pcb_tool_state_provider.h>

#include <board.h>
#include <board_design_settings.h>
#include <callback_gal.h>
#include <gal/gal_display_options.h>
#include <json_common.h>
#include <pcb_track.h>
#include <tool/tool_event.h>
#include <tool/tool_manager.h>
#include <view/view.h>

#include <fstream>
#include <sstream>
#include <string>

#ifndef QA_SRC_ROOT
#error QA_SRC_ROOT must be defined for AI PCB tool state provider tests.
#endif

namespace
{
nlohmann::json parseJson( const wxString& aJson )
{
    wxScopedCharBuffer buffer = aJson.ToUTF8();
    return nlohmann::json::parse(
            buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string() );
}


std::string readPcbEditFrameSource()
{
    const std::string path = std::string( QA_SRC_ROOT ) + "/pcbnew/pcb_edit_frame.cpp";
    std::ifstream     in( path, std::ios::binary );

    BOOST_REQUIRE_MESSAGE( in.good(), "Unable to read source file: " << path );

    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}


std::string sourceSlice( const std::string& aSource, const std::string& aStartNeedle,
                         const std::string& aEndNeedle )
{
    const size_t start = aSource.find( aStartNeedle );
    BOOST_REQUIRE_MESSAGE( start != std::string::npos,
                           "Unable to find source marker: " << aStartNeedle );

    const size_t end = aSource.find( aEndNeedle, start + aStartNeedle.size() );
    BOOST_REQUIRE_MESSAGE( end != std::string::npos,
                           "Unable to find source marker: " << aEndNeedle );

    return aSource.substr( start, end - start );
}
} // namespace


BOOST_AUTO_TEST_SUITE( AiPcbToolStateProvider )


BOOST_AUTO_TEST_CASE( PcbEditFrameDoesNotContinuouslyRequestIdleForBackgroundAgent )
{
    const std::string source = readPcbEditFrameSource();
    const std::string idleHandler = sourceSlice(
            source, "Bind( wxEVT_IDLE,", "resolveCanvasType();" );

    BOOST_CHECK_EQUAL( idleHandler.find( "PulseBackgroundAgent" ),
                       std::string::npos );
    BOOST_CHECK_EQUAL( idleHandler.find( "ShouldContinueBackgroundIdlePulse()" ),
                       std::string::npos );
    BOOST_CHECK_EQUAL( idleHandler.find( "aEvent.RequestMore()" ),
                       std::string::npos );
}


BOOST_AUTO_TEST_CASE( MapsRecentPcbActionsIntoToolStateKinds )
{
    KISURF_AI_PCB_TOOL_STATE_PROVIDER provider( nullptr );

    AI_CONTEXT_VERSION version;
    version.m_DocumentRevision = 7;

    TOOL_EVENT routeEvent( TC_COMMAND, TA_ACTIVATE, "pcbnew.InteractiveRouter.SingleTrack" );
    routeEvent.SetMousePosition( VECTOR2D( 100, 200 ) );
    provider.RecordToolEvent( routeEvent );

    AI_TOOL_STATE_SNAPSHOT snapshot = provider.BuildToolState( version );

    BOOST_CHECK_EQUAL( static_cast<int>( snapshot.m_EditorKind ),
                       static_cast<int>( AI_EDITOR_KIND::Pcb ) );
    BOOST_CHECK_EQUAL( static_cast<int>( snapshot.m_Kind ),
                       static_cast<int>( AI_TOOL_STATE_KIND::RoutingTrack ) );
    BOOST_CHECK_EQUAL( snapshot.m_ContextVersion.m_DocumentRevision, 7 );
    BOOST_CHECK_EQUAL( snapshot.m_ActiveActionName,
                       wxString( wxS( "pcbnew.InteractiveRouter.SingleTrack" ) ) );
    BOOST_REQUIRE( snapshot.m_HasCursorBoardPosition );
    BOOST_CHECK_EQUAL( snapshot.m_CursorBoardPosition.x, 100 );
    BOOST_CHECK_EQUAL( snapshot.m_CursorBoardPosition.y, 200 );

    provider.RecordToolEvent( TOOL_EVENT( TC_COMMAND, TA_ACTIVATE,
                                          "pcbnew.InteractiveDrawing.via" ) );
    snapshot = provider.BuildToolState( version );
    BOOST_CHECK_EQUAL( static_cast<int>( snapshot.m_Kind ),
                       static_cast<int>( AI_TOOL_STATE_KIND::PlacingVia ) );

    provider.RecordToolEvent( TOOL_EVENT( TC_COMMAND, TA_ACTIVATE,
                                          "pcbnew.EditorControl.placeFootprint" ) );
    snapshot = provider.BuildToolState( version );
    BOOST_CHECK_EQUAL( static_cast<int>( snapshot.m_Kind ),
                       static_cast<int>( AI_TOOL_STATE_KIND::PlacingFootprint ) );

    provider.RecordToolEvent( TOOL_EVENT( TC_COMMAND, TA_ACTIVATE,
                                          "pcbnew.InteractiveDrawing.zone" ) );
    snapshot = provider.BuildToolState( version );
    BOOST_CHECK_EQUAL( static_cast<int>( snapshot.m_Kind ),
                       static_cast<int>( AI_TOOL_STATE_KIND::DrawingZone ) );

    provider.RecordToolEvent( TOOL_EVENT( TC_COMMAND, TA_ACTIVATE,
                                          "pcbnew.InteractiveDrawing.ruleArea" ) );
    snapshot = provider.BuildToolState( version );
    BOOST_CHECK_EQUAL( static_cast<int>( snapshot.m_Kind ),
                       static_cast<int>( AI_TOOL_STATE_KIND::DrawingZone ) );

    provider.RecordToolEvent( TOOL_EVENT( TC_COMMAND, TA_ACTIVATE,
                                          "pcbnew.InteractiveDrawing.zoneCutout" ) );
    snapshot = provider.BuildToolState( version );
    BOOST_CHECK_EQUAL( static_cast<int>( snapshot.m_Kind ),
                       static_cast<int>( AI_TOOL_STATE_KIND::DrawingZone ) );

    provider.RecordToolEvent( TOOL_EVENT( TC_COMMAND, TA_ACTIVATE,
                                          "pcbnew.InteractiveMove.move" ) );
    snapshot = provider.BuildToolState( version );
    BOOST_CHECK_EQUAL( static_cast<int>( snapshot.m_Kind ),
                       static_cast<int>( AI_TOOL_STATE_KIND::MovingSelection ) );
}


BOOST_AUTO_TEST_CASE( ObservesToolManagerEvents )
{
    TOOL_MANAGER manager;
    KISURF_AI_PCB_TOOL_STATE_PROVIDER provider( &manager );

    TOOL_EVENT event( TC_COMMAND, TA_ACTIVATE, "pcbnew.InteractiveRouter.SingleTrack" );
    event.SetMousePosition( VECTOR2D( 12, 34 ) );
    manager.ProcessEvent( event );

    AI_CONTEXT_VERSION version;
    AI_TOOL_STATE_SNAPSHOT snapshot = provider.BuildToolState( version );

    BOOST_CHECK_EQUAL( static_cast<int>( snapshot.m_Kind ),
                       static_cast<int>( AI_TOOL_STATE_KIND::RoutingTrack ) );
    BOOST_CHECK_EQUAL( snapshot.m_ActiveActionName,
                       wxString( wxS( "pcbnew.InteractiveRouter.SingleTrack" ) ) );
    BOOST_REQUIRE( snapshot.m_HasCursorBoardPosition );
    BOOST_CHECK_EQUAL( snapshot.m_CursorBoardPosition.x, 12 );
    BOOST_CHECK_EQUAL( snapshot.m_CursorBoardPosition.y, 34 );
}


BOOST_AUTO_TEST_CASE( CancelClearsStaleActiveActionState )
{
    KISURF_AI_PCB_TOOL_STATE_PROVIDER provider( nullptr );

    provider.RecordToolEvent( TOOL_EVENT( TC_COMMAND, TA_ACTIVATE,
                                          "pcbnew.InteractiveRouter.SingleTrack" ) );

    AI_TOOL_STATE_SNAPSHOT active = provider.BuildToolState( AI_CONTEXT_VERSION() );
    BOOST_CHECK_EQUAL( static_cast<int>( active.m_Kind ),
                       static_cast<int>( AI_TOOL_STATE_KIND::RoutingTrack ) );

    provider.RecordToolEvent( TOOL_EVENT( TC_COMMAND, TA_CANCEL_TOOL ) );

    AI_TOOL_STATE_SNAPSHOT cancelled = provider.BuildToolState( AI_CONTEXT_VERSION() );
    BOOST_CHECK_EQUAL( static_cast<int>( cancelled.m_Kind ),
                       static_cast<int>( AI_TOOL_STATE_KIND::Idle ) );
    BOOST_CHECK( cancelled.m_ActiveActionName.IsEmpty() );
}


BOOST_AUTO_TEST_CASE( RoutingModeContextIncludesBoardLayerWidthAndCursor )
{
    BOARD board;
    board.GetDesignSettings().UseCustomTrackViaSize( true );
    board.GetDesignSettings().SetCustomTrackWidth( 123000 );

    TOOL_MANAGER manager;
    manager.SetEnvironment( &board, nullptr, nullptr, nullptr, nullptr );

    KISURF_AI_PCB_TOOL_STATE_PROVIDER provider( &manager );

    TOOL_EVENT event( TC_COMMAND, TA_ACTIVATE, "pcbnew.InteractiveRouter.SingleTrack" );
    event.SetMousePosition( VECTOR2D( 44, 55 ) );
    manager.ProcessEvent( event );

    AI_TOOL_STATE_SNAPSHOT snapshot = provider.BuildToolState( AI_CONTEXT_VERSION() );

    BOOST_REQUIRE( !snapshot.m_SharedContextJson.IsEmpty() );
    BOOST_REQUIRE( !snapshot.m_ModeContextJson.IsEmpty() );

    nlohmann::json shared = parseJson( snapshot.m_SharedContextJson );
    nlohmann::json mode = parseJson( snapshot.m_ModeContextJson );

    BOOST_CHECK_EQUAL( shared["active_layer"].get<std::string>(), "F.Cu" );
    BOOST_CHECK_EQUAL( shared["track_width"].get<int>(), 123000 );
    BOOST_CHECK_EQUAL( mode["layer"].get<std::string>(), "F.Cu" );
    BOOST_CHECK_EQUAL( mode["width"].get<int>(), 123000 );
    BOOST_CHECK_EQUAL( mode["cursor"]["x"].get<int>(), 44 );
    BOOST_CHECK_EQUAL( mode["cursor"]["y"].get<int>(), 55 );
}


BOOST_AUTO_TEST_CASE( RoutingModeContextIncludesViewportAndCursorRegion )
{
    BOARD board;
    KIGFX::GAL_DISPLAY_OPTIONS galOptions;
    CALLBACK_GAL gal(
            galOptions,
            []( const VECTOR2I&, const VECTOR2I& ) {},
            []( const VECTOR2I&, const VECTOR2I&, const VECTOR2I& ) {} );
    gal.SetScreenSize( VECTOR2I( 5000, 3000 ) );

    KIGFX::VIEW view;
    view.SetGAL( &gal );
    view.SetViewport( BOX2D( VECTOR2D( 1000.0, 2000.0 ), VECTOR2D( 5000.0, 3000.0 ) ) );

    TOOL_MANAGER manager;
    manager.SetEnvironment( &board, &view, nullptr, nullptr, nullptr );

    KISURF_AI_PCB_TOOL_STATE_PROVIDER provider( &manager );

    TOOL_EVENT event( TC_COMMAND, TA_ACTIVATE, "pcbnew.InteractiveRouter.SingleTrack" );
    event.SetMousePosition( VECTOR2D( 2500, 3200 ) );
    manager.ProcessEvent( event );

    AI_TOOL_STATE_SNAPSHOT snapshot = provider.BuildToolState( AI_CONTEXT_VERSION() );

    BOOST_REQUIRE( !snapshot.m_ModeContextJson.IsEmpty() );

    nlohmann::json mode = parseJson( snapshot.m_ModeContextJson );
    const BOX2D     actualViewport = view.GetViewport();
    const VECTOR2D  actualCenter = actualViewport.Centre();

    BOOST_REQUIRE( mode.contains( "viewport" ) );
    BOOST_CHECK_EQUAL( mode["viewport"]["source"].get<std::string>(),
                       "KIGFX::VIEW::GetViewport" );
    BOOST_CHECK_EQUAL( mode["viewport"]["x"].get<int>(),
                       static_cast<int>( actualViewport.GetX() ) );
    BOOST_CHECK_EQUAL( mode["viewport"]["y"].get<int>(),
                       static_cast<int>( actualViewport.GetY() ) );
    BOOST_CHECK_EQUAL( mode["viewport"]["width"].get<int>(),
                       static_cast<int>( actualViewport.GetWidth() ) );
    BOOST_CHECK_EQUAL( mode["viewport"]["height"].get<int>(),
                       static_cast<int>( actualViewport.GetHeight() ) );
    BOOST_CHECK_EQUAL( mode["viewport"]["center"]["x"].get<int>(),
                       static_cast<int>( actualCenter.x ) );
    BOOST_CHECK_EQUAL( mode["viewport"]["center"]["y"].get<int>(),
                       static_cast<int>( actualCenter.y ) );
    BOOST_CHECK_EQUAL( mode["viewport"]["right"].get<int>(),
                       static_cast<int>( actualViewport.GetX() + actualViewport.GetWidth() ) );
    BOOST_CHECK_EQUAL( mode["viewport"]["bottom"].get<int>(),
                       static_cast<int>( actualViewport.GetY() + actualViewport.GetHeight() ) );
    BOOST_CHECK_EQUAL( mode["viewport"]["top_left"]["x"].get<int>(),
                       static_cast<int>( actualViewport.GetX() ) );
    BOOST_CHECK_EQUAL( mode["viewport"]["top_left"]["y"].get<int>(),
                       static_cast<int>( actualViewport.GetY() ) );
    BOOST_CHECK_EQUAL( mode["viewport"]["bottom_right"]["x"].get<int>(),
                       static_cast<int>( actualViewport.GetX() + actualViewport.GetWidth() ) );
    BOOST_CHECK_EQUAL( mode["viewport"]["bottom_right"]["y"].get<int>(),
                       static_cast<int>( actualViewport.GetY() + actualViewport.GetHeight() ) );
    BOOST_CHECK_EQUAL( mode["viewport"]["screen_size"]["width"].get<int>(),
                       view.GetScreenPixelSize().x );
    BOOST_CHECK_EQUAL( mode["viewport"]["screen_size"]["height"].get<int>(),
                       view.GetScreenPixelSize().y );

    BOOST_REQUIRE( mode.contains( "cursor_region" ) );
    BOOST_CHECK_EQUAL( mode["cursor_region"]["x"].get<int>(), 2500 );
    BOOST_CHECK_EQUAL( mode["cursor_region"]["y"].get<int>(), 3200 );
    BOOST_CHECK_EQUAL( mode["cursor_region"]["width"].get<int>(), 0 );
    BOOST_CHECK_EQUAL( mode["cursor_region"]["height"].get<int>(), 0 );
    BOOST_CHECK_EQUAL( mode["cursor_region"]["source"].get<std::string>(), "cursor" );
}


BOOST_AUTO_TEST_CASE( SharedContextBoardHashChangesWithBoardGeometry )
{
    BOARD board;

    PCB_TRACK* track = new PCB_TRACK( &board );
    track->SetStart( VECTOR2I( 1000, 2000 ) );
    track->SetEnd( VECTOR2I( 3000, 2000 ) );
    track->SetLayer( F_Cu );
    track->SetWidth( 150000 );
    board.Add( track );

    TOOL_MANAGER manager;
    manager.SetEnvironment( &board, nullptr, nullptr, nullptr, nullptr );

    KISURF_AI_PCB_TOOL_STATE_PROVIDER provider( &manager );

    AI_TOOL_STATE_SNAPSHOT first = provider.BuildToolState( AI_CONTEXT_VERSION() );
    nlohmann::json firstShared = parseJson( first.m_SharedContextJson );
    BOOST_REQUIRE( firstShared.contains( "board_hash" ) );
    const std::string firstHash = firstShared["board_hash"].get<std::string>();
    BOOST_CHECK( !firstHash.empty() );

    track->SetEnd( VECTOR2I( 4000, 2000 ) );

    AI_TOOL_STATE_SNAPSHOT second = provider.BuildToolState( AI_CONTEXT_VERSION() );
    nlohmann::json secondShared = parseJson( second.m_SharedContextJson );
    BOOST_REQUIRE( secondShared.contains( "board_hash" ) );
    BOOST_CHECK_NE( secondShared["board_hash"].get<std::string>(), firstHash );
}


BOOST_AUTO_TEST_CASE( ViaModeContextIncludesBoardViaSizesAndCursor )
{
    BOARD board;
    board.GetDesignSettings().UseCustomTrackViaSize( true );
    board.GetDesignSettings().SetCustomViaSize( 700000 );
    board.GetDesignSettings().SetCustomViaDrill( 330000 );

    TOOL_MANAGER manager;
    manager.SetEnvironment( &board, nullptr, nullptr, nullptr, nullptr );

    KISURF_AI_PCB_TOOL_STATE_PROVIDER provider( &manager );

    TOOL_EVENT event( TC_COMMAND, TA_ACTIVATE, "pcbnew.InteractiveDrawing.via" );
    event.SetMousePosition( VECTOR2D( 71, 82 ) );
    manager.ProcessEvent( event );

    AI_TOOL_STATE_SNAPSHOT snapshot = provider.BuildToolState( AI_CONTEXT_VERSION() );

    BOOST_REQUIRE( !snapshot.m_SharedContextJson.IsEmpty() );
    BOOST_REQUIRE( !snapshot.m_ModeContextJson.IsEmpty() );

    nlohmann::json shared = parseJson( snapshot.m_SharedContextJson );
    nlohmann::json mode = parseJson( snapshot.m_ModeContextJson );

    BOOST_CHECK_EQUAL( shared["via_diameter"].get<int>(), 700000 );
    BOOST_CHECK_EQUAL( shared["via_drill"].get<int>(), 330000 );
    BOOST_CHECK_EQUAL( mode["diameter"].get<int>(), 700000 );
    BOOST_CHECK_EQUAL( mode["drill"].get<int>(), 330000 );
    BOOST_REQUIRE( mode.contains( "net" ) );
    BOOST_CHECK_EQUAL( mode["net"].get<std::string>(), "" );
    BOOST_REQUIRE( mode.contains( "layer_pair" ) );
    BOOST_CHECK_EQUAL( mode["layer_pair"]["start"].get<std::string>(), "F.Cu" );
    BOOST_CHECK_EQUAL( mode["layer_pair"]["end"].get<std::string>(), "B.Cu" );
    BOOST_CHECK_EQUAL( mode["cursor"]["x"].get<int>(), 71 );
    BOOST_CHECK_EQUAL( mode["cursor"]["y"].get<int>(), 82 );
}


BOOST_AUTO_TEST_CASE( SkipsRoutingSizesWhenBoardNetSettingsAreUnavailable )
{
    BOARD board;
    board.GetDesignSettings().m_NetSettings.reset();

    TOOL_MANAGER manager;
    manager.SetEnvironment( &board, nullptr, nullptr, nullptr, nullptr );

    KISURF_AI_PCB_TOOL_STATE_PROVIDER provider( &manager );

    TOOL_EVENT event( TC_COMMAND, TA_ACTIVATE, "pcbnew.InteractiveRouter.SingleTrack" );
    event.SetMousePosition( VECTOR2D( 22, 33 ) );
    manager.ProcessEvent( event );

    AI_TOOL_STATE_SNAPSHOT snapshot = provider.BuildToolState( AI_CONTEXT_VERSION() );

    BOOST_REQUIRE( !snapshot.m_SharedContextJson.IsEmpty() );
    BOOST_REQUIRE( !snapshot.m_ModeContextJson.IsEmpty() );

    nlohmann::json shared = parseJson( snapshot.m_SharedContextJson );
    nlohmann::json mode = parseJson( snapshot.m_ModeContextJson );

    BOOST_CHECK_EQUAL( shared["active_layer"].get<std::string>(), "F.Cu" );
    BOOST_CHECK( shared.contains( "board_hash" ) );
    BOOST_CHECK( !shared.contains( "track_width" ) );
    BOOST_CHECK( !shared.contains( "via_diameter" ) );
    BOOST_CHECK( !shared.contains( "via_drill" ) );

    BOOST_CHECK_EQUAL( mode["mode"].get<std::string>(), "routing_track" );
    BOOST_CHECK_EQUAL( mode["layer"].get<std::string>(), "F.Cu" );
    BOOST_CHECK( !mode.contains( "width" ) );
    BOOST_CHECK_EQUAL( mode["cursor"]["x"].get<int>(), 22 );
    BOOST_CHECK_EQUAL( mode["cursor"]["y"].get<int>(), 33 );
}


BOOST_AUTO_TEST_SUITE_END()
