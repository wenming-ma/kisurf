#include <boost/test/unit_test.hpp>

#include <kisurf_ai_pcb_object_resolver.h>
#include <kisurf_ai_pcb_operation_edit_adapter.h>

#include <board.h>
#include <board_commit.h>
#include <commit.h>
#include <netinfo.h>
#include <pcb_shape.h>
#include <pcb_track.h>
#include <tool/tool_manager.h>
#include <zone.h>

#include <cstdlib>
#include <vector>
#include <wx/string.h>

namespace
{

class PCB_ADD_SPY_COMMIT : public COMMIT
{
public:
    struct ADDED_ITEM
    {
        BOARD_ITEM*  m_Item = nullptr;
        BASE_SCREEN* m_Screen = nullptr;
        CHANGE_TYPE  m_ChangeType = CHT_ADD;
    };

    explicit PCB_ADD_SPY_COMMIT( BOARD& aBoard ) :
            m_Board( aBoard )
    {
    }

    COMMIT& Stage( EDA_ITEM* aItem, CHANGE_TYPE aChangeType,
                   BASE_SCREEN* aScreen = nullptr,
                   RECURSE_MODE aRecurse = RECURSE_MODE::NO_RECURSE ) override
    {
        wxUnusedVar( aRecurse );

        BOARD_ITEM* boardItem = dynamic_cast<BOARD_ITEM*>( aItem );
        int         changeType = aChangeType & CHT_TYPE;

        if( boardItem && changeType == CHT_ADD )
            m_StagedAdded.push_back( { boardItem, aScreen, aChangeType } );

        return *this;
    }

    void Push( const wxString& aMessage = wxEmptyString, int aFlags = 0 ) override
    {
        wxUnusedVar( aFlags );
        ++m_PushCount;
        m_LastMessage = aMessage;

        for( const ADDED_ITEM& item : m_StagedAdded )
        {
            m_Board.Add( item.m_Item );
            m_Added.push_back( item );
        }

        m_StagedAdded.clear();
    }

    void Revert() override
    {
        ++m_RevertCount;

        for( const ADDED_ITEM& item : m_StagedAdded )
            delete item.m_Item;

        m_StagedAdded.clear();
    }

    std::vector<ADDED_ITEM> m_Added;
    int                    m_PushCount = 0;
    int                    m_RevertCount = 0;
    wxString               m_LastMessage;

private:
    EDA_ITEM* undoLevelItem( EDA_ITEM* aItem ) const override { return aItem; }
    EDA_ITEM* makeImage( EDA_ITEM* aItem ) const override { return aItem->Clone(); }

    BOARD&                  m_Board;
    std::vector<ADDED_ITEM> m_StagedAdded;
};


struct PCB_OPERATION_FIXTURE
{
    PCB_OPERATION_FIXTURE()
    {
        m_Gnd = new NETINFO_ITEM( &m_Board, wxS( "GND" ), 1 );
        m_Board.Add( m_Gnd );
    }

    BOARD         m_Board;
    NETINFO_ITEM* m_Gnd = nullptr;
};


AI_OBJECT_REF routePreviewRef( const wxString& aNet = wxS( "GND" ) )
{
    return AI_OBJECT_REF(
            KIID(), PCB_TRACE_T, wxS( "preview:route" ),
            wxString::Format(
                    wxS( "{\"operation\":\"route_segment_preview\",\"net\":\"%s\","
                         "\"layer\":\"F.Cu\",\"width\":150000,"
                         "\"start\":{\"x\":100,\"y\":200},"
                         "\"end\":{\"x\":300,\"y\":200}}" ),
                    aNet ) );
}


AI_OBJECT_REF viaPreviewRef()
{
    return AI_OBJECT_REF(
            KIID(), PCB_VIA_T, wxS( "preview:via" ),
            wxS( "{\"operation\":\"place_via_preview\",\"net\":\"GND\","
                 "\"diameter\":600000,\"drill\":300000,"
                 "\"position\":{\"x\":400,\"y\":500}}" ) );
}


AI_OBJECT_REF shapePreviewRef()
{
    return AI_OBJECT_REF(
            KIID(), PCB_SHAPE_T, wxS( "preview:shape" ),
            wxS( "{\"operation\":\"create_shape_preview\",\"shape\":\"rectangle\","
                 "\"layer\":\"F.SilkS\",\"width\":120000,"
                 "\"start\":{\"x\":10,\"y\":20},"
                 "\"end\":{\"x\":110,\"y\":220}}" ) );
}


AI_OBJECT_REF circleShapePreviewRef()
{
    return AI_OBJECT_REF(
            KIID(), PCB_SHAPE_T, wxS( "preview:circle" ),
            wxS( "{\"operation\":\"create_shape_preview\",\"shape\":\"circle\","
                 "\"layer\":\"F.SilkS\",\"width\":50000,"
                 "\"center\":{\"x\":400,\"y\":500},"
                 "\"radius\":125000}" ) );
}


AI_OBJECT_REF arcShapePreviewRef()
{
    return AI_OBJECT_REF(
            KIID(), PCB_SHAPE_T, wxS( "preview:arc" ),
            wxS( "{\"operation\":\"create_shape_preview\",\"shape\":\"arc\","
                 "\"layer\":\"F.SilkS\",\"width\":50000,"
                 "\"start\":{\"x\":0,\"y\":0},"
                 "\"mid\":{\"x\":50,\"y\":100},"
                 "\"end\":{\"x\":100,\"y\":0}}" ) );
}


AI_OBJECT_REF zonePreviewRef( const wxString& aNet = wxS( "GND" ) )
{
    return AI_OBJECT_REF(
            KIID(), PCB_ZONE_T, wxS( "preview:copper_zone" ),
            wxString::Format(
                    wxS( "{\"operation\":\"create_copper_zone_preview\",\"net\":\"%s\","
                         "\"layer\":\"F.Cu\",\"points\":["
                         "{\"x\":0,\"y\":0},{\"x\":1000,\"y\":0},"
                         "{\"x\":1000,\"y\":500},{\"x\":0,\"y\":500}]}" ),
                    aNet ) );
}


std::vector<PCB_TRACK*> boardTracksOfType( BOARD& aBoard, KICAD_T aType )
{
    std::vector<PCB_TRACK*> items;

    for( PCB_TRACK* track : aBoard.Tracks() )
    {
        if( track->Type() == aType )
            items.push_back( track );
    }

    return items;
}

} // namespace


BOOST_AUTO_TEST_SUITE( AiPcbOperationEditAdapter )


BOOST_AUTO_TEST_CASE( SessionAddsRouteSegmentThroughOneCommit )
{
    PCB_OPERATION_FIXTURE         fixture;
    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    PCB_ADD_SPY_COMMIT            commit( fixture.m_Board );
    KISURF_AI_PCB_OPERATION_EDIT_ADAPTER adapter( resolver, commit );
    AI_EDIT_SESSION                       session( adapter );

    BOOST_CHECK( session.Apply( { routePreviewRef() }, AI_VALIDATION_SUMMARY() ) );

    BOOST_REQUIRE_EQUAL( commit.m_Added.size(), 1 );
    BOOST_CHECK_EQUAL( commit.m_PushCount, 1 );
    BOOST_CHECK_EQUAL( commit.m_RevertCount, 0 );
    BOOST_CHECK( adapter.WasCommitted() );
    BOOST_CHECK( !adapter.WasReverted() );

    std::vector<PCB_TRACK*> tracks = boardTracksOfType( fixture.m_Board, PCB_TRACE_T );
    BOOST_REQUIRE_EQUAL( tracks.size(), 1 );

    PCB_TRACK* track = tracks.front();
    BOOST_CHECK_EQUAL( track->GetStart().x, 100 );
    BOOST_CHECK_EQUAL( track->GetStart().y, 200 );
    BOOST_CHECK_EQUAL( track->GetEnd().x, 300 );
    BOOST_CHECK_EQUAL( track->GetEnd().y, 200 );
    BOOST_CHECK_EQUAL( track->GetLayer(), F_Cu );
    BOOST_CHECK_EQUAL( track->GetWidth(), 150000 );
    BOOST_CHECK_EQUAL( track->GetNetCode(), fixture.m_Gnd->GetNetCode() );
}


BOOST_AUTO_TEST_CASE( SessionAddsViaThroughOneCommit )
{
    PCB_OPERATION_FIXTURE         fixture;
    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    PCB_ADD_SPY_COMMIT            commit( fixture.m_Board );
    KISURF_AI_PCB_OPERATION_EDIT_ADAPTER adapter( resolver, commit );
    AI_EDIT_SESSION                       session( adapter );

    BOOST_CHECK( session.Apply( { viaPreviewRef() }, AI_VALIDATION_SUMMARY() ) );

    BOOST_REQUIRE_EQUAL( commit.m_Added.size(), 1 );
    BOOST_CHECK_EQUAL( commit.m_PushCount, 1 );
    BOOST_CHECK_EQUAL( commit.m_RevertCount, 0 );

    std::vector<PCB_TRACK*> vias = boardTracksOfType( fixture.m_Board, PCB_VIA_T );
    BOOST_REQUIRE_EQUAL( vias.size(), 1 );

    PCB_VIA* via = static_cast<PCB_VIA*>( vias.front() );
    BOOST_CHECK_EQUAL( via->GetPosition().x, 400 );
    BOOST_CHECK_EQUAL( via->GetPosition().y, 500 );
    BOOST_CHECK_EQUAL( via->GetWidth( F_Cu ), 600000 );
    BOOST_CHECK_EQUAL( via->GetPrimaryDrillSize().x, 300000 );
    BOOST_CHECK_EQUAL( via->GetPrimaryDrillSize().y, 300000 );
    BOOST_CHECK_EQUAL( via->GetNetCode(), fixture.m_Gnd->GetNetCode() );
}


BOOST_AUTO_TEST_CASE( SessionAddsViaThroughNativeBoardCommit )
{
    PCB_OPERATION_FIXTURE         fixture;
    TOOL_MANAGER                  toolManager;
    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );

    toolManager.SetEnvironment( &fixture.m_Board, nullptr, nullptr, nullptr, nullptr );

    BOARD_COMMIT                         commit( &toolManager, true, false );
    KISURF_AI_PCB_OPERATION_EDIT_ADAPTER adapter( resolver, commit );
    AI_EDIT_SESSION                      session( adapter );

    BOOST_CHECK( session.Apply( { viaPreviewRef() }, AI_VALIDATION_SUMMARY() ) );
    BOOST_CHECK( adapter.WasCommitted() );
    BOOST_CHECK( !adapter.WasReverted() );

    std::vector<PCB_TRACK*> vias = boardTracksOfType( fixture.m_Board, PCB_VIA_T );
    BOOST_REQUIRE_EQUAL( vias.size(), 1 );

    PCB_VIA* via = static_cast<PCB_VIA*>( vias.front() );
    BOOST_CHECK_EQUAL( via->GetPosition().x, 400 );
    BOOST_CHECK_EQUAL( via->GetPosition().y, 500 );
    BOOST_CHECK_EQUAL( via->GetNetCode(), fixture.m_Gnd->GetNetCode() );
}


BOOST_AUTO_TEST_CASE( SessionAddsShapeThroughOneCommit )
{
    PCB_OPERATION_FIXTURE         fixture;
    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    PCB_ADD_SPY_COMMIT            commit( fixture.m_Board );
    KISURF_AI_PCB_OPERATION_EDIT_ADAPTER adapter( resolver, commit );
    AI_EDIT_SESSION                       session( adapter );

    BOOST_CHECK( session.Apply( { shapePreviewRef() }, AI_VALIDATION_SUMMARY() ) );

    BOOST_REQUIRE_EQUAL( commit.m_Added.size(), 1 );
    BOOST_CHECK_EQUAL( commit.m_PushCount, 1 );
    BOOST_CHECK_EQUAL( commit.m_RevertCount, 0 );
    BOOST_REQUIRE_EQUAL( fixture.m_Board.Drawings().size(), 1 );

    BOARD_ITEM* drawing = fixture.m_Board.Drawings().front();
    BOOST_REQUIRE_EQUAL( drawing->Type(), PCB_SHAPE_T );

    PCB_SHAPE* shape = static_cast<PCB_SHAPE*>( drawing );
    BOOST_CHECK( shape->GetShape() == SHAPE_T::RECTANGLE );
    BOOST_CHECK_EQUAL( shape->GetLayer(), F_SilkS );
    BOOST_CHECK_EQUAL( shape->GetStart().x, 10 );
    BOOST_CHECK_EQUAL( shape->GetStart().y, 20 );
    BOOST_CHECK_EQUAL( shape->GetEnd().x, 110 );
    BOOST_CHECK_EQUAL( shape->GetEnd().y, 220 );
    BOOST_CHECK_EQUAL( shape->GetWidth(), 120000 );
}


BOOST_AUTO_TEST_CASE( SessionAddsCircleShapeThroughOneCommit )
{
    PCB_OPERATION_FIXTURE         fixture;
    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    PCB_ADD_SPY_COMMIT            commit( fixture.m_Board );
    KISURF_AI_PCB_OPERATION_EDIT_ADAPTER adapter( resolver, commit );
    AI_EDIT_SESSION                       session( adapter );

    BOOST_CHECK( session.Apply( { circleShapePreviewRef() }, AI_VALIDATION_SUMMARY() ) );

    BOOST_REQUIRE_EQUAL( commit.m_Added.size(), 1 );
    BOOST_CHECK_EQUAL( commit.m_PushCount, 1 );
    BOOST_CHECK_EQUAL( commit.m_RevertCount, 0 );
    BOOST_REQUIRE_EQUAL( fixture.m_Board.Drawings().size(), 1 );

    BOARD_ITEM* drawing = fixture.m_Board.Drawings().front();
    BOOST_REQUIRE_EQUAL( drawing->Type(), PCB_SHAPE_T );

    PCB_SHAPE* shape = static_cast<PCB_SHAPE*>( drawing );
    BOOST_CHECK( shape->GetShape() == SHAPE_T::CIRCLE );
    BOOST_CHECK_EQUAL( shape->GetLayer(), F_SilkS );
    BOOST_CHECK_EQUAL( shape->GetCenter().x, 400 );
    BOOST_CHECK_EQUAL( shape->GetCenter().y, 500 );
    BOOST_CHECK_EQUAL( shape->GetRadius(), 125000 );
    BOOST_CHECK_EQUAL( shape->GetWidth(), 50000 );
}


BOOST_AUTO_TEST_CASE( SessionAddsArcShapeThroughOneCommit )
{
    PCB_OPERATION_FIXTURE         fixture;
    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    PCB_ADD_SPY_COMMIT            commit( fixture.m_Board );
    KISURF_AI_PCB_OPERATION_EDIT_ADAPTER adapter( resolver, commit );
    AI_EDIT_SESSION                       session( adapter );

    BOOST_CHECK( session.Apply( { arcShapePreviewRef() }, AI_VALIDATION_SUMMARY() ) );

    BOOST_REQUIRE_EQUAL( commit.m_Added.size(), 1 );
    BOOST_CHECK_EQUAL( commit.m_PushCount, 1 );
    BOOST_CHECK_EQUAL( commit.m_RevertCount, 0 );
    BOOST_REQUIRE_EQUAL( fixture.m_Board.Drawings().size(), 1 );

    BOARD_ITEM* drawing = fixture.m_Board.Drawings().front();
    BOOST_REQUIRE_EQUAL( drawing->Type(), PCB_SHAPE_T );

    PCB_SHAPE* shape = static_cast<PCB_SHAPE*>( drawing );
    BOOST_CHECK( shape->GetShape() == SHAPE_T::ARC );
    BOOST_CHECK_EQUAL( shape->GetLayer(), F_SilkS );
    BOOST_CHECK( shape->EndsSwapped() );
    BOOST_CHECK_EQUAL( shape->GetStart().x, 100 );
    BOOST_CHECK_EQUAL( shape->GetStart().y, 0 );
    BOOST_CHECK_EQUAL( shape->GetArcMid().x, 50 );
    BOOST_CHECK_LE( std::abs( shape->GetArcMid().y - 100 ), 1 );
    BOOST_CHECK_EQUAL( shape->GetEnd().x, 0 );
    BOOST_CHECK_EQUAL( shape->GetEnd().y, 0 );
    BOOST_CHECK_EQUAL( shape->GetWidth(), 50000 );
}


BOOST_AUTO_TEST_CASE( SessionAddsCopperZoneThroughOneCommit )
{
    PCB_OPERATION_FIXTURE         fixture;
    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    PCB_ADD_SPY_COMMIT            commit( fixture.m_Board );
    KISURF_AI_PCB_OPERATION_EDIT_ADAPTER adapter( resolver, commit );
    AI_EDIT_SESSION                       session( adapter );

    BOOST_CHECK( session.Apply( { zonePreviewRef() }, AI_VALIDATION_SUMMARY() ) );

    BOOST_REQUIRE_EQUAL( commit.m_Added.size(), 1 );
    BOOST_CHECK_EQUAL( commit.m_PushCount, 1 );
    BOOST_CHECK_EQUAL( commit.m_RevertCount, 0 );
    BOOST_REQUIRE_EQUAL( fixture.m_Board.Zones().size(), 1 );

    ZONE* zone = fixture.m_Board.Zones().front();
    BOOST_CHECK_EQUAL( zone->GetLayer(), F_Cu );
    BOOST_CHECK_EQUAL( zone->GetNetCode(), fixture.m_Gnd->GetNetCode() );
    BOOST_REQUIRE( zone->Outline() );
    BOOST_REQUIRE_EQUAL( zone->Outline()->OutlineCount(), 1 );
    BOOST_CHECK_EQUAL( zone->Outline()->COutline( 0 ).PointCount(), 4 );
}


BOOST_AUTO_TEST_CASE( UnknownNetRevertsAndDoesNotPush )
{
    PCB_OPERATION_FIXTURE         fixture;
    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    PCB_ADD_SPY_COMMIT            commit( fixture.m_Board );
    KISURF_AI_PCB_OPERATION_EDIT_ADAPTER adapter( resolver, commit );
    AI_EDIT_SESSION                       session( adapter );

    BOOST_CHECK( !session.Apply( { routePreviewRef( wxS( "NOPE" ) ) },
                                 AI_VALIDATION_SUMMARY() ) );
    BOOST_CHECK_EQUAL( commit.m_Added.size(), 0 );
    BOOST_CHECK_EQUAL( commit.m_PushCount, 0 );
    BOOST_CHECK_EQUAL( commit.m_RevertCount, 1 );
    BOOST_CHECK( adapter.WasReverted() );
    BOOST_REQUIRE_EQUAL( adapter.FailedObjects().size(), 1 );
    BOOST_CHECK( fixture.m_Board.Tracks().empty() );
}


BOOST_AUTO_TEST_SUITE_END()
