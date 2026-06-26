#include <boost/test/unit_test.hpp>

#include <json_common.h>
#include <kisurf/ai/ai_atomic_operation_executor.h>
#include <kisurf/ai/ai_execution_session.h>
#include <kisurf/ai/ai_shadow_board.h>

namespace
{
AI_EXECUTION_SESSION makeSession()
{
    AI_EXECUTION_SESSION::OPEN_OPTIONS options;
    options.m_SessionId = 77;
    options.m_BoardId = wxS( "pcb-main" );
    options.m_BaseHash = wxS( "base-hash-shadow" );
    options.m_EditorKind = AI_EDITOR_KIND::Pcb;
    options.m_ContextVersion.m_DocumentRevision = 10;
    options.m_ContextVersion.m_SelectionRevision = 11;
    options.m_ContextVersion.m_ViewRevision = 12;
    return AI_EXECUTION_SESSION( options );
}
} // namespace


BOOST_AUTO_TEST_SUITE( AiShadowBoard )


BOOST_AUTO_TEST_CASE( CreateViaMutatesSemanticShadowBoardAndJournal )
{
    AI_EXECUTION_SESSION session = makeSession();
    const uint64_t stepId = session.BeginStep( wxS( "place one via" ) );
    BOOST_REQUIRE_NE( stepId, 0 );

    AI_ATOMIC_EXECUTION_RESULT result = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateVia,
            wxS( "{\"alias\":\"gnd-via-0\",\"net\":\"GND\","
                 "\"position\":{\"x\":1000000,\"y\":2000000},"
                 "\"diameter\":600000,\"drill\":300000,"
                 "\"layer_pair\":[\"F.Cu\",\"B.Cu\"],"
                 "\"metadata\":{\"source\":\"unit-test\"}}" ) );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_REQUIRE_EQUAL( result.m_CreatedHandles.size(), 1 );
    BOOST_CHECK_EQUAL( result.m_OperationIds.size(), 1 );

    const AI_SHADOW_ITEM* via =
            session.ShadowBoard().FindItem( result.m_CreatedHandles.front() );
    BOOST_REQUIRE( via );
    BOOST_CHECK_EQUAL( via->m_Type, wxString( wxS( "via" ) ) );
    BOOST_CHECK_EQUAL( via->m_Alias, wxString( wxS( "gnd-via-0" ) ) );
    BOOST_CHECK_EQUAL( via->m_Net, wxString( wxS( "GND" ) ) );
    BOOST_CHECK( via->m_GeometryJson.Contains( wxS( "1000000" ) ) );

    BOOST_REQUIRE_EQUAL( session.Journal().Operations().size(), 1 );
    const AI_SESSION_OPERATION_RECORD& record =
            session.Journal().Operations().front();
    BOOST_CHECK( record.m_Kind == AI_SESSION_OPERATION_KIND::CreateVia );
    BOOST_REQUIRE_EQUAL( record.m_CreatedHandles.size(), 1 );
    BOOST_CHECK_EQUAL( record.m_CreatedHandles.front().m_Alias,
                       wxString( wxS( "gnd-via-0" ) ) );

    nlohmann::json summary =
            nlohmann::json::parse( session.ShadowBoard().QueryBoardSummary().ToStdString() );
    BOOST_CHECK_EQUAL( summary["items_total"].get<size_t>(), 1 );
    BOOST_CHECK_EQUAL( summary["vias"].get<size_t>(), 1 );
    BOOST_CHECK_EQUAL( summary["track_segments"].get<size_t>(), 0 );
}


BOOST_AUTO_TEST_CASE( TrackPolylineLowersIntoTrackSegmentAtomicOps )
{
    AI_EXECUTION_SESSION session = makeSession();
    session.BeginStep( wxS( "route polyline" ) );

    AI_ATOMIC_EXECUTION_RESULT result = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateTrackPolyline,
            wxS( "{\"alias\":\"clk-route\",\"net\":\"/CLK\",\"layer\":\"F.Cu\","
                 "\"width\":150000,"
                 "\"points\":[{\"x\":0,\"y\":0},{\"x\":1000,\"y\":0},"
                 "{\"x\":1000,\"y\":2000}]}" ) );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_CHECK_EQUAL( result.m_CreatedHandles.size(), 2 );
    BOOST_CHECK_EQUAL( result.m_OperationIds.size(), 2 );
    BOOST_CHECK_EQUAL( session.Journal().Operations().size(), 2 );

    for( const AI_SESSION_OPERATION_RECORD& record : session.Journal().Operations() )
        BOOST_CHECK( record.m_Kind == AI_SESSION_OPERATION_KIND::CreateTrackSegment );

    nlohmann::json summary =
            nlohmann::json::parse( session.ShadowBoard().QueryBoardSummary().ToStdString() );
    BOOST_CHECK_EQUAL( summary["items_total"].get<size_t>(), 2 );
    BOOST_CHECK_EQUAL( summary["track_segments"].get<size_t>(), 2 );

    std::vector<AI_SHADOW_ITEM> clkItems =
            session.ShadowBoard().QueryItems( wxS( "{\"net\":\"/CLK\"}" ) );
    BOOST_CHECK_EQUAL( clkItems.size(), 2 );
}


BOOST_AUTO_TEST_CASE( CreateZoneAndShapeRequireExplicitGeometry )
{
    AI_EXECUTION_SESSION session = makeSession();
    session.BeginStep( wxS( "validate create geometry" ) );

    AI_ATOMIC_EXECUTION_RESULT zone = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateZone,
            wxS( "{\"net\":\"GND\",\"layer_set\":[\"F.Cu\"]}" ) );

    BOOST_CHECK( !zone.m_Ok );
    BOOST_CHECK_EQUAL( zone.m_ErrorCode, wxString( wxS( "invalid_arguments" ) ) );

    AI_ATOMIC_EXECUTION_RESULT shape = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateShape,
            wxS( "{\"shape_type\":\"circle\",\"layer\":\"F.SilkS\"}" ) );

    BOOST_CHECK( !shape.m_Ok );
    BOOST_CHECK_EQUAL( shape.m_ErrorCode, wxString( wxS( "invalid_arguments" ) ) );
    BOOST_CHECK_EQUAL( session.ShadowBoard().LiveItemCount(), 0 );
    BOOST_CHECK( session.Journal().Operations().empty() );
}


BOOST_AUTO_TEST_CASE( CreateZoneRejectsInvalidTypedPropertiesBeforeJournalAppend )
{
    AI_EXECUTION_SESSION session = makeSession();
    session.BeginStep( wxS( "validate zone typed properties" ) );

    AI_ATOMIC_EXECUTION_RESULT invalidPriority = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateZone,
            wxS( "{\"alias\":\"bad-priority\",\"net\":\"GND\","
                 "\"layer_set\":[\"F.Cu\"],\"priority\":-1,"
                 "\"outline\":{\"points\":[{\"x\":0,\"y\":0},{\"x\":1000,\"y\":0},"
                 "{\"x\":1000,\"y\":1000},{\"x\":0,\"y\":1000}]}}" ) );

    BOOST_CHECK( !invalidPriority.m_Ok );
    BOOST_CHECK_EQUAL( invalidPriority.m_ErrorCode, wxString( wxS( "invalid_arguments" ) ) );

    AI_ATOMIC_EXECUTION_RESULT invalidFillMode = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateZone,
            wxS( "{\"alias\":\"bad-fill-mode\",\"net\":\"GND\","
                 "\"layer_set\":[\"F.Cu\"],\"fill_mode\":\"sparkle\","
                 "\"outline\":{\"points\":[{\"x\":0,\"y\":0},{\"x\":1000,\"y\":0},"
                 "{\"x\":1000,\"y\":1000},{\"x\":0,\"y\":1000}]}}" ) );

    BOOST_CHECK( !invalidFillMode.m_Ok );
    BOOST_CHECK_EQUAL( invalidFillMode.m_ErrorCode, wxString( wxS( "invalid_arguments" ) ) );

    AI_ATOMIC_EXECUTION_RESULT invalidClearance = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateZone,
            wxS( "{\"alias\":\"bad-clearance\",\"net\":\"GND\","
                 "\"layer_set\":[\"F.Cu\"],\"clearance\":-25,"
                 "\"outline\":{\"points\":[{\"x\":0,\"y\":0},{\"x\":1000,\"y\":0},"
                 "{\"x\":1000,\"y\":1000},{\"x\":0,\"y\":1000}]}}" ) );

    BOOST_CHECK( !invalidClearance.m_Ok );
    BOOST_CHECK_EQUAL( invalidClearance.m_ErrorCode, wxString( wxS( "invalid_arguments" ) ) );
    BOOST_CHECK_EQUAL( session.ShadowBoard().LiveItemCount(), 0 );
    BOOST_CHECK( session.Journal().Operations().empty() );
}


BOOST_AUTO_TEST_CASE( SetItemPropertiesMergesTypedPropertyPatchInShadowBoard )
{
    AI_EXECUTION_SESSION session = makeSession();
    session.BeginStep( wxS( "patch zone typed properties" ) );

    AI_ATOMIC_EXECUTION_RESULT zone = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateZone,
            wxS( "{\"alias\":\"patchable-zone\",\"net\":\"GND\","
                 "\"layer_set\":[\"F.Cu\"],\"clearance\":250000,\"priority\":2,"
                 "\"outline\":{\"points\":[{\"x\":0,\"y\":0},{\"x\":1000,\"y\":0},"
                 "{\"x\":1000,\"y\":1000},{\"x\":0,\"y\":1000}]}}" ) );
    BOOST_REQUIRE( zone.m_Ok );

    AI_ATOMIC_EXECUTION_RESULT props = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::SetItemProperties,
            wxS( "{\"handle\":\"patchable-zone\","
                 "\"typed_props\":{\"fill_mode\":\"hatch_pattern\"}}" ) );
    BOOST_REQUIRE( props.m_Ok );

    const AI_SHADOW_ITEM* item =
            session.ShadowBoard().FindItem( zone.m_CreatedHandles.front() );
    BOOST_REQUIRE( item );

    nlohmann::json typedProps =
            nlohmann::json::parse( item->m_PropertiesJson.ToStdString() );
    BOOST_CHECK_EQUAL( typedProps["clearance"].get<int>(), 250000 );
    BOOST_CHECK_EQUAL( typedProps["priority"].get<int>(), 2 );
    BOOST_CHECK_EQUAL( typedProps["fill_mode"].get<std::string>(),
                       "hatch_pattern" );
}


BOOST_AUTO_TEST_CASE( SetItemPropertiesRejectsInvalidZoneTypedPropsBeforeJournalAppend )
{
    AI_EXECUTION_SESSION session = makeSession();
    session.BeginStep( wxS( "reject invalid zone property patch" ) );

    AI_ATOMIC_EXECUTION_RESULT zone = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateZone,
            wxS( "{\"alias\":\"strict-zone\",\"net\":\"GND\","
                 "\"layer_set\":[\"F.Cu\"],\"clearance\":250000,"
                 "\"outline\":{\"points\":[{\"x\":0,\"y\":0},{\"x\":1000,\"y\":0},"
                 "{\"x\":1000,\"y\":1000},{\"x\":0,\"y\":1000}]}}" ) );
    BOOST_REQUIRE( zone.m_Ok );

    const size_t operationCountBefore = session.Journal().Operations().size();
    const AI_SHADOW_ITEM* before =
            session.ShadowBoard().FindItem( zone.m_CreatedHandles.front() );
    BOOST_REQUIRE( before );
    const wxString propertiesBefore = before->m_PropertiesJson;

    AI_ATOMIC_EXECUTION_RESULT props = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::SetItemProperties,
            wxS( "{\"handle\":\"strict-zone\","
                 "\"typed_props\":{\"clearance\":-1,\"fill_mode\":\"sparkle\"}}" ) );

    BOOST_CHECK( !props.m_Ok );
    BOOST_CHECK_EQUAL( props.m_ErrorCode, wxString( wxS( "invalid_arguments" ) ) );
    BOOST_CHECK_EQUAL( session.Journal().Operations().size(), operationCountBefore );

    const AI_SHADOW_ITEM* after =
            session.ShadowBoard().FindItem( zone.m_CreatedHandles.front() );
    BOOST_REQUIRE( after );
    BOOST_CHECK_EQUAL( after->m_PropertiesJson, propertiesBefore );
}


BOOST_AUTO_TEST_CASE( UpdateGeometryRejectsNonObjectPatchBeforeJournalAppend )
{
    AI_EXECUTION_SESSION session = makeSession();
    session.BeginStep( wxS( "create geometry patch target" ) );

    AI_ATOMIC_EXECUTION_RESULT created = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateVia,
            wxS( "{\"alias\":\"patch-target\",\"net\":\"GND\","
                 "\"position\":{\"x\":100,\"y\":200}}" ) );
    BOOST_REQUIRE( created.m_Ok );

    const uint64_t epochBefore = session.Epoch();
    const size_t operationCountBefore = session.Journal().Operations().size();

    AI_ATOMIC_EXECUTION_RESULT patch = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::UpdateItemGeometry,
            wxS( "{\"handle\":\"patch-target\","
                 "\"geometry_patch\":\"not an object\"}" ) );

    BOOST_CHECK( !patch.m_Ok );
    BOOST_CHECK_EQUAL( patch.m_ErrorCode, wxString( wxS( "invalid_arguments" ) ) );
    BOOST_CHECK_EQUAL( session.Epoch(), epochBefore );
    BOOST_CHECK_EQUAL( session.Journal().Operations().size(), operationCountBefore );
}


BOOST_AUTO_TEST_CASE( RunValidationStoresStructuredResultInJournal )
{
    AI_EXECUTION_SESSION session = makeSession();
    session.BeginStep( wxS( "validate shadow board" ) );

    AI_ATOMIC_EXECUTION_RESULT result = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::RunValidation,
            wxS( "{\"scope\":\"session\",\"level\":\"drc_lite\"}" ) );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_REQUIRE_EQUAL( session.Journal().Operations().size(), 1 );

    const AI_SESSION_OPERATION_RECORD& record = session.Journal().Operations().front();
    BOOST_CHECK( record.m_Kind == AI_SESSION_OPERATION_KIND::RunValidation );
    BOOST_REQUIRE( !record.m_ResultJson.IsEmpty() );

    nlohmann::json recordResult =
            nlohmann::json::parse( record.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( recordResult["status"].get<std::string>(),
                       "validation_completed" );
    BOOST_CHECK_EQUAL( recordResult["validation"]["scope"].get<std::string>(),
                       "session" );
    BOOST_CHECK_EQUAL( recordResult["validation"]["level"].get<std::string>(),
                       "drc_lite" );
    BOOST_CHECK_EQUAL( recordResult["validation"]["issue_count"].get<size_t>(), 0 );
    BOOST_REQUIRE_EQUAL( recordResult["validation"]["warnings"].size(), 1 );
    BOOST_CHECK( recordResult["validation"]["warnings"][0].get<std::string>().find(
                         "native DRC" )
                 != std::string::npos );

    nlohmann::json executionResult =
            nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( executionResult["validation"]["level"].get<std::string>(),
                       "drc_lite" );
}


BOOST_AUTO_TEST_CASE( RunValidationReportsZeroLengthTrackGeometryIssue )
{
    AI_EXECUTION_SESSION session = makeSession();
    session.BeginStep( wxS( "validate invalid track geometry" ) );

    AI_ATOMIC_EXECUTION_RESULT track = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateTrackSegment,
            wxS( "{\"alias\":\"bad-track\",\"net\":\"GND\",\"layer\":\"F.Cu\","
                 "\"start\":{\"x\":0,\"y\":0},\"end\":{\"x\":1000,\"y\":0}}" ) );
    BOOST_REQUIRE( track.m_Ok );

    AI_ATOMIC_EXECUTION_RESULT patch = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::UpdateItemGeometry,
            wxS( "{\"handle\":\"bad-track\","
                 "\"geometry_patch\":{\"start\":{\"x\":0,\"y\":0},"
                 "\"end\":{\"x\":0,\"y\":0}}}" ) );
    BOOST_REQUIRE( patch.m_Ok );

    AI_ATOMIC_EXECUTION_RESULT validation = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::RunValidation,
            wxS( "{\"scope\":\"session\",\"level\":\"geometry\"}" ) );

    BOOST_REQUIRE( validation.m_Ok );
    nlohmann::json result =
            nlohmann::json::parse( validation.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( result["validation"]["issue_count"].get<size_t>(), 1 );
    BOOST_REQUIRE_EQUAL( result["validation"]["issues"].size(), 1 );
    BOOST_CHECK_EQUAL( result["validation"]["issues"][0]["code"].get<std::string>(),
                       "zero_length_track_segment" );
    BOOST_CHECK_EQUAL( result["validation"]["issues"][0]["alias"].get<std::string>(),
                       "bad-track" );
}


BOOST_AUTO_TEST_CASE( RefillZonesResolvesZoneHandlesInJournal )
{
    AI_EXECUTION_SESSION session = makeSession();
    session.BeginStep( wxS( "create and refill zone" ) );

    AI_ATOMIC_EXECUTION_RESULT zone = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateZone,
            wxS( "{\"alias\":\"gnd-zone\",\"net\":\"GND\",\"layer_set\":[\"F.Cu\"],"
                 "\"outline\":{\"points\":[{\"x\":0,\"y\":0},{\"x\":1000,\"y\":0},"
                 "{\"x\":1000,\"y\":1000},{\"x\":0,\"y\":1000}]}}" ) );
    BOOST_REQUIRE( zone.m_Ok );

    AI_ATOMIC_EXECUTION_RESULT refill = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::RefillZones,
            wxS( "{\"handles\":[\"gnd-zone\"]}" ) );

    BOOST_REQUIRE( refill.m_Ok );
    BOOST_REQUIRE_EQUAL( refill.m_ResolvedHandles.size(), 1 );
    BOOST_CHECK_EQUAL( refill.m_ResolvedHandles.front().m_Alias,
                       wxString( wxS( "gnd-zone" ) ) );

    BOOST_REQUIRE_EQUAL( session.Journal().Operations().size(), 2 );
    const AI_SESSION_OPERATION_RECORD& record = session.Journal().Operations().back();
    BOOST_CHECK( record.m_Kind == AI_SESSION_OPERATION_KIND::RefillZones );
    BOOST_REQUIRE_EQUAL( record.m_ResolvedHandles.size(), 1 );
    BOOST_CHECK_EQUAL( record.m_ResolvedHandles.front().m_Alias,
                       wxString( wxS( "gnd-zone" ) ) );
}


BOOST_AUTO_TEST_CASE( QueryItemsCanFilterZonesByBoundingBox )
{
    AI_EXECUTION_SESSION session = makeSession();
    session.BeginStep( wxS( "create separated zones" ) );

    AI_ATOMIC_EXECUTION_RESULT nearZone = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateZone,
            wxS( "{\"alias\":\"near-zone\",\"net\":\"GND\",\"layer_set\":[\"F.Cu\"],"
                 "\"outline\":{\"points\":[{\"x\":0,\"y\":0},{\"x\":1000,\"y\":0},"
                 "{\"x\":1000,\"y\":1000},{\"x\":0,\"y\":1000}]}}" ) );
    BOOST_REQUIRE( nearZone.m_Ok );

    AI_ATOMIC_EXECUTION_RESULT farZone = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateZone,
            wxS( "{\"alias\":\"far-zone\",\"net\":\"GND\",\"layer_set\":[\"F.Cu\"],"
                 "\"outline\":{\"points\":[{\"x\":100000,\"y\":100000},"
                 "{\"x\":101000,\"y\":100000},{\"x\":101000,\"y\":101000},"
                 "{\"x\":100000,\"y\":101000}]}}" ) );
    BOOST_REQUIRE( farZone.m_Ok );

    std::vector<AI_SHADOW_ITEM> affectedZones = session.ShadowBoard().QueryItems(
            wxS( "{\"type\":\"zone\","
                 "\"bbox\":{\"x\":-100,\"y\":-100,\"width\":2000,\"height\":2000}}" ) );

    BOOST_REQUIRE_EQUAL( affectedZones.size(), 1 );
    BOOST_CHECK_EQUAL( affectedZones.front().m_Alias, wxString( wxS( "near-zone" ) ) );
}


BOOST_AUTO_TEST_CASE( MoveItemsTranslatesAnnularZoneOuterAndInnerRings )
{
    AI_EXECUTION_SESSION session = makeSession();
    session.BeginStep( wxS( "move annular zone" ) );

    AI_ATOMIC_EXECUTION_RESULT zone = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateZone,
            wxS( "{\"alias\":\"annular-zone\",\"net\":\"GND\","
                 "\"layer_set\":[\"F.Cu\"],"
                 "\"outline\":{\"outer\":[{\"x\":0,\"y\":0},{\"x\":100,\"y\":0},"
                 "{\"x\":100,\"y\":100},{\"x\":0,\"y\":100}],"
                 "\"inner\":[{\"x\":25,\"y\":25},{\"x\":75,\"y\":25},"
                 "{\"x\":75,\"y\":75},{\"x\":25,\"y\":75}]}}" ) );
    BOOST_REQUIRE( zone.m_Ok );

    AI_ATOMIC_EXECUTION_RESULT move = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::MoveItems,
            wxS( "{\"handles\":[\"annular-zone\"],\"delta\":{\"x\":10,\"y\":20}}" ) );
    BOOST_REQUIRE( move.m_Ok );

    std::vector<AI_SHADOW_ITEM> zones =
            session.ShadowBoard().QueryItems( wxS( "{\"alias\":\"annular-zone\"}" ) );
    BOOST_REQUIRE_EQUAL( zones.size(), 1 );

    nlohmann::json geometry =
            nlohmann::json::parse( zones.front().m_GeometryJson.ToStdString() );
    BOOST_CHECK_EQUAL( geometry["outer"][0]["x"].get<int>(), 10 );
    BOOST_CHECK_EQUAL( geometry["outer"][0]["y"].get<int>(), 20 );
    BOOST_CHECK_EQUAL( geometry["inner"][0]["x"].get<int>(), 35 );
    BOOST_CHECK_EQUAL( geometry["inner"][0]["y"].get<int>(), 45 );
}


BOOST_AUTO_TEST_CASE( UpdateItemGeometryCanReplaceZoneOutlineInShadowBoard )
{
    AI_EXECUTION_SESSION session = makeSession();
    session.BeginStep( wxS( "reshape zone shadow" ) );

    AI_ATOMIC_EXECUTION_RESULT zone = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateZone,
            wxS( "{\"alias\":\"zone-to-reshape\",\"net\":\"GND\","
                 "\"layer_set\":[\"F.Cu\"],"
                 "\"outline\":{\"points\":[{\"x\":0,\"y\":0},{\"x\":100,\"y\":0},"
                 "{\"x\":100,\"y\":100},{\"x\":0,\"y\":100}]}}" ) );
    BOOST_REQUIRE( zone.m_Ok );

    AI_ATOMIC_EXECUTION_RESULT patch = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::UpdateItemGeometry,
            wxS( "{\"handle\":\"zone-to-reshape\","
                 "\"geometry_patch\":{\"points\":[{\"x\":0,\"y\":0},"
                 "{\"x\":300,\"y\":0},{\"x\":300,\"y\":100},"
                 "{\"x\":0,\"y\":100}]}}" ) );

    BOOST_REQUIRE( patch.m_Ok );

    std::vector<AI_SHADOW_ITEM> zones =
            session.ShadowBoard().QueryItems( wxS( "{\"alias\":\"zone-to-reshape\"}" ) );
    BOOST_REQUIRE_EQUAL( zones.size(), 1 );
    nlohmann::json geometry =
            nlohmann::json::parse( zones.front().m_GeometryJson.ToStdString() );
    BOOST_CHECK_EQUAL( geometry["points"][1]["x"].get<int>(), 300 );
}


BOOST_AUTO_TEST_CASE( QueryItemsCanFilterSelectedShadowItems )
{
    AI_EXECUTION_SESSION session = makeSession();
    session.BeginStep( wxS( "create selectable vias" ) );

    AI_ATOMIC_EXECUTION_RESULT selectedVia = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateVia,
            wxS( "{\"alias\":\"selected-via\",\"net\":\"GND\","
                 "\"position\":{\"x\":10,\"y\":20}}" ) );
    BOOST_REQUIRE( selectedVia.m_Ok );

    AI_ATOMIC_EXECUTION_RESULT unselectedVia = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateVia,
            wxS( "{\"alias\":\"unselected-via\",\"net\":\"GND\","
                 "\"position\":{\"x\":30,\"y\":40}}" ) );
    BOOST_REQUIRE( unselectedVia.m_Ok );

    AI_ATOMIC_EXECUTION_RESULT metadata = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::SetMetadata,
            wxS( "{\"handle\":\"selected-via\","
                 "\"key_values\":{\"selected\":\"true\"}}" ) );
    BOOST_REQUIRE( metadata.m_Ok );

    std::vector<AI_SHADOW_ITEM> selectedItems =
            session.ShadowBoard().QueryItems( wxS( "{\"selection\":true}" ) );

    BOOST_REQUIRE_EQUAL( selectedItems.size(), 1 );
    BOOST_CHECK_EQUAL( selectedItems.front().m_Alias,
                       wxString( wxS( "selected-via" ) ) );
}


BOOST_AUTO_TEST_CASE( QueryItemsCanFilterByHandle )
{
    AI_EXECUTION_SESSION session = makeSession();
    session.BeginStep( wxS( "create handle filtered vias" ) );

    AI_ATOMIC_EXECUTION_RESULT firstVia = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateVia,
            wxS( "{\"alias\":\"handle-via-a\",\"net\":\"GND\","
                 "\"position\":{\"x\":10,\"y\":20}}" ) );
    BOOST_REQUIRE( firstVia.m_Ok );

    AI_ATOMIC_EXECUTION_RESULT secondVia = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateVia,
            wxS( "{\"alias\":\"handle-via-b\",\"net\":\"GND\","
                 "\"position\":{\"x\":30,\"y\":40}}" ) );
    BOOST_REQUIRE( secondVia.m_Ok );

    const AI_SESSION_HANDLE target = secondVia.m_CreatedHandles.front();
    const wxString filter = wxString::Format(
            wxS( "{\"handle\":{\"session_id\":%llu,"
                 "\"handle_id\":%llu,\"generation\":%llu}}" ),
            static_cast<unsigned long long>( target.m_SessionId ),
            static_cast<unsigned long long>( target.m_HandleId ),
            static_cast<unsigned long long>( target.m_Generation ) );

    std::vector<AI_SHADOW_ITEM> byObjectHandle = session.ShadowBoard().QueryItems( filter );
    BOOST_REQUIRE_EQUAL( byObjectHandle.size(), 1 );
    BOOST_CHECK_EQUAL( byObjectHandle.front().m_Alias,
                       wxString( wxS( "handle-via-b" ) ) );

    std::vector<AI_SHADOW_ITEM> byAliasHandle =
            session.ShadowBoard().QueryItems(
                    wxS( "{\"handle\":\"handle-via-a\"}" ) );
    BOOST_REQUIRE_EQUAL( byAliasHandle.size(), 1 );
    BOOST_CHECK_EQUAL( byAliasHandle.front().m_Alias,
                       wxString( wxS( "handle-via-a" ) ) );
}


BOOST_AUTO_TEST_CASE( RefillZonesResolvesAffectedAreaZoneHandlesInJournal )
{
    AI_EXECUTION_SESSION session = makeSession();
    session.BeginStep( wxS( "create and refill affected zone" ) );

    AI_ATOMIC_EXECUTION_RESULT nearZone = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateZone,
            wxS( "{\"alias\":\"near-zone\",\"net\":\"GND\",\"layer_set\":[\"F.Cu\"],"
                 "\"outline\":{\"points\":[{\"x\":0,\"y\":0},{\"x\":1000,\"y\":0},"
                 "{\"x\":1000,\"y\":1000},{\"x\":0,\"y\":1000}]}}" ) );
    BOOST_REQUIRE( nearZone.m_Ok );

    AI_ATOMIC_EXECUTION_RESULT farZone = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateZone,
            wxS( "{\"alias\":\"far-zone\",\"net\":\"GND\",\"layer_set\":[\"F.Cu\"],"
                 "\"outline\":{\"points\":[{\"x\":100000,\"y\":100000},"
                 "{\"x\":101000,\"y\":100000},{\"x\":101000,\"y\":101000},"
                 "{\"x\":100000,\"y\":101000}]}}" ) );
    BOOST_REQUIRE( farZone.m_Ok );

    AI_ATOMIC_EXECUTION_RESULT refill = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::RefillZones,
            wxS( "{\"affected_area\":{\"min\":{\"x\":-100,\"y\":-100},"
                 "\"max\":{\"x\":2000,\"y\":2000}}}" ) );

    BOOST_REQUIRE( refill.m_Ok );
    BOOST_REQUIRE_EQUAL( refill.m_ResolvedHandles.size(), 1 );
    BOOST_CHECK_EQUAL( refill.m_ResolvedHandles.front().m_Alias,
                       wxString( wxS( "near-zone" ) ) );

    BOOST_REQUIRE_EQUAL( session.Journal().Operations().size(), 3 );
    const AI_SESSION_OPERATION_RECORD& record = session.Journal().Operations().back();
    BOOST_CHECK( record.m_Kind == AI_SESSION_OPERATION_KIND::RefillZones );
    BOOST_REQUIRE_EQUAL( record.m_ResolvedHandles.size(), 1 );
    BOOST_CHECK_EQUAL( record.m_ResolvedHandles.front().m_Alias,
                       wxString( wxS( "near-zone" ) ) );
}


BOOST_AUTO_TEST_CASE( SetItemLayerAcceptsLayerSetForMultiLayerItems )
{
    AI_EXECUTION_SESSION session = makeSession();
    session.BeginStep( wxS( "make zone multi-layer" ) );

    AI_ATOMIC_EXECUTION_RESULT zone = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateZone,
            wxS( "{\"alias\":\"zone0\",\"net\":\"GND\",\"layer_set\":[\"F.Cu\"],"
                 "\"outline\":{\"points\":[{\"x\":0,\"y\":0},{\"x\":1000,\"y\":0},"
                 "{\"x\":1000,\"y\":1000},{\"x\":0,\"y\":1000}]}}" ) );
    BOOST_REQUIRE( zone.m_Ok );

    AI_ATOMIC_EXECUTION_RESULT setLayer = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::SetItemLayer,
            wxS( "{\"handle\":\"zone0\",\"layer_set\":[\"F.Cu\",\"B.Cu\"]}" ) );

    BOOST_REQUIRE( setLayer.m_Ok );
    BOOST_REQUIRE_EQUAL( setLayer.m_ResolvedHandles.size(), 1 );

    const AI_SHADOW_ITEM* item =
            session.ShadowBoard().FindItem( zone.m_CreatedHandles.front() );
    BOOST_REQUIRE( item );
    BOOST_CHECK_EQUAL( item->m_Layer, wxString( wxS( "F.Cu" ) ) );
    BOOST_REQUIRE_EQUAL( item->m_Layers.size(), 2 );
    BOOST_CHECK_EQUAL( item->m_Layers[0], wxString( wxS( "F.Cu" ) ) );
    BOOST_CHECK_EQUAL( item->m_Layers[1], wxString( wxS( "B.Cu" ) ) );

    BOOST_REQUIRE_EQUAL( session.Journal().Operations().size(), 2 );
    BOOST_CHECK( session.Journal().Operations().back().m_Kind
                 == AI_SESSION_OPERATION_KIND::SetItemLayer );
}


BOOST_AUTO_TEST_CASE( CheckpointRollbackRestoresSemanticShadowBoard )
{
    AI_EXECUTION_SESSION session = makeSession();

    const uint64_t stableStepId = session.BeginStep( wxS( "place stable via" ) );
    AI_ATOMIC_EXECUTION_RESULT stable = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateVia,
            wxS( "{\"alias\":\"stable\",\"net\":\"GND\","
                 "\"position\":{\"x\":0,\"y\":0}}" ) );
    session.EndStep( stableStepId );
    BOOST_REQUIRE( stable.m_Ok );

    const uint64_t checkpoint = session.Checkpoint( wxS( "before trial" ) );

    const uint64_t trialStepId = session.BeginStep( wxS( "trial via" ) );
    AI_ATOMIC_EXECUTION_RESULT trial = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateVia,
            wxS( "{\"alias\":\"trial\",\"net\":\"GND\","
                 "\"position\":{\"x\":100,\"y\":100}}" ) );
    session.EndStep( trialStepId );
    BOOST_REQUIRE( trial.m_Ok );
    BOOST_CHECK_EQUAL( session.ShadowBoard().LiveItemCount(), 2 );

    BOOST_REQUIRE( session.RollbackTo( checkpoint ) );
    BOOST_CHECK_EQUAL( session.ShadowBoard().LiveItemCount(), 1 );
    BOOST_CHECK( session.ShadowBoard().FindItem( stable.m_CreatedHandles.front() ) );
    BOOST_CHECK( !session.ShadowBoard().FindItem( trial.m_CreatedHandles.front() ) );
    BOOST_CHECK( session.ResolveHandle( trial.m_CreatedHandles.front() )
                 == AI_SESSION_HANDLE_STATUS::Stale );
}


BOOST_AUTO_TEST_CASE( HandleMutationsMoveSetMetadataAndDeleteItems )
{
    AI_EXECUTION_SESSION session = makeSession();

    session.BeginStep( wxS( "create and edit via" ) );
    AI_ATOMIC_EXECUTION_RESULT created = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateVia,
            wxS( "{\"alias\":\"editable\",\"net\":\"GND\","
                 "\"position\":{\"x\":10,\"y\":20}}" ) );
    BOOST_REQUIRE( created.m_Ok );

    AI_SESSION_HANDLE handle = created.m_CreatedHandles.front();
    wxString handleJson = wxString::Format(
            wxS( "{\"handle_id\":%llu,\"generation\":%llu}" ),
            static_cast<unsigned long long>( handle.m_HandleId ),
            static_cast<unsigned long long>( handle.m_Generation ) );

    AI_ATOMIC_EXECUTION_RESULT move = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::MoveItems,
            wxString::Format( wxS( "{\"handles\":[%s],\"delta\":{\"x\":5,\"y\":-5}}" ),
                              handleJson ) );
    BOOST_REQUIRE( move.m_Ok );

    AI_ATOMIC_EXECUTION_RESULT metadata = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::SetMetadata,
            wxString::Format( wxS( "{\"handle\":%s,\"key_values\":{\"phase\":\"trial\"}}" ),
                              handleJson ) );
    BOOST_REQUIRE( metadata.m_Ok );

    const AI_SHADOW_ITEM* edited = session.ShadowBoard().FindItem( handle );
    BOOST_REQUIRE( edited );
    BOOST_CHECK( edited->m_GeometryJson.Contains( wxS( "\"x\":15" ) ) );
    BOOST_REQUIRE( edited->m_Metadata.count( wxS( "phase" ) ) );
    BOOST_CHECK_EQUAL( edited->m_Metadata.at( wxS( "phase" ) ),
                       wxString( wxS( "trial" ) ) );

    AI_ATOMIC_EXECUTION_RESULT deleted = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::DeleteItems,
            wxString::Format( wxS( "{\"handles\":[%s]}" ), handleJson ) );
    BOOST_REQUIRE( deleted.m_Ok );

    BOOST_CHECK( !session.ShadowBoard().FindItem( handle ) );
    BOOST_CHECK_EQUAL( session.ShadowBoard().LiveItemCount(), 0 );
}


BOOST_AUTO_TEST_CASE( MoveItemsCanUseTargetPositionsForSingleHandle )
{
    AI_EXECUTION_SESSION session = makeSession();

    session.BeginStep( wxS( "target move via" ) );
    AI_ATOMIC_EXECUTION_RESULT created = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateVia,
            wxS( "{\"alias\":\"targetable\",\"net\":\"GND\","
                 "\"position\":{\"x\":10,\"y\":20}}" ) );
    BOOST_REQUIRE( created.m_Ok );

    AI_SESSION_HANDLE handle = created.m_CreatedHandles.front();
    wxString handleJson = wxString::Format(
            wxS( "{\"handle_id\":%llu,\"generation\":%llu}" ),
            static_cast<unsigned long long>( handle.m_HandleId ),
            static_cast<unsigned long long>( handle.m_Generation ) );

    AI_ATOMIC_EXECUTION_RESULT move = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::MoveItems,
            wxString::Format(
                    wxS( "{\"handles\":[%s],\"target_positions\":{\"x\":50,\"y\":60}}" ),
                    handleJson ) );
    BOOST_REQUIRE( move.m_Ok );

    const AI_SHADOW_ITEM* moved = session.ShadowBoard().FindItem( handle );
    BOOST_REQUIRE( moved );
    BOOST_CHECK( moved->m_GeometryJson.Contains( wxS( "\"x\":50" ) ) );
    BOOST_CHECK( moved->m_GeometryJson.Contains( wxS( "\"y\":60" ) ) );
}


BOOST_AUTO_TEST_CASE( FailedMultiHandleMutationLeavesShadowBoardAndJournalUnchanged )
{
    AI_EXECUTION_SESSION session = makeSession();

    session.BeginStep( wxS( "create vias before failed move" ) );
    AI_ATOMIC_EXECUTION_RESULT first = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateVia,
            wxS( "{\"alias\":\"mv0\",\"position\":{\"x\":10,\"y\":20}}" ) );
    BOOST_REQUIRE( first.m_Ok );

    AI_ATOMIC_EXECUTION_RESULT second = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateVia,
            wxS( "{\"alias\":\"mv1\",\"position\":{\"x\":30,\"y\":40}}" ) );
    BOOST_REQUIRE( second.m_Ok );

    const uint64_t epochBefore = session.Epoch();
    const size_t operationCountBefore = session.Journal().Operations().size();
    const wxString firstGeometryBefore =
            session.ShadowBoard().FindItem( first.m_CreatedHandles.front() )->m_GeometryJson;
    const wxString secondGeometryBefore =
            session.ShadowBoard().FindItem( second.m_CreatedHandles.front() )->m_GeometryJson;

    AI_ATOMIC_EXECUTION_RESULT move = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::MoveItems,
            wxS( "{\"handles\":[\"mv0\",\"mv1\"],"
                 "\"target_positions\":[{\"x\":100,\"y\":200}]}" ) );

    BOOST_CHECK( !move.m_Ok );
    BOOST_CHECK_EQUAL( move.m_ErrorCode, wxString( wxS( "mutation_failed" ) ) );
    BOOST_CHECK_EQUAL( session.Epoch(), epochBefore );
    BOOST_CHECK_EQUAL( session.Journal().Operations().size(), operationCountBefore );

    const AI_SHADOW_ITEM* firstAfter =
            session.ShadowBoard().FindItem( first.m_CreatedHandles.front() );
    const AI_SHADOW_ITEM* secondAfter =
            session.ShadowBoard().FindItem( second.m_CreatedHandles.front() );
    BOOST_REQUIRE( firstAfter );
    BOOST_REQUIRE( secondAfter );
    BOOST_CHECK_EQUAL( firstAfter->m_GeometryJson, firstGeometryBefore );
    BOOST_CHECK_EQUAL( secondAfter->m_GeometryJson, secondGeometryBefore );
}


BOOST_AUTO_TEST_SUITE_END()
