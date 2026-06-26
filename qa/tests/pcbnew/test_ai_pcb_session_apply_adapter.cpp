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
#include <kisurf_ai_pcb_session_validation_service.h>

#include <board.h>
#include <board_design_settings.h>
#include <drc/drc_engine.h>
#include <footprint.h>
#include <json_common.h>
#include <netinfo.h>
#include <pad.h>
#include <pcb_shape.h>
#include <pcb_track.h>
#include <tool/tool_manager.h>
#include <wx/filename.h>
#include <zone.h>

namespace
{
AI_EXECUTION_SESSION makeSession()
{
    AI_EXECUTION_SESSION::OPEN_OPTIONS options;
    options.m_SessionId = 211;
    options.m_BoardId = wxS( "pcb-session-accept" );
    options.m_BaseHash = wxS( "board-hash-a" );
    options.m_EditorKind = AI_EDITOR_KIND::Pcb;
    return AI_EXECUTION_SESSION( std::move( options ) );
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


PCB_TRACK* addTrackSegment( BOARD& aBoard, NETINFO_ITEM* aNet,
                            const VECTOR2I& aStart, const VECTOR2I& aEnd )
{
    PCB_TRACK* track = new PCB_TRACK( &aBoard );
    track->SetStart( aStart );
    track->SetEnd( aEnd );
    track->SetLayer( F_Cu );
    track->SetWidth( 100000 );
    track->SetNet( aNet );
    aBoard.Add( track );
    return track;
}


PCB_VIA* addVia( BOARD& aBoard, NETINFO_ITEM* aNet, const VECTOR2I& aPosition )
{
    PCB_VIA* via = new PCB_VIA( &aBoard );
    via->SetPosition( aPosition );
    via->SetWidth( 600000 );
    via->SetPrimaryDrillSize( VECTOR2I( 300000, 300000 ) );
    via->SetLayerPair( F_Cu, B_Cu );
    via->SetNet( aNet );
    aBoard.Add( via );
    return via;
}


ZONE* addZone( BOARD& aBoard, NETINFO_ITEM* aNet,
               const VECTOR2I& aOrigin = VECTOR2I( 0, 0 ) )
{
    ZONE* zone = new ZONE( &aBoard );
    zone->SetLayer( F_Cu );
    zone->SetNet( aNet );

    SHAPE_LINE_CHAIN outline;
    outline.Append( aOrigin + VECTOR2I( 0, 0 ) );
    outline.Append( aOrigin + VECTOR2I( 1000, 0 ) );
    outline.Append( aOrigin + VECTOR2I( 1000, 1000 ) );
    outline.Append( aOrigin + VECTOR2I( 0, 1000 ) );
    outline.SetClosed( true );
    zone->AddPolygon( outline );

    aBoard.Add( zone );
    return zone;
}


PCB_SHAPE* addShape( BOARD& aBoard )
{
    PCB_SHAPE* shape = new PCB_SHAPE( &aBoard, SHAPE_T::SEGMENT );
    shape->SetLayer( F_SilkS );
    shape->SetStart( VECTOR2I( 10, 20 ) );
    shape->SetEnd( VECTOR2I( 30, 40 ) );
    shape->SetWidth( 50000 );
    aBoard.Add( shape );
    return shape;
}


PAD* addFootprintPad( BOARD& aBoard, NETINFO_ITEM* aNet )
{
    FOOTPRINT* footprint = new FOOTPRINT( &aBoard );
    footprint->SetReference( wxS( "U1" ) );
    footprint->SetPosition( VECTOR2I( 1000, 2000 ) );

    PAD* pad = new PAD( footprint );
    pad->SetNumber( wxS( "1" ) );
    pad->SetPosition( VECTOR2I( 1100, 2100 ) );
    pad->SetLayerSet( PAD::SMDMask() );
    pad->SetSize( F_Cu, VECTOR2I( 300, 400 ) );
    pad->SetNet( aNet );
    footprint->Add( pad );

    aBoard.Add( footprint );
    return pad;
}


FOOTPRINT* addFootprintWithPad( BOARD& aBoard, NETINFO_ITEM* aNet )
{
    PAD* pad = addFootprintPad( aBoard, aNet );
    return pad->GetParentFootprint();
}
} // namespace


BOOST_AUTO_TEST_SUITE( AiPcbSessionApplyAdapter )


BOOST_AUTO_TEST_CASE( AcceptReplayAppliesTargetPositionMoveToCreatedVia )
{
    BOARD board;
    NETINFO_ITEM* gnd = new NETINFO_ITEM( &board, wxS( "GND" ), 1 );
    board.Add( gnd );

    TOOL_MANAGER toolManager;
    toolManager.SetEnvironment( &board, nullptr, nullptr, nullptr, nullptr );

    KISURF_AI_PCB_SESSION_APPLY_ADAPTER adapter( board, toolManager );
    AI_EXECUTION_SESSION session = makeSession();

    const uint64_t stepId = session.BeginStep( wxS( "create then place via" ) );
    BOOST_REQUIRE_NE( stepId, 0 );

    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateVia,
            wxS( "{\"alias\":\"target-via\",\"net\":\"GND\","
                 "\"diameter\":600000,\"drill\":300000,"
                 "\"position\":{\"x\":10,\"y\":20}}" ) )
                           .m_Ok );

    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::MoveItems,
            wxS( "{\"handles\":[\"target-via\"],"
                 "\"target_positions\":{\"x\":50,\"y\":60}}" ) )
                           .m_Ok );

    session.EndStep( stepId );
    BOOST_CHECK( boardVias( board ).empty() );

    AI_ACCEPT_APPLY_RESULT result =
            AI_ACCEPT_APPLIER::Apply( session, wxS( "board-hash-a" ),
                                      session.ContextVersion(), adapter );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_CHECK( result.m_BoardMutated );

    std::vector<PCB_VIA*> vias = boardVias( board );
    BOOST_REQUIRE_EQUAL( vias.size(), 1 );
    BOOST_CHECK_EQUAL( vias.front()->GetPosition().x, 50 );
    BOOST_CHECK_EQUAL( vias.front()->GetPosition().y, 60 );
    BOOST_CHECK_EQUAL( vias.front()->GetNetCode(), gnd->GetNetCode() );
}


BOOST_AUTO_TEST_CASE( NativeValidationServiceRunsDrcLiteOnBoardDrcEngine )
{
    BOARD board;
    BOARD_DESIGN_SETTINGS& bds = board.GetDesignSettings();
    bds.m_DRCEngine = std::make_shared<DRC_ENGINE>( &board, &bds );
    bds.m_DRCEngine->InitEngine( wxFileName() );

    AI_EXECUTION_SESSION session = makeSession();
    const uint64_t stepId = session.BeginStep( wxS( "native validation" ) );
    BOOST_REQUIRE_NE( stepId, 0 );

    const wxString args = wxS( "{\"scope\":\"session\",\"level\":\"drc_lite\"}" );
    AI_ATOMIC_EXECUTION_RESULT commonValidation =
            AI_ATOMIC_OPERATION_EXECUTOR::Execute(
                    session, AI_SESSION_OPERATION_KIND::RunValidation, args );
    BOOST_REQUIRE( commonValidation.m_Ok );

    KISURF_AI_PCB_SESSION_VALIDATION_SERVICE validationService( board );
    AI_SESSION_VALIDATION_RESULT nativeValidation =
            validationService.RunValidation( session, args,
                                             commonValidation.m_ResultJson );

    BOOST_REQUIRE( nativeValidation.m_Ok );
    nlohmann::json payload =
            nlohmann::json::parse( nativeValidation.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["validation"]["native_backend"].get<std::string>(),
                       "pcbnew.drc_engine" );
    BOOST_CHECK_EQUAL( payload["validation"]["validated_state"].get<std::string>(),
                       "live_board" );
    BOOST_CHECK( payload["validation"]["preview_state_exact"].get<bool>() );
    BOOST_CHECK( payload["validation"]["accept_validation_sufficient"].get<bool>() );
    BOOST_CHECK_EQUAL( payload["validation"]["accept_validation_reason"].get<std::string>(),
                       "native_drc_matches_preview_state" );
    BOOST_CHECK_EQUAL( payload["validation"]["session_mutation_count"].get<size_t>(), 0 );
    BOOST_CHECK_EQUAL( payload["validation"]["status"].get<std::string>(),
                       "native_checked" );
    BOOST_CHECK_EQUAL( payload["validation"]["issue_count"].get<size_t>(),
                       payload["validation"]["issues"].size() );
    BOOST_CHECK( payload["validation"]["warnings"].empty() );
}


BOOST_AUTO_TEST_CASE( NativeValidationServiceProjectsDrcIssueItemBboxes )
{
    BOARD board;
    NETINFO_ITEM* gnd = new NETINFO_ITEM( &board, wxS( "GND" ), 1 );
    NETINFO_ITEM* vcc = new NETINFO_ITEM( &board, wxS( "VCC" ), 2 );
    board.Add( gnd );
    board.Add( vcc );

    PCB_TRACK* gndTrack =
            addTrackSegment( board, gnd, VECTOR2I( 0, 0 ), VECTOR2I( 1000000, 0 ) );
    PCB_TRACK* vccTrack =
            addTrackSegment( board, vcc, VECTOR2I( 0, 0 ), VECTOR2I( 1000000, 0 ) );

    BOARD_DESIGN_SETTINGS& bds = board.GetDesignSettings();
    bds.m_DRCEngine = std::make_shared<DRC_ENGINE>( &board, &bds );
    bds.m_DRCEngine->InitEngine( wxFileName() );

    AI_EXECUTION_SESSION session = makeSession();
    const uint64_t stepId = session.BeginStep( wxS( "native bbox validation" ) );
    BOOST_REQUIRE_NE( stepId, 0 );

    const wxString args = wxS( "{\"scope\":\"board\",\"level\":\"full_drc\"}" );
    AI_ATOMIC_EXECUTION_RESULT commonValidation =
            AI_ATOMIC_OPERATION_EXECUTOR::Execute(
                    session, AI_SESSION_OPERATION_KIND::RunValidation, args );
    BOOST_REQUIRE( commonValidation.m_Ok );
    session.EndStep( stepId );

    KISURF_AI_PCB_SESSION_VALIDATION_SERVICE validationService( board );
    AI_SESSION_VALIDATION_RESULT nativeValidation =
            validationService.RunValidation( session, args,
                                             commonValidation.m_ResultJson );

    BOOST_REQUIRE( nativeValidation.m_Ok );
    nlohmann::json payload =
            nlohmann::json::parse( nativeValidation.m_ResultJson.ToStdString() );
    BOOST_REQUIRE_GT( payload["validation"]["issue_count"].get<size_t>(), 0 );

    bool foundItemIssue = false;

    for( const nlohmann::json& issue : payload["validation"]["issues"] )
    {
        if( !issue.contains( "main_item_uuid" ) )
            continue;

        wxUnusedVar( gndTrack );
        wxUnusedVar( vccTrack );

        foundItemIssue = true;
        BOOST_REQUIRE( issue.contains( "main_item_bbox" ) );
        BOOST_CHECK( issue["main_item_bbox"].contains( "x" ) );
        BOOST_CHECK( issue["main_item_bbox"].contains( "y" ) );
        BOOST_CHECK( issue["main_item_bbox"].contains( "width" ) );
        BOOST_CHECK( issue["main_item_bbox"].contains( "height" ) );

        if( issue.contains( "aux_item_uuid" ) )
        {
            BOOST_REQUIRE( issue.contains( "aux_item_bbox" ) );
            BOOST_CHECK( issue["aux_item_bbox"].contains( "x" ) );
            BOOST_CHECK( issue["aux_item_bbox"].contains( "y" ) );
            BOOST_CHECK( issue["aux_item_bbox"].contains( "width" ) );
            BOOST_CHECK( issue["aux_item_bbox"].contains( "height" ) );
        }

        break;
    }

    BOOST_CHECK( foundItemIssue );
}


BOOST_AUTO_TEST_CASE( NativeValidationServiceRunsDrcOnPreviewBoardWhenSessionHasMutations )
{
    BOARD board;
    NETINFO_ITEM* gnd = new NETINFO_ITEM( &board, wxS( "GND" ), 1 );
    board.Add( gnd );

    BOARD_DESIGN_SETTINGS& bds = board.GetDesignSettings();
    bds.m_DRCEngine = std::make_shared<DRC_ENGINE>( &board, &bds );
    bds.m_DRCEngine->InitEngine( wxFileName() );

    AI_EXECUTION_SESSION session = makeSession();
    const uint64_t mutationStep = session.BeginStep( wxS( "session mutation" ) );
    BOOST_REQUIRE_NE( mutationStep, 0 );

    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateVia,
            wxS( "{\"alias\":\"preview-only-via\",\"net\":\"GND\","
                 "\"diameter\":600000,\"drill\":300000,"
                 "\"position\":{\"x\":25,\"y\":50}}" ) )
                           .m_Ok );

    const wxString args = wxS( "{\"scope\":\"session\",\"level\":\"drc_lite\"}" );
    AI_ATOMIC_EXECUTION_RESULT commonValidation =
            AI_ATOMIC_OPERATION_EXECUTOR::Execute(
                    session, AI_SESSION_OPERATION_KIND::RunValidation, args );
    BOOST_REQUIRE( commonValidation.m_Ok );

    KISURF_AI_PCB_SESSION_VALIDATION_SERVICE validationService( board );
    AI_SESSION_VALIDATION_RESULT nativeValidation =
            validationService.RunValidation( session, args,
                                             commonValidation.m_ResultJson );

    BOOST_REQUIRE( nativeValidation.m_Ok );
    nlohmann::json payload =
            nlohmann::json::parse( nativeValidation.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["validation"]["native_backend"].get<std::string>(),
                       "pcbnew.drc_engine" );
    BOOST_CHECK_EQUAL( payload["validation"]["validated_state"].get<std::string>(),
                       "preview_board" );
    BOOST_CHECK( payload["validation"]["preview_state_exact"].get<bool>() );
    BOOST_CHECK( payload["validation"]["accept_validation_sufficient"].get<bool>() );
    BOOST_CHECK_EQUAL( payload["validation"]["accept_validation_reason"].get<std::string>(),
                       "native_drc_matches_preview_state" );
    BOOST_CHECK_EQUAL( payload["validation"]["session_mutation_count"].get<size_t>(), 1 );
    BOOST_CHECK_EQUAL( boardVias( board ).size(), 0 );
}


BOOST_AUTO_TEST_CASE( ShadowSeederReconstructsLiveBoardItems )
{
    BOARD board;
    NETINFO_ITEM* gnd = new NETINFO_ITEM( &board, wxS( "GND" ), 1 );
    board.Add( gnd );

    addTrackSegment( board, gnd, VECTOR2I( 0, 0 ), VECTOR2I( 100, 0 ) );
    addVia( board, gnd, VECTOR2I( 200, 300 ) );
    addZone( board, gnd );
    addShape( board );

    AI_EXECUTION_SESSION session = makeSession();
    KISURF_AI_PCB_SESSION_SHADOW_SEEDER seeder( board );
    seeder.Seed( session );

    nlohmann::json summary =
            nlohmann::json::parse( session.ShadowBoard().QueryBoardSummary().ToStdString() );
    BOOST_CHECK_EQUAL( summary["items_total"].get<size_t>(), 4 );
    BOOST_CHECK_EQUAL( summary["vias"].get<size_t>(), 1 );
    BOOST_CHECK_EQUAL( summary["track_segments"].get<size_t>(), 1 );
    BOOST_CHECK_EQUAL( summary["zones"].get<size_t>(), 1 );
    BOOST_CHECK_EQUAL( summary["shapes"].get<size_t>(), 1 );

    std::vector<AI_SHADOW_ITEM> tracks =
            session.ShadowBoard().QueryItems( wxS( "{\"type\":\"track_segment\"}" ) );
    BOOST_REQUIRE_EQUAL( tracks.size(), 1 );
    BOOST_CHECK( tracks.front().m_Metadata.count( wxS( "live_uuid" ) ) == 1 );
}


BOOST_AUTO_TEST_CASE( ShadowSeederMarksSelectedLiveBoardItems )
{
    BOARD board;
    NETINFO_ITEM* gnd = new NETINFO_ITEM( &board, wxS( "GND" ), 1 );
    board.Add( gnd );

    PCB_VIA* selectedVia = addVia( board, gnd, VECTOR2I( 200, 300 ) );
    addTrackSegment( board, gnd, VECTOR2I( 0, 0 ), VECTOR2I( 100, 0 ) );
    selectedVia->SetSelected();

    AI_EXECUTION_SESSION session = makeSession();
    KISURF_AI_PCB_SESSION_SHADOW_SEEDER seeder( board );
    seeder.Seed( session );

    std::vector<AI_SHADOW_ITEM> selectedItems =
            session.ShadowBoard().QueryItems( wxS( "{\"selection\":true}" ) );

    BOOST_REQUIRE_EQUAL( selectedItems.size(), 1 );
    BOOST_CHECK_EQUAL( selectedItems.front().m_Type, wxString( wxS( "via" ) ) );
    BOOST_REQUIRE( selectedItems.front().m_Metadata.count( wxS( "selected" ) ) == 1 );
    BOOST_CHECK_EQUAL( selectedItems.front().m_Metadata.at( wxS( "selected" ) ),
                       wxString( wxS( "true" ) ) );
}


BOOST_AUTO_TEST_CASE( ShadowSeederReconstructsSelectedPads )
{
    BOARD board;
    NETINFO_ITEM* gnd = new NETINFO_ITEM( &board, wxS( "GND" ), 1 );
    board.Add( gnd );

    PAD* selectedPad = addFootprintPad( board, gnd );
    selectedPad->GetParentFootprint()->SetValue( wxS( "MCU" ) );
    selectedPad->SetSelected();

    AI_EXECUTION_SESSION session = makeSession();
    KISURF_AI_PCB_SESSION_SHADOW_SEEDER seeder( board );
    seeder.Seed( session );

    std::vector<AI_SHADOW_ITEM> selectedItems =
            session.ShadowBoard().QueryItems( wxS( "{\"selection\":true}" ) );

    BOOST_REQUIRE_EQUAL( selectedItems.size(), 1 );
    BOOST_CHECK_EQUAL( selectedItems.front().m_Type, wxString( wxS( "pad" ) ) );
    BOOST_CHECK_EQUAL( selectedItems.front().m_Net, wxString( wxS( "GND" ) ) );
    BOOST_CHECK_EQUAL( selectedItems.front().m_Layer, wxString( wxS( "F.Cu" ) ) );
    BOOST_REQUIRE( selectedItems.front().m_Metadata.count( wxS( "footprint_reference" ) ) == 1 );
    BOOST_CHECK_EQUAL( selectedItems.front().m_Metadata.at( wxS( "footprint_reference" ) ),
                       wxString( wxS( "U1" ) ) );
    BOOST_REQUIRE( selectedItems.front().m_Metadata.count( wxS( "footprint_value" ) ) == 1 );
    BOOST_CHECK_EQUAL( selectedItems.front().m_Metadata.at( wxS( "footprint_value" ) ),
                       wxString( wxS( "MCU" ) ) );
    BOOST_REQUIRE( selectedItems.front().m_Metadata.count( wxS( "pad_number" ) ) == 1 );
    BOOST_CHECK_EQUAL( selectedItems.front().m_Metadata.at( wxS( "pad_number" ) ),
                       wxString( wxS( "1" ) ) );
    BOOST_REQUIRE( selectedItems.front().m_Metadata.count( wxS( "live_uuid" ) ) == 1 );

    std::vector<AI_SHADOW_ITEM> pads =
            session.ShadowBoard().QueryItems( wxS( "{\"type\":\"pad\",\"net\":\"GND\"}" ) );
    BOOST_REQUIRE_EQUAL( pads.size(), 1 );
    BOOST_CHECK_EQUAL( pads.front().m_Alias, selectedItems.front().m_Alias );

    nlohmann::json summary =
            nlohmann::json::parse( session.ShadowBoard().QueryBoardSummary().ToStdString() );
    BOOST_CHECK_EQUAL( summary["footprints"].get<size_t>(), 1 );
    BOOST_CHECK_EQUAL( summary["pads"].get<size_t>(), 1 );
}


BOOST_AUTO_TEST_CASE( ShadowSeederExposesFootprintIdentityFacts )
{
    BOARD board;
    NETINFO_ITEM* gnd = new NETINFO_ITEM( &board, wxS( "GND" ), 1 );
    board.Add( gnd );

    FOOTPRINT* footprint = addFootprintWithPad( board, gnd );
    footprint->SetReference( wxS( "U7" ) );
    footprint->SetValue( wxS( "LDO-3V3" ) );

    AI_EXECUTION_SESSION session = makeSession();
    KISURF_AI_PCB_SESSION_SHADOW_SEEDER seeder( board );
    seeder.Seed( session );

    std::vector<AI_SHADOW_ITEM> footprints =
            session.ShadowBoard().QueryItems( wxS( "{\"type\":\"footprint\"}" ) );
    BOOST_REQUIRE_EQUAL( footprints.size(), 1 );

    nlohmann::json geometry =
            nlohmann::json::parse( footprints.front().m_GeometryJson.ToStdString() );
    BOOST_CHECK_EQUAL( geometry["reference"].get<std::string>(), "U7" );
    BOOST_CHECK_EQUAL( geometry["value"].get<std::string>(), "LDO-3V3" );

    BOOST_REQUIRE( footprints.front().m_Metadata.count( wxS( "footprint_reference" ) ) == 1 );
    BOOST_REQUIRE( footprints.front().m_Metadata.count( wxS( "footprint_value" ) ) == 1 );
    BOOST_CHECK_EQUAL( footprints.front().m_Metadata.at( wxS( "footprint_reference" ) ),
                       wxString( wxS( "U7" ) ) );
    BOOST_CHECK_EQUAL( footprints.front().m_Metadata.at( wxS( "footprint_value" ) ),
                       wxString( wxS( "LDO-3V3" ) ) );
}


BOOST_AUTO_TEST_CASE( ShadowSeederExposesPadsWhenParentFootprintIsSelected )
{
    BOARD board;
    NETINFO_ITEM* gnd = new NETINFO_ITEM( &board, wxS( "GND" ), 1 );
    board.Add( gnd );

    FOOTPRINT* selectedFootprint = addFootprintWithPad( board, gnd );
    selectedFootprint->SetSelected();

    AI_EXECUTION_SESSION session = makeSession();
    KISURF_AI_PCB_SESSION_SHADOW_SEEDER seeder( board );
    seeder.Seed( session );

    std::vector<AI_SHADOW_ITEM> selectedItems =
            session.ShadowBoard().QueryItems( wxS( "{\"selection\":true}" ) );

    BOOST_REQUIRE_EQUAL( selectedItems.size(), 2 );

    const AI_SHADOW_ITEM* footprintItem = nullptr;
    const AI_SHADOW_ITEM* padItem = nullptr;

    for( const AI_SHADOW_ITEM& item : selectedItems )
    {
        if( item.m_Type == wxS( "footprint" ) )
            footprintItem = &item;
        else if( item.m_Type == wxS( "pad" ) )
            padItem = &item;
    }

    BOOST_REQUIRE( footprintItem );
    BOOST_REQUIRE( padItem );
    BOOST_CHECK_EQUAL( footprintItem->m_Metadata.at( wxS( "selected" ) ),
                       wxString( wxS( "true" ) ) );
    BOOST_CHECK_EQUAL( padItem->m_Metadata.at( wxS( "selected" ) ),
                       wxString( wxS( "true" ) ) );
    BOOST_REQUIRE( padItem->m_Metadata.count( wxS( "selection_inherited_from" ) ) == 1 );
    BOOST_CHECK_EQUAL( padItem->m_Metadata.at( wxS( "selection_inherited_from" ) ),
                       selectedFootprint->m_Uuid.AsString() );
    BOOST_REQUIRE( padItem->m_Metadata.count( wxS( "selection_inherited_from_type" ) ) == 1 );
    BOOST_CHECK_EQUAL( padItem->m_Metadata.at( wxS( "selection_inherited_from_type" ) ),
                       wxString( wxS( "footprint" ) ) );
    BOOST_CHECK_EQUAL( padItem->m_Metadata.at( wxS( "footprint_reference" ) ),
                       wxString( wxS( "U1" ) ) );
    BOOST_CHECK_EQUAL( padItem->m_Metadata.at( wxS( "pad_number" ) ),
                       wxString( wxS( "1" ) ) );
}


BOOST_AUTO_TEST_CASE( AcceptReplayAppliesGeometryPatchToSeededLiveTrack )
{
    BOARD board;
    NETINFO_ITEM* gnd = new NETINFO_ITEM( &board, wxS( "GND" ), 1 );
    board.Add( gnd );
    PCB_TRACK* track = addTrackSegment( board, gnd, VECTOR2I( 0, 0 ), VECTOR2I( 100, 0 ) );

    TOOL_MANAGER toolManager;
    toolManager.SetEnvironment( &board, nullptr, nullptr, nullptr, nullptr );

    AI_EXECUTION_SESSION session = makeSession();
    KISURF_AI_PCB_SESSION_SHADOW_SEEDER seeder( board );
    seeder.Seed( session );

    std::vector<AI_SHADOW_ITEM> tracks =
            session.ShadowBoard().QueryItems( wxS( "{\"type\":\"track_segment\"}" ) );
    BOOST_REQUIRE_EQUAL( tracks.size(), 1 );

    const uint64_t stepId = session.BeginStep( wxS( "patch seeded track" ) );
    BOOST_REQUIRE_NE( stepId, 0 );
    nlohmann::json handle = {
        { "session_id", tracks.front().m_Handle.m_SessionId },
        { "handle_id", tracks.front().m_Handle.m_HandleId },
        { "generation", tracks.front().m_Handle.m_Generation }
    };
    nlohmann::json args = {
        { "handle", handle },
        { "geometry_patch", { { "end", { { "x", 500 }, { "y", 600 } } },
                              { "width", 150000 } } }
    };
    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::UpdateItemGeometry,
            wxString::FromUTF8( args.dump().c_str() ) )
                           .m_Ok );
    session.EndStep( stepId );

    KISURF_AI_PCB_SESSION_APPLY_ADAPTER adapter( board, toolManager );
    AI_ACCEPT_APPLY_RESULT result =
            AI_ACCEPT_APPLIER::Apply( session, wxS( "board-hash-a" ),
                                      session.ContextVersion(), adapter );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_CHECK( result.m_BoardMutated );
    BOOST_CHECK_EQUAL( track->GetEnd().x, 500 );
    BOOST_CHECK_EQUAL( track->GetEnd().y, 600 );
    BOOST_CHECK_EQUAL( track->GetWidth(), 150000 );
}


BOOST_AUTO_TEST_CASE( AcceptReplayAppliesOrientationPropertyToSeededLiveFootprint )
{
    BOARD board;
    NETINFO_ITEM* gnd = new NETINFO_ITEM( &board, wxS( "GND" ), 1 );
    board.Add( gnd );
    FOOTPRINT* footprint = addFootprintWithPad( board, gnd );
    footprint->SetOrientation( EDA_ANGLE( 0.0, DEGREES_T ) );

    TOOL_MANAGER toolManager;
    toolManager.SetEnvironment( &board, nullptr, nullptr, nullptr, nullptr );

    AI_EXECUTION_SESSION session = makeSession();
    KISURF_AI_PCB_SESSION_SHADOW_SEEDER seeder( board );
    seeder.Seed( session );

    std::vector<AI_SHADOW_ITEM> footprints =
            session.ShadowBoard().QueryItems( wxS( "{\"type\":\"footprint\"}" ) );
    BOOST_REQUIRE_EQUAL( footprints.size(), 1 );

    const uint64_t stepId = session.BeginStep( wxS( "rotate seeded footprint" ) );
    BOOST_REQUIRE_NE( stepId, 0 );

    nlohmann::json handle = {
        { "session_id", footprints.front().m_Handle.m_SessionId },
        { "handle_id", footprints.front().m_Handle.m_HandleId },
        { "generation", footprints.front().m_Handle.m_Generation }
    };
    nlohmann::json args = {
        { "handle", handle },
        { "typed_props", { { "orientation_degrees", 90.0 } } }
    };

    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::SetItemProperties,
            wxString::FromUTF8( args.dump().c_str() ) )
                           .m_Ok );
    session.EndStep( stepId );

    KISURF_AI_PCB_SESSION_APPLY_ADAPTER adapter( board, toolManager );
    AI_ACCEPT_APPLY_RESULT result =
            AI_ACCEPT_APPLIER::Apply( session, wxS( "board-hash-a" ),
                                      session.ContextVersion(), adapter );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_CHECK( result.m_BoardMutated );
    BOOST_CHECK_CLOSE( footprint->GetOrientation().AsDegrees(), 90.0, 1e-6 );
}


BOOST_AUTO_TEST_CASE( AcceptReplayAppliesSidePropertyToSeededLiveFootprint )
{
    BOARD board;
    NETINFO_ITEM* gnd = new NETINFO_ITEM( &board, wxS( "GND" ), 1 );
    board.Add( gnd );
    FOOTPRINT* footprint = addFootprintWithPad( board, gnd );
    footprint->SetLayer( F_Cu );

    TOOL_MANAGER toolManager;
    toolManager.SetEnvironment( &board, nullptr, nullptr, nullptr, nullptr );

    AI_EXECUTION_SESSION session = makeSession();
    KISURF_AI_PCB_SESSION_SHADOW_SEEDER seeder( board );
    seeder.Seed( session );

    std::vector<AI_SHADOW_ITEM> footprints =
            session.ShadowBoard().QueryItems( wxS( "{\"type\":\"footprint\"}" ) );
    BOOST_REQUIRE_EQUAL( footprints.size(), 1 );

    const uint64_t stepId = session.BeginStep( wxS( "flip seeded footprint" ) );
    BOOST_REQUIRE_NE( stepId, 0 );

    nlohmann::json handle = {
        { "session_id", footprints.front().m_Handle.m_SessionId },
        { "handle_id", footprints.front().m_Handle.m_HandleId },
        { "generation", footprints.front().m_Handle.m_Generation }
    };
    nlohmann::json args = {
        { "handle", handle },
        { "typed_props", { { "side", "B.Cu" } } }
    };

    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::SetItemProperties,
            wxString::FromUTF8( args.dump().c_str() ) )
                           .m_Ok );
    session.EndStep( stepId );

    KISURF_AI_PCB_SESSION_APPLY_ADAPTER adapter( board, toolManager );
    AI_ACCEPT_APPLY_RESULT result =
            AI_ACCEPT_APPLIER::Apply( session, wxS( "board-hash-a" ),
                                      session.ContextVersion(), adapter );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_CHECK( result.m_BoardMutated );
    BOOST_CHECK_EQUAL( footprint->GetLayer(), B_Cu );
    BOOST_CHECK( footprint->IsFlipped() );
}


BOOST_AUTO_TEST_CASE( AcceptReplayAppliesIdentityPropertiesToSeededLiveFootprint )
{
    BOARD board;
    NETINFO_ITEM* gnd = new NETINFO_ITEM( &board, wxS( "GND" ), 1 );
    board.Add( gnd );
    FOOTPRINT* footprint = addFootprintWithPad( board, gnd );
    footprint->SetReference( wxS( "U1" ) );
    footprint->SetValue( wxS( "MCU" ) );

    TOOL_MANAGER toolManager;
    toolManager.SetEnvironment( &board, nullptr, nullptr, nullptr, nullptr );

    AI_EXECUTION_SESSION session = makeSession();
    KISURF_AI_PCB_SESSION_SHADOW_SEEDER seeder( board );
    seeder.Seed( session );

    std::vector<AI_SHADOW_ITEM> footprints =
            session.ShadowBoard().QueryItems( wxS( "{\"type\":\"footprint\"}" ) );
    BOOST_REQUIRE_EQUAL( footprints.size(), 1 );

    const uint64_t stepId = session.BeginStep( wxS( "rename seeded footprint" ) );
    BOOST_REQUIRE_NE( stepId, 0 );

    nlohmann::json handle = {
        { "session_id", footprints.front().m_Handle.m_SessionId },
        { "handle_id", footprints.front().m_Handle.m_HandleId },
        { "generation", footprints.front().m_Handle.m_Generation }
    };
    nlohmann::json args = {
        { "handle", handle },
        { "typed_props", { { "reference", "U42" }, { "value", "STM32F4" } } }
    };

    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::SetItemProperties,
            wxString::FromUTF8( args.dump().c_str() ) )
                           .m_Ok );
    session.EndStep( stepId );

    KISURF_AI_PCB_SESSION_APPLY_ADAPTER adapter( board, toolManager );
    AI_ACCEPT_APPLY_RESULT result =
            AI_ACCEPT_APPLIER::Apply( session, wxS( "board-hash-a" ),
                                      session.ContextVersion(), adapter );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_CHECK( result.m_BoardMutated );
    BOOST_CHECK_EQUAL( footprint->GetReference(), wxString( wxS( "U42" ) ) );
    BOOST_CHECK_EQUAL( footprint->GetValue(), wxString( wxS( "STM32F4" ) ) );
}


BOOST_AUTO_TEST_CASE( AcceptReplayAppliesLayerSetToCreatedZone )
{
    BOARD board;
    NETINFO_ITEM* gnd = new NETINFO_ITEM( &board, wxS( "GND" ), 1 );
    board.Add( gnd );

    TOOL_MANAGER toolManager;
    toolManager.SetEnvironment( &board, nullptr, nullptr, nullptr, nullptr );

    AI_EXECUTION_SESSION session = makeSession();
    const uint64_t stepId = session.BeginStep( wxS( "create multilayer zone" ) );
    BOOST_REQUIRE_NE( stepId, 0 );

    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateZone,
            wxS( "{\"alias\":\"gnd-zone\",\"net\":\"GND\",\"layer_set\":[\"F.Cu\"],"
                 "\"outline\":{\"points\":[{\"x\":0,\"y\":0},{\"x\":1000,\"y\":0},"
                 "{\"x\":1000,\"y\":1000},{\"x\":0,\"y\":1000}]}}" ) )
                           .m_Ok );

    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::SetItemLayer,
            wxS( "{\"handle\":\"gnd-zone\",\"layer_set\":[\"F.Cu\",\"B.Cu\"]}" ) )
                           .m_Ok );
    session.EndStep( stepId );

    KISURF_AI_PCB_SESSION_APPLY_ADAPTER adapter( board, toolManager );
    AI_ACCEPT_APPLY_RESULT result =
            AI_ACCEPT_APPLIER::Apply( session, wxS( "board-hash-a" ),
                                      session.ContextVersion(), adapter );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_CHECK( result.m_BoardMutated );
    BOOST_REQUIRE_EQUAL( board.Zones().size(), 1 );

    LSET layers = board.Zones().front()->GetLayerSet();
    BOOST_CHECK( layers.test( F_Cu ) );
    BOOST_CHECK( layers.test( B_Cu ) );
}


BOOST_AUTO_TEST_CASE( AcceptReplayAppliesGeometryPatchToCreatedZoneOutline )
{
    BOARD board;
    NETINFO_ITEM* gnd = new NETINFO_ITEM( &board, wxS( "GND" ), 1 );
    board.Add( gnd );

    TOOL_MANAGER toolManager;
    toolManager.SetEnvironment( &board, nullptr, nullptr, nullptr, nullptr );

    AI_EXECUTION_SESSION session = makeSession();
    const uint64_t stepId = session.BeginStep( wxS( "reshape zone" ) );
    BOOST_REQUIRE_NE( stepId, 0 );

    AI_ATOMIC_EXECUTION_RESULT zone = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateZone,
            wxS( "{\"alias\":\"reshaped-zone\",\"net\":\"GND\",\"layer_set\":[\"F.Cu\"],"
                 "\"outline\":{\"points\":[{\"x\":0,\"y\":0},{\"x\":1000,\"y\":0},"
                 "{\"x\":1000,\"y\":1000},{\"x\":0,\"y\":1000}]}}" ) );
    BOOST_REQUIRE( zone.m_Ok );

    AI_ATOMIC_EXECUTION_RESULT patch = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::UpdateItemGeometry,
            wxS( "{\"handle\":\"reshaped-zone\","
                 "\"geometry_patch\":{\"points\":[{\"x\":0,\"y\":0},"
                 "{\"x\":3000,\"y\":0},{\"x\":3000,\"y\":1000},"
                 "{\"x\":0,\"y\":1000}]}}" ) );
    BOOST_REQUIRE( patch.m_Ok );
    session.EndStep( stepId );

    KISURF_AI_PCB_SESSION_APPLY_ADAPTER adapter( board, toolManager );
    AI_ACCEPT_APPLY_RESULT result =
            AI_ACCEPT_APPLIER::Apply( session, wxS( "board-hash-a" ),
                                      session.ContextVersion(), adapter );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_REQUIRE_EQUAL( board.Zones().size(), 1 );
    BOOST_REQUIRE( board.Zones().front()->Outline() );
    BOOST_REQUIRE_EQUAL( board.Zones().front()->Outline()->OutlineCount(), 1 );
    BOOST_CHECK_EQUAL( board.Zones().front()->Outline()->COutline( 0 ).CPoint( 1 ).x,
                       3000 );
}


BOOST_AUTO_TEST_CASE( AcceptReplayAppliesZonePriorityClearanceAndFillMode )
{
    BOARD board;
    NETINFO_ITEM* gnd = new NETINFO_ITEM( &board, wxS( "GND" ), 1 );
    board.Add( gnd );

    TOOL_MANAGER toolManager;
    toolManager.SetEnvironment( &board, nullptr, nullptr, nullptr, nullptr );

    AI_EXECUTION_SESSION session = makeSession();
    const uint64_t stepId = session.BeginStep( wxS( "create configured zone" ) );
    BOOST_REQUIRE_NE( stepId, 0 );

    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateZone,
            wxS( "{\"alias\":\"configured-zone\",\"net\":\"GND\","
                 "\"layer_set\":[\"F.Cu\"],\"clearance\":250000,"
                 "\"priority\":7,\"fill_mode\":\"hatch_pattern\","
                 "\"outline\":{\"points\":[{\"x\":0,\"y\":0},{\"x\":2000,\"y\":0},"
                 "{\"x\":2000,\"y\":2000},{\"x\":0,\"y\":2000}]}}" ) )
                           .m_Ok );
    session.EndStep( stepId );

    KISURF_AI_PCB_SESSION_APPLY_ADAPTER adapter( board, toolManager );
    AI_ACCEPT_APPLY_RESULT result =
            AI_ACCEPT_APPLIER::Apply( session, wxS( "board-hash-a" ),
                                      session.ContextVersion(), adapter );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_REQUIRE_EQUAL( board.Zones().size(), 1 );

    ZONE* zone = board.Zones().front();
    BOOST_CHECK_EQUAL( zone->GetAssignedPriority(), 7u );
    BOOST_CHECK( zone->GetFillMode() == ZONE_FILL_MODE::HATCH_PATTERN );
    BOOST_REQUIRE( zone->GetLocalClearance().has_value() );
    BOOST_CHECK_EQUAL( *zone->GetLocalClearance(), 250000 );
}


BOOST_AUTO_TEST_CASE( AcceptReplayAppliesSetItemPropertiesToCreatedZone )
{
    BOARD board;
    NETINFO_ITEM* gnd = new NETINFO_ITEM( &board, wxS( "GND" ), 1 );
    board.Add( gnd );

    TOOL_MANAGER toolManager;
    toolManager.SetEnvironment( &board, nullptr, nullptr, nullptr, nullptr );

    AI_EXECUTION_SESSION session = makeSession();
    const uint64_t stepId = session.BeginStep( wxS( "configure zone properties" ) );
    BOOST_REQUIRE_NE( stepId, 0 );

    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateZone,
            wxS( "{\"alias\":\"props-zone\",\"net\":\"GND\",\"layer_set\":[\"F.Cu\"],"
                 "\"outline\":{\"points\":[{\"x\":0,\"y\":0},{\"x\":2000,\"y\":0},"
                 "{\"x\":2000,\"y\":2000},{\"x\":0,\"y\":2000}]}}" ) )
                           .m_Ok );

    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::SetItemProperties,
            wxS( "{\"handle\":\"props-zone\",\"typed_props\":{\"clearance\":125000,"
                 "\"priority\":4,\"fill_mode\":\"copper_thieving\"}}" ) )
                           .m_Ok );
    session.EndStep( stepId );

    KISURF_AI_PCB_SESSION_APPLY_ADAPTER adapter( board, toolManager );
    AI_ACCEPT_APPLY_RESULT result =
            AI_ACCEPT_APPLIER::Apply( session, wxS( "board-hash-a" ),
                                      session.ContextVersion(), adapter );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_REQUIRE_EQUAL( board.Zones().size(), 1 );

    ZONE* zone = board.Zones().front();
    BOOST_CHECK_EQUAL( zone->GetAssignedPriority(), 4u );
    BOOST_CHECK( zone->GetFillMode() == ZONE_FILL_MODE::COPPER_THIEVING );
    BOOST_REQUIRE( zone->GetLocalClearance().has_value() );
    BOOST_CHECK_EQUAL( *zone->GetLocalClearance(), 125000 );
}


BOOST_AUTO_TEST_CASE( AcceptReplayRefillsOnlyZonesIntersectingAffectedArea )
{
    BOARD board;
    NETINFO_ITEM* gnd = new NETINFO_ITEM( &board, wxS( "GND" ), 1 );
    board.Add( gnd );

    ZONE* nearZone = addZone( board, gnd, VECTOR2I( 0, 0 ) );
    ZONE* farZone = addZone( board, gnd, VECTOR2I( 100000, 100000 ) );
    nearZone->SetIsFilled( false );
    farZone->SetIsFilled( false );

    TOOL_MANAGER toolManager;
    toolManager.SetEnvironment( &board, nullptr, nullptr, nullptr, nullptr );

    AI_EXECUTION_SESSION session = makeSession();
    const uint64_t stepId = session.BeginStep( wxS( "refill affected area" ) );
    BOOST_REQUIRE_NE( stepId, 0 );

    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::RefillZones,
            wxS( "{\"affected_area\":{\"x\":-100,\"y\":-100,"
                 "\"width\":2000,\"height\":2000}}" ) )
                           .m_Ok );
    session.EndStep( stepId );

    KISURF_AI_PCB_SESSION_APPLY_ADAPTER adapter( board, toolManager );
    AI_ACCEPT_APPLY_RESULT result =
            AI_ACCEPT_APPLIER::Apply( session, wxS( "board-hash-a" ),
                                      session.ContextVersion(), adapter );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_CHECK( result.m_BoardMutated );
    BOOST_CHECK( nearZone->IsFilled() );
    BOOST_CHECK( !farZone->IsFilled() );
}


BOOST_AUTO_TEST_SUITE_END()
