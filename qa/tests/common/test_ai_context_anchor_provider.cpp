#include <boost/test/unit_test.hpp>

#include <json_common.h>
#include <kisurf/ai/ai_context_anchor_provider.h>

#include <string>
#include <vector>

namespace
{
std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


nlohmann::json detailsJson( const AI_CONTEXT_ANCHOR& aAnchor )
{
    BOOST_REQUIRE( !aAnchor.m_DetailsJson.IsEmpty() );
    return nlohmann::json::parse( toUtf8String( aAnchor.m_DetailsJson ) );
}


const AI_CONTEXT_ANCHOR* findAnchor( const AI_CONTEXT_SNAPSHOT& aSnapshot,
                                     const wxString& aId )
{
    for( const AI_CONTEXT_ANCHOR& anchor : aSnapshot.m_Anchors )
    {
        if( anchor.m_Id == aId )
            return &anchor;
    }

    return nullptr;
}


AI_CONTEXT_ANCHOR existingPadAnchor()
{
    AI_CONTEXT_ANCHOR anchor;
    anchor.m_Id = wxS( "pcb.pad.existing.center" );
    anchor.m_Kind = AI_CONTEXT_ANCHOR_KIND::RouteTarget;
    anchor.m_EditorKind = AI_EDITOR_KIND::Pcb;
    anchor.m_Label = wxS( "pad:U1.1:center" );
    anchor.m_Position = VECTOR2I( 1000, 2000 );
    anchor.m_HasPosition = true;
    anchor.m_Confidence = 1.0;
    return anchor;
}


AI_CONTEXT_ANCHOR existingPlacementAnchor()
{
    AI_CONTEXT_ANCHOR anchor;
    anchor.m_Id = wxS( "tool.placement.cursor" );
    anchor.m_Kind = AI_CONTEXT_ANCHOR_KIND::PlacementCandidate;
    anchor.m_EditorKind = AI_EDITOR_KIND::Pcb;
    anchor.m_Label = wxS( "placement:stale" );
    anchor.m_Position = VECTOR2I( 10, 20 );
    anchor.m_HasPosition = true;
    anchor.m_Confidence = 0.25;
    return anchor;
}


AI_CONTEXT_SNAPSHOT routingSnapshot( const wxString& aModeContext )
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_Anchors.push_back( existingPadAnchor() );
    snapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::RoutingTrack;
    snapshot.m_ToolState.m_ModeContextJson = aModeContext;
    return snapshot;
}


AI_CONTEXT_SNAPSHOT placementSnapshot( const wxString& aModeContext )
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_Anchors.push_back( existingPadAnchor() );
    snapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    snapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::PlacingFootprint;
    snapshot.m_ToolState.m_ModeContextJson = aModeContext;
    return snapshot;
}


wxString routingModeContextWithCursor()
{
    return wxS( "{\"net\":\"/GPIO\",\"layer\":\"F.Cu\",\"width\":150000,"
                "\"start\":{\"x\":100,\"y\":200},"
                "\"cursor\":{\"x\":500,\"y\":350}}" );
}
} // namespace


BOOST_AUTO_TEST_SUITE( AiContextAnchorProvider )


BOOST_AUTO_TEST_CASE( RoutingToolStateAddsDynamicRouteAnchors )
{
    AI_CONTEXT_SNAPSHOT snapshot = routingSnapshot( routingModeContextWithCursor() );

    AppendAiToolStateAnchors( snapshot );

    BOOST_CHECK( findAnchor( snapshot, wxS( "pcb.pad.existing.center" ) ) );

    const AI_CONTEXT_ANCHOR* start =
            findAnchor( snapshot, wxS( "tool.routing.start" ) );
    const AI_CONTEXT_ANCHOR* currentEnd =
            findAnchor( snapshot, wxS( "tool.routing.current_end" ) );
    const AI_CONTEXT_ANCHOR* horizontal =
            findAnchor( snapshot, wxS( "tool.routing.orthogonal.horizontal" ) );
    const AI_CONTEXT_ANCHOR* vertical =
            findAnchor( snapshot, wxS( "tool.routing.orthogonal.vertical" ) );
    const AI_CONTEXT_ANCHOR* fortyFive =
            findAnchor( snapshot, wxS( "tool.routing.fortyfive.horizontal" ) );

    BOOST_REQUIRE( start );
    BOOST_REQUIRE( currentEnd );
    BOOST_REQUIRE( horizontal );
    BOOST_REQUIRE( vertical );
    BOOST_REQUIRE( fortyFive );
    BOOST_CHECK( !findAnchor( snapshot, wxS( "tool.routing.fortyfive.vertical" ) ) );

    BOOST_CHECK_EQUAL( static_cast<int>( start->m_Kind ),
                       static_cast<int>( AI_CONTEXT_ANCHOR_KIND::RouteStart ) );
    BOOST_CHECK_EQUAL( static_cast<int>( currentEnd->m_Kind ),
                       static_cast<int>( AI_CONTEXT_ANCHOR_KIND::RouteCandidate ) );
    BOOST_CHECK_EQUAL( static_cast<int>( horizontal->m_Kind ),
                       static_cast<int>( AI_CONTEXT_ANCHOR_KIND::OrthogonalBreakout ) );
    BOOST_CHECK_EQUAL( static_cast<int>( fortyFive->m_Kind ),
                       static_cast<int>( AI_CONTEXT_ANCHOR_KIND::FortyFiveIntersection ) );

    BOOST_CHECK_EQUAL( start->m_Position.x, 100 );
    BOOST_CHECK_EQUAL( start->m_Position.y, 200 );
    BOOST_CHECK_EQUAL( currentEnd->m_Position.x, 500 );
    BOOST_CHECK_EQUAL( currentEnd->m_Position.y, 350 );
    BOOST_CHECK_EQUAL( horizontal->m_Position.x, 500 );
    BOOST_CHECK_EQUAL( horizontal->m_Position.y, 200 );
    BOOST_CHECK_EQUAL( vertical->m_Position.x, 100 );
    BOOST_CHECK_EQUAL( vertical->m_Position.y, 350 );
    BOOST_CHECK_EQUAL( fortyFive->m_Position.x, 350 );
    BOOST_CHECK_EQUAL( fortyFive->m_Position.y, 200 );

    nlohmann::json details = detailsJson( *fortyFive );
    BOOST_CHECK_EQUAL( details["source"].get<std::string>(), "tool_state" );
    BOOST_CHECK_EQUAL( details["mode"].get<std::string>(), "routing_track" );
    BOOST_CHECK_EQUAL( details["role"].get<std::string>(), "fortyfive_horizontal" );
    BOOST_CHECK_EQUAL( details["net"].get<std::string>(), "/GPIO" );
    BOOST_CHECK_EQUAL( details["layer"].get<std::string>(), "F.Cu" );
    BOOST_CHECK_EQUAL( details["width"].get<int>(), 150000 );
    BOOST_CHECK_EQUAL( details["start"]["x"].get<int>(), 100 );
    BOOST_CHECK_EQUAL( details["target"]["y"].get<int>(), 350 );
    BOOST_CHECK_EQUAL( details["position"]["x"].get<int>(), 350 );
}


BOOST_AUTO_TEST_CASE( RoutingToolStateUsesCurrentEndWhenCursorMissing )
{
    AI_CONTEXT_SNAPSHOT snapshot = routingSnapshot(
            wxS( "{\"net\":\"/GPIO\",\"layer\":\"B.Cu\",\"width\":120000,"
                 "\"start\":{\"x\":100,\"y\":200},"
                 "\"current_end\":{\"x\":210,\"y\":260}}" ) );

    AppendAiToolStateAnchors( snapshot );

    const AI_CONTEXT_ANCHOR* currentEnd =
            findAnchor( snapshot, wxS( "tool.routing.current_end" ) );

    BOOST_REQUIRE( currentEnd );
    BOOST_CHECK_EQUAL( currentEnd->m_Position.x, 210 );
    BOOST_CHECK_EQUAL( currentEnd->m_Position.y, 260 );
}


BOOST_AUTO_TEST_CASE( RoutingToolStateFallsBackToToolCursorPosition )
{
    AI_CONTEXT_SNAPSHOT snapshot = routingSnapshot(
            wxS( "{\"net\":\"/GPIO\",\"layer\":\"F.Cu\",\"width\":150000,"
                 "\"start\":{\"x\":100,\"y\":200}}" ) );
    snapshot.m_ToolState.m_HasCursorBoardPosition = true;
    snapshot.m_ToolState.m_CursorBoardPosition = VECTOR2I( 300, 400 );

    AppendAiToolStateAnchors( snapshot );

    const AI_CONTEXT_ANCHOR* currentEnd =
            findAnchor( snapshot, wxS( "tool.routing.current_end" ) );

    BOOST_REQUIRE( currentEnd );
    BOOST_CHECK_EQUAL( currentEnd->m_Position.x, 300 );
    BOOST_CHECK_EQUAL( currentEnd->m_Position.y, 400 );
}


BOOST_AUTO_TEST_CASE( FootprintPlacementAddsCandidateAnchorsAroundCursor )
{
    AI_CONTEXT_SNAPSHOT snapshot = placementSnapshot(
            wxS( "{\"mode\":\"placing_footprint\","
                 "\"cursor\":{\"x\":1000,\"y\":2000},"
                 "\"pitch\":250}" ) );

    AppendAiToolStateAnchors( snapshot );

    const AI_CONTEXT_ANCHOR* cursor =
            findAnchor( snapshot, wxS( "tool.placement.cursor" ) );
    const AI_CONTEXT_ANCHOR* east =
            findAnchor( snapshot, wxS( "tool.placement.grid.east" ) );
    const AI_CONTEXT_ANCHOR* south =
            findAnchor( snapshot, wxS( "tool.placement.grid.south" ) );
    const AI_CONTEXT_ANCHOR* diagonal =
            findAnchor( snapshot, wxS( "tool.placement.grid.diagonal" ) );

    BOOST_REQUIRE( cursor );
    BOOST_REQUIRE( east );
    BOOST_REQUIRE( south );
    BOOST_REQUIRE( diagonal );

    BOOST_CHECK_EQUAL( static_cast<int>( cursor->m_Kind ),
                       static_cast<int>( AI_CONTEXT_ANCHOR_KIND::PlacementCandidate ) );
    BOOST_CHECK_EQUAL( cursor->m_Position.x, 1000 );
    BOOST_CHECK_EQUAL( cursor->m_Position.y, 2000 );
    BOOST_CHECK_EQUAL( east->m_Position.x, 1250 );
    BOOST_CHECK_EQUAL( east->m_Position.y, 2000 );
    BOOST_CHECK_EQUAL( south->m_Position.x, 1000 );
    BOOST_CHECK_EQUAL( south->m_Position.y, 2250 );
    BOOST_CHECK_EQUAL( diagonal->m_Position.x, 1250 );
    BOOST_CHECK_EQUAL( diagonal->m_Position.y, 2250 );

    nlohmann::json details = detailsJson( *east );
    BOOST_CHECK_EQUAL( details["source"].get<std::string>(), "tool_state" );
    BOOST_CHECK_EQUAL( details["mode"].get<std::string>(), "placing_footprint" );
    BOOST_CHECK_EQUAL( details["role"].get<std::string>(), "grid_east" );
    BOOST_CHECK_EQUAL( details["pitch"].get<int>(), 250 );
    BOOST_CHECK_EQUAL( details["cursor"]["x"].get<int>(), 1000 );
    BOOST_CHECK_EQUAL( details["position"]["x"].get<int>(), 1250 );
}


BOOST_AUTO_TEST_CASE( FootprintPlacementUsesSnapshotCursorWhenModeCursorMissing )
{
    AI_CONTEXT_SNAPSHOT snapshot =
            placementSnapshot( wxS( "{\"mode\":\"placing_footprint\","
                                    "\"placement_pitch\":300}" ) );
    snapshot.m_ToolState.m_HasCursorBoardPosition = true;
    snapshot.m_ToolState.m_CursorBoardPosition = VECTOR2I( 700, 900 );

    AppendAiToolStateAnchors( snapshot );

    const AI_CONTEXT_ANCHOR* diagonal =
            findAnchor( snapshot, wxS( "tool.placement.grid.diagonal" ) );

    BOOST_REQUIRE( diagonal );
    BOOST_CHECK_EQUAL( diagonal->m_Position.x, 1000 );
    BOOST_CHECK_EQUAL( diagonal->m_Position.y, 1200 );
}


BOOST_AUTO_TEST_CASE( FootprintPlacementUsesDefaultPitchWhenModePitchMissing )
{
    AI_CONTEXT_SNAPSHOT snapshot = placementSnapshot(
            wxS( "{\"mode\":\"placing_footprint\","
                 "\"cursor\":{\"x\":100,\"y\":200}}" ) );

    AppendAiToolStateAnchors( snapshot );

    const AI_CONTEXT_ANCHOR* east =
            findAnchor( snapshot, wxS( "tool.placement.grid.east" ) );

    BOOST_REQUIRE( east );
    BOOST_CHECK_EQUAL( east->m_Position.x, 1000100 );
    BOOST_CHECK_EQUAL( east->m_Position.y, 200 );

    nlohmann::json details = detailsJson( *east );
    BOOST_CHECK_EQUAL( details["pitch"].get<int>(), 1000000 );
}


BOOST_AUTO_TEST_CASE( FootprintPlacementClearsStalePlacementAnchorsWithoutCursor )
{
    AI_CONTEXT_SNAPSHOT snapshot =
            placementSnapshot( wxS( "{\"mode\":\"placing_footprint\"}" ) );
    snapshot.m_Anchors.push_back( existingPlacementAnchor() );

    AppendAiToolStateAnchors( snapshot );

    BOOST_CHECK( !findAnchor( snapshot, wxS( "tool.placement.cursor" ) ) );
    BOOST_CHECK( findAnchor( snapshot, wxS( "pcb.pad.existing.center" ) ) );
}


BOOST_AUTO_TEST_CASE( InactiveOrIncompleteToolStateAddsNoAnchors )
{
    std::vector<AI_CONTEXT_SNAPSHOT> snapshots;

    AI_CONTEXT_SNAPSHOT selecting = routingSnapshot( routingModeContextWithCursor() );
    selecting.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::Selecting;
    snapshots.push_back( selecting );

    AI_CONTEXT_SNAPSHOT schematic = routingSnapshot( routingModeContextWithCursor() );
    schematic.m_EditorKind = AI_EDITOR_KIND::Schematic;
    snapshots.push_back( schematic );

    snapshots.push_back( routingSnapshot( wxS( "not-json" ) ) );

    snapshots.push_back( routingSnapshot(
            wxS( "{\"net\":\"/GPIO\",\"layer\":\"F.Cu\","
                 "\"start\":{\"x\":100,\"y\":200},"
                 "\"cursor\":{\"x\":500,\"y\":350}}" ) ) );

    snapshots.push_back( routingSnapshot(
            wxS( "{\"net\":\"/GPIO\",\"layer\":\"F.Cu\",\"width\":150000,"
                 "\"start\":{\"x\":100,\"y\":200},"
                 "\"cursor\":{\"x\":100,\"y\":200}}" ) ) );

    for( AI_CONTEXT_SNAPSHOT& snapshot : snapshots )
    {
        AppendAiToolStateAnchors( snapshot );
        BOOST_REQUIRE_EQUAL( snapshot.m_Anchors.size(), 1 );
        BOOST_CHECK( findAnchor( snapshot, wxS( "pcb.pad.existing.center" ) ) );
    }
}


BOOST_AUTO_TEST_SUITE_END()
