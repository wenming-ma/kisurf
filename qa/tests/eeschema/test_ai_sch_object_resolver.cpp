#include <boost/test/unit_test.hpp>

#include <kisurf_ai_sch_context_adapter.h>
#include <kisurf_ai_sch_object_resolver.h>

#include <sch_field.h>
#include <sch_screen.h>
#include <sch_symbol.h>

namespace
{

struct SCH_RESOLVER_FIXTURE
{
    SCH_RESOLVER_FIXTURE()
    {
        m_SymbolA = new SCH_SYMBOL();
        m_SymbolB = new SCH_SYMBOL();

        m_SymbolA->GetField( FIELD_T::REFERENCE )->SetText( wxS( "R1" ) );
        m_SymbolB->GetField( FIELD_T::REFERENCE )->SetText( wxS( "U2" ) );

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

BOOST_AUTO_TEST_SUITE( AiSchObjectResolver )

BOOST_AUTO_TEST_CASE( ResolvesSymbolReferenceFromContextAdapter )
{
    SCH_RESOLVER_FIXTURE fixture;
    std::vector<AI_OBJECT_REF> refs = visibleRefs( fixture.m_Screen );
    BOOST_REQUIRE_EQUAL( refs.size(), 2 );
    const AI_OBJECT_REF* refA = findRefByLabel( refs, wxS( "R1" ) );
    const AI_OBJECT_REF* refB = findRefByLabel( refs, wxS( "U2" ) );
    BOOST_REQUIRE( refA );
    BOOST_REQUIRE( refB );

    KISURF_AI_SCH_OBJECT_RESOLVER resolver( fixture.m_Screen );

    BOOST_CHECK( resolver.Resolve( *refA ) == fixture.m_SymbolA );
    BOOST_CHECK( resolver.Resolve( *refB ) == fixture.m_SymbolB );
}

BOOST_AUTO_TEST_CASE( UnknownUuidReturnsNull )
{
    SCH_RESOLVER_FIXTURE fixture;
    KISURF_AI_SCH_OBJECT_RESOLVER resolver( fixture.m_Screen );

    AI_OBJECT_REF missing( KIID(), SCH_SYMBOL_T, wxS( "missing" ) );

    BOOST_CHECK( resolver.Resolve( missing ) == nullptr );
}

BOOST_AUTO_TEST_CASE( TypeMismatchReturnsNull )
{
    SCH_RESOLVER_FIXTURE fixture;
    std::vector<AI_OBJECT_REF> refs = visibleRefs( fixture.m_Screen );
    BOOST_REQUIRE_EQUAL( refs.size(), 2 );
    const AI_OBJECT_REF* refA = findRefByLabel( refs, wxS( "R1" ) );
    BOOST_REQUIRE( refA );

    AI_OBJECT_REF mismatch = *refA;
    mismatch.m_Type = SCH_LINE_T;

    KISURF_AI_SCH_OBJECT_RESOLVER resolver( fixture.m_Screen );

    BOOST_CHECK( resolver.Resolve( mismatch ) == nullptr );
}

BOOST_AUTO_TEST_CASE( ResolveAllSkipsMissingReferencesAndPreservesOrder )
{
    SCH_RESOLVER_FIXTURE fixture;
    std::vector<AI_OBJECT_REF> refs = visibleRefs( fixture.m_Screen );
    BOOST_REQUIRE_EQUAL( refs.size(), 2 );
    const AI_OBJECT_REF* refA = findRefByLabel( refs, wxS( "R1" ) );
    const AI_OBJECT_REF* refB = findRefByLabel( refs, wxS( "U2" ) );
    BOOST_REQUIRE( refA );
    BOOST_REQUIRE( refB );

    AI_OBJECT_REF missing( KIID(), SCH_SYMBOL_T, wxS( "missing" ) );

    KISURF_AI_SCH_OBJECT_RESOLVER resolver( fixture.m_Screen );
    std::vector<SCH_ITEM*> resolved = resolver.ResolveAll( { *refB, missing, *refA } );

    BOOST_REQUIRE_EQUAL( resolved.size(), 2 );
    BOOST_CHECK( resolved[0] == fixture.m_SymbolB );
    BOOST_CHECK( resolved[1] == fixture.m_SymbolA );
}

BOOST_AUTO_TEST_SUITE_END()
