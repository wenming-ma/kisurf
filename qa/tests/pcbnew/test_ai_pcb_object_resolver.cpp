#include <boost/test/unit_test.hpp>

#include <kisurf_ai_pcb_context_adapter.h>
#include <kisurf_ai_pcb_object_resolver.h>

#include <board.h>
#include <footprint.h>
#include <pad.h>
#include <pcb_barcode.h>
#include <pcb_dimension.h>
#include <pcb_field.h>
#include <pcb_shape.h>
#include <pcb_table.h>
#include <pcb_tablecell.h>
#include <pcb_target.h>
#include <pcb_text.h>
#include <pcb_textbox.h>
#include <pcb_track.h>
#include <zone.h>

namespace
{

struct PCB_RESOLVER_FIXTURE
{
    PCB_RESOLVER_FIXTURE()
    {
        m_Footprint = new FOOTPRINT( &m_Board );
        m_PadA = new PAD( m_Footprint );
        m_PadB = new PAD( m_Footprint );

        m_Footprint->SetReference( wxS( "U1" ) );
        m_PadA->SetNumber( wxS( "1" ) );
        m_PadB->SetNumber( wxS( "2" ) );

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

void appendRectangle( ZONE& aZone )
{
    aZone.AppendCorner( VECTOR2I( 0, 0 ), -1 );
    aZone.AppendCorner( VECTOR2I( 1000, 0 ), -1 );
    aZone.AppendCorner( VECTOR2I( 1000, 500 ), -1 );
    aZone.AppendCorner( VECTOR2I( 0, 500 ), -1 );
}

} // namespace

BOOST_AUTO_TEST_SUITE( AiPcbObjectResolver )

BOOST_AUTO_TEST_CASE( ResolvesPadReferenceFromContextAdapter )
{
    PCB_RESOLVER_FIXTURE fixture;
    std::vector<AI_OBJECT_REF> refs = visibleRefs( fixture.m_Board );
    BOOST_REQUIRE_GE( refs.size(), 2 );
    const AI_OBJECT_REF* refA = findRefByLabel( refs, wxS( "U1.1" ) );
    const AI_OBJECT_REF* refB = findRefByLabel( refs, wxS( "U1.2" ) );
    BOOST_REQUIRE( refA );
    BOOST_REQUIRE( refB );

    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );

    BOOST_CHECK( resolver.Resolve( *refA ) == fixture.m_PadA );
    BOOST_CHECK( resolver.Resolve( *refB ) == fixture.m_PadB );
}


BOOST_AUTO_TEST_CASE( ResolvesFootprintReferenceFromContextAdapter )
{
    PCB_RESOLVER_FIXTURE fixture;
    std::vector<AI_OBJECT_REF> refs = visibleRefs( fixture.m_Board );
    BOOST_REQUIRE_GE( refs.size(), 3 );
    const AI_OBJECT_REF* ref = findRefByLabel( refs, wxS( "U1" ) );
    BOOST_REQUIRE( ref );

    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );

    BOOST_CHECK( resolver.Resolve( *ref ) == fixture.m_Footprint );
}


BOOST_AUTO_TEST_CASE( ResolvesTrackAndViaReferencesFromContextAdapter )
{
    BOARD board;

    PCB_TRACK* track = new PCB_TRACK( &board );
    track->SetStart( VECTOR2I( 0, 0 ) );
    track->SetEnd( VECTOR2I( 100, 200 ) );
    board.Add( track );

    PCB_VIA* via = new PCB_VIA( &board );
    via->SetPosition( VECTOR2I( 300, 400 ) );
    board.Add( via );

    std::vector<AI_OBJECT_REF> refs = visibleRefs( board );
    const AI_OBJECT_REF* trackRef = findRefByLabel( refs, wxS( "track:0,0->100,200" ) );
    const AI_OBJECT_REF* viaRef = findRefByLabel( refs, wxS( "via:300,400" ) );

    BOOST_REQUIRE( trackRef );
    BOOST_REQUIRE( viaRef );

    KISURF_AI_PCB_OBJECT_RESOLVER resolver( board );

    BOOST_CHECK( resolver.Resolve( *trackRef ) == track );
    BOOST_CHECK( resolver.Resolve( *viaRef ) == via );
}


BOOST_AUTO_TEST_CASE( ResolvesArcReferenceFromContextAdapter )
{
    BOARD board;

    PCB_ARC* arc = new PCB_ARC( &board );
    arc->SetStart( VECTOR2I( 0, 0 ) );
    arc->SetMid( VECTOR2I( 50, 100 ) );
    arc->SetEnd( VECTOR2I( 100, 0 ) );
    board.Add( arc );

    std::vector<AI_OBJECT_REF> refs = visibleRefs( board );
    const AI_OBJECT_REF* arcRef = findRefByLabel( refs, wxS( "arc:0,0->50,100->100,0" ) );

    BOOST_REQUIRE( arcRef );

    KISURF_AI_PCB_OBJECT_RESOLVER resolver( board );

    BOOST_CHECK( resolver.Resolve( *arcRef ) == arc );
}


BOOST_AUTO_TEST_CASE( ResolvesDrawingShapeReference )
{
    BOARD board;

    PCB_SHAPE* edge = new PCB_SHAPE( &board, SHAPE_T::SEGMENT );
    edge->SetStart( VECTOR2I( 0, 0 ) );
    edge->SetEnd( VECTOR2I( 1000, 0 ) );
    edge->SetLayer( Edge_Cuts );
    board.Add( edge );

    AI_OBJECT_REF ref( edge->m_Uuid, PCB_SHAPE_T, wxS( "edge:0,0->1000,0" ) );
    KISURF_AI_PCB_OBJECT_RESOLVER resolver( board );

    BOOST_CHECK( resolver.Resolve( ref ) == edge );
}


BOOST_AUTO_TEST_CASE( ResolvesZoneReference )
{
    BOARD board;

    ZONE* zone = new ZONE( &board );
    zone->SetZoneName( wxS( "GND_POUR" ) );
    zone->SetLayerSet( LSET( { F_Cu } ) );
    appendRectangle( *zone );
    board.Add( zone );

    AI_OBJECT_REF ref( zone->m_Uuid, PCB_ZONE_T, wxS( "zone:GND_POUR" ) );
    KISURF_AI_PCB_OBJECT_RESOLVER resolver( board );

    BOOST_CHECK( resolver.Resolve( ref ) == zone );
}


BOOST_AUTO_TEST_CASE( ResolvesBoardTextReferences )
{
    BOARD board;

    PCB_TEXT* text = new PCB_TEXT( &board );
    text->SetText( wxS( "JTAG HEADER" ) );
    board.Add( text );

    PCB_TEXTBOX* textbox = new PCB_TEXTBOX( &board );
    textbox->SetText( wxS( "Assembly note" ) );
    board.Add( textbox );

    KISURF_AI_PCB_OBJECT_RESOLVER resolver( board );

    BOOST_CHECK( resolver.Resolve( AI_OBJECT_REF( text->m_Uuid, PCB_TEXT_T,
                                                  wxS( "text:JTAG HEADER" ) ) )
                 == text );
    BOOST_CHECK( resolver.Resolve( AI_OBJECT_REF( textbox->m_Uuid, PCB_TEXTBOX_T,
                                                  wxS( "textbox:Assembly note" ) ) )
                 == textbox );
}


BOOST_AUTO_TEST_CASE( ResolvesFootprintChildReferences )
{
    BOARD board;

    FOOTPRINT* footprint = new FOOTPRINT( &board );
    footprint->SetReference( wxS( "U1" ) );

    PCB_FIELD* reference = footprint->GetField( FIELD_T::REFERENCE );
    BOOST_REQUIRE( reference );

    PCB_SHAPE* silkLine = new PCB_SHAPE( footprint );
    silkLine->SetShape( SHAPE_T::SEGMENT );
    footprint->Add( silkLine );

    PCB_TEXT* pinOne = new PCB_TEXT( footprint );
    pinOne->SetText( wxS( "PIN 1" ) );
    footprint->Add( pinOne );

    board.Add( footprint );

    KISURF_AI_PCB_OBJECT_RESOLVER resolver( board );

    BOOST_CHECK( resolver.Resolve( AI_OBJECT_REF( reference->m_Uuid, PCB_FIELD_T,
                                                  wxS( "fp:U1/field:Reference" ) ) )
                 == reference );
    BOOST_CHECK( resolver.Resolve( AI_OBJECT_REF( silkLine->m_Uuid, PCB_SHAPE_T,
                                                  wxS( "fp:U1/shape:segment" ) ) )
                 == silkLine );
    BOOST_CHECK( resolver.Resolve( AI_OBJECT_REF( pinOne->m_Uuid, PCB_TEXT_T,
                                                  wxS( "fp:U1/text:PIN 1" ) ) )
                 == pinOne );
}


BOOST_AUTO_TEST_CASE( ResolvesBoardAnnotationReferences )
{
    BOARD board;

    PCB_TARGET* target = new PCB_TARGET( &board, 0, Dwgs_User, VECTOR2I( 100, 200 ),
                                         1000, 100 );
    board.Add( target );

    PCB_BARCODE* barcode = new PCB_BARCODE( &board );
    barcode->SetText( wxS( "SN-001" ) );
    board.Add( barcode );

    PCB_TABLE* table = new PCB_TABLE( &board, 100 );
    table->SetColCount( 1 );

    PCB_TABLECELL* cell = new PCB_TABLECELL( table );
    cell->SetText( wxS( "Part" ) );
    table->AddCell( cell );
    board.Add( table );

    PCB_DIM_ALIGNED* dimension = new PCB_DIM_ALIGNED( &board );
    dimension->SetStart( VECTOR2I( 0, 0 ) );
    dimension->SetEnd( VECTOR2I( 1000, 0 ) );
    dimension->Update();
    board.Add( dimension );

    KISURF_AI_PCB_OBJECT_RESOLVER resolver( board );

    BOOST_CHECK( resolver.Resolve( AI_OBJECT_REF( target->m_Uuid, PCB_TARGET_T,
                                                  wxS( "target:100,200" ) ) )
                 == target );
    BOOST_CHECK( resolver.Resolve( AI_OBJECT_REF( barcode->m_Uuid, PCB_BARCODE_T,
                                                  wxS( "barcode:SN-001" ) ) )
                 == barcode );
    BOOST_CHECK( resolver.Resolve( AI_OBJECT_REF( table->m_Uuid, PCB_TABLE_T,
                                                  wxS( "table:0,0" ) ) )
                 == table );
    BOOST_CHECK( resolver.Resolve( AI_OBJECT_REF( cell->m_Uuid, PCB_TABLECELL_T,
                                                  wxS( "table-cell:A1" ) ) )
                 == cell );
    BOOST_CHECK( resolver.Resolve( AI_OBJECT_REF( dimension->m_Uuid, PCB_DIM_ALIGNED_T,
                                                  wxS( "dimension:0,0->1000,0" ) ) )
                 == dimension );
}


BOOST_AUTO_TEST_CASE( UnknownUuidReturnsNull )
{
    PCB_RESOLVER_FIXTURE fixture;
    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );

    AI_OBJECT_REF missing( KIID(), PCB_PAD_T, wxS( "missing" ) );

    BOOST_CHECK( resolver.Resolve( missing ) == nullptr );
}

BOOST_AUTO_TEST_CASE( TypeMismatchReturnsNull )
{
    PCB_RESOLVER_FIXTURE fixture;
    std::vector<AI_OBJECT_REF> refs = visibleRefs( fixture.m_Board );
    BOOST_REQUIRE_GE( refs.size(), 2 );
    const AI_OBJECT_REF* refA = findRefByLabel( refs, wxS( "U1.1" ) );
    BOOST_REQUIRE( refA );

    AI_OBJECT_REF mismatch = *refA;
    mismatch.m_Type = PCB_TRACE_T;

    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );

    BOOST_CHECK( resolver.Resolve( mismatch ) == nullptr );
}

BOOST_AUTO_TEST_CASE( ResolveAllSkipsMissingReferencesAndPreservesOrder )
{
    PCB_RESOLVER_FIXTURE fixture;
    std::vector<AI_OBJECT_REF> refs = visibleRefs( fixture.m_Board );
    BOOST_REQUIRE_GE( refs.size(), 2 );
    const AI_OBJECT_REF* refA = findRefByLabel( refs, wxS( "U1.1" ) );
    const AI_OBJECT_REF* refB = findRefByLabel( refs, wxS( "U1.2" ) );
    BOOST_REQUIRE( refA );
    BOOST_REQUIRE( refB );

    AI_OBJECT_REF missing( KIID(), PCB_PAD_T, wxS( "missing" ) );

    KISURF_AI_PCB_OBJECT_RESOLVER resolver( fixture.m_Board );
    std::vector<BOARD_ITEM*> resolved = resolver.ResolveAll( { *refB, missing, *refA } );

    BOOST_REQUIRE_EQUAL( resolved.size(), 2 );
    BOOST_CHECK( resolved[0] == fixture.m_PadB );
    BOOST_CHECK( resolved[1] == fixture.m_PadA );
}

BOOST_AUTO_TEST_SUITE_END()
