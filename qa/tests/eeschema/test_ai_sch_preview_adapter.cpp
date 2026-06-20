#include <boost/test/unit_test.hpp>

#include <kisurf/ai/ai_preview_manager.h>
#include <kisurf_ai_sch_context_adapter.h>
#include <kisurf_ai_sch_object_resolver.h>
#include <kisurf_ai_sch_preview_adapter.h>

#include <sch_field.h>
#include <sch_screen.h>
#include <sch_symbol.h>
#include <view/view.h>

namespace
{

struct SCH_PREVIEW_FIXTURE
{
    SCH_PREVIEW_FIXTURE()
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

BOOST_AUTO_TEST_SUITE( AiSchPreviewAdapter )

BOOST_AUTO_TEST_CASE( SessionShowsResolvedSymbolInPreview )
{
    SCH_PREVIEW_FIXTURE fixture;
    KIGFX::VIEW         view;
    KISURF_AI_SCH_OBJECT_RESOLVER resolver( fixture.m_Screen );
    KISURF_AI_SCH_PREVIEW_ADAPTER adapter( resolver, view );
    AI_PREVIEW_MANAGER             session( adapter );

    std::vector<AI_OBJECT_REF> refs = visibleRefs( fixture.m_Screen );
    const AI_OBJECT_REF*       ref = findRefByLabel( refs, wxS( "R1" ) );
    BOOST_REQUIRE( ref );

    session.ShowObject( *ref );

    BOOST_CHECK_EQUAL( adapter.ActivePreviewId(), 1 );
    BOOST_REQUIRE_EQUAL( adapter.PreviewedItems().size(), 1 );
    BOOST_CHECK( adapter.PreviewedItems().front() == fixture.m_SymbolA );
}

BOOST_AUTO_TEST_CASE( MovePreviewShowsMovedCloneWithoutChangingOriginal )
{
    SCH_PREVIEW_FIXTURE fixture;
    VECTOR2I            original = fixture.m_SymbolA->GetPosition();
    KIGFX::VIEW         view;
    KISURF_AI_SCH_OBJECT_RESOLVER resolver( fixture.m_Screen );
    KISURF_AI_SCH_PREVIEW_ADAPTER adapter( resolver, view, VECTOR2I( 100, -25 ) );
    AI_PREVIEW_MANAGER             session( adapter );

    std::vector<AI_OBJECT_REF> refs = visibleRefs( fixture.m_Screen );
    const AI_OBJECT_REF*       ref = findRefByLabel( refs, wxS( "R1" ) );
    BOOST_REQUIRE( ref );

    session.ShowObject( *ref );

    BOOST_REQUIRE_EQUAL( adapter.PreviewedItems().size(), 1 );
    BOOST_CHECK( adapter.PreviewedItems().front() != fixture.m_SymbolA );
    BOOST_CHECK( fixture.m_SymbolA->GetPosition() == original );
    BOOST_CHECK( adapter.PreviewedItems().front()->GetPosition()
                 == original + VECTOR2I( 100, -25 ) );
}

BOOST_AUTO_TEST_CASE( UnknownReferenceIsSkipped )
{
    SCH_PREVIEW_FIXTURE fixture;
    KIGFX::VIEW         view;
    KISURF_AI_SCH_OBJECT_RESOLVER resolver( fixture.m_Screen );
    KISURF_AI_SCH_PREVIEW_ADAPTER adapter( resolver, view );
    AI_PREVIEW_MANAGER             session( adapter );

    AI_OBJECT_REF missing( KIID(), SCH_SYMBOL_T, wxS( "missing" ) );

    session.ShowObject( missing );

    BOOST_CHECK_EQUAL( adapter.ActivePreviewId(), 1 );
    BOOST_CHECK( adapter.PreviewedItems().empty() );
}

BOOST_AUTO_TEST_CASE( StalePreviewIdIsIgnored )
{
    SCH_PREVIEW_FIXTURE fixture;
    KIGFX::VIEW         view;
    KISURF_AI_SCH_OBJECT_RESOLVER resolver( fixture.m_Screen );
    KISURF_AI_SCH_PREVIEW_ADAPTER adapter( resolver, view );

    std::vector<AI_OBJECT_REF> refs = visibleRefs( fixture.m_Screen );
    const AI_OBJECT_REF*       ref = findRefByLabel( refs, wxS( "R1" ) );
    BOOST_REQUIRE( ref );

    adapter.BeginPreview( 42 );
    adapter.ShowObject( 7, *ref );

    BOOST_CHECK_EQUAL( adapter.ActivePreviewId(), 42 );
    BOOST_CHECK( adapter.PreviewedItems().empty() );
}

BOOST_AUTO_TEST_CASE( ClearPreviewResetsActivePreview )
{
    SCH_PREVIEW_FIXTURE fixture;
    KIGFX::VIEW         view;
    KISURF_AI_SCH_OBJECT_RESOLVER resolver( fixture.m_Screen );
    KISURF_AI_SCH_PREVIEW_ADAPTER adapter( resolver, view );
    AI_PREVIEW_MANAGER             session( adapter );

    std::vector<AI_OBJECT_REF> refs = visibleRefs( fixture.m_Screen );
    const AI_OBJECT_REF*       ref = findRefByLabel( refs, wxS( "R1" ) );
    BOOST_REQUIRE( ref );

    session.ShowObject( *ref );
    session.ClearPreview();

    BOOST_CHECK_EQUAL( adapter.ActivePreviewId(), 0 );
    BOOST_CHECK( adapter.PreviewedItems().empty() );
}

BOOST_AUTO_TEST_SUITE_END()
