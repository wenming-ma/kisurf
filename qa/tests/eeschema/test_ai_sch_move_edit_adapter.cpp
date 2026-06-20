#include <boost/test/unit_test.hpp>

#include <kisurf_ai_sch_context_adapter.h>
#include <kisurf_ai_sch_move_edit_adapter.h>
#include <kisurf_ai_sch_object_resolver.h>

#include <commit.h>
#include <sch_field.h>
#include <sch_item.h>
#include <sch_screen.h>
#include <sch_symbol.h>

#include <vector>
#include <wx/string.h>

namespace
{

class SCH_SPY_COMMIT : public COMMIT
{
public:
    struct MODIFIED_ITEM
    {
        SCH_ITEM*    m_Item = nullptr;
        BASE_SCREEN* m_Screen = nullptr;
        VECTOR2I     m_OriginalPosition;
        CHANGE_TYPE  m_ChangeType = CHT_MODIFY;
    };

    COMMIT& Stage( EDA_ITEM* aItem, CHANGE_TYPE aChangeType,
                   BASE_SCREEN* aScreen = nullptr,
                   RECURSE_MODE aRecurse = RECURSE_MODE::NO_RECURSE ) override
    {
        wxUnusedVar( aRecurse );

        SCH_ITEM* schItem = dynamic_cast<SCH_ITEM*>( aItem );
        int       changeType = aChangeType & CHT_TYPE;

        if( schItem && changeType == CHT_MODIFY )
        {
            m_Modified.push_back( { schItem, aScreen, schItem->GetPosition(),
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


struct SCH_MOVE_FIXTURE
{
    SCH_MOVE_FIXTURE()
    {
        m_SymbolA = new SCH_SYMBOL();
        m_SymbolB = new SCH_SYMBOL();

        m_SymbolA->GetField( FIELD_T::REFERENCE )->SetText( wxS( "R1" ) );
        m_SymbolB->GetField( FIELD_T::REFERENCE )->SetText( wxS( "U2" ) );
        m_SymbolA->SetPosition( VECTOR2I( 1000, 2000 ) );
        m_SymbolB->SetPosition( VECTOR2I( 4000, 5000 ) );

        m_Screen.Append( m_SymbolA, false );
        m_Screen.Append( m_SymbolB, false );
    }

    SCH_SCREEN  m_Screen;
    SCH_SYMBOL* m_SymbolA = nullptr;
    SCH_SYMBOL* m_SymbolB = nullptr;
};


std::vector<AI_OBJECT_REF> visibleRefs( SCH_SCREEN& aScreen )
{
    KISURF_AI_SCH_CONTEXT_ADAPTER adapter( aScreen );
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


BOOST_AUTO_TEST_SUITE( AiSchMoveEditAdapter )


BOOST_AUTO_TEST_CASE( SessionMovesResolvedSymbolThroughOneCommit )
{
    SCH_MOVE_FIXTURE fixture;
    VECTOR2I         original = fixture.m_SymbolA->GetPosition();
    std::vector<AI_OBJECT_REF> refs = visibleRefs( fixture.m_Screen );
    const AI_OBJECT_REF*       refA = findRefByLabel( refs, wxS( "R1" ) );
    BOOST_REQUIRE( refA );

    KISURF_AI_SCH_OBJECT_RESOLVER resolver( fixture.m_Screen );
    SCH_SPY_COMMIT                commit;
    KISURF_AI_SCH_MOVE_EDIT_ADAPTER adapter( resolver, commit, fixture.m_Screen,
                                             VECTOR2I( 100, -25 ) );
    AI_EDIT_SESSION                  session( adapter );

    BOOST_CHECK( session.Apply( { *refA }, AI_VALIDATION_SUMMARY() ) );
    BOOST_REQUIRE_EQUAL( commit.m_Modified.size(), 1 );
    BOOST_CHECK( commit.m_Modified.front().m_Item == fixture.m_SymbolA );
    BOOST_CHECK( commit.m_Modified.front().m_Screen == &fixture.m_Screen );
    BOOST_CHECK_EQUAL( commit.m_PushCount, 1 );
    BOOST_CHECK_EQUAL( commit.m_RevertCount, 0 );
    BOOST_CHECK( fixture.m_SymbolA->GetPosition() == original + VECTOR2I( 100, -25 ) );
    BOOST_CHECK( adapter.WasCommitted() );
    BOOST_CHECK( !adapter.WasReverted() );
    BOOST_REQUIRE_EQUAL( adapter.MovedItems().size(), 1 );
    BOOST_CHECK( adapter.MovedItems().front() == fixture.m_SymbolA );
}


BOOST_AUTO_TEST_CASE( UnknownReferenceRevertsAndDoesNotPush )
{
    SCH_MOVE_FIXTURE fixture;
    KISURF_AI_SCH_OBJECT_RESOLVER resolver( fixture.m_Screen );
    SCH_SPY_COMMIT                commit;
    KISURF_AI_SCH_MOVE_EDIT_ADAPTER adapter( resolver, commit, fixture.m_Screen,
                                             VECTOR2I( 100, -25 ) );
    AI_EDIT_SESSION                  session( adapter );
    AI_OBJECT_REF                    missing( KIID(), SCH_SYMBOL_T, wxS( "missing" ) );

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
    SCH_MOVE_FIXTURE fixture;
    VECTOR2I         original = fixture.m_SymbolA->GetPosition();
    std::vector<AI_OBJECT_REF> refs = visibleRefs( fixture.m_Screen );
    const AI_OBJECT_REF*       refA = findRefByLabel( refs, wxS( "R1" ) );
    BOOST_REQUIRE( refA );

    KISURF_AI_SCH_OBJECT_RESOLVER resolver( fixture.m_Screen );
    SCH_SPY_COMMIT                commit;
    KISURF_AI_SCH_MOVE_EDIT_ADAPTER adapter( resolver, commit, fixture.m_Screen,
                                             VECTOR2I( 100, -25 ) );
    AI_EDIT_SESSION                  session( adapter );
    AI_OBJECT_REF                    missing( KIID(), SCH_SYMBOL_T, wxS( "missing" ) );

    BOOST_CHECK( !session.Apply( { *refA, missing }, AI_VALIDATION_SUMMARY() ) );
    BOOST_REQUIRE_EQUAL( commit.m_Modified.size(), 1 );
    BOOST_CHECK_EQUAL( commit.m_PushCount, 0 );
    BOOST_CHECK_EQUAL( commit.m_RevertCount, 1 );
    BOOST_CHECK( fixture.m_SymbolA->GetPosition() == original );
    BOOST_CHECK( adapter.WasReverted() );
    BOOST_REQUIRE_EQUAL( adapter.FailedObjects().size(), 1 );
}


BOOST_AUTO_TEST_SUITE_END()
