#include <boost/test/unit_test.hpp>
#include <json_common.h>

#include <kisurf_ai_sch_context_adapter.h>

#include <sch_field.h>
#include <sch_junction.h>
#include <sch_label.h>
#include <sch_line.h>
#include <sch_no_connect.h>
#include <sch_screen.h>
#include <sch_symbol.h>

namespace
{

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


nlohmann::json detailsForLabel( const std::vector<AI_OBJECT_REF>& aRefs,
                                const wxString& aLabel )
{
    const AI_OBJECT_REF* ref = findRefByLabel( aRefs, aLabel );

    BOOST_REQUIRE_MESSAGE( ref, "missing AI object ref label: " << aLabel.ToStdString() );
    BOOST_REQUIRE( !ref->m_DetailsJson.IsEmpty() );

    return nlohmann::json::parse( ref->m_DetailsJson.ToStdString() );
}

} // namespace

BOOST_AUTO_TEST_SUITE( AiSchContextAdapter )

BOOST_AUTO_TEST_CASE( AdapterCollectsSymbolsAsVisibleObjects )
{
    SCH_SCREEN screen;
    SCH_SYMBOL* symbol = new SCH_SYMBOL();

    symbol->GetField( FIELD_T::REFERENCE )->SetText( wxS( "R1" ) );
    screen.Append( symbol, false );

    KISURF_AI_SCH_CONTEXT_ADAPTER adapter( screen );
    AI_CONTEXT_INDEX              index = adapter.BuildIndex();

    BOOST_CHECK( index.EditorKind() == AI_EDITOR_KIND::Schematic );
    BOOST_CHECK( index.Version().IsValid() );
    BOOST_REQUIRE_EQUAL( index.VisibleObjects().size(), 1 );
    BOOST_CHECK( index.VisibleObjects().front().m_Uuid == symbol->m_Uuid );
    BOOST_CHECK_EQUAL( index.VisibleObjects().front().m_Type, SCH_SYMBOL_T );
    BOOST_CHECK_EQUAL( index.VisibleObjects().front().m_Label, wxString( wxS( "R1" ) ) );
}

BOOST_AUTO_TEST_CASE( AdapterCollectsSelectedSymbols )
{
    SCH_SCREEN screen;
    SCH_SYMBOL* symbol = new SCH_SYMBOL();

    symbol->GetField( FIELD_T::REFERENCE )->SetText( wxS( "U2" ) );
    symbol->SetSelected();
    screen.Append( symbol, false );

    KISURF_AI_SCH_CONTEXT_ADAPTER adapter( screen );
    AI_CONTEXT_INDEX              index = adapter.BuildIndex();

    BOOST_REQUIRE_EQUAL( index.SelectedObjects().size(), 1 );
    BOOST_CHECK( index.SelectedObjects().front().m_Uuid == symbol->m_Uuid );
    BOOST_CHECK_EQUAL( index.SelectedObjects().front().m_Label, wxString( wxS( "U2" ) ) );
}


BOOST_AUTO_TEST_CASE( AdapterAddsStructuredDetailsToCommonSchematicObjects )
{
    SCH_SCREEN screen;

    SCH_SYMBOL* symbol = new SCH_SYMBOL();
    symbol->GetField( FIELD_T::REFERENCE )->SetText( wxS( "R1" ) );
    symbol->GetField( FIELD_T::VALUE )->SetText( wxS( "10k" ) );
    symbol->GetField( FIELD_T::FOOTPRINT )->SetText( wxS( "Resistor_SMD:R_0603" ) );
    symbol->SetPosition( VECTOR2I( 100, 200 ) );
    screen.Append( symbol, false );

    SCH_LINE* wire = new SCH_LINE( VECTOR2I( 0, 0 ), LAYER_WIRE );
    wire->SetEndPoint( VECTOR2I( 1000, 0 ) );
    screen.Append( wire, false );

    SCH_LINE* bus = new SCH_LINE( VECTOR2I( 0, 500 ), LAYER_BUS );
    bus->SetEndPoint( VECTOR2I( 1000, 500 ) );
    screen.Append( bus, false );

    screen.Append( new SCH_LABEL( VECTOR2I( 10, 20 ), wxS( "DATA" ) ), false );
    screen.Append( new SCH_GLOBALLABEL( VECTOR2I( 30, 40 ), wxS( "+5V" ) ), false );
    screen.Append( new SCH_HIERLABEL( VECTOR2I( 50, 60 ), wxS( "READY" ) ), false );
    screen.Append( new SCH_JUNCTION( VECTOR2I( 70, 80 ) ), false );
    screen.Append( new SCH_NO_CONNECT( VECTOR2I( 90, 100 ) ), false );

    KISURF_AI_SCH_CONTEXT_ADAPTER adapter( screen );
    AI_CONTEXT_INDEX              index = adapter.BuildIndex();

    nlohmann::json symbolDetails = detailsForLabel( index.VisibleObjects(), wxS( "R1" ) );
    BOOST_CHECK_EQUAL( symbolDetails["kind"].get<std::string>(), "symbol" );
    BOOST_CHECK_EQUAL( symbolDetails["reference"].get<std::string>(), "R1" );
    BOOST_CHECK_EQUAL( symbolDetails["value"].get<std::string>(), "10k" );
    BOOST_CHECK_EQUAL( symbolDetails["footprint"].get<std::string>(), "Resistor_SMD:R_0603" );
    BOOST_CHECK_EQUAL( symbolDetails["position"]["x"].get<int>(), 100 );
    BOOST_CHECK_EQUAL( symbolDetails["position"]["y"].get<int>(), 200 );

    nlohmann::json wireDetails =
            detailsForLabel( index.VisibleObjects(), wxS( "wire:0,0->1000,0" ) );
    BOOST_CHECK_EQUAL( wireDetails["kind"].get<std::string>(), "wire" );
    BOOST_CHECK_EQUAL( wireDetails["start"]["x"].get<int>(), 0 );
    BOOST_CHECK_EQUAL( wireDetails["end"]["x"].get<int>(), 1000 );
    BOOST_CHECK_EQUAL( wireDetails["layer"].get<std::string>(), "wire" );

    nlohmann::json busDetails =
            detailsForLabel( index.VisibleObjects(), wxS( "bus:0,500->1000,500" ) );
    BOOST_CHECK_EQUAL( busDetails["kind"].get<std::string>(), "bus" );
    BOOST_CHECK_EQUAL( busDetails["start"]["y"].get<int>(), 500 );
    BOOST_CHECK_EQUAL( busDetails["layer"].get<std::string>(), "bus" );

    nlohmann::json labelDetails =
            detailsForLabel( index.VisibleObjects(), wxS( "label:DATA" ) );
    BOOST_CHECK_EQUAL( labelDetails["kind"].get<std::string>(), "label" );
    BOOST_CHECK_EQUAL( labelDetails["text"].get<std::string>(), "DATA" );
    BOOST_CHECK_EQUAL( labelDetails["position"]["x"].get<int>(), 10 );

    nlohmann::json globalDetails =
            detailsForLabel( index.VisibleObjects(), wxS( "global_label:+5V" ) );
    BOOST_CHECK_EQUAL( globalDetails["kind"].get<std::string>(), "global_label" );
    BOOST_CHECK_EQUAL( globalDetails["text"].get<std::string>(), "+5V" );

    nlohmann::json hierDetails =
            detailsForLabel( index.VisibleObjects(), wxS( "hier_label:READY" ) );
    BOOST_CHECK_EQUAL( hierDetails["kind"].get<std::string>(), "hier_label" );
    BOOST_CHECK_EQUAL( hierDetails["position"]["y"].get<int>(), 60 );

    nlohmann::json junctionDetails =
            detailsForLabel( index.VisibleObjects(), wxS( "junction:70,80" ) );
    BOOST_CHECK_EQUAL( junctionDetails["kind"].get<std::string>(), "junction" );
    BOOST_CHECK_EQUAL( junctionDetails["position"]["x"].get<int>(), 70 );

    nlohmann::json noConnectDetails =
            detailsForLabel( index.VisibleObjects(), wxS( "no_connect:90,100" ) );
    BOOST_CHECK_EQUAL( noConnectDetails["kind"].get<std::string>(), "no_connect" );
    BOOST_CHECK_EQUAL( noConnectDetails["position"]["y"].get<int>(), 100 );
}

BOOST_AUTO_TEST_SUITE_END()
