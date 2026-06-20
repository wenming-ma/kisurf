#include <boost/test/unit_test.hpp>

#include <kisurf_ai_pcb_context_adapter.h>
#include <kisurf_ai_pcb_move_edit_adapter.h>
#include <kisurf_ai_pcb_object_resolver.h>

#include <board.h>
#include <board_item.h>
#include <commit.h>
#include <footprint.h>
#include <pad.h>

#include <vector>
#include <wx/string.h>

namespace
{

class PCB_SPY_COMMIT : public COMMIT
{
public:
    struct MODIFIED_ITEM
    {
        BOARD_ITEM*  m_Item = nullptr;
        BASE_SCREEN* m_Screen = nullptr;
        VECTOR2I     m_OriginalPosition;
        CHANGE_TYPE  m_ChangeType = CHT_MODIFY;
    };

    COMMIT& Stage( EDA_ITEM* aItem, CHANGE_TYPE aChangeType,
                   BASE_SCREEN* aScreen = nullptr,
                   RECURSE_MODE aRecurse = RECURSE_MODE::NO_RECURSE ) override
    {
        wxUnusedVar( aRecurse );

        BOARD_ITEM* boardItem = dynamic_cast<BOARD_ITEM*>( aItem );
        int         changeType = aChangeType & CHT_TYPE;

        if( boardItem && changeType == CHT_MODIFY )
        {
            m_Modified.push_back( { boardItem, aScreen, boardItem->GetPosition(),
                                    aChangeType } );
        }

        return *this;
    }

    void Push( const wxString& aMessage = wxEmptyString, int aFlags = 0 ) override
    {
        wxUnusedVar( aFlags );
        ++m_PushCount;
        m_LastMessage = aMessage;
    }

    void Revert() override
    {
        ++m_RevertCount;

        for( auto it = m_Modified.rbegin(); it != m_Modified.rend(); ++it )
            it->m_Item->SetPosition( it->m_OriginalPosition );
    }

    std::vector<MODIFIED_ITEM> m_Modified;
    int                        m_PushCount = 0;
    int                        m_RevertCount = 0;
    wxString                   m_LastMessage;

private:
    EDA_ITEM* undoLevelItem( EDA_ITEM* aItem ) const override { return aItem; }
    EDA_ITEM* makeImage( EDA_ITEM* aItem ) const override { return aItem->Clone(); }
};


struct PCB_MOVE_FIXTURE
{
    PCB_MOVE_FIXTURE()
    {
        m_Footprint = new FOOTPRINT( &m_Board );
        m_PadA = new PAD( m_Footprint );
        m_PadB = new PAD( m_Footprint );

        m_Footprint->SetReference( wxS( "U1" ) );
        m_PadA->SetNumber( wxS( "1" ) );
        m_PadB->SetNumber( wxS( "2" ) );
        m_PadA->SetPosition( VECTOR2I( 1000, 2000 ) );
        m_PadB->SetPosition( VECTOR2I( 4000, 5000 ) );

        m_Footprint->Add( m_PadA );
        m_Footprint->Add( m_PadB );
        m_Board.Add( m_Footprint );
    }

    BOARD      m_Board;
    FOOTPRINT* m_Footprint = nullptr;
    PAD*       m_PadA = nullptr;
    PAD*       m_PadB = nullptr;
};


std::vector<AI_OBJECT_REF> visibleRefs( BOARD& aBoard )
{
    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( aBoard );
    return adapter.BuildIndex().VisibleObjects();
}


const AI_OBJECT_REF* findRefByLabel( const std::vector<AI_OBJECT_REF>& aRefs,
                                     const wxString& aLabel )
{
    for( const AI_OBJECT_REF& ref : aRefs )
    {
        if( ref.m_Label == aLabel )
            return &ref;
    }

    return nullptr;
}

} // namespace


BOOST_AUTO_TEST_SUITE( AiPcbMoveEditAdapter )


BOOST_AUTO_TEST_CASE( SessionMovesResolvedPadThroughOneCommit )
{
    PCB_MOVE_FIXTURE fixture;
    VECTOR2I         original = fixture.m_PadA->GetPosition();
    std::vector<AI_OBJECT_REF> refs = visibleRefs( fixture.m_Board );
    const AI_OBJECT_REF*       refA = findRefByLabel( refs, wxS( "U1.1" ) );
    BOOST_REQUIRE( refA );

    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    PCB_SPY_COMMIT                commit;
    KISURF_AI_PCB_MOVE_EDIT_ADAPTER adapter( resolver, commit, VECTOR2I( 100, -25 ) );
    AI_EDIT_SESSION                  session( adapter );

    BOOST_CHECK( session.Apply( { *refA }, AI_VALIDATION_SUMMARY() ) );
    BOOST_REQUIRE_EQUAL( commit.m_Modified.size(), 1 );
    BOOST_CHECK( commit.m_Modified.front().m_Item == fixture.m_PadA );
    BOOST_CHECK_EQUAL( commit.m_PushCount, 1 );
    BOOST_CHECK_EQUAL( commit.m_RevertCount, 0 );
    BOOST_CHECK( fixture.m_PadA->GetPosition() == original + VECTOR2I( 100, -25 ) );
    BOOST_CHECK( adapter.WasCommitted() );
    BOOST_CHECK( !adapter.WasReverted() );
    BOOST_REQUIRE_EQUAL( adapter.MovedItems().size(), 1 );
    BOOST_CHECK( adapter.MovedItems().front() == fixture.m_PadA );
}


BOOST_AUTO_TEST_CASE( UnknownReferenceRevertsAndDoesNotPush )
{
    PCB_MOVE_FIXTURE fixture;
    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    PCB_SPY_COMMIT                commit;
    KISURF_AI_PCB_MOVE_EDIT_ADAPTER adapter( resolver, commit, VECTOR2I( 100, -25 ) );
    AI_EDIT_SESSION                  session( adapter );
    AI_OBJECT_REF                    missing( KIID(), PCB_PAD_T, wxS( "missing" ) );

    BOOST_CHECK( !session.Apply( { missing }, AI_VALIDATION_SUMMARY() ) );
    BOOST_CHECK_EQUAL( commit.m_Modified.size(), 0 );
    BOOST_CHECK_EQUAL( commit.m_PushCount, 0 );
    BOOST_CHECK_EQUAL( commit.m_RevertCount, 1 );
    BOOST_CHECK( !adapter.WasCommitted() );
    BOOST_CHECK( adapter.WasReverted() );
    BOOST_REQUIRE_EQUAL( adapter.FailedObjects().size(), 1 );
    BOOST_CHECK_EQUAL( adapter.FailedObjects().front().m_Label, wxString( wxS( "missing" ) ) );
}


BOOST_AUTO_TEST_CASE( SecondObjectFailureRevertsFirstMove )
{
    PCB_MOVE_FIXTURE fixture;
    VECTOR2I         original = fixture.m_PadA->GetPosition();
    std::vector<AI_OBJECT_REF> refs = visibleRefs( fixture.m_Board );
    const AI_OBJECT_REF*       refA = findRefByLabel( refs, wxS( "U1.1" ) );
    BOOST_REQUIRE( refA );

    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    PCB_SPY_COMMIT                commit;
    KISURF_AI_PCB_MOVE_EDIT_ADAPTER adapter( resolver, commit, VECTOR2I( 100, -25 ) );
    AI_EDIT_SESSION                  session( adapter );
    AI_OBJECT_REF                    missing( KIID(), PCB_PAD_T, wxS( "missing" ) );

    BOOST_CHECK( !session.Apply( { *refA, missing }, AI_VALIDATION_SUMMARY() ) );
    BOOST_REQUIRE_EQUAL( commit.m_Modified.size(), 1 );
    BOOST_CHECK_EQUAL( commit.m_PushCount, 0 );
    BOOST_CHECK_EQUAL( commit.m_RevertCount, 1 );
    BOOST_CHECK( fixture.m_PadA->GetPosition() == original );
    BOOST_CHECK( adapter.WasReverted() );
    BOOST_REQUIRE_EQUAL( adapter.FailedObjects().size(), 1 );
}


BOOST_AUTO_TEST_SUITE_END()
