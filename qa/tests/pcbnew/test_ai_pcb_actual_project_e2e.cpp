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
#include <kisurf/ai/ai_session_tool_call_handler.h>
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


std::string qaDataBoardPath( const std::string& aRelativePath )
{
    const std::filesystem::path sourceFile = __FILE__;
    const std::filesystem::path repoRoot =
            sourceFile.parent_path().parent_path().parent_path().parent_path();

    return ( repoRoot / "qa" / "data" / aRelativePath ).string();
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


size_t boardRoutingItemCount( BOARD& aBoard )
{
    size_t count = 0;

    for( PCB_TRACK* track : aBoard.Tracks() )
    {
        wxUnusedVar( track );
        ++count;
    }

    return count;
}


size_t boardTrackSegmentCount( BOARD& aBoard )
{
    size_t count = 0;

    for( PCB_TRACK* track : aBoard.Tracks() )
    {
        if( !dynamic_cast<PCB_VIA*>( track ) )
            ++count;
    }

    return count;
}


size_t boardNamedNetCount( BOARD& aBoard )
{
    size_t count = 0;

    for( NETINFO_ITEM* net : aBoard.GetNetInfo() )
    {
        if( net && !net->GetNetname().IsEmpty() )
            ++count;
    }

    return count;
}


class FAILING_SHADOW_SEEDER : public AI_SESSION_SHADOW_BOARD_SEEDER
{
public:
    void Seed( AI_EXECUTION_SESSION& aSession ) override
    {
        wxUnusedVar( aSession );
        BOOST_FAIL( "Chat direct current-board tools must not seed a shadow board." );
    }
};


AI_ATOMIC_EXECUTION_RESULT executeJson( AI_EXECUTION_SESSION& aSession,
                                        AI_SESSION_OPERATION_KIND aKind,
                                        const nlohmann::json& aArgs )
{
    return AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            aSession, aKind, wxString::FromUTF8( aArgs.dump().c_str() ) );
}


AI_PROVIDER_REQUEST liveToolRequest()
{
    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 9401;
    request.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_ContextVersion.m_DocumentRevision = 21;
    request.m_ContextVersion.m_SelectionRevision = 3;
    request.m_ContextVersion.m_ViewRevision = 5;
    request.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_ContextSnapshot.m_Version = request.m_ContextVersion;
    return request;
}


AI_TOOL_CALL_RECORD liveToolCall( const wxString& aToolName,
                                  const nlohmann::json& aArguments )
{
    static uint64_t callCounter = 0;

    AI_TOOL_CALL_RECORD call;
    call.m_RequestId = 9401;
    call.m_ToolCallId = wxString::Format(
            wxS( "actual_project_live_%llu" ),
            static_cast<unsigned long long>( ++callCounter ) );
    call.m_ToolName = aToolName;
    call.m_ArgumentsJson = wxString::FromUTF8( aArguments.dump().c_str() );
    return call;
}


nlohmann::json resultPayload( const AI_TOOL_INVOCATION_RESULT& aResult )
{
    return nlohmann::json::parse( aResult.m_ResultJson.ToStdString() );
}


bool viaAtPosition( const BOARD& aBoard, const VECTOR2I& aPosition )
{
    for( PCB_TRACK* track : aBoard.Tracks() )
    {
        if( PCB_VIA* via = dynamic_cast<PCB_VIA*>( track ) )
        {
            if( via->GetPosition() == aPosition )
                return true;
        }
    }

    return false;
}


std::optional<nlohmann::json> findViaItemAtPosition(
        const nlohmann::json& aItemsPayload,
        const VECTOR2I& aPosition )
{
    if( !aItemsPayload.contains( "items" ) || !aItemsPayload["items"].is_array() )
        return std::nullopt;

    for( const nlohmann::json& item : aItemsPayload["items"] )
    {
        const nlohmann::json& geometry = item["geometry"];

        if( geometry.contains( "position" )
            && geometry["position"].value( "x", 0 ) == aPosition.x
            && geometry["position"].value( "y", 0 ) == aPosition.y )
        {
            return item;
        }
    }

    return std::nullopt;
}


nlohmann::json itemHandlesFromPayload( const nlohmann::json& aItemsPayload )
{
    nlohmann::json handles = nlohmann::json::array();

    if( !aItemsPayload.contains( "items" ) || !aItemsPayload["items"].is_array() )
        return handles;

    for( const nlohmann::json& item : aItemsPayload["items"] )
    {
        if( item.contains( "handle" ) )
            handles.push_back( item["handle"] );
    }

    return handles;
}

} // namespace


BOOST_AUTO_TEST_SUITE( AiPcbActualProjectE2E )


BOOST_AUTO_TEST_CASE( PicProgrammerSeederMatchesLoadedBoardTrackAndViaCounts )
{
    std::unique_ptr<BOARD> board = KI_TEST::ReadBoardFromFileOrStream(
            demoBoardPath( "pic_programmer/pic_programmer.kicad_pcb" ) );
    BOOST_REQUIRE( board );

    board->BuildListOfNets();
    board->BuildConnectivity();

    AI_EXECUTION_SESSION session = makeActualProjectSession();
    KISURF_AI_PCB_SESSION_SHADOW_SEEDER seeder(
            *board,
            KISURF_AI_PCB_SESSION_SHADOW_SEEDER::SEED_OPTIONS{ false } );
    seeder.Seed( session );

    nlohmann::json summary =
            nlohmann::json::parse( session.ShadowBoard().QueryBoardSummary().ToStdString() );
    BOOST_CHECK_EQUAL( summary["track_segments"].get<size_t>(), 370 );
    BOOST_CHECK_EQUAL( summary["vias"].get<size_t>(), 6 );
}


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


BOOST_AUTO_TEST_CASE( DemoBoardChatDirectLiveToolsCoverCreateQueryMoveDelete )
{
    std::unique_ptr<BOARD> board = KI_TEST::ReadBoardFromFileOrStream(
            demoBoardPath( "video/video.kicad_pcb" ) );
    BOOST_REQUIRE( board );

    board->BuildListOfNets();
    board->BuildConnectivity();

    BOOST_REQUIRE( board->FindNet( wxS( "GND" ) ) );

    TOOL_MANAGER toolManager;
    toolManager.SetEnvironment( board.get(), nullptr, nullptr, nullptr, nullptr );

    KISURF_AI_PCB_SESSION_APPLY_ADAPTER adapter( *board, toolManager );
    KISURF_AI_PCB_SESSION_SHADOW_SEEDER seeder( *board );
    AI_SESSION_TOOL_CALL_HANDLER handler( nullptr, &adapter, nullptr, &seeder );
    handler.SetDirectLiveApplyAfterMutation( true );

    AI_PROVIDER_REQUEST request = liveToolRequest();
    const size_t initialViaCount = boardVias( *board ).size();
    const VECTOR2I createdPosition( 25000000, 20000000 );
    const VECTOR2I movedPosition = createdPosition + VECTOR2I( 500000, -250000 );

    AI_TOOL_INVOCATION_RESULT trackAliasQuery = handler.HandleToolCall(
            request,
            liveToolCall( wxS( "kisurf_query_items" ),
                          { { "filter", { { "type", "tracks" } } } } ) );
    BOOST_REQUIRE_MESSAGE( trackAliasQuery.m_Allowed,
                           trackAliasQuery.m_ResultJson.ToStdString() );
    nlohmann::json trackAliasPayload = resultPayload( trackAliasQuery );
    BOOST_CHECK_GT( trackAliasPayload["returned_count"].get<size_t>(), 0 );
    BOOST_CHECK_EQUAL( trackAliasPayload["items"][0]["type"].get<std::string>(),
                       "track_segment" );

    AI_TOOL_INVOCATION_RESULT createResult = handler.HandleToolCall(
            request,
            liveToolCall(
                    wxS( "kisurf_run_atomic_operation" ),
                    { { "kind", "pcb.create_via" },
                      { "arguments",
                        { { "alias", "chat-direct-e2e-via" },
                          { "net", "GND" },
                          { "diameter", 600000 },
                          { "drill", 300000 },
                          { "position",
                            { { "x", createdPosition.x },
                              { "y", createdPosition.y } } } } } } ) );

    BOOST_REQUIRE_MESSAGE( createResult.m_Allowed,
                           createResult.m_ResultJson.ToStdString() );
    BOOST_CHECK( createResult.m_Executed );
    BOOST_CHECK( !handler.ActiveSession() );
    BOOST_CHECK_EQUAL( boardVias( *board ).size(), initialViaCount + 1 );
    BOOST_CHECK( viaAtPosition( *board, createdPosition ) );

    nlohmann::json createPayload = resultPayload( createResult );
    BOOST_CHECK( createPayload["board_mutated"].get<bool>() );
    BOOST_CHECK( !createPayload.contains( "shadow_board_mutated" ) );
    BOOST_CHECK_EQUAL( createPayload["current_board_apply"]["status"].get<std::string>(),
                       "applied" );
    BOOST_CHECK_EQUAL( createPayload.dump().find( "session" ), std::string::npos );
    BOOST_CHECK_EQUAL( createPayload.dump().find( "shadow" ), std::string::npos );

    AI_TOOL_INVOCATION_RESULT queryCreated = handler.HandleToolCall(
            request,
            liveToolCall( wxS( "kisurf_query_items" ),
                          { { "filter", { { "type", "via" } } } } ) );
    BOOST_REQUIRE_MESSAGE( queryCreated.m_Allowed,
                           queryCreated.m_ResultJson.ToStdString() );

    std::optional<nlohmann::json> createdItem =
            findViaItemAtPosition( resultPayload( queryCreated ), createdPosition );
    BOOST_REQUIRE( createdItem.has_value() );

    AI_TOOL_INVOCATION_RESULT moveResult = handler.HandleToolCall(
            request,
            liveToolCall(
                    wxS( "kisurf_run_atomic_operation" ),
                    { { "kind", "pcb.move_items" },
                      { "arguments",
                        { { "handles", nlohmann::json::array(
                                                { ( *createdItem )["handle"] } ) },
                          { "delta", { { "x", 500000 }, { "y", -250000 } } } } } } ) );

    BOOST_REQUIRE_MESSAGE( moveResult.m_Allowed,
                           moveResult.m_ResultJson.ToStdString() );
    BOOST_CHECK( moveResult.m_Executed );
    BOOST_CHECK( !handler.ActiveSession() );
    BOOST_CHECK( !viaAtPosition( *board, createdPosition ) );
    BOOST_CHECK( viaAtPosition( *board, movedPosition ) );

    AI_TOOL_INVOCATION_RESULT queryMoved = handler.HandleToolCall(
            request,
            liveToolCall( wxS( "kisurf_query_items" ),
                          { { "filter", { { "type", "via" } } } } ) );
    BOOST_REQUIRE_MESSAGE( queryMoved.m_Allowed,
                           queryMoved.m_ResultJson.ToStdString() );

    std::optional<nlohmann::json> movedItem =
            findViaItemAtPosition( resultPayload( queryMoved ), movedPosition );
    BOOST_REQUIRE( movedItem.has_value() );

    AI_TOOL_INVOCATION_RESULT deleteResult = handler.HandleToolCall(
            request,
            liveToolCall(
                    wxS( "kisurf_run_atomic_operation" ),
                    { { "kind", "pcb.delete_items" },
                      { "arguments",
                        { { "handles", nlohmann::json::array(
                                                { ( *movedItem )["handle"] } ) } } } } ) );

    BOOST_REQUIRE_MESSAGE( deleteResult.m_Allowed,
                           deleteResult.m_ResultJson.ToStdString() );
    BOOST_CHECK( deleteResult.m_Executed );
    BOOST_CHECK( !handler.ActiveSession() );
    BOOST_CHECK_EQUAL( boardVias( *board ).size(), initialViaCount );
    BOOST_CHECK( !viaAtPosition( *board, movedPosition ) );
}


BOOST_AUTO_TEST_CASE( PicProgrammerChatDirectLiveCanReuseCreatedAliasAcrossCalls )
{
    std::unique_ptr<BOARD> board = KI_TEST::ReadBoardFromFileOrStream(
            demoBoardPath( "pic_programmer/pic_programmer.kicad_pcb" ) );
    BOOST_REQUIRE( board );

    board->BuildListOfNets();
    board->BuildConnectivity();

    TOOL_MANAGER toolManager;
    toolManager.SetEnvironment( board.get(), nullptr, nullptr, nullptr, nullptr );

    KISURF_AI_PCB_SESSION_APPLY_ADAPTER adapter( *board, toolManager );
    KISURF_AI_PCB_SESSION_SHADOW_SEEDER seeder( *board );
    AI_SESSION_TOOL_CALL_HANDLER handler( nullptr, &adapter, nullptr, &seeder );
    handler.SetDirectLiveApplyAfterMutation( true );

    AI_PROVIDER_REQUEST request = liveToolRequest();
    const size_t        initialViaCount = boardVias( *board ).size();
    const VECTOR2I      createdPosition( 25000000, 20000000 );
    const VECTOR2I      movedPosition = createdPosition + VECTOR2I( 500000, -250000 );

    AI_TOOL_INVOCATION_RESULT createResult = handler.HandleToolCall(
            request,
            liveToolCall(
                    wxS( "kisurf_run_atomic_operation" ),
                    { { "kind", "pcb.create_via" },
                      { "arguments",
                        { { "alias", "crud-smoke-via" },
                          { "net", "GND" },
                          { "diameter", 600000 },
                          { "drill", 300000 },
                          { "position",
                            { { "x", createdPosition.x },
                              { "y", createdPosition.y } } } } } } ) );

    BOOST_REQUIRE_MESSAGE( createResult.m_Allowed,
                           createResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( boardVias( *board ).size(), initialViaCount + 1 );
    BOOST_CHECK( viaAtPosition( *board, createdPosition ) );

    AI_TOOL_INVOCATION_RESULT queryAlias = handler.HandleToolCall(
            request,
            liveToolCall( wxS( "kisurf_query_items" ),
                          { { "filter",
                              { { "type", "via" }, { "alias", "crud-smoke-via" } } } } ) );
    BOOST_REQUIRE_MESSAGE( queryAlias.m_Allowed,
                           queryAlias.m_ResultJson.ToStdString() );
    nlohmann::json aliasPayload = resultPayload( queryAlias );
    BOOST_REQUIRE_EQUAL( aliasPayload["returned_count"].get<size_t>(), 1 );
    BOOST_CHECK_EQUAL( aliasPayload["items"][0]["alias"].get<std::string>(),
                       "crud-smoke-via" );

    AI_TOOL_INVOCATION_RESULT moveByAlias = handler.HandleToolCall(
            request,
            liveToolCall(
                    wxS( "kisurf_run_atomic_operation" ),
                    { { "kind", "pcb.move_items" },
                      { "items", aliasPayload["items"][0]["handle"] },
                      { "arguments",
                        { { "delta_x", 500000 }, { "delta_y", -250000 } } } } ) );
    BOOST_REQUIRE_MESSAGE( moveByAlias.m_Allowed,
                           moveByAlias.m_ResultJson.ToStdString() );
    BOOST_CHECK( !handler.ActiveSession() );
    BOOST_CHECK( !viaAtPosition( *board, createdPosition ) );
    BOOST_CHECK( viaAtPosition( *board, movedPosition ) );

    AI_TOOL_INVOCATION_RESULT deleteByAlias = handler.HandleToolCall(
            request,
            liveToolCall( wxS( "kisurf_run_atomic_operation" ),
                          { { "kind", "pcb.delete_items" },
                            { "arguments",
                              { { "items", aliasPayload["items"][0]["handle"] } } } } ) );
    BOOST_REQUIRE_MESSAGE( deleteByAlias.m_Allowed,
                           deleteByAlias.m_ResultJson.ToStdString() );
    BOOST_CHECK( !handler.ActiveSession() );
    BOOST_CHECK_EQUAL( boardVias( *board ).size(), initialViaCount );
    BOOST_CHECK( !viaAtPosition( *board, movedPosition ) );
}


BOOST_AUTO_TEST_CASE( PicProgrammerChatDirectLiveResolvesRelativePointReference )
{
    std::unique_ptr<BOARD> board = KI_TEST::ReadBoardFromFileOrStream(
            demoBoardPath( "pic_programmer/pic_programmer.kicad_pcb" ) );
    BOOST_REQUIRE( board );

    board->BuildListOfNets();
    board->BuildConnectivity();

    TOOL_MANAGER toolManager;
    toolManager.SetEnvironment( board.get(), nullptr, nullptr, nullptr, nullptr );

    KISURF_AI_PCB_SESSION_APPLY_ADAPTER adapter( *board, toolManager );
    KISURF_AI_PCB_SESSION_SHADOW_SEEDER seeder( *board );
    AI_SESSION_TOOL_CALL_HANDLER handler( nullptr, &adapter, nullptr, &seeder );
    handler.SetDirectLiveApplyAfterMutation( true );

    AI_PROVIDER_REQUEST request = liveToolRequest();
    const size_t        initialViaCount = boardVias( *board ).size();
    const VECTOR2I      basePosition( 25000000, 20000000 );
    const VECTOR2I      relativePosition( 26000000, 19500000 );

    AI_TOOL_INVOCATION_RESULT createBase = handler.HandleToolCall(
            request,
            liveToolCall(
                    wxS( "kisurf_run_atomic_operation" ),
                    { { "kind", "pcb.create_via" },
                      { "arguments",
                        { { "alias", "relative-base-via" },
                          { "net", "GND" },
                          { "diameter", 0.6 },
                          { "drill", 0.3 },
                          { "position", nlohmann::json::array( { 25.0, 20.0 } ) } } } } ) );

    BOOST_REQUIRE_MESSAGE( createBase.m_Allowed,
                           createBase.m_ResultJson.ToStdString() );
    BOOST_CHECK( createBase.m_Executed );
    BOOST_CHECK( !handler.ActiveSession() );
    BOOST_CHECK( viaAtPosition( *board, basePosition ) );

    AI_TOOL_INVOCATION_RESULT createRelative = handler.HandleToolCall(
            request,
            liveToolCall(
                    wxS( "kisurf_run_atomic_operation" ),
                    { { "kind", "pcb.create_via" },
                      { "arguments",
                        { { "alias", "relative-offset-via" },
                          { "net", "GND" },
                          { "diameter", 0.6 },
                          { "drill", 0.3 },
                          { "position",
                            { { "relative_to", "relative-base-via" },
                              { "anchor", "center" },
                              { "offset",
                                { { "x", 1.0 }, { "y", -0.5 } } } } } } } } ) );

    BOOST_REQUIRE_MESSAGE( createRelative.m_Allowed,
                           createRelative.m_ResultJson.ToStdString() );
    BOOST_CHECK( createRelative.m_Executed );
    BOOST_CHECK( !handler.ActiveSession() );
    BOOST_CHECK_EQUAL( boardVias( *board ).size(), initialViaCount + 2 );
    BOOST_CHECK( viaAtPosition( *board, relativePosition ) );
}


BOOST_AUTO_TEST_CASE( PicProgrammerChatDirectLiveCanMoveCreatedAliasWithPositionShortcut )
{
    std::unique_ptr<BOARD> board = KI_TEST::ReadBoardFromFileOrStream(
            demoBoardPath( "pic_programmer/pic_programmer.kicad_pcb" ) );
    BOOST_REQUIRE( board );

    board->BuildListOfNets();
    board->BuildConnectivity();

    TOOL_MANAGER toolManager;
    toolManager.SetEnvironment( board.get(), nullptr, nullptr, nullptr, nullptr );

    KISURF_AI_PCB_SESSION_APPLY_ADAPTER adapter( *board, toolManager );
    KISURF_AI_PCB_SESSION_SHADOW_SEEDER seeder( *board );
    AI_SESSION_TOOL_CALL_HANDLER handler( nullptr, &adapter, nullptr, &seeder );
    handler.SetDirectLiveApplyAfterMutation( true );

    AI_PROVIDER_REQUEST request = liveToolRequest();
    const size_t        initialViaCount = boardVias( *board ).size();
    const VECTOR2I      createdPosition( 25000000, 20000000 );
    const VECTOR2I      movedPosition( 25500000, 19750000 );

    AI_TOOL_INVOCATION_RESULT createResult = handler.HandleToolCall(
            request,
            liveToolCall(
                    wxS( "kisurf_run_atomic_operation" ),
                    { { "kind", "pcb.create_via" },
                      { "arguments",
                        { { "alias", "crud-position-shortcut-via" },
                          { "net", "GND" },
                          { "diameter", 600000 },
                          { "drill", 300000 },
                          { "position",
                            { { "x", createdPosition.x },
                              { "y", createdPosition.y } } } } } } ) );

    BOOST_REQUIRE_MESSAGE( createResult.m_Allowed,
                           createResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( boardVias( *board ).size(), initialViaCount + 1 );
    BOOST_CHECK( viaAtPosition( *board, createdPosition ) );

    AI_TOOL_INVOCATION_RESULT moveResult = handler.HandleToolCall(
            request,
            liveToolCall(
                    wxS( "kisurf_run_atomic_operation" ),
                    { { "kind", "pcb.move_items" },
                      { "arguments",
                        { { "items", { { "alias", "crud-position-shortcut-via" } } },
                          { "position", nlohmann::json::array( { 25.5, 19.75 } ) } } } } ) );

    BOOST_REQUIRE_MESSAGE( moveResult.m_Allowed,
                           moveResult.m_ResultJson.ToStdString() );
    BOOST_CHECK( !handler.ActiveSession() );
    BOOST_CHECK( !viaAtPosition( *board, createdPosition ) );
    BOOST_CHECK( viaAtPosition( *board, movedPosition ) );

    AI_TOOL_INVOCATION_RESULT deleteResult = handler.HandleToolCall(
            request,
            liveToolCall( wxS( "kisurf_run_atomic_operation" ),
                          { { "kind", "pcb.delete_items" },
                            { "arguments",
                              { { "items",
                                  { { "alias", "crud-position-shortcut-via" } } } } } } ) );

    BOOST_REQUIRE_MESSAGE( deleteResult.m_Allowed,
                           deleteResult.m_ResultJson.ToStdString() );
    BOOST_CHECK( !handler.ActiveSession() );
    BOOST_CHECK_EQUAL( boardVias( *board ).size(), initialViaCount );
    BOOST_CHECK( !viaAtPosition( *board, movedPosition ) );
}


BOOST_AUTO_TEST_CASE( PicProgrammerChatDirectLiveCanMovePerItemHandleWithPosition )
{
    std::unique_ptr<BOARD> board = KI_TEST::ReadBoardFromFileOrStream(
            demoBoardPath( "pic_programmer/pic_programmer.kicad_pcb" ) );
    BOOST_REQUIRE( board );

    board->BuildListOfNets();
    board->BuildConnectivity();

    TOOL_MANAGER toolManager;
    toolManager.SetEnvironment( board.get(), nullptr, nullptr, nullptr, nullptr );

    KISURF_AI_PCB_SESSION_APPLY_ADAPTER adapter( *board, toolManager );
    KISURF_AI_PCB_SESSION_SHADOW_SEEDER seeder( *board );
    AI_SESSION_TOOL_CALL_HANDLER handler( nullptr, &adapter, nullptr, &seeder );
    handler.SetDirectLiveApplyAfterMutation( true );

    AI_PROVIDER_REQUEST request = liveToolRequest();
    const size_t        initialViaCount = boardVias( *board ).size();
    const VECTOR2I      createdPosition( 25000000, 20000000 );
    const VECTOR2I      movedPosition( 25500000, 19750000 );

    AI_TOOL_INVOCATION_RESULT createResult = handler.HandleToolCall(
            request,
            liveToolCall(
                    wxS( "kisurf_run_atomic_operation" ),
                    { { "kind", "pcb.create_via" },
                      { "arguments",
                        { { "alias", "crud-per-item-position-via" },
                          { "net", "GND" },
                          { "diameter", 0.6 },
                          { "drill", 0.3 },
                          { "position",
                            { { "x", 25.0 }, { "y", 20.0 } } } } } } ) );

    BOOST_REQUIRE_MESSAGE( createResult.m_Allowed,
                           createResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( boardVias( *board ).size(), initialViaCount + 1 );
    BOOST_CHECK( viaAtPosition( *board, createdPosition ) );

    AI_TOOL_INVOCATION_RESULT queryResult = handler.HandleToolCall(
            request,
            liveToolCall( wxS( "kisurf_query_items" ),
                          { { "filter",
                              { { "type", "via" },
                                { "alias", "crud-per-item-position-via" } } } } ) );

    BOOST_REQUIRE_MESSAGE( queryResult.m_Allowed,
                           queryResult.m_ResultJson.ToStdString() );
    nlohmann::json queryPayload = resultPayload( queryResult );
    BOOST_REQUIRE_EQUAL( queryPayload["returned_count"].get<size_t>(), 1 );

    AI_TOOL_INVOCATION_RESULT moveResult = handler.HandleToolCall(
            request,
            liveToolCall(
                    wxS( "kisurf_run_atomic_operation" ),
                    { { "kind", "pcb.move_items" },
                      { "arguments",
                        { { "items",
                            nlohmann::json::array(
                                    { { { "handle",
                                          queryPayload["items"][0]["handle"] },
                                        { "position",
                                          { { "x", 25.5 }, { "y", 19.75 } } } } } ) } } } } ) );

    BOOST_REQUIRE_MESSAGE( moveResult.m_Allowed,
                           moveResult.m_ResultJson.ToStdString() );
    BOOST_CHECK( !handler.ActiveSession() );
    BOOST_CHECK( !viaAtPosition( *board, createdPosition ) );
    BOOST_CHECK( viaAtPosition( *board, movedPosition ) );

    AI_TOOL_INVOCATION_RESULT deleteResult = handler.HandleToolCall(
            request,
            liveToolCall( wxS( "kisurf_run_atomic_operation" ),
                          { { "kind", "pcb.delete_items" },
                            { "arguments",
                              { { "items",
                                  { { "alias", "crud-per-item-position-via" } } } } } } ) );

    BOOST_REQUIRE_MESSAGE( deleteResult.m_Allowed,
                           deleteResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( boardVias( *board ).size(), initialViaCount );
}


BOOST_AUTO_TEST_CASE( PicProgrammerChatDirectLiveCanDeleteAllQueriedTracks )
{
    std::unique_ptr<BOARD> board = KI_TEST::ReadBoardFromFileOrStream(
            demoBoardPath( "pic_programmer/pic_programmer.kicad_pcb" ) );
    BOOST_REQUIRE( board );

    board->BuildListOfNets();
    board->BuildConnectivity();

    TOOL_MANAGER toolManager;
    toolManager.SetEnvironment( board.get(), nullptr, nullptr, nullptr, nullptr );

    KISURF_AI_PCB_SESSION_APPLY_ADAPTER adapter( *board, toolManager );
    KISURF_AI_PCB_SESSION_SHADOW_SEEDER seeder( *board );
    AI_SESSION_TOOL_CALL_HANDLER handler( nullptr, &adapter, nullptr, &seeder );
    handler.SetDirectLiveApplyAfterMutation( true );

    AI_PROVIDER_REQUEST request = liveToolRequest();
    const size_t        initialTrackCount = boardTrackSegmentCount( *board );
    BOOST_REQUIRE_GT( initialTrackCount, 0 );

    AI_TOOL_INVOCATION_RESULT queryTracks = handler.HandleToolCall(
            request,
            liveToolCall( wxS( "kisurf_query_items" ),
                          { { "filter", { { "type", "tracks" } } } } ) );
    BOOST_REQUIRE_MESSAGE( queryTracks.m_Allowed,
                           queryTracks.m_ResultJson.ToStdString() );

    nlohmann::json queryPayload = resultPayload( queryTracks );
    BOOST_REQUIRE_EQUAL( queryPayload["returned_count"].get<size_t>(),
                         initialTrackCount );
    BOOST_REQUIRE( queryPayload["items"].is_array() );

    nlohmann::json handles = nlohmann::json::array();

    for( const nlohmann::json& item : queryPayload["items"] )
        handles.push_back( item["handle"] );

    AI_TOOL_INVOCATION_RESULT deleteTracks = handler.HandleToolCall(
            request,
            liveToolCall( wxS( "kisurf_run_atomic_operation" ),
                          { { "kind", "pcb.delete_items" },
                            { "arguments", { { "handles", std::move( handles ) } } } } ) );

    BOOST_REQUIRE_MESSAGE( deleteTracks.m_Allowed,
                           deleteTracks.m_ResultJson.ToStdString() );
    BOOST_CHECK( deleteTracks.m_Executed );
    BOOST_CHECK( !handler.ActiveSession() );
    BOOST_CHECK_EQUAL( boardTrackSegmentCount( *board ), 0 );

    nlohmann::json deletePayload = resultPayload( deleteTracks );
    BOOST_CHECK( deletePayload["board_mutated"].get<bool>() );
    BOOST_CHECK_EQUAL( deletePayload["current_board_apply"]["status"].get<std::string>(),
                       "applied" );
    BOOST_CHECK_EQUAL(
            deletePayload["current_board_apply"]["applied_operation_count"].get<size_t>(),
            1 );
    BOOST_CHECK_EQUAL( deletePayload.dump().find( "session" ), std::string::npos );
    BOOST_CHECK_EQUAL( deletePayload.dump().find( "shadow" ), std::string::npos );
}


BOOST_AUTO_TEST_CASE( PicProgrammerChatDirectLiveSummaryMatchesLoadedBoardCounts )
{
    std::unique_ptr<BOARD> board = KI_TEST::ReadBoardFromFileOrStream(
            demoBoardPath( "pic_programmer/pic_programmer.kicad_pcb" ) );
    BOOST_REQUIRE( board );

    board->BuildListOfNets();
    board->BuildConnectivity();

    KISURF_AI_PCB_SESSION_APPLY_ADAPTER adapter( *board );
    KISURF_AI_PCB_SESSION_SHADOW_SEEDER seeder( *board );
    AI_SESSION_TOOL_CALL_HANDLER handler( nullptr, &adapter, nullptr, &seeder );
    handler.SetDirectLiveApplyAfterMutation( true );

    AI_PROVIDER_REQUEST request = liveToolRequest();
    AI_TOOL_INVOCATION_RESULT summaryResult = handler.HandleToolCall(
            request,
            liveToolCall( wxS( "kisurf_query_board_summary" ), nlohmann::json::object() ) );
    BOOST_REQUIRE_MESSAGE( summaryResult.m_Allowed,
                           summaryResult.m_ResultJson.ToStdString() );

    nlohmann::json summaryPayload = resultPayload( summaryResult );
    const nlohmann::json& summary = summaryPayload["summary"];
    BOOST_CHECK_EQUAL( summary["pads"].get<size_t>(), board->GetPads().size() );
    BOOST_CHECK_EQUAL( summary["vias"].get<size_t>(), boardVias( *board ).size() );
    BOOST_CHECK_EQUAL( summary["track_segments"].get<size_t>(),
                       boardTrackSegmentCount( *board ) );
    BOOST_REQUIRE( summary.contains( "nets" ) );
    BOOST_REQUIRE( summary["nets"].is_number() );
    BOOST_CHECK_EQUAL( static_cast<size_t>( summary["nets"].get<double>() ),
                       boardNamedNetCount( *board ) );

    AI_TOOL_INVOCATION_RESULT netsResult = handler.HandleToolCall(
            request,
            liveToolCall( wxS( "kisurf_query_nets" ), nlohmann::json::object() ) );
    BOOST_REQUIRE_MESSAGE( netsResult.m_Allowed,
                           netsResult.m_ResultJson.ToStdString() );

    nlohmann::json netsPayload = resultPayload( netsResult );
    BOOST_CHECK_EQUAL( netsPayload["nets"].size(), boardNamedNetCount( *board ) );
}


BOOST_AUTO_TEST_CASE( PnsPicProgrammerChatDirectSummaryMatchesLoadedBoardCounts )
{
    std::unique_ptr<BOARD> board = KI_TEST::ReadBoardFromFileOrStream(
            qaDataBoardPath( "pcbnew/pns_regressions/boards/pic_programmer.kicad_pcb" ) );
    BOOST_REQUIRE( board );

    board->BuildListOfNets();
    board->BuildConnectivity();

    BOOST_CHECK_EQUAL( boardTrackSegmentCount( *board ), 370 );
    BOOST_CHECK_EQUAL( boardVias( *board ).size(), 6 );

    KISURF_AI_PCB_SESSION_APPLY_ADAPTER adapter( *board );
    KISURF_AI_PCB_SESSION_SHADOW_SEEDER seeder( *board );
    AI_SESSION_TOOL_CALL_HANDLER handler( nullptr, &adapter, nullptr, &seeder );
    handler.SetDirectLiveApplyAfterMutation( true );

    AI_PROVIDER_REQUEST request = liveToolRequest();
    AI_TOOL_INVOCATION_RESULT summary = handler.HandleToolCall(
            request,
            liveToolCall( wxS( "kisurf_query_board_summary" ), nlohmann::json::object() ) );

    BOOST_REQUIRE_MESSAGE( summary.m_Allowed, summary.m_ResultJson.ToStdString() );
    nlohmann::json summaryPayload = resultPayload( summary );
    const nlohmann::json& summaryJson = summaryPayload["summary"];
    BOOST_CHECK_EQUAL( summaryJson["track_segments"].get<size_t>(),
                       boardTrackSegmentCount( *board ) );
    BOOST_CHECK_EQUAL( summaryJson["vias"].get<size_t>(),
                       boardVias( *board ).size() );
}


BOOST_AUTO_TEST_CASE( PicProgrammerChatDirectLiveCanDeleteAllRoutingObjects )
{
    std::unique_ptr<BOARD> board = KI_TEST::ReadBoardFromFileOrStream(
            demoBoardPath( "pic_programmer/pic_programmer.kicad_pcb" ) );
    BOOST_REQUIRE( board );

    board->BuildListOfNets();
    board->BuildConnectivity();

    BOOST_REQUIRE_GT( boardTrackSegmentCount( *board ), 0 );
    BOOST_REQUIRE_GT( boardVias( *board ).size(), 0 );
    const size_t initialRoutingCount = boardRoutingItemCount( *board );
    BOOST_REQUIRE_GT( initialRoutingCount, 0 );

    KISURF_AI_PCB_SESSION_APPLY_ADAPTER adapter( *board );
    KISURF_AI_PCB_SESSION_SHADOW_SEEDER seeder( *board );
    AI_SESSION_TOOL_CALL_HANDLER handler( nullptr, &adapter, nullptr, &seeder );
    handler.SetDirectLiveApplyAfterMutation( true );

    AI_PROVIDER_REQUEST request = liveToolRequest();
    AI_TOOL_INVOCATION_RESULT queryRouting = handler.HandleToolCall(
            request,
            liveToolCall( wxS( "kisurf_query_items" ),
                          { { "filter", { { "type", "routing" } } } } ) );

    BOOST_REQUIRE_MESSAGE( queryRouting.m_Allowed,
                           queryRouting.m_ResultJson.ToStdString() );
    nlohmann::json routingPayload = resultPayload( queryRouting );
    BOOST_CHECK_EQUAL( routingPayload["returned_count"].get<size_t>(),
                       initialRoutingCount );

    AI_TOOL_INVOCATION_RESULT deleteRouting = handler.HandleToolCall(
            request,
            liveToolCall(
                    wxS( "kisurf_run_atomic_operation" ),
                    { { "kind", "pcb.delete_items" },
                      { "arguments",
                        { { "handles", itemHandlesFromPayload( routingPayload ) } } } } ) );

    BOOST_REQUIRE_MESSAGE( deleteRouting.m_Allowed,
                           deleteRouting.m_ResultJson.ToStdString() );
    BOOST_CHECK( deleteRouting.m_Executed );
    BOOST_CHECK( !handler.ActiveSession() );
    BOOST_CHECK_EQUAL( boardRoutingItemCount( *board ), 0 );
    BOOST_CHECK_EQUAL( boardTrackSegmentCount( *board ), 0 );
    BOOST_CHECK_EQUAL( boardVias( *board ).size(), 0 );
}


BOOST_AUTO_TEST_CASE( PicProgrammerChatDirectLiveCanDeleteAllRoutingObjectsByFilter )
{
    std::unique_ptr<BOARD> board = KI_TEST::ReadBoardFromFileOrStream(
            demoBoardPath( "pic_programmer/pic_programmer.kicad_pcb" ) );
    BOOST_REQUIRE( board );

    board->BuildListOfNets();
    board->BuildConnectivity();

    const size_t initialRoutingCount = boardRoutingItemCount( *board );
    BOOST_REQUIRE_GT( initialRoutingCount, 0 );

    KISURF_AI_PCB_SESSION_APPLY_ADAPTER adapter( *board );
    KISURF_AI_PCB_SESSION_SHADOW_SEEDER seeder( *board );
    AI_SESSION_TOOL_CALL_HANDLER handler( nullptr, &adapter, nullptr, &seeder );
    handler.SetDirectLiveApplyAfterMutation( true );

    AI_PROVIDER_REQUEST request = liveToolRequest();
    AI_TOOL_INVOCATION_RESULT deleteRouting = handler.HandleToolCall(
            request,
            liveToolCall(
                    wxS( "kisurf_run_atomic_operation" ),
                    { { "kind", "pcb.delete_items" },
                      { "arguments", { { "filter", { { "type", "routing" } } } } } } ) );

    BOOST_REQUIRE_MESSAGE( deleteRouting.m_Allowed,
                           deleteRouting.m_ResultJson.ToStdString() );
    BOOST_CHECK( deleteRouting.m_Executed );
    BOOST_CHECK( !handler.ActiveSession() );
    BOOST_CHECK_EQUAL( boardRoutingItemCount( *board ), 0 );
    BOOST_CHECK_EQUAL( boardTrackSegmentCount( *board ), 0 );
    BOOST_CHECK_EQUAL( boardVias( *board ).size(), 0 );
}


BOOST_AUTO_TEST_CASE( PicProgrammerChatDirectLiveDoesNotSeedShadowBoard )
{
    std::unique_ptr<BOARD> board = KI_TEST::ReadBoardFromFileOrStream(
            demoBoardPath( "pic_programmer/pic_programmer.kicad_pcb" ) );
    BOOST_REQUIRE( board );

    board->BuildListOfNets();
    board->BuildConnectivity();

    const size_t initialRoutingCount = boardRoutingItemCount( *board );
    BOOST_REQUIRE_GT( initialRoutingCount, 0 );

    KISURF_AI_PCB_SESSION_APPLY_ADAPTER adapter( *board );
    FAILING_SHADOW_SEEDER                 seeder;
    AI_SESSION_TOOL_CALL_HANDLER          handler( nullptr, &adapter, nullptr, &seeder );
    handler.SetDirectLiveApplyAfterMutation( true );

    AI_PROVIDER_REQUEST request = liveToolRequest();
    AI_TOOL_INVOCATION_RESULT summary = handler.HandleToolCall(
            request,
            liveToolCall( wxS( "kisurf_query_board_summary" ), nlohmann::json::object() ) );

    BOOST_REQUIRE_MESSAGE( summary.m_Allowed, summary.m_ResultJson.ToStdString() );
    nlohmann::json summaryPayload = resultPayload( summary );
    BOOST_CHECK_EQUAL(
            summaryPayload["summary"]["track_segments"].get<size_t>(),
            boardTrackSegmentCount( *board ) );
    BOOST_CHECK( !handler.ActiveSession() );

    AI_TOOL_INVOCATION_RESULT queryRouting = handler.HandleToolCall(
            request,
            liveToolCall( wxS( "kisurf_query_items" ),
                          { { "filter", { { "type", "routing" } } } } ) );

    BOOST_REQUIRE_MESSAGE( queryRouting.m_Allowed,
                           queryRouting.m_ResultJson.ToStdString() );
    nlohmann::json routingPayload = resultPayload( queryRouting );
    BOOST_CHECK_EQUAL( routingPayload["returned_count"].get<size_t>(),
                       initialRoutingCount );
    BOOST_REQUIRE( routingPayload["items"].is_array() );
    BOOST_REQUIRE( !routingPayload["items"].empty() );
    BOOST_REQUIRE( routingPayload["items"][0]["handle"].contains( "uuid" ) );
    BOOST_CHECK( !routingPayload["items"][0]["handle"].contains( "session_id" ) );
    BOOST_CHECK( !handler.ActiveSession() );

    AI_TOOL_INVOCATION_RESULT deleteRouting = handler.HandleToolCall(
            request,
            liveToolCall(
                    wxS( "kisurf_run_atomic_operation" ),
                    { { "kind", "pcb.delete_items" },
                      { "arguments", { { "filter", { { "type", "routing" } } } } } } ) );

    BOOST_REQUIRE_MESSAGE( deleteRouting.m_Allowed,
                           deleteRouting.m_ResultJson.ToStdString() );
    BOOST_CHECK( deleteRouting.m_Executed );
    BOOST_CHECK_EQUAL( boardRoutingItemCount( *board ), 0 );
    BOOST_CHECK( !handler.ActiveSession() );

    nlohmann::json deletePayload = resultPayload( deleteRouting );
    BOOST_CHECK_EQUAL( deletePayload["current_board_apply"]["status"].get<std::string>(),
                       "applied" );
    BOOST_CHECK_EQUAL( deletePayload.dump().find( "session" ), std::string::npos );
    BOOST_CHECK_EQUAL( deletePayload.dump().find( "shadow" ), std::string::npos );
}


BOOST_AUTO_TEST_SUITE_END()
