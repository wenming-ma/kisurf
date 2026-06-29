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

#include <kisurf/ai/ai_accept_applier.h>
#include <kisurf/ai/ai_atomic_operation_executor.h>
#include <kisurf/ai/ai_execution_session.h>
#include <kisurf/ai/ai_shadow_board.h>
#include <kisurf_ai_pcb_session_apply_adapter.h>
#include <kisurf_ai_pcb_session_shadow_seeder.h>
#include <pcbnew_utils/board_file_utils.h>

#include <board.h>
#include <json_common.h>
#include <netinfo.h>
#include <pcb_track.h>
#include <tool/tool_manager.h>

#include <filesystem>
#include <nlohmann/json.hpp>

namespace
{

std::string demoBoardPath( const std::string& aRelativePath )
{
    const std::filesystem::path sourceFile = __FILE__;
    const std::filesystem::path repoRoot =
            sourceFile.parent_path().parent_path().parent_path().parent_path();

    return ( repoRoot / "demos" / aRelativePath )
            .string();
}


AI_EXECUTION_SESSION makeActualProjectSession()
{
    AI_EXECUTION_SESSION::OPEN_OPTIONS options;
    options.m_SessionId = 9301;
    options.m_BoardId = wxS( "demos/video/video.kicad_pcb" );
    options.m_BaseHash = wxS( "actual-project-demo-hash" );
    options.m_EditorKind = AI_EDITOR_KIND::Pcb;
    return AI_EXECUTION_SESSION( std::move( options ) );
}


nlohmann::json handleJson( const AI_SESSION_HANDLE& aHandle )
{
    return {
        { "session_id", aHandle.m_SessionId },
        { "handle_id", aHandle.m_HandleId },
        { "generation", aHandle.m_Generation }
    };
}


std::vector<PCB_VIA*> boardVias( BOARD& aBoard )
{
    std::vector<PCB_VIA*> vias;

    for( PCB_TRACK* track : aBoard.Tracks() )
    {
        if( PCB_VIA* via = dynamic_cast<PCB_VIA*>( track ) )
            vias.push_back( via );
    }

    return vias;
}


AI_ATOMIC_EXECUTION_RESULT executeJson( AI_EXECUTION_SESSION& aSession,
                                        AI_SESSION_OPERATION_KIND aKind,
                                        const nlohmann::json& aArgs )
{
    return AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            aSession, aKind, wxString::FromUTF8( aArgs.dump().c_str() ) );
}

} // namespace


BOOST_AUTO_TEST_SUITE( AiPcbActualProjectE2E )


BOOST_AUTO_TEST_CASE( DemoBoardSessionCoversQueryCreateUpdateDeleteAndAccept )
{
    std::unique_ptr<BOARD> board = KI_TEST::ReadBoardFromFileOrStream(
            demoBoardPath( "video/video.kicad_pcb" ) );
    BOOST_REQUIRE( board );

    board->BuildListOfNets();
    board->BuildConnectivity();

    NETINFO_ITEM* gnd = board->FindNet( wxS( "GND" ) );
    BOOST_REQUIRE( gnd );

    TOOL_MANAGER toolManager;
    toolManager.SetEnvironment( board.get(), nullptr, nullptr, nullptr, nullptr );

    AI_EXECUTION_SESSION session = makeActualProjectSession();
    KISURF_AI_PCB_SESSION_SHADOW_SEEDER seeder( *board );
    seeder.Seed( session );

    std::vector<AI_SHADOW_ITEM> seededTracks =
            session.ShadowBoard().QueryItems( wxS( "{\"type\":\"track_segment\"}" ) );
    BOOST_REQUIRE( !seededTracks.empty() );

    const nlohmann::json trackGeometry = nlohmann::json::parse(
            seededTracks.front().m_GeometryJson.ToStdString() );
    BOOST_REQUIRE( trackGeometry.contains( "end" ) );
    BOOST_REQUIRE( trackGeometry["end"].contains( "x" ) );
    BOOST_REQUIRE( trackGeometry["end"].contains( "y" ) );

    const VECTOR2I originalTrackEnd( trackGeometry["end"]["x"].get<int>(),
                                     trackGeometry["end"]["y"].get<int>() );
    const VECTOR2I patchedTrackEnd = originalTrackEnd + VECTOR2I( 100000, 50000 );

    const size_t initialViaCount = boardVias( *board ).size();
    const uint64_t stepId = session.BeginStep(
            wxS( "actual project CRUD session" ) );
    BOOST_REQUIRE_NE( stepId, 0 );

    std::vector<AI_SHADOW_ITEM> queriedTracks =
            session.ShadowBoard().QueryItems( wxS( "{\"type\":\"track_segment\"}" ) );
    BOOST_CHECK_EQUAL( queriedTracks.size(), seededTracks.size() );

    AI_ATOMIC_EXECUTION_RESULT createResult =
            executeJson( session, AI_SESSION_OPERATION_KIND::CreateVia,
                         { { "alias", "actual-project-via" },
                           { "net", "GND" },
                           { "diameter", 600000 },
                           { "drill", 300000 },
                           { "position", { { "x", 25000000 }, { "y", 20000000 } } },
                           { "metadata", { { "source", "actual_project_e2e" } } } } );
    BOOST_REQUIRE( createResult.m_Ok );

    AI_ATOMIC_EXECUTION_RESULT updateResult =
            executeJson( session, AI_SESSION_OPERATION_KIND::UpdateItemGeometry,
                         { { "handle", handleJson( seededTracks.front().m_Handle ) },
                           { "geometry_patch",
                             { { "end",
                                 { { "x", patchedTrackEnd.x },
                                   { "y", patchedTrackEnd.y } } } } } } );
    BOOST_REQUIRE( updateResult.m_Ok );

    AI_ATOMIC_EXECUTION_RESULT deleteResult =
            executeJson( session, AI_SESSION_OPERATION_KIND::DeleteItems,
                         { { "handles", { "actual-project-via" } } } );
    BOOST_REQUIRE( deleteResult.m_Ok );

    session.EndStep( stepId );

    KISURF_AI_PCB_SESSION_APPLY_ADAPTER adapter( *board, toolManager );
    AI_ACCEPT_APPLY_RESULT accept =
            AI_ACCEPT_APPLIER::Apply( session, wxS( "actual-project-demo-hash" ),
                                      session.ContextVersion(), adapter );
    BOOST_REQUIRE( accept.m_Ok );
    BOOST_CHECK( accept.m_BoardMutated );
    BOOST_CHECK_EQUAL( boardVias( *board ).size(), initialViaCount );

    std::vector<AI_SHADOW_ITEM> postItems =
            session.ShadowBoard().QueryItems( wxS( "{\"type\":\"track_segment\"}" ) );
    BOOST_REQUIRE( !postItems.empty() );
    BOOST_CHECK( session.Journal().Operations().size() >= 3 );
}


BOOST_AUTO_TEST_SUITE_END()
