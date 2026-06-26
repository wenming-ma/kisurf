#include <boost/test/unit_test.hpp>
#include <json_common.h>

#include <kisurf_ai_pcb_context_adapter.h>

#include <board.h>
#include <board_design_settings.h>
#include <connectivity/connectivity_data.h>
#include <drc/drc_engine.h>
#include <drc/drc_rule.h>
#include <drc/drc_rule_condition.h>
#include <footprint.h>
#include <netinfo.h>
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
#include <project/project_local_settings.h>
#include <project/net_settings.h>
#include <settings/settings_manager.h>
#include <memory>
#include <set>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <zone.h>

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


const AI_CONTEXT_ANCHOR* findAnchorById( const std::vector<AI_CONTEXT_ANCHOR>& aAnchors,
                                         const wxString& aId )
{
    for( const AI_CONTEXT_ANCHOR& anchor : aAnchors )
    {
        if( anchor.m_Id == aId )
            return &anchor;
    }

    return nullptr;
}


nlohmann::json anchorDetails( const AI_CONTEXT_ANCHOR& aAnchor )
{
    BOOST_REQUIRE( !aAnchor.m_DetailsJson.IsEmpty() );
    return nlohmann::json::parse( aAnchor.m_DetailsJson.ToStdString() );
}


wxString anchorId( const wxString& aPrefix, const KIID& aUuid, const wxString& aRole )
{
    return wxS( "pcb." ) + aPrefix + wxS( "." ) + aUuid.AsString() + wxS( "." ) + aRole;
}


nlohmann::json detailsForLabel( const std::vector<AI_OBJECT_REF>& aRefs,
                                const wxString& aLabel )
{
    const AI_OBJECT_REF* ref = findRefByLabel( aRefs, aLabel );

    BOOST_REQUIRE( ref );
    BOOST_REQUIRE( !ref->m_DetailsJson.IsEmpty() );

    return nlohmann::json::parse( ref->m_DetailsJson.ToStdString() );
}


const nlohmann::json* findLayerById( const nlohmann::json& aLayers, PCB_LAYER_ID aLayer )
{
    for( const nlohmann::json& layer : aLayers )
    {
        if( layer["id"].get<int>() == static_cast<int>( aLayer ) )
            return &layer;
    }

    return nullptr;
}


const nlohmann::json* findNetByCode( const nlohmann::json& aNets, int aNetCode )
{
    for( const nlohmann::json& net : aNets )
    {
        if( net["code"].get<int>() == aNetCode )
            return &net;
    }

    return nullptr;
}


const nlohmann::json* findObstacleByKindAndLabel( const nlohmann::json& aObstacles,
                                                  const std::string& aKind,
                                                  const std::string& aLabel )
{
    for( const nlohmann::json& obstacle : aObstacles )
    {
        if( obstacle["kind"].get<std::string>() == aKind
            && obstacle["label"].get<std::string>() == aLabel )
        {
            return &obstacle;
        }
    }

    return nullptr;
}


const nlohmann::json* findWorstConstraintByType( const nlohmann::json& aConstraints,
                                                 const std::string& aType )
{
    for( const nlohmann::json& constraint : aConstraints )
    {
        if( constraint["type"].get<std::string>() == aType )
            return &constraint;
    }

    return nullptr;
}


const nlohmann::json* findPairConstraintByLabels( const nlohmann::json& aConstraints,
                                                  const std::string& aLabelA,
                                                  const std::string& aLabelB )
{
    for( const nlohmann::json& constraint : aConstraints )
    {
        const std::string source = constraint["source_item"]["label"].get<std::string>();
        const std::string target = constraint["target_item"]["label"].get<std::string>();

        if( ( source == aLabelA && target == aLabelB )
            || ( source == aLabelB && target == aLabelA ) )
        {
            return &constraint;
        }
    }

    return nullptr;
}


const nlohmann::json* findGeometryRuleCoverage( const nlohmann::json& aCoverage,
                                                const std::string& aRule,
                                                const std::string& aGeometry )
{
    for( const nlohmann::json& coverage : aCoverage )
    {
        if( coverage["rule"].get<std::string>().find( aRule ) != std::string::npos
            && coverage["geometry"].get<std::string>() == aGeometry )
        {
            return &coverage;
        }
    }

    return nullptr;
}


const nlohmann::json* findComponentContainingLabel( const nlohmann::json& aComponents,
                                                    const std::string& aLabel )
{
    for( const nlohmann::json& component : aComponents )
    {
        for( const nlohmann::json& item : component["items"] )
        {
            if( item["label"].get<std::string>() == aLabel )
                return &component;
        }
    }

    return nullptr;
}


const nlohmann::json* findNetComponentSummaryByCode( const nlohmann::json& aSummaries,
                                                     int aNetCode )
{
    for( const nlohmann::json& summary : aSummaries )
    {
        if( summary["net_code"].get<int>() == aNetCode )
            return &summary;
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


PAD* addRoundPad( FOOTPRINT& aFootprint, const wxString& aNumber, const VECTOR2I& aPosition,
                  int aNetCode )
{
    PAD* pad = new PAD( &aFootprint );

    pad->SetNumber( aNumber );
    pad->SetPosition( aPosition );
    pad->SetShape( PADSTACK::ALL_LAYERS, PAD_SHAPE::CIRCLE );
    pad->SetSize( PADSTACK::ALL_LAYERS, VECTOR2I( 1000, 1000 ) );
    pad->SetLayerSet( LSET( { F_Cu, F_Mask } ) );
    pad->SetAttribute( PAD_ATTRIB::SMD );
    pad->SetNetCode( aNetCode );

    aFootprint.Add( pad );

    return pad;
}
} // namespace

BOOST_AUTO_TEST_SUITE( AiPcbContextAdapter )

BOOST_AUTO_TEST_CASE( AdapterAddsBoardSummaryObservationFacts )
{
    BOARD board;
    board.Add( new NETINFO_ITEM( &board, wxS( "/GND" ), 1 ) );
    board.GetDesignSettings().m_MinClearance = 111000;
    board.GetDesignSettings().m_CopperEdgeClearance = 222000;
    board.GetDesignSettings().m_HoleClearance = 333000;
    board.GetDesignSettings().m_NetSettings->GetDefaultNetclass()->SetClearance( 444000 );
    board.GetDesignSettings().m_NetSettings->GetDefaultNetclass()->SetTrackWidth( 555000 );
    board.GetDesignSettings().m_NetSettings->GetDefaultNetclass()->SetViaDiameter( 666000 );
    board.GetDesignSettings().m_NetSettings->GetDefaultNetclass()->SetViaDrill( 777000 );

    FOOTPRINT* footprint = new FOOTPRINT( &board );
    footprint->SetReference( wxS( "U1" ) );
    board.Add( footprint );

    PCB_TRACK* track = new PCB_TRACK( &board );
    track->SetStart( VECTOR2I( 100, 200 ) );
    track->SetEnd( VECTOR2I( 300, 200 ) );
    track->SetLayer( F_Cu );
    track->SetNetCode( 1 );
    board.Add( track );

    ZONE* zone = new ZONE( &board );
    zone->SetLayerSet( LSET( { F_Cu } ) );
    zone->SetNetCode( 1 );
    appendRectangle( *zone );
    board.Add( zone );

    PCB_SHAPE* outline = new PCB_SHAPE( &board, SHAPE_T::RECTANGLE );
    outline->SetLayer( Edge_Cuts );
    outline->SetStart( VECTOR2I( 1000, 2000 ) );
    outline->SetEnd( VECTOR2I( 5000, 7000 ) );
    outline->SetWidth( 0 );
    board.Add( outline );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_SNAPSHOT           snapshot = adapter.BuildIndex().BuildSnapshot();

    BOOST_REQUIRE( !snapshot.m_Summary.IsEmpty() );

    nlohmann::json summary = nlohmann::json::parse( snapshot.m_Summary.ToStdString() );
    BOOST_CHECK_EQUAL( summary["kind"].get<std::string>(), "pcb_board_summary" );
    BOOST_CHECK_EQUAL( summary["net_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( summary["footprint_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( summary["track_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( summary["zone_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( summary["edge_cut_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( summary["clearance_sources"]["source"].get<std::string>(),
                       "board_design_settings" );
    BOOST_CHECK_EQUAL( summary["clearance_sources"]["minimum_clearance"].get<int>(), 111000 );
    BOOST_CHECK_EQUAL( summary["clearance_sources"]["copper_edge_clearance"].get<int>(), 222000 );
    BOOST_CHECK_EQUAL( summary["clearance_sources"]["hole_clearance"].get<int>(), 333000 );
    BOOST_CHECK_EQUAL(
            summary["clearance_sources"]["default_netclass"]["name"].get<std::string>(),
            "Default" );
    BOOST_CHECK_EQUAL(
            summary["clearance_sources"]["default_netclass"]["clearance"].get<int>(),
            444000 );
    BOOST_CHECK_EQUAL(
            summary["clearance_sources"]["default_netclass"]["track_width"].get<int>(),
            555000 );
    BOOST_CHECK_EQUAL(
            summary["clearance_sources"]["default_netclass"]["via_diameter"].get<int>(),
            666000 );
    BOOST_CHECK_EQUAL(
            summary["clearance_sources"]["default_netclass"]["via_drill"].get<int>(),
            777000 );
    BOOST_CHECK_EQUAL( summary["board_edges_bbox"]["defined"].get<bool>(), true );
    BOOST_CHECK_EQUAL( summary["board_edges_bbox"]["origin"]["x"].get<int>(), 1000 );
    BOOST_CHECK_EQUAL( summary["board_edges_bbox"]["origin"]["y"].get<int>(), 2000 );
    BOOST_CHECK_EQUAL( summary["board_edges_bbox"]["end"]["x"].get<int>(), 5000 );
    BOOST_CHECK_EQUAL( summary["board_edges_bbox"]["end"]["y"].get<int>(), 7000 );
}


BOOST_AUTO_TEST_CASE( AdapterAddsLayerContextObservationFacts )
{
    SETTINGS_MANAGER mgr;
    wxString         projectPath = wxStandardPaths::Get().GetTempDir()
                           + wxFileName::GetPathSeparator()
                           + wxS( "kisurf_ai_layer_context.kicad_pro" );

    mgr.LoadProject( projectPath.ToStdString() );

    BOARD board;
    board.SetProject( &mgr.Prj() );
    board.SetCopperLayerCount( 4 );
    board.SetVisibleLayers( LSET( { F_Cu, Edge_Cuts } ) );
    mgr.Prj().GetLocalSettings().m_ActiveLayer = B_Cu;

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_SNAPSHOT           snapshot = adapter.BuildIndex().BuildSnapshot();

    nlohmann::json summary = nlohmann::json::parse( snapshot.m_Summary.ToStdString() );
    nlohmann::json layerContext = summary["layer_context"];

    BOOST_CHECK_EQUAL( layerContext["source"].get<std::string>(), "board" );
    BOOST_CHECK_EQUAL( layerContext["visible_layers_source"].get<std::string>(),
                       "project_local_settings" );
    BOOST_CHECK_EQUAL( layerContext["copper_layer_count"].get<int>(), 4 );
    BOOST_CHECK_GE( layerContext["enabled_layer_count"].get<int>(), 4 );
    BOOST_CHECK_EQUAL( layerContext["visible_layer_count"].get<int>(), 2 );
    BOOST_CHECK_EQUAL( layerContext["active_layer"]["id"].get<int>(), static_cast<int>( B_Cu ) );
    BOOST_CHECK_EQUAL( layerContext["active_layer"]["name"].get<std::string>(), "B.Cu" );
    BOOST_CHECK_EQUAL( layerContext["active_layer"]["visible"].get<bool>(), false );
    BOOST_CHECK_EQUAL( layerContext["active_layer"]["copper"].get<bool>(), true );

    const nlohmann::json* fCu = findLayerById( layerContext["layers"], F_Cu );
    const nlohmann::json* bCu = findLayerById( layerContext["layers"], B_Cu );
    const nlohmann::json* edgeCuts = findLayerById( layerContext["layers"], Edge_Cuts );

    BOOST_REQUIRE( fCu );
    BOOST_REQUIRE( bCu );
    BOOST_REQUIRE( edgeCuts );
    BOOST_CHECK_EQUAL( ( *fCu )["name"].get<std::string>(), "F.Cu" );
    BOOST_CHECK_EQUAL( ( *fCu )["copper"].get<bool>(), true );
    BOOST_CHECK_EQUAL( ( *fCu )["visible"].get<bool>(), true );
    BOOST_CHECK_EQUAL( ( *bCu )["copper"].get<bool>(), true );
    BOOST_CHECK_EQUAL( ( *bCu )["visible"].get<bool>(), false );
    BOOST_CHECK_EQUAL( ( *edgeCuts )["name"].get<std::string>(), "Edge.Cuts" );
    BOOST_CHECK_EQUAL( ( *edgeCuts )["copper"].get<bool>(), false );
    BOOST_CHECK_EQUAL( ( *edgeCuts )["visible"].get<bool>(), true );

    board.ClearProject();
    mgr.UnloadProject( &mgr.Prj(), false );
}


BOOST_AUTO_TEST_CASE( AdapterAddsObstacleObservationFacts )
{
    BOARD board;
    board.Add( new NETINFO_ITEM( &board, wxS( "/SIG" ), 1 ) );

    FOOTPRINT* footprint = new FOOTPRINT( &board );
    footprint->SetReference( wxS( "U1" ) );
    addRoundPad( *footprint, wxS( "1" ), VECTOR2I( 100, 200 ), 1 );
    board.Add( footprint );

    PCB_TRACK* track = new PCB_TRACK( &board );
    track->SetStart( VECTOR2I( 0, 0 ) );
    track->SetEnd( VECTOR2I( 1000, 0 ) );
    track->SetLayer( F_Cu );
    track->SetWidth( 120 );
    track->SetNetCode( 1 );
    board.Add( track );

    PCB_VIA* via = new PCB_VIA( &board );
    via->SetPosition( VECTOR2I( 500, 600 ) );
    via->SetWidth( PADSTACK::ALL_LAYERS, 300 );
    via->SetDrill( 100 );
    via->SetNetCode( 1 );
    board.Add( via );

    ZONE* keepout = new ZONE( &board );
    keepout->SetZoneName( wxS( "NO_ROUTING" ) );
    keepout->SetIsRuleArea( true );
    keepout->SetLayerSet( LSET( { F_Cu, B_Cu } ) );
    keepout->SetDoNotAllowTracks( true );
    keepout->SetDoNotAllowVias( true );
    appendRectangle( *keepout );
    board.Add( keepout );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_SNAPSHOT           snapshot = adapter.BuildIndex().BuildSnapshot();

    nlohmann::json summary = nlohmann::json::parse( snapshot.m_Summary.ToStdString() );
    nlohmann::json obstacleFacts = summary["obstacle_facts"];

    BOOST_CHECK_EQUAL( obstacleFacts["source"].get<std::string>(), "board" );
    BOOST_CHECK_EQUAL( obstacleFacts["obstacle_count"].get<int>(), 5 );
    BOOST_CHECK_EQUAL( obstacleFacts["obstacle_sample_truncated"].get<bool>(), false );
    BOOST_REQUIRE_EQUAL( obstacleFacts["obstacles"].size(), 5u );

    const nlohmann::json* footprintObstacle =
            findObstacleByKindAndLabel( obstacleFacts["obstacles"], "footprint", "U1" );
    const nlohmann::json* padObstacle =
            findObstacleByKindAndLabel( obstacleFacts["obstacles"], "pad", "U1.1" );
    const nlohmann::json* trackObstacle =
            findObstacleByKindAndLabel( obstacleFacts["obstacles"], "track", "track:0,0->1000,0" );
    const nlohmann::json* viaObstacle =
            findObstacleByKindAndLabel( obstacleFacts["obstacles"], "via", "via:500,600" );
    const nlohmann::json* keepoutObstacle =
            findObstacleByKindAndLabel( obstacleFacts["obstacles"], "keepout", "keepout:NO_ROUTING" );

    BOOST_REQUIRE( footprintObstacle );
    BOOST_REQUIRE( padObstacle );
    BOOST_REQUIRE( trackObstacle );
    BOOST_REQUIRE( viaObstacle );
    BOOST_REQUIRE( keepoutObstacle );
    BOOST_CHECK_EQUAL( ( *padObstacle )["position"]["x"].get<int>(), 100 );
    BOOST_CHECK_EQUAL( ( *padObstacle )["net_code"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( ( *trackObstacle )["net_code"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( ( *trackObstacle )["layer"].get<std::string>(), "F.Cu" );
    BOOST_CHECK_EQUAL( ( *viaObstacle )["position"]["x"].get<int>(), 500 );
    BOOST_CHECK_EQUAL( ( *keepoutObstacle )["blocks"]["tracks"].get<bool>(), true );
    BOOST_CHECK_EQUAL( ( *keepoutObstacle )["blocks"]["vias"].get<bool>(), true );
}


BOOST_AUTO_TEST_CASE( AdapterAddsConnectivityObservationFacts )
{
    BOARD board;
    board.Add( new NETINFO_ITEM( &board, wxS( "/SIG" ), 1 ) );

    FOOTPRINT* left = new FOOTPRINT( &board );
    left->SetReference( wxS( "U1" ) );
    addRoundPad( *left, wxS( "1" ), VECTOR2I( 0, 0 ), 1 );
    board.Add( left );

    FOOTPRINT* right = new FOOTPRINT( &board );
    right->SetReference( wxS( "U2" ) );
    addRoundPad( *right, wxS( "1" ), VECTOR2I( 100000, 0 ), 1 );
    board.Add( right );

    BOOST_REQUIRE( board.BuildConnectivity() );
    BOOST_REQUIRE_EQUAL( board.GetConnectivity()->GetUnconnectedCount( false ), 1u );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_SNAPSHOT           snapshot = adapter.BuildIndex().BuildSnapshot();

    nlohmann::json summary = nlohmann::json::parse( snapshot.m_Summary.ToStdString() );
    nlohmann::json connectivity = summary["connectivity_summary"];

    BOOST_CHECK_EQUAL( connectivity["source"].get<std::string>(), "board_connectivity" );
    BOOST_CHECK_EQUAL( connectivity["present"].get<bool>(), true );
    BOOST_CHECK_EQUAL( connectivity["net_count"].get<int>(),
                       board.GetConnectivity()->GetNetCount() );
    BOOST_CHECK_EQUAL( connectivity["node_count"].get<int>(),
                       board.GetConnectivity()->GetNodeCount() );
    BOOST_CHECK_EQUAL( connectivity["pad_count"].get<int>(),
                       board.GetConnectivity()->GetPadCount() );
    BOOST_CHECK_EQUAL( connectivity["ratsnest_unconnected_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( connectivity["visible_ratsnest_unconnected_count"].get<int>(),
                       board.GetConnectivity()->GetUnconnectedCount( true ) );
    BOOST_CHECK_EQUAL( connectivity["local_ratsnest_line_count"].get<int>(), 0 );

    BOOST_REQUIRE_EQUAL( connectivity["unconnected_edges"].size(), 1u );

    nlohmann::json edge = connectivity["unconnected_edges"][0];
    BOOST_CHECK_EQUAL( edge["net_code"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( edge["net_name"].get<std::string>(), "/SIG" );
    BOOST_CHECK_EQUAL( edge["visible"].get<bool>(), true );
    BOOST_CHECK_EQUAL( edge["source"]["item_type"].get<int>(), PCB_PAD_T );
    BOOST_CHECK_EQUAL( edge["target"]["item_type"].get<int>(), PCB_PAD_T );

    std::set<int> endpointXs = { edge["source"]["position"]["x"].get<int>(),
                                 edge["target"]["position"]["x"].get<int>() };
    std::set<int> endpointYs = { edge["source"]["position"]["y"].get<int>(),
                                 edge["target"]["position"]["y"].get<int>() };

    BOOST_CHECK( endpointXs == std::set<int>( { 0, 100000 } ) );
    BOOST_CHECK( endpointYs == std::set<int>( { 0 } ) );
    BOOST_CHECK_EQUAL( connectivity["unconnected_edge_sample_truncated"].get<bool>(), false );

    const nlohmann::json* sigFact = findNetByCode( summary["net_facts"], 1 );

    BOOST_REQUIRE( sigFact );
    BOOST_CHECK_EQUAL( ( *sigFact )["topology"]["node_count"].get<int>(), 2 );
    BOOST_CHECK_EQUAL( ( *sigFact )["topology"]["pad_count"].get<int>(), 2 );
    BOOST_CHECK_EQUAL( ( *sigFact )["topology"]["unconnected_edge_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( ( *sigFact )["topology"]["visible_unconnected_edge_count"].get<int>(), 1 );

    nlohmann::json topology = ( *sigFact )["topology"];
    BOOST_REQUIRE_EQUAL( topology["items"].size(), 2u );
    BOOST_CHECK_EQUAL( topology["item_sample_truncated"].get<bool>(), false );

    std::set<std::string> itemRefs = { topology["items"][0]["label"].get<std::string>(),
                                       topology["items"][1]["label"].get<std::string>() };
    std::set<int> itemXs = { topology["items"][0]["position"]["x"].get<int>(),
                             topology["items"][1]["position"]["x"].get<int>() };

    BOOST_CHECK( itemRefs == std::set<std::string>( { "U1.1", "U2.1" } ) );
    BOOST_CHECK( itemXs == std::set<int>( { 0, 100000 } ) );
    BOOST_CHECK_EQUAL( topology["items"][0]["kind"].get<std::string>(), "pad" );
    BOOST_CHECK_EQUAL( topology["items"][1]["kind"].get<std::string>(), "pad" );

    BOOST_REQUIRE_EQUAL( topology["unconnected_edges"].size(), 1u );
    BOOST_CHECK_EQUAL( topology["unconnected_edges"][0]["net_code"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( topology["unconnected_edges"][0]["net_name"].get<std::string>(), "/SIG" );
    BOOST_CHECK_EQUAL( topology["unconnected_edge_sample_truncated"].get<bool>(), false );
}


BOOST_AUTO_TEST_CASE( AdapterAddsConnectivityGraphComponentFacts )
{
    BOARD board;
    board.Add( new NETINFO_ITEM( &board, wxS( "/SIG" ), 1 ) );

    FOOTPRINT* left = new FOOTPRINT( &board );
    left->SetReference( wxS( "U1" ) );
    addRoundPad( *left, wxS( "1" ), VECTOR2I( 0, 0 ), 1 );
    board.Add( left );

    FOOTPRINT* right = new FOOTPRINT( &board );
    right->SetReference( wxS( "U2" ) );
    addRoundPad( *right, wxS( "1" ), VECTOR2I( 100000, 0 ), 1 );
    board.Add( right );

    FOOTPRINT* isolated = new FOOTPRINT( &board );
    isolated->SetReference( wxS( "U3" ) );
    addRoundPad( *isolated, wxS( "1" ), VECTOR2I( 300000, 0 ), 1 );
    board.Add( isolated );

    PCB_TRACK* track = new PCB_TRACK( &board );
    track->SetStart( VECTOR2I( 0, 0 ) );
    track->SetEnd( VECTOR2I( 100000, 0 ) );
    track->SetLayer( F_Cu );
    track->SetNetCode( 1 );
    board.Add( track );

    BOOST_REQUIRE( board.BuildConnectivity() );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_SNAPSHOT           snapshot = adapter.BuildIndex().BuildSnapshot();

    nlohmann::json summary = nlohmann::json::parse( snapshot.m_Summary.ToStdString() );
    const nlohmann::json* sigFact = findNetByCode( summary["net_facts"], 1 );

    BOOST_REQUIRE( sigFact );

    nlohmann::json topology = ( *sigFact )["topology"];
    BOOST_CHECK_EQUAL( topology["component_count"].get<int>(), 2 );
    BOOST_CHECK_EQUAL( topology["component_sample_truncated"].get<bool>(), false );
    BOOST_REQUIRE_EQUAL( topology["components"].size(), 2u );

    const nlohmann::json* routedComponent =
            findComponentContainingLabel( topology["components"], "track:0,0->100000,0" );
    const nlohmann::json* isolatedComponent =
            findComponentContainingLabel( topology["components"], "U3.1" );

    BOOST_REQUIRE( routedComponent );
    BOOST_REQUIRE( isolatedComponent );
    BOOST_CHECK_EQUAL( ( *routedComponent )["item_count"].get<int>(), 3 );
    BOOST_CHECK_EQUAL( ( *isolatedComponent )["item_count"].get<int>(), 1 );

    BOOST_REQUIRE_EQUAL( topology["ratsnest_component_edges"].size(), 1u );
    BOOST_CHECK_NE(
            topology["ratsnest_component_edges"][0]["source_component"].get<int>(),
            topology["ratsnest_component_edges"][0]["target_component"].get<int>() );
    BOOST_CHECK_EQUAL(
            topology["ratsnest_component_edge_sample_truncated"].get<bool>(), false );

    nlohmann::json connectivity = summary["connectivity_summary"];
    BOOST_REQUIRE( connectivity.contains( "net_component_summaries" ) );

    const nlohmann::json* sigSummary =
            findNetComponentSummaryByCode( connectivity["net_component_summaries"], 1 );

    BOOST_REQUIRE( sigSummary );
    BOOST_CHECK_EQUAL( ( *sigSummary )["net_name"].get<std::string>(), "/SIG" );
    BOOST_CHECK_EQUAL( ( *sigSummary )["component_count"].get<int>(), 2 );
    BOOST_CHECK_EQUAL( ( *sigSummary )["sample_component_count"].get<int>(), 2 );
    BOOST_CHECK_EQUAL( ( *sigSummary )["component_sample_truncated"].get<bool>(), false );
    BOOST_CHECK_EQUAL( ( *sigSummary )["ratsnest_component_edge_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( ( *sigSummary )["visible_ratsnest_component_edge_count"].get<int>(), 1 );
    BOOST_REQUIRE_EQUAL( ( *sigSummary )["components"].size(), 2u );

    const nlohmann::json* summaryRoutedComponent =
            findComponentContainingLabel( ( *sigSummary )["components"], "track:0,0->100000,0" );

    BOOST_REQUIRE( summaryRoutedComponent );
    BOOST_CHECK_EQUAL( ( *summaryRoutedComponent )["item_count"].get<int>(), 3 );
}


BOOST_AUTO_TEST_CASE( AdapterAddsPerNetNetclassObservationFacts )
{
    BOARD board;

    std::shared_ptr<NETCLASS> power = std::make_shared<NETCLASS>( wxS( "Power" ) );
    power->SetClearance( 123000 );
    power->SetTrackWidth( 234000 );
    power->SetViaDiameter( 345000 );
    power->SetViaDrill( 456000 );
    board.GetDesignSettings().m_NetSettings->SetNetclass( power->GetName(), power );

    NETINFO_ITEM* sig = new NETINFO_ITEM( &board, wxS( "/SIG" ), 1 );
    sig->SetNetClass( power );
    board.Add( sig );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_SNAPSHOT           snapshot = adapter.BuildIndex().BuildSnapshot();

    nlohmann::json summary = nlohmann::json::parse( snapshot.m_Summary.ToStdString() );
    const nlohmann::json* sigFact = findNetByCode( summary["net_facts"], 1 );

    BOOST_REQUIRE( sigFact );
    BOOST_CHECK_EQUAL( ( *sigFact )["name"].get<std::string>(), "/SIG" );
    BOOST_CHECK_EQUAL( ( *sigFact )["netclass"]["name"].get<std::string>(), "Power" );
    BOOST_CHECK_EQUAL( ( *sigFact )["netclass"]["clearance"].get<int>(), 123000 );
    BOOST_CHECK_EQUAL( ( *sigFact )["netclass"]["track_width"].get<int>(), 234000 );
    BOOST_CHECK_EQUAL( ( *sigFact )["netclass"]["via_diameter"].get<int>(), 345000 );
    BOOST_CHECK_EQUAL( ( *sigFact )["netclass"]["via_drill"].get<int>(), 456000 );
}


BOOST_AUTO_TEST_CASE( AdapterCollectsFootprintsAndPadsAsVisibleObjects )
{
    BOARD board;

    FOOTPRINT* footprint = new FOOTPRINT( &board );
    PAD*       pad = new PAD( footprint );

    pad->SetNumber( wxS( "1" ) );
    footprint->SetReference( wxS( "U1" ) );
    footprint->Add( pad );
    board.Add( footprint );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_INDEX              index = adapter.BuildIndex();

    BOOST_CHECK( index.EditorKind() == AI_EDITOR_KIND::Pcb );
    BOOST_CHECK( index.Version().IsValid() );
    BOOST_REQUIRE_GE( index.VisibleObjects().size(), 2 );

    const AI_OBJECT_REF* footprintRef = findRefByLabel( index.VisibleObjects(), wxS( "U1" ) );
    const AI_OBJECT_REF* padRef = findRefByLabel( index.VisibleObjects(), wxS( "U1.1" ) );

    BOOST_REQUIRE( footprintRef );
    BOOST_REQUIRE( padRef );
    BOOST_CHECK( footprintRef->m_Uuid == footprint->m_Uuid );
    BOOST_CHECK_EQUAL( footprintRef->m_Type, PCB_FOOTPRINT_T );
    BOOST_CHECK( padRef->m_Uuid == pad->m_Uuid );
    BOOST_CHECK_EQUAL( padRef->m_Type, PCB_PAD_T );
}


BOOST_AUTO_TEST_CASE( AdapterCollectsFootprintFieldsAsVisibleObjects )
{
    BOARD board;

    FOOTPRINT* footprint = new FOOTPRINT( &board );
    footprint->SetReference( wxS( "U1" ) );
    footprint->SetValue( wxS( "MCU" ) );

    PCB_FIELD* reference = footprint->GetField( FIELD_T::REFERENCE );
    BOOST_REQUIRE( reference );
    reference->SetPosition( VECTOR2I( 100, 200 ) );
    reference->SetLayer( F_SilkS );
    reference->SetTextSize( VECTOR2I( 1200, 900 ) );
    reference->SetSelected();

    board.Add( footprint );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_INDEX              index = adapter.BuildIndex();

    const AI_OBJECT_REF* ref =
            findRefByLabel( index.VisibleObjects(), wxS( "fp:U1/field:Reference" ) );

    BOOST_REQUIRE( ref );
    BOOST_CHECK( ref->m_Uuid == reference->m_Uuid );
    BOOST_CHECK_EQUAL( ref->m_Type, PCB_FIELD_T );
    BOOST_CHECK( findRefByLabel( index.SelectedObjects(), wxS( "fp:U1/field:Reference" ) ) );

    nlohmann::json details =
            detailsForLabel( index.VisibleObjects(), wxS( "fp:U1/field:Reference" ) );
    BOOST_CHECK_EQUAL( details["kind"].get<std::string>(), "field" );
    BOOST_CHECK_EQUAL( details["text"].get<std::string>(), "U1" );
    BOOST_CHECK_EQUAL( details["shown_text"].get<std::string>(), "U1" );
    BOOST_CHECK_EQUAL( details["parent_footprint_reference"].get<std::string>(), "U1" );
    BOOST_CHECK( !details["parent_footprint_uuid"].get<std::string>().empty() );
    BOOST_CHECK_EQUAL( details["field_name"].get<std::string>(), "Reference" );
    BOOST_CHECK_EQUAL( details["field_canonical_name"].get<std::string>(), "Reference" );
    BOOST_CHECK_EQUAL( details["is_reference"].get<bool>(), true );
    BOOST_CHECK_EQUAL( details["is_value"].get<bool>(), false );
    BOOST_CHECK_EQUAL( details["position"]["x"].get<int>(), 100 );
    BOOST_CHECK_EQUAL( details["layer"].get<std::string>(), "F.Silkscreen" );
}


BOOST_AUTO_TEST_CASE( AdapterCollectsFootprintGraphicalItemsAsVisibleObjects )
{
    BOARD board;

    FOOTPRINT* footprint = new FOOTPRINT( &board );
    footprint->SetReference( wxS( "U1" ) );

    PCB_SHAPE* silkLine = new PCB_SHAPE( footprint );
    silkLine->SetShape( SHAPE_T::SEGMENT );
    silkLine->SetStart( VECTOR2I( 0, 0 ) );
    silkLine->SetEnd( VECTOR2I( 1000, 0 ) );
    silkLine->SetLayer( F_SilkS );
    silkLine->SetWidth( 100 );
    silkLine->SetSelected();
    footprint->Add( silkLine );

    PCB_TEXT* pinOne = new PCB_TEXT( footprint );
    pinOne->SetText( wxS( "PIN 1" ) );
    pinOne->SetPosition( VECTOR2I( 50, 75 ) );
    pinOne->SetLayer( F_Fab );
    pinOne->SetTextSize( VECTOR2I( 500, 500 ) );
    pinOne->SetSelected();
    footprint->Add( pinOne );

    board.Add( footprint );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_INDEX              index = adapter.BuildIndex();

    const AI_OBJECT_REF* shapeRef =
            findRefByLabel( index.VisibleObjects(), wxS( "fp:U1/shape:segment:0,0->1000,0" ) );
    const AI_OBJECT_REF* textRef =
            findRefByLabel( index.VisibleObjects(), wxS( "fp:U1/text:PIN 1" ) );

    BOOST_REQUIRE( shapeRef );
    BOOST_REQUIRE( textRef );
    BOOST_CHECK_EQUAL( shapeRef->m_Type, PCB_SHAPE_T );
    BOOST_CHECK_EQUAL( textRef->m_Type, PCB_TEXT_T );
    BOOST_CHECK( findRefByLabel( index.SelectedObjects(),
                                 wxS( "fp:U1/shape:segment:0,0->1000,0" ) ) );
    BOOST_CHECK( findRefByLabel( index.SelectedObjects(), wxS( "fp:U1/text:PIN 1" ) ) );

    nlohmann::json shapeDetails =
            detailsForLabel( index.VisibleObjects(), wxS( "fp:U1/shape:segment:0,0->1000,0" ) );
    BOOST_CHECK_EQUAL( shapeDetails["kind"].get<std::string>(), "shape" );
    BOOST_CHECK_EQUAL( shapeDetails["parent_footprint_reference"].get<std::string>(), "U1" );
    BOOST_CHECK_EQUAL( shapeDetails["layer"].get<std::string>(), "F.Silkscreen" );

    nlohmann::json textDetails =
            detailsForLabel( index.VisibleObjects(), wxS( "fp:U1/text:PIN 1" ) );
    BOOST_CHECK_EQUAL( textDetails["kind"].get<std::string>(), "text" );
    BOOST_CHECK_EQUAL( textDetails["parent_footprint_reference"].get<std::string>(), "U1" );
    BOOST_CHECK_EQUAL( textDetails["text"].get<std::string>(), "PIN 1" );
    BOOST_CHECK_EQUAL( textDetails["layer"].get<std::string>(), "F.Fab" );
}


BOOST_AUTO_TEST_CASE( AdapterCollectsSelectedPads )
{
    BOARD board;

    FOOTPRINT* footprint = new FOOTPRINT( &board );
    PAD*       pad = new PAD( footprint );

    pad->SetNumber( wxS( "2" ) );
    pad->SetSelected();
    footprint->SetReference( wxS( "J1" ) );
    footprint->Add( pad );
    board.Add( footprint );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_INDEX              index = adapter.BuildIndex();

    BOOST_REQUIRE_EQUAL( index.SelectedObjects().size(), 1 );
    BOOST_CHECK( index.SelectedObjects().front().m_Uuid == pad->m_Uuid );
    BOOST_CHECK_EQUAL( index.SelectedObjects().front().m_Label, wxString( wxS( "J1.2" ) ) );
}


BOOST_AUTO_TEST_CASE( AdapterCollectsSelectedFootprints )
{
    BOARD board;

    FOOTPRINT* footprint = new FOOTPRINT( &board );
    PAD*       pad = new PAD( footprint );

    pad->SetNumber( wxS( "1" ) );
    footprint->SetReference( wxS( "U2" ) );
    footprint->SetSelected();
    footprint->Add( pad );
    board.Add( footprint );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_INDEX              index = adapter.BuildIndex();

    BOOST_REQUIRE_EQUAL( index.SelectedObjects().size(), 1 );
    BOOST_CHECK( index.SelectedObjects().front().m_Uuid == footprint->m_Uuid );
    BOOST_CHECK_EQUAL( index.SelectedObjects().front().m_Type, PCB_FOOTPRINT_T );
    BOOST_CHECK_EQUAL( index.SelectedObjects().front().m_Label, wxString( wxS( "U2" ) ) );
}


BOOST_AUTO_TEST_CASE( AdapterCollectsTracksAndViasAsRoutingObjects )
{
    BOARD board;

    PCB_TRACK* track = new PCB_TRACK( &board );
    track->SetStart( VECTOR2I( 0, 0 ) );
    track->SetEnd( VECTOR2I( 100, 200 ) );
    track->SetSelected();
    board.Add( track );

    PCB_VIA* via = new PCB_VIA( &board );
    via->SetPosition( VECTOR2I( 300, 400 ) );
    via->SetSelected();
    board.Add( via );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_INDEX              index = adapter.BuildIndex();

    const AI_OBJECT_REF* trackRef =
            findRefByLabel( index.VisibleObjects(), wxS( "track:0,0->100,200" ) );
    const AI_OBJECT_REF* viaRef =
            findRefByLabel( index.VisibleObjects(), wxS( "via:300,400" ) );

    BOOST_REQUIRE( trackRef );
    BOOST_REQUIRE( viaRef );
    BOOST_CHECK( trackRef->m_Uuid == track->m_Uuid );
    BOOST_CHECK_EQUAL( trackRef->m_Type, PCB_TRACE_T );
    BOOST_CHECK( viaRef->m_Uuid == via->m_Uuid );
    BOOST_CHECK_EQUAL( viaRef->m_Type, PCB_VIA_T );
    BOOST_REQUIRE_EQUAL( index.SelectedObjects().size(), 2 );
    BOOST_CHECK( findRefByLabel( index.SelectedObjects(), wxS( "track:0,0->100,200" ) ) );
    BOOST_CHECK( findRefByLabel( index.SelectedObjects(), wxS( "via:300,400" ) ) );
}


BOOST_AUTO_TEST_CASE( AdapterCollectsArcsAsRoutingObjects )
{
    BOARD board;

    PCB_ARC* arc = new PCB_ARC( &board );
    arc->SetStart( VECTOR2I( 0, 0 ) );
    arc->SetMid( VECTOR2I( 50, 100 ) );
    arc->SetEnd( VECTOR2I( 100, 0 ) );
    arc->SetSelected();
    board.Add( arc );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_INDEX              index = adapter.BuildIndex();

    const AI_OBJECT_REF* arcRef =
            findRefByLabel( index.VisibleObjects(), wxS( "arc:0,0->50,100->100,0" ) );

    BOOST_REQUIRE( arcRef );
    BOOST_CHECK( arcRef->m_Uuid == arc->m_Uuid );
    BOOST_CHECK_EQUAL( arcRef->m_Type, PCB_ARC_T );
    BOOST_REQUIRE_EQUAL( index.SelectedObjects().size(), 1 );
    BOOST_CHECK( findRefByLabel( index.SelectedObjects(), wxS( "arc:0,0->50,100->100,0" ) ) );
}


BOOST_AUTO_TEST_CASE( AdapterCollectsDrawingShapesAsVisibleObjects )
{
    BOARD board;

    PCB_SHAPE* edge = new PCB_SHAPE( &board, SHAPE_T::SEGMENT );
    edge->SetStart( VECTOR2I( 0, 0 ) );
    edge->SetEnd( VECTOR2I( 1000, 0 ) );
    edge->SetLayer( Edge_Cuts );
    edge->SetWidth( 50 );
    edge->SetSelected();
    board.Add( edge );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_INDEX              index = adapter.BuildIndex();

    const AI_OBJECT_REF* edgeRef =
            findRefByLabel( index.VisibleObjects(), wxS( "edge:0,0->1000,0" ) );

    BOOST_REQUIRE( edgeRef );
    BOOST_CHECK( edgeRef->m_Uuid == edge->m_Uuid );
    BOOST_CHECK_EQUAL( edgeRef->m_Type, PCB_SHAPE_T );
    BOOST_CHECK( findRefByLabel( index.SelectedObjects(), wxS( "edge:0,0->1000,0" ) ) );

    nlohmann::json edgeDetails =
            detailsForLabel( index.VisibleObjects(), wxS( "edge:0,0->1000,0" ) );
    BOOST_CHECK_EQUAL( edgeDetails["kind"].get<std::string>(), "shape" );
    BOOST_CHECK_EQUAL( edgeDetails["shape"].get<std::string>(), "segment" );
    BOOST_CHECK_EQUAL( edgeDetails["layer"].get<std::string>(), "Edge.Cuts" );
    BOOST_CHECK_EQUAL( edgeDetails["width"].get<int>(), 50 );
    BOOST_CHECK_EQUAL( edgeDetails["start"]["x"].get<int>(), 0 );
    BOOST_CHECK_EQUAL( edgeDetails["end"]["x"].get<int>(), 1000 );
    BOOST_CHECK_EQUAL( edgeDetails["net_code"].get<int>(), NETINFO_LIST::UNCONNECTED );
    BOOST_CHECK_EQUAL( edgeDetails["net_name"].get<std::string>(), "" );
}


BOOST_AUTO_TEST_CASE( AdapterCollectsBoardTextAsVisibleObjects )
{
    BOARD board;

    PCB_TEXT* text = new PCB_TEXT( &board );
    text->SetText( wxS( "JTAG HEADER" ) );
    text->SetPosition( VECTOR2I( 100, 200 ) );
    text->SetLayer( F_SilkS );
    text->SetTextSize( VECTOR2I( 1200, 900 ) );
    text->SetTextAngle( EDA_ANGLE( 90, DEGREES_T ) );
    text->SetBold( true );
    text->SetSelected();
    board.Add( text );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_INDEX              index = adapter.BuildIndex();

    const AI_OBJECT_REF* textRef =
            findRefByLabel( index.VisibleObjects(), wxS( "text:JTAG HEADER" ) );

    BOOST_REQUIRE( textRef );
    BOOST_CHECK( textRef->m_Uuid == text->m_Uuid );
    BOOST_CHECK_EQUAL( textRef->m_Type, PCB_TEXT_T );
    BOOST_CHECK( findRefByLabel( index.SelectedObjects(), wxS( "text:JTAG HEADER" ) ) );

    nlohmann::json details =
            detailsForLabel( index.VisibleObjects(), wxS( "text:JTAG HEADER" ) );
    BOOST_CHECK_EQUAL( details["kind"].get<std::string>(), "text" );
    BOOST_CHECK_EQUAL( details["text"].get<std::string>(), "JTAG HEADER" );
    BOOST_CHECK_EQUAL( details["shown_text"].get<std::string>(), "JTAG HEADER" );
    BOOST_CHECK_EQUAL( details["position"]["x"].get<int>(), 100 );
    BOOST_CHECK_EQUAL( details["size"]["x"].get<int>(), 1200 );
    BOOST_CHECK_EQUAL( details["layer"].get<std::string>(), "F.Silkscreen" );
    BOOST_CHECK_EQUAL( details["angle_degrees"].get<int>(), 90 );
    BOOST_CHECK_EQUAL( details["visible"].get<bool>(), true );
    BOOST_CHECK_EQUAL( details["mirrored"].get<bool>(), false );
    BOOST_CHECK_EQUAL( details["bold"].get<bool>(), true );
    BOOST_CHECK_EQUAL( details["italic"].get<bool>(), false );
    BOOST_CHECK_EQUAL( details["h_justify"].get<std::string>(), "center" );
    BOOST_CHECK_EQUAL( details["v_justify"].get<std::string>(), "center" );
}


BOOST_AUTO_TEST_CASE( AdapterCollectsBoardTextboxesAsVisibleObjects )
{
    BOARD board;

    PCB_TEXTBOX* textbox = new PCB_TEXTBOX( &board );
    textbox->SetText( wxS( "Assembly note" ) );
    textbox->SetStart( VECTOR2I( 0, 0 ) );
    textbox->SetEnd( VECTOR2I( 200000, 1000 ) );
    textbox->SetLayer( Cmts_User );
    textbox->SetTextSize( VECTOR2I( 1000, 500 ) );
    textbox->SetHorizJustify( GR_TEXT_H_ALIGN_LEFT );
    textbox->SetVertJustify( GR_TEXT_V_ALIGN_TOP );
    textbox->SetBorderEnabled( true );
    textbox->SetBorderWidth( 100 );
    textbox->SetSelected();
    board.Add( textbox );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_INDEX              index = adapter.BuildIndex();

    const AI_OBJECT_REF* textboxRef =
            findRefByLabel( index.VisibleObjects(), wxS( "textbox:Assembly note" ) );

    BOOST_REQUIRE( textboxRef );
    BOOST_CHECK( textboxRef->m_Uuid == textbox->m_Uuid );
    BOOST_CHECK_EQUAL( textboxRef->m_Type, PCB_TEXTBOX_T );
    BOOST_CHECK( findRefByLabel( index.SelectedObjects(), wxS( "textbox:Assembly note" ) ) );

    nlohmann::json details =
            detailsForLabel( index.VisibleObjects(), wxS( "textbox:Assembly note" ) );
    BOOST_CHECK_EQUAL( details["kind"].get<std::string>(), "textbox" );
    BOOST_CHECK_EQUAL( details["text"].get<std::string>(), "Assembly note" );
    BOOST_CHECK_EQUAL( details["shown_text"].get<std::string>(), "Assembly\nnote" );
    BOOST_CHECK_EQUAL( details["layer"].get<std::string>(), "User.Comments" );
    BOOST_CHECK_EQUAL( details["start"]["x"].get<int>(), 0 );
    BOOST_CHECK_EQUAL( details["end"]["x"].get<int>(), 200000 );
    BOOST_CHECK_EQUAL( details["border_enabled"].get<bool>(), true );
    BOOST_CHECK_EQUAL( details["border_width"].get<int>(), 100 );
    BOOST_CHECK_EQUAL( details["h_justify"].get<std::string>(), "left" );
    BOOST_CHECK_EQUAL( details["v_justify"].get<std::string>(), "top" );
}


BOOST_AUTO_TEST_CASE( AdapterCollectsBoardTargetsAndBarcodesAsVisibleObjects )
{
    BOARD board;

    PCB_TARGET* target = new PCB_TARGET( &board, 0, Dwgs_User, VECTOR2I( 100, 200 ),
                                         1000, 100 );
    target->SetSelected();
    board.Add( target );

    PCB_BARCODE* barcode = new PCB_BARCODE( &board );
    barcode->SetText( wxS( "SN-001" ) );
    barcode->SetKind( BARCODE_T::QR_CODE );
    barcode->SetPosition( VECTOR2I( 300, 400 ) );
    barcode->SetLayer( F_SilkS );
    barcode->SetWidth( 2000 );
    barcode->SetHeight( 2000 );
    barcode->SetShowText( true );
    barcode->SetSelected();
    board.Add( barcode );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_INDEX              index = adapter.BuildIndex();

    const AI_OBJECT_REF* targetRef =
            findRefByLabel( index.VisibleObjects(), wxS( "target:100,200" ) );
    const AI_OBJECT_REF* barcodeRef =
            findRefByLabel( index.VisibleObjects(), wxS( "barcode:SN-001" ) );

    BOOST_REQUIRE( targetRef );
    BOOST_REQUIRE( barcodeRef );
    BOOST_CHECK_EQUAL( targetRef->m_Type, PCB_TARGET_T );
    BOOST_CHECK_EQUAL( barcodeRef->m_Type, PCB_BARCODE_T );
    BOOST_CHECK( findRefByLabel( index.SelectedObjects(), wxS( "target:100,200" ) ) );
    BOOST_CHECK( findRefByLabel( index.SelectedObjects(), wxS( "barcode:SN-001" ) ) );

    nlohmann::json targetDetails =
            detailsForLabel( index.VisibleObjects(), wxS( "target:100,200" ) );
    BOOST_CHECK_EQUAL( targetDetails["kind"].get<std::string>(), "target" );
    BOOST_CHECK_EQUAL( targetDetails["position"]["x"].get<int>(), 100 );
    BOOST_CHECK_EQUAL( targetDetails["layer"].get<std::string>(), "User.Drawings" );

    nlohmann::json barcodeDetails =
            detailsForLabel( index.VisibleObjects(), wxS( "barcode:SN-001" ) );
    BOOST_CHECK_EQUAL( barcodeDetails["kind"].get<std::string>(), "barcode" );
    BOOST_CHECK_EQUAL( barcodeDetails["text"].get<std::string>(), "SN-001" );
    BOOST_CHECK_EQUAL( barcodeDetails["barcode_kind"].get<std::string>(), "qr_code" );
    BOOST_CHECK_EQUAL( barcodeDetails["layer"].get<std::string>(), "F.Silkscreen" );
}


BOOST_AUTO_TEST_CASE( AdapterCollectsBoardTablesAndCellsAsVisibleObjects )
{
    BOARD board;

    PCB_TABLE* table = new PCB_TABLE( &board, 100 );
    table->SetPosition( VECTOR2I( 0, 0 ) );
    table->SetLayer( Cmts_User );
    table->SetColCount( 2 );

    PCB_TABLECELL* firstCell = new PCB_TABLECELL( table );
    firstCell->SetText( wxS( "Part" ) );
    firstCell->SetStart( VECTOR2I( 0, 0 ) );
    firstCell->SetEnd( VECTOR2I( 1000, 500 ) );
    firstCell->SetSelected();
    table->AddCell( firstCell );

    PCB_TABLECELL* secondCell = new PCB_TABLECELL( table );
    secondCell->SetText( wxS( "Qty" ) );
    secondCell->SetStart( VECTOR2I( 1000, 0 ) );
    secondCell->SetEnd( VECTOR2I( 2000, 500 ) );
    table->AddCell( secondCell );

    table->SetSelected();
    board.Add( table );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_INDEX              index = adapter.BuildIndex();

    const AI_OBJECT_REF* tableRef = findRefByLabel( index.VisibleObjects(), wxS( "table:0,0" ) );
    const AI_OBJECT_REF* cellRef =
            findRefByLabel( index.VisibleObjects(), wxS( "table-cell:A1" ) );

    BOOST_REQUIRE( tableRef );
    BOOST_REQUIRE( cellRef );
    BOOST_CHECK_EQUAL( tableRef->m_Type, PCB_TABLE_T );
    BOOST_CHECK_EQUAL( cellRef->m_Type, PCB_TABLECELL_T );
    BOOST_CHECK( findRefByLabel( index.SelectedObjects(), wxS( "table:0,0" ) ) );
    BOOST_CHECK( findRefByLabel( index.SelectedObjects(), wxS( "table-cell:A1" ) ) );

    nlohmann::json tableDetails =
            detailsForLabel( index.VisibleObjects(), wxS( "table:0,0" ) );
    BOOST_CHECK_EQUAL( tableDetails["kind"].get<std::string>(), "table" );
    BOOST_CHECK_EQUAL( tableDetails["columns"].get<int>(), 2 );
    BOOST_CHECK_EQUAL( tableDetails["rows"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( tableDetails["cell_count"].get<int>(), 2 );

    nlohmann::json cellDetails =
            detailsForLabel( index.VisibleObjects(), wxS( "table-cell:A1" ) );
    BOOST_CHECK_EQUAL( cellDetails["kind"].get<std::string>(), "table_cell" );
    BOOST_CHECK_EQUAL( cellDetails["text"].get<std::string>(), "Part" );
    BOOST_CHECK_EQUAL( cellDetails["address"].get<std::string>(), "A1" );
    BOOST_CHECK( !cellDetails["parent_table_uuid"].get<std::string>().empty() );
}


BOOST_AUTO_TEST_CASE( AdapterCollectsBoardDimensionsAsVisibleObjects )
{
    BOARD board;

    PCB_DIM_ALIGNED* dimension = new PCB_DIM_ALIGNED( &board );
    dimension->SetStart( VECTOR2I( 0, 0 ) );
    dimension->SetEnd( VECTOR2I( 1000, 0 ) );
    dimension->SetHeight( 500 );
    dimension->SetLayer( Dwgs_User );
    dimension->SetLineThickness( 100 );
    dimension->SetSelected();
    dimension->Update();
    board.Add( dimension );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_INDEX              index = adapter.BuildIndex();

    const AI_OBJECT_REF* ref =
            findRefByLabel( index.VisibleObjects(), wxS( "dimension:0,0->1000,0" ) );

    BOOST_REQUIRE( ref );
    BOOST_CHECK( ref->m_Uuid == dimension->m_Uuid );
    BOOST_CHECK_EQUAL( ref->m_Type, PCB_DIM_ALIGNED_T );
    BOOST_CHECK( findRefByLabel( index.SelectedObjects(), wxS( "dimension:0,0->1000,0" ) ) );

    nlohmann::json details =
            detailsForLabel( index.VisibleObjects(), wxS( "dimension:0,0->1000,0" ) );
    BOOST_CHECK_EQUAL( details["kind"].get<std::string>(), "dimension" );
    BOOST_CHECK_EQUAL( details["dimension_type"].get<std::string>(), "aligned" );
    BOOST_CHECK_EQUAL( details["start"]["x"].get<int>(), 0 );
    BOOST_CHECK_EQUAL( details["end"]["x"].get<int>(), 1000 );
    BOOST_CHECK_EQUAL( details["layer"].get<std::string>(), "User.Drawings" );
}


BOOST_AUTO_TEST_CASE( AdapterCollectsZonesAsVisibleObjects )
{
    BOARD board;
    board.Add( new NETINFO_ITEM( &board, wxS( "/GND" ), 1 ) );

    ZONE* zone = new ZONE( &board );
    zone->SetZoneName( wxS( "GND_POUR" ) );
    zone->SetLayerSet( LSET( { F_Cu } ) );
    zone->SetNetCode( 1 );
    zone->SetAssignedPriority( 2 );
    zone->SetSelected();
    appendRectangle( *zone );
    board.Add( zone );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_INDEX              index = adapter.BuildIndex();

    const AI_OBJECT_REF* zoneRef =
            findRefByLabel( index.VisibleObjects(), wxS( "zone:GND_POUR" ) );

    BOOST_REQUIRE( zoneRef );
    BOOST_CHECK( zoneRef->m_Uuid == zone->m_Uuid );
    BOOST_CHECK_EQUAL( zoneRef->m_Type, PCB_ZONE_T );
    BOOST_CHECK( findRefByLabel( index.SelectedObjects(), wxS( "zone:GND_POUR" ) ) );

    nlohmann::json details =
            detailsForLabel( index.VisibleObjects(), wxS( "zone:GND_POUR" ) );
    BOOST_CHECK_EQUAL( details["kind"].get<std::string>(), "zone" );
    BOOST_CHECK_EQUAL( details["zone_kind"].get<std::string>(), "copper" );
    BOOST_CHECK_EQUAL( details["name"].get<std::string>(), "GND_POUR" );
    BOOST_CHECK_EQUAL( details["layers"][0].get<std::string>(), "F.Cu" );
    BOOST_CHECK_EQUAL( details["first_layer"].get<std::string>(), "F.Cu" );
    BOOST_CHECK_EQUAL( details["corner_count"].get<int>(), 4 );
    BOOST_CHECK_EQUAL( details["position"]["x"].get<int>(), 0 );
    BOOST_REQUIRE( details.contains( "bbox" ) );
    BOOST_CHECK_EQUAL( details["bbox"]["width"].get<int>(), 1000 );
    BOOST_CHECK_EQUAL( details["bbox"]["height"].get<int>(), 500 );
    BOOST_CHECK_EQUAL( details["net_code"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( details["net_name"].get<std::string>(), "/GND" );
    BOOST_CHECK_EQUAL( details["priority"].get<int>(), 2 );
    BOOST_CHECK_EQUAL( details["is_rule_area"].get<bool>(), false );
    BOOST_CHECK_EQUAL( details["has_keepout"].get<bool>(), false );
}


BOOST_AUTO_TEST_CASE( AdapterCollectsKeepoutRuleAreasAsVisibleObjects )
{
    BOARD board;
    board.GetDesignSettings().m_TrackMinWidth = 12000;
    board.GetDesignSettings().m_ViasMinSize = 24000;
    board.GetDesignSettings().m_MinThroughDrill = 8000;

    ZONE* keepout = new ZONE( &board );
    keepout->SetZoneName( wxS( "NO_ROUTING" ) );
    keepout->SetIsRuleArea( true );
    keepout->SetLayerSet( LSET( { F_Cu, B_Cu } ) );
    keepout->SetDoNotAllowTracks( true );
    keepout->SetDoNotAllowVias( true );
    keepout->SetDoNotAllowPads( false );
    keepout->SetDoNotAllowFootprints( false );
    keepout->SetDoNotAllowZoneFills( false );
    keepout->SetSelected();
    appendRectangle( *keepout );
    board.Add( keepout );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_INDEX              index = adapter.BuildIndex();

    const AI_OBJECT_REF* keepoutRef =
            findRefByLabel( index.VisibleObjects(), wxS( "keepout:NO_ROUTING" ) );

    BOOST_REQUIRE( keepoutRef );
    BOOST_CHECK( keepoutRef->m_Uuid == keepout->m_Uuid );
    BOOST_CHECK_EQUAL( keepoutRef->m_Type, PCB_ZONE_T );
    BOOST_CHECK( findRefByLabel( index.SelectedObjects(), wxS( "keepout:NO_ROUTING" ) ) );

    nlohmann::json details =
            detailsForLabel( index.VisibleObjects(), wxS( "keepout:NO_ROUTING" ) );
    BOOST_CHECK_EQUAL( details["kind"].get<std::string>(), "zone" );
    BOOST_CHECK_EQUAL( details["zone_kind"].get<std::string>(), "keepout" );
    BOOST_CHECK_EQUAL( details["name"].get<std::string>(), "NO_ROUTING" );
    BOOST_REQUIRE( details.contains( "bbox" ) );
    BOOST_CHECK_EQUAL( details["bbox"]["width"].get<int>(), 1000 );
    BOOST_CHECK_EQUAL( details["bbox"]["height"].get<int>(), 500 );
    BOOST_CHECK_EQUAL( details["is_rule_area"].get<bool>(), true );
    BOOST_CHECK_EQUAL( details["has_keepout"].get<bool>(), true );
    BOOST_CHECK_EQUAL( details["keepout"]["tracks"].get<bool>(), true );
    BOOST_CHECK_EQUAL( details["keepout"]["vias"].get<bool>(), true );
    BOOST_CHECK_EQUAL( details["keepout"]["pads"].get<bool>(), false );
    BOOST_CHECK_EQUAL( details["keepout"]["footprints"].get<bool>(), false );
    BOOST_CHECK_EQUAL( details["keepout"]["zone_fills"].get<bool>(), false );

    AI_CONTEXT_SNAPSHOT snapshot = index.BuildSnapshot();
    nlohmann::json      summary = nlohmann::json::parse( snapshot.m_Summary.ToStdString() );
    nlohmann::json      constraints = summary["constraint_facts"];

    BOOST_CHECK_EQUAL( constraints["source"].get<std::string>(), "board" );
    BOOST_CHECK_EQUAL( constraints["minimums"]["min_track_width"].get<int>(), 12000 );
    BOOST_CHECK_EQUAL( constraints["minimums"]["min_via_size"].get<int>(), 24000 );
    BOOST_CHECK_EQUAL( constraints["minimums"]["min_through_drill"].get<int>(), 8000 );
    BOOST_CHECK_EQUAL( constraints["rule_area_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( constraints["keepout_count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( constraints["keepout_sample_truncated"].get<bool>(), false );
    BOOST_REQUIRE_EQUAL( constraints["keepouts"].size(), 1u );
    BOOST_CHECK_EQUAL( constraints["keepouts"][0]["name"].get<std::string>(), "NO_ROUTING" );
    BOOST_CHECK_EQUAL( constraints["keepouts"][0]["layers"][0].get<std::string>(), "F.Cu" );
    BOOST_CHECK_EQUAL( constraints["keepouts"][0]["layers"][1].get<std::string>(), "B.Cu" );
    BOOST_CHECK_EQUAL( constraints["keepouts"][0]["bbox"]["width"].get<int>(), 1000 );
    BOOST_CHECK_EQUAL( constraints["keepouts"][0]["bbox"]["height"].get<int>(), 500 );
    BOOST_CHECK_EQUAL( constraints["keepouts"][0]["blocks"]["tracks"].get<bool>(), true );
    BOOST_CHECK_EQUAL( constraints["keepouts"][0]["blocks"]["vias"].get<bool>(), true );
    BOOST_CHECK_EQUAL( constraints["keepouts"][0]["blocks"]["pads"].get<bool>(), false );
}


BOOST_AUTO_TEST_CASE( AdapterAddsEffectiveConstraintObservationFacts )
{
    BOARD                  board;
    BOARD_DESIGN_SETTINGS& settings = board.GetDesignSettings();
    const int              ruleClearance = 987654;

    auto rule = std::make_shared<DRC_RULE>( wxT( "AI Physical Hole Clearance" ) );

    DRC_CONSTRAINT constraint( PHYSICAL_HOLE_CLEARANCE_CONSTRAINT );
    constraint.Value().SetMin( ruleClearance );
    rule->AddConstraint( constraint );

    auto engine = std::make_shared<DRC_ENGINE>( &board, &settings );
    engine->InitEngine( rule );
    settings.m_DRCEngine = engine;

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_SNAPSHOT           snapshot = adapter.BuildIndex().BuildSnapshot();

    nlohmann::json summary = nlohmann::json::parse( snapshot.m_Summary.ToStdString() );
    nlohmann::json effective = summary["constraint_facts"]["effective_constraints"];

    BOOST_CHECK_EQUAL( effective["drc_engine_present"].get<bool>(), true );
    BOOST_CHECK_EQUAL( effective["rules_valid"].get<bool>(), true );
    BOOST_CHECK_EQUAL( effective["worst_constraint_sample_truncated"].get<bool>(), false );

    const nlohmann::json* physicalHole =
            findWorstConstraintByType( effective["worst_constraints"],
                                       "physical_hole_clearance" );

    BOOST_REQUIRE( physicalHole );
    BOOST_CHECK_EQUAL( ( *physicalHole )["enum"].get<int>(),
                       static_cast<int>( PHYSICAL_HOLE_CLEARANCE_CONSTRAINT ) );
    BOOST_CHECK_EQUAL( ( *physicalHole )["value"]["min"].get<int>(), ruleClearance );
    BOOST_CHECK_EQUAL( ( *physicalHole )["value"]["has_min"].get<bool>(), true );
    BOOST_CHECK( ( *physicalHole )["name"].get<std::string>().find(
                         "AI Physical Hole Clearance" )
                 != std::string::npos );
}


BOOST_AUTO_TEST_CASE( AdapterAddsPairSpecificEffectiveConstraintFacts )
{
    BOARD                  board;
    BOARD_DESIGN_SETTINGS& settings = board.GetDesignSettings();

    std::shared_ptr<NETCLASS> power = std::make_shared<NETCLASS>( wxS( "Power" ) );
    power->SetClearance( 910000 );
    settings.m_NetSettings->SetNetclass( power->GetName(), power );

    std::shared_ptr<NETCLASS> logic = std::make_shared<NETCLASS>( wxS( "Logic" ) );
    logic->SetClearance( 120000 );
    settings.m_NetSettings->SetNetclass( logic->GetName(), logic );

    NETINFO_ITEM* pwr = new NETINFO_ITEM( &board, wxS( "/PWR" ), 1 );
    pwr->SetNetClass( power );
    board.Add( pwr );

    NETINFO_ITEM* sig = new NETINFO_ITEM( &board, wxS( "/SIG" ), 2 );
    sig->SetNetClass( logic );
    board.Add( sig );

    PCB_TRACK* pwrTrack = new PCB_TRACK( &board );
    pwrTrack->SetStart( VECTOR2I( 0, 0 ) );
    pwrTrack->SetEnd( VECTOR2I( 1000, 0 ) );
    pwrTrack->SetLayer( F_Cu );
    pwrTrack->SetWidth( 150000 );
    pwrTrack->SetNetCode( 1 );
    board.Add( pwrTrack );

    PCB_TRACK* sigTrack = new PCB_TRACK( &board );
    sigTrack->SetStart( VECTOR2I( 0, 500000 ) );
    sigTrack->SetEnd( VECTOR2I( 1000, 500000 ) );
    sigTrack->SetLayer( F_Cu );
    sigTrack->SetWidth( 150000 );
    sigTrack->SetNetCode( 2 );
    board.Add( sigTrack );

    auto engine = std::make_shared<DRC_ENGINE>( &board, &settings );
    engine->InitEngine( wxFileName() );
    settings.m_DRCEngine = engine;

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_SNAPSHOT           snapshot = adapter.BuildIndex().BuildSnapshot();

    nlohmann::json summary = nlohmann::json::parse( snapshot.m_Summary.ToStdString() );
    nlohmann::json effective = summary["constraint_facts"]["effective_constraints"];

    BOOST_CHECK_EQUAL( effective["pair_effective_constraint_sample_truncated"].get<bool>(),
                       false );
    BOOST_REQUIRE_GE( effective["pair_effective_constraints"].size(), 1u );

    const nlohmann::json* pairConstraint =
            findPairConstraintByLabels( effective["pair_effective_constraints"],
                                        "track:0,0->1000,0",
                                        "track:0,500000->1000,500000" );

    BOOST_REQUIRE( pairConstraint );
    BOOST_CHECK_EQUAL( ( *pairConstraint )["type"].get<std::string>(), "clearance" );
    BOOST_CHECK_EQUAL( ( *pairConstraint )["layer"].get<std::string>(), "F.Cu" );

    std::set<std::string> pairNets = {
        ( *pairConstraint )["source_item"]["net"].get<std::string>(),
        ( *pairConstraint )["target_item"]["net"].get<std::string>()
    };

    BOOST_CHECK( pairNets.find( "/PWR" ) != pairNets.end() );
    BOOST_CHECK( pairNets.find( "/SIG" ) != pairNets.end() );
    BOOST_CHECK_EQUAL( ( *pairConstraint )["value"]["min"].get<int>(), 910000 );
    BOOST_CHECK_EQUAL( ( *pairConstraint )["value"]["has_min"].get<bool>(), true );
}


BOOST_AUTO_TEST_CASE( AdapterAddsGeometrySpecificEffectiveConstraintFacts )
{
    BOARD                  board;
    BOARD_DESIGN_SETTINGS& settings = board.GetDesignSettings();

    board.Add( new NETINFO_ITEM( &board, wxS( "/IN_AREA" ), 1 ) );
    board.Add( new NETINFO_ITEM( &board, wxS( "/OUT_AREA" ), 2 ) );

    ZONE* area = new ZONE( &board );
    area->SetZoneName( wxS( "AI_RULE_AREA" ) );
    area->SetIsRuleArea( true );
    area->SetLayerSet( LSET( { F_Cu } ) );

    SHAPE_POLY_SET areaOutline;
    areaOutline.NewOutline();
    areaOutline.Append( VECTOR2I( 0, 0 ) );
    areaOutline.Append( VECTOR2I( 1000000, 0 ) );
    areaOutline.Append( VECTOR2I( 1000000, 500000 ) );
    areaOutline.Append( VECTOR2I( 0, 500000 ) );
    area->AddPolygon( areaOutline.COutline( 0 ) );

    board.Add( area );

    PCB_TRACK* insideTrack = new PCB_TRACK( &board );
    insideTrack->SetStart( VECTOR2I( 100000, 100000 ) );
    insideTrack->SetEnd( VECTOR2I( 200000, 100000 ) );
    insideTrack->SetLayer( F_Cu );
    insideTrack->SetWidth( 50000 );
    insideTrack->SetNetCode( 1 );
    board.Add( insideTrack );

    PCB_TRACK* outsideTrack = new PCB_TRACK( &board );
    outsideTrack->SetStart( VECTOR2I( 2000000, 100000 ) );
    outsideTrack->SetEnd( VECTOR2I( 2100000, 100000 ) );
    outsideTrack->SetLayer( F_Cu );
    outsideTrack->SetWidth( 50000 );
    outsideTrack->SetNetCode( 2 );
    board.Add( outsideTrack );

    auto rule = std::make_shared<DRC_RULE>( wxS( "AI Area Clearance" ) );
    rule->m_Condition = new DRC_RULE_CONDITION( wxS( "A.intersectsArea('AI_RULE_AREA')" ) );

    DRC_CONSTRAINT constraint( CLEARANCE_CONSTRAINT );
    constraint.Value().SetMin( 777000 );
    rule->AddConstraint( constraint );

    auto engine = std::make_shared<DRC_ENGINE>( &board, &settings );
    engine->InitEngine( rule );
    settings.m_DRCEngine = engine;

    DRC_CONSTRAINT direct =
            engine->EvalRules( CLEARANCE_CONSTRAINT, insideTrack, outsideTrack, F_Cu );
    BOOST_REQUIRE( direct.GetValue().HasMin() );
    BOOST_CHECK_EQUAL( direct.GetValue().Min(), 777000 );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_SNAPSHOT           snapshot = adapter.BuildIndex().BuildSnapshot();

    nlohmann::json summary = nlohmann::json::parse( snapshot.m_Summary.ToStdString() );
    nlohmann::json effective = summary["constraint_facts"]["effective_constraints"];

    BOOST_CHECK_EQUAL( effective["geometry_dependent_rules_present"].get<bool>(), true );

    const nlohmann::json* pairConstraint =
            findPairConstraintByLabels( effective["pair_effective_constraints"],
                                        "track:100000,100000->200000,100000",
                                        "track:2000000,100000->2100000,100000" );

    BOOST_REQUIRE( pairConstraint );
    BOOST_CHECK_EQUAL( ( *pairConstraint )["type"].get<std::string>(), "clearance" );
    BOOST_CHECK_EQUAL( ( *pairConstraint )["value"]["min"].get<int>(), 777000 );
    BOOST_CHECK( ( *pairConstraint )["name"].get<std::string>().find(
                         "AI Area Clearance" )
                 != std::string::npos );
    BOOST_CHECK_EQUAL( ( *pairConstraint )["evaluation_source"].get<std::string>(),
                       "DRC_ENGINE::EvalRules" );
    BOOST_CHECK_EQUAL( ( *pairConstraint )["geometry_dependent_rules_present"].get<bool>(),
                       true );
    BOOST_CHECK( ( *pairConstraint )["source_item"].contains( "bbox" ) );
    BOOST_CHECK( ( *pairConstraint )["target_item"].contains( "bbox" ) );

    BOOST_CHECK_EQUAL( effective["geometry_specific_rule_coverage_truncated"].get<bool>(),
                       false );
    BOOST_REQUIRE_GE( effective["geometry_specific_rule_coverage"].size(), 1u );

    const nlohmann::json* coverage =
            findGeometryRuleCoverage( effective["geometry_specific_rule_coverage"],
                                      "AI Area Clearance", "track_to_track" );

    BOOST_REQUIRE( coverage );
    BOOST_CHECK_EQUAL( ( *coverage )["constraint_type"].get<std::string>(), "clearance" );
    BOOST_CHECK_EQUAL( ( *coverage )["layer"].get<std::string>(), "F.Cu" );
    BOOST_CHECK_EQUAL( ( *coverage )["covered"].get<bool>(), true );
    BOOST_CHECK_EQUAL( ( *coverage )["source"].get<std::string>(),
                       "DRC_ENGINE::EvalRules" );
    BOOST_CHECK_EQUAL( ( *coverage )["value"]["min"].get<int>(), 777000 );
    BOOST_CHECK( ( *coverage )["source_item"].contains( "bbox" ) );
    BOOST_CHECK( ( *coverage )["target_item"].contains( "bbox" ) );
}


BOOST_AUTO_TEST_CASE( AdapterAddsStructuredDetailsToRoutingObjects )
{
    BOARD board;
    board.Add( new NETINFO_ITEM( &board, wxS( "/CLK" ), 1 ) );

    PCB_TRACK* track = new PCB_TRACK( &board );
    track->SetStart( VECTOR2I( 0, 0 ) );
    track->SetEnd( VECTOR2I( 100, 200 ) );
    track->SetLayer( F_Cu );
    track->SetWidth( 250 );
    track->SetNetCode( 1 );
    board.Add( track );

    PCB_ARC* arc = new PCB_ARC( &board );
    arc->SetStart( VECTOR2I( 10, 20 ) );
    arc->SetMid( VECTOR2I( 50, 90 ) );
    arc->SetEnd( VECTOR2I( 90, 20 ) );
    arc->SetLayer( B_Cu );
    arc->SetWidth( 300 );
    arc->SetNetCode( 1 );
    board.Add( arc );

    PCB_VIA* via = new PCB_VIA( &board );
    via->SetPosition( VECTOR2I( 300, 400 ) );
    via->SetWidth( 600 );
    via->SetNetCode( 1 );
    board.Add( via );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_INDEX              index = adapter.BuildIndex();

    nlohmann::json trackDetails =
            detailsForLabel( index.VisibleObjects(), wxS( "track:0,0->100,200" ) );
    BOOST_CHECK_EQUAL( trackDetails["kind"].get<std::string>(), "track" );
    BOOST_CHECK_EQUAL( trackDetails["start"]["x"].get<int>(), 0 );
    BOOST_CHECK_EQUAL( trackDetails["end"]["y"].get<int>(), 200 );
    BOOST_CHECK_EQUAL( trackDetails["layer"].get<std::string>(), "F.Cu" );
    BOOST_CHECK_EQUAL( trackDetails["width"].get<int>(), 250 );
    BOOST_CHECK_EQUAL( trackDetails["net_code"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( trackDetails["net_name"].get<std::string>(), "/CLK" );

    nlohmann::json arcDetails =
            detailsForLabel( index.VisibleObjects(), wxS( "arc:10,20->50,90->90,20" ) );
    BOOST_CHECK_EQUAL( arcDetails["kind"].get<std::string>(), "arc" );
    BOOST_CHECK_EQUAL( arcDetails["mid"]["x"].get<int>(), 50 );
    BOOST_CHECK_EQUAL( arcDetails["layer"].get<std::string>(), "B.Cu" );
    BOOST_CHECK_EQUAL( arcDetails["width"].get<int>(), 300 );

    nlohmann::json viaDetails =
            detailsForLabel( index.VisibleObjects(), wxS( "via:300,400" ) );
    BOOST_CHECK_EQUAL( viaDetails["kind"].get<std::string>(), "via" );
    BOOST_CHECK_EQUAL( viaDetails["position"]["x"].get<int>(), 300 );
    BOOST_CHECK_EQUAL( viaDetails["diameter"].get<int>(), 600 );
    BOOST_CHECK_EQUAL( viaDetails["net_name"].get<std::string>(), "/CLK" );
}


BOOST_AUTO_TEST_CASE( AdapterAddsStructuredDetailsToFootprintsAndPads )
{
    BOARD board;
    board.Add( new NETINFO_ITEM( &board, wxS( "/GPIO" ), 1 ) );

    FOOTPRINT* footprint = new FOOTPRINT( &board );
    footprint->SetReference( wxS( "U3" ) );
    footprint->SetValue( wxS( "MCU" ) );
    footprint->SetFPID( LIB_ID( wxS( "Package_QFP" ), wxS( "TQFP-32_7x7mm_P0.8mm" ) ) );
    footprint->SetPosition( VECTOR2I( 1000, 2000 ) );
    footprint->SetOrientation( EDA_ANGLE( 45.0, DEGREES_T ) );
    footprint->SetLayer( F_Cu );

    PAD* pad = new PAD( footprint );
    pad->SetNumber( wxS( "7" ) );
    pad->SetPosition( VECTOR2I( 1200, 2300 ) );
    pad->SetSize( PADSTACK::ALL_LAYERS, VECTOR2I( 300, 500 ) );
    pad->SetShape( PADSTACK::ALL_LAYERS, PAD_SHAPE::RECTANGLE );
    pad->SetDrillSize( VECTOR2I( 80, 90 ) );
    pad->SetOrientation( EDA_ANGLE( 90.0, DEGREES_T ) );
    pad->SetLayerSet( PAD::SMDMask() );
    pad->SetNetCode( 1 );
    footprint->Add( pad );
    board.Add( footprint );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_INDEX              index = adapter.BuildIndex();

    nlohmann::json footprintDetails = detailsForLabel( index.VisibleObjects(), wxS( "U3" ) );
    BOOST_CHECK_EQUAL( footprintDetails["kind"].get<std::string>(), "footprint" );
    BOOST_CHECK_EQUAL( footprintDetails["reference"].get<std::string>(), "U3" );
    BOOST_CHECK_EQUAL( footprintDetails["value"].get<std::string>(), "MCU" );
    BOOST_CHECK_EQUAL( footprintDetails["footprint_id"].get<std::string>(),
                       "Package_QFP:TQFP-32_7x7mm_P0.8mm" );
    BOOST_CHECK_EQUAL( footprintDetails["position"]["x"].get<int>(), 1000 );
    BOOST_CHECK_EQUAL( footprintDetails["position"]["y"].get<int>(), 2000 );
    BOOST_CHECK_EQUAL( footprintDetails["orientation_degrees"].get<double>(), 45.0 );
    BOOST_CHECK_EQUAL( footprintDetails["layer"].get<std::string>(), "F.Cu" );
    BOOST_CHECK_EQUAL( footprintDetails["pad_count"].get<int>(), 1 );

    nlohmann::json padDetails = detailsForLabel( index.VisibleObjects(), wxS( "U3.7" ) );
    BOOST_CHECK_EQUAL( padDetails["kind"].get<std::string>(), "pad" );
    BOOST_CHECK_EQUAL( padDetails["footprint_reference"].get<std::string>(), "U3" );
    BOOST_CHECK_EQUAL( padDetails["number"].get<std::string>(), "7" );
    BOOST_CHECK_EQUAL( padDetails["position"]["x"].get<int>(), 1200 );
    BOOST_CHECK_EQUAL( padDetails["size"]["x"].get<int>(), 300 );
    BOOST_CHECK_EQUAL( padDetails["size"]["y"].get<int>(), 500 );
    BOOST_CHECK_EQUAL( padDetails["drill"]["x"].get<int>(), 80 );
    BOOST_CHECK_EQUAL( padDetails["drill"]["y"].get<int>(), 90 );
    BOOST_CHECK_EQUAL( padDetails["shape"].get<std::string>(), "rect" );
    BOOST_CHECK_EQUAL( padDetails["layer"].get<std::string>(), "F.Cu" );
    BOOST_CHECK_EQUAL( padDetails["orientation_degrees"].get<double>(), 90.0 );
    BOOST_CHECK_EQUAL( padDetails["net_code"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( padDetails["net_name"].get<std::string>(), "/GPIO" );
}


BOOST_AUTO_TEST_CASE( AdapterAddsFootprintGeometryExtents )
{
    BOARD board;

    FOOTPRINT* footprint = new FOOTPRINT( &board );
    footprint->SetReference( wxS( "U10" ) );
    footprint->SetLayer( F_Cu );

    PAD* pad = new PAD( footprint );
    pad->SetNumber( wxS( "1" ) );
    pad->SetPosition( VECTOR2I( 1000000, 2000000 ) );
    pad->SetSize( PADSTACK::ALL_LAYERS, VECTOR2I( 200000, 400000 ) );
    pad->SetShape( PADSTACK::ALL_LAYERS, PAD_SHAPE::RECTANGLE );
    pad->SetLayerSet( PAD::SMDMask() );
    footprint->Add( pad );

    PCB_SHAPE* courtyard = new PCB_SHAPE( footprint, SHAPE_T::POLY );
    courtyard->SetLayer( F_CrtYd );
    courtyard->SetWidth( 50000 );
    courtyard->SetPolyPoints( {
            VECTOR2I( 800000, 1700000 ),
            VECTOR2I( 1300000, 1700000 ),
            VECTOR2I( 1300000, 2300000 ),
            VECTOR2I( 800000, 2300000 ),
    } );
    footprint->Add( courtyard );

    board.Add( footprint );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_INDEX              index = adapter.BuildIndex();

    nlohmann::json details = detailsForLabel( index.VisibleObjects(), wxS( "U10" ) );
    const BOX2I footprintBBox = footprint->GetBoundingBox( false );
    const BOX2I padsBBox = pad->GetBoundingBox();
    const SHAPE_POLY_SET& courtyardPoly = footprint->GetCourtyard( F_CrtYd );
    BOOST_REQUIRE( !courtyardPoly.IsEmpty() );
    const BOX2I courtyardBBox = courtyardPoly.BBox();

    BOOST_CHECK_EQUAL( details["bbox"]["x"].get<int>(), footprintBBox.GetX() );
    BOOST_CHECK_EQUAL( details["bbox"]["y"].get<int>(), footprintBBox.GetY() );
    BOOST_CHECK_EQUAL( details["bbox"]["width"].get<int>(), footprintBBox.GetWidth() );
    BOOST_CHECK_EQUAL( details["bbox"]["height"].get<int>(), footprintBBox.GetHeight() );
    BOOST_CHECK_EQUAL( details["pads_bbox"]["x"].get<int>(), padsBBox.GetX() );
    BOOST_CHECK_EQUAL( details["pads_bbox"]["y"].get<int>(), padsBBox.GetY() );
    BOOST_CHECK_EQUAL( details["pads_bbox"]["width"].get<int>(), padsBBox.GetWidth() );
    BOOST_CHECK_EQUAL( details["pads_bbox"]["height"].get<int>(), padsBBox.GetHeight() );
    BOOST_CHECK_EQUAL( details["courtyard_bbox"]["x"].get<int>(), courtyardBBox.GetX() );
    BOOST_CHECK_EQUAL( details["courtyard_bbox"]["y"].get<int>(), courtyardBBox.GetY() );
    BOOST_CHECK_EQUAL( details["courtyard_bbox"]["width"].get<int>(), courtyardBBox.GetWidth() );
    BOOST_CHECK_EQUAL( details["courtyard_bbox"]["height"].get<int>(), courtyardBBox.GetHeight() );
}


BOOST_AUTO_TEST_CASE( AdapterAddsPadAndViaSemanticAnchors )
{
    BOARD board;
    board.Add( new NETINFO_ITEM( &board, wxS( "/GPIO" ), 1 ) );

    FOOTPRINT* footprint = new FOOTPRINT( &board );
    footprint->SetReference( wxS( "U9" ) );
    footprint->SetValue( wxS( "MCU" ) );
    footprint->SetFPID( LIB_ID( wxS( "Package_QFP" ), wxS( "TQFP-32" ) ) );
    footprint->SetPosition( VECTOR2I( 1000, 2000 ) );
    footprint->SetLayer( F_Cu );

    PAD* pad = new PAD( footprint );
    pad->SetNumber( wxS( "1" ) );
    pad->SetPosition( VECTOR2I( 1200, 2300 ) );
    pad->SetLayerSet( PAD::SMDMask() );
    pad->SetNetCode( 1 );
    footprint->Add( pad );
    board.Add( footprint );

    PCB_VIA* via = new PCB_VIA( &board );
    via->SetPosition( VECTOR2I( 3000, 4000 ) );
    via->SetWidth( 600 );
    via->SetNetCode( 1 );
    board.Add( via );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_INDEX              index = adapter.BuildIndex();

    const wxString padAnchorId = anchorId( wxS( "pad" ), pad->m_Uuid, wxS( "center" ) );
    const wxString viaAnchorId = anchorId( wxS( "via" ), via->m_Uuid, wxS( "center" ) );

    const AI_CONTEXT_ANCHOR* padAnchor = findAnchorById( index.Anchors(), padAnchorId );
    const AI_CONTEXT_ANCHOR* viaAnchor = findAnchorById( index.Anchors(), viaAnchorId );

    BOOST_REQUIRE( padAnchor );
    BOOST_REQUIRE( viaAnchor );
    BOOST_CHECK_EQUAL( static_cast<int>( padAnchor->m_Kind ),
                       static_cast<int>( AI_CONTEXT_ANCHOR_KIND::RouteTarget ) );
    BOOST_CHECK_EQUAL( padAnchor->m_Label, wxString( wxS( "pad:U9.1:center" ) ) );
    BOOST_CHECK_EQUAL( padAnchor->m_Position.x, 1200 );
    BOOST_CHECK_EQUAL( padAnchor->m_Position.y, 2300 );
    BOOST_CHECK_EQUAL( padAnchor->m_Confidence, 1.0 );
    BOOST_CHECK_EQUAL( static_cast<int>( viaAnchor->m_Kind ),
                       static_cast<int>( AI_CONTEXT_ANCHOR_KIND::RouteTarget ) );
    BOOST_CHECK_EQUAL( viaAnchor->m_Position.x, 3000 );
    BOOST_CHECK_EQUAL( viaAnchor->m_Layer, -1 );

    nlohmann::json padDetails = anchorDetails( *padAnchor );
    BOOST_CHECK_EQUAL( padDetails["role"].get<std::string>(), "center" );
    BOOST_CHECK_EQUAL( padDetails["source_label"].get<std::string>(), "U9.1" );
    BOOST_CHECK_EQUAL( padDetails["footprint_reference"].get<std::string>(), "U9" );
    BOOST_CHECK_EQUAL( padDetails["pad_number"].get<std::string>(), "1" );
    BOOST_CHECK_EQUAL( padDetails["net_name"].get<std::string>(), "/GPIO" );
    BOOST_CHECK_EQUAL( padDetails["position"]["x"].get<int>(), 1200 );

    AI_CONTEXT_SNAPSHOT snapshot = index.BuildSnapshot();
    BOOST_CHECK( findAnchorById( snapshot.m_Anchors, padAnchorId ) );
    BOOST_CHECK( findAnchorById( snapshot.m_Anchors, viaAnchorId ) );
}


BOOST_AUTO_TEST_CASE( AdapterAddsRouteAndShapeSemanticAnchors )
{
    BOARD board;
    board.Add( new NETINFO_ITEM( &board, wxS( "/CLK" ), 1 ) );

    PCB_TRACK* track = new PCB_TRACK( &board );
    track->SetStart( VECTOR2I( 10, 20 ) );
    track->SetEnd( VECTOR2I( 110, 120 ) );
    track->SetLayer( F_Cu );
    track->SetWidth( 250 );
    track->SetNetCode( 1 );
    board.Add( track );

    PCB_SHAPE* shape = new PCB_SHAPE( &board, SHAPE_T::SEGMENT );
    shape->SetStart( VECTOR2I( 500, 600 ) );
    shape->SetEnd( VECTOR2I( 700, 800 ) );
    shape->SetLayer( Edge_Cuts );
    shape->SetWidth( 50 );
    board.Add( shape );

    KISURF_AI_PCB_CONTEXT_ADAPTER adapter( board );
    AI_CONTEXT_INDEX              index = adapter.BuildIndex();

    const wxString trackStartId = anchorId( wxS( "track" ), track->m_Uuid, wxS( "start" ) );
    const wxString trackEndId = anchorId( wxS( "track" ), track->m_Uuid, wxS( "end" ) );
    const wxString shapeStartId = anchorId( wxS( "shape" ), shape->m_Uuid, wxS( "start" ) );
    const wxString shapeEndId = anchorId( wxS( "shape" ), shape->m_Uuid, wxS( "end" ) );

    const AI_CONTEXT_ANCHOR* trackStart = findAnchorById( index.Anchors(), trackStartId );
    const AI_CONTEXT_ANCHOR* trackEnd = findAnchorById( index.Anchors(), trackEndId );
    const AI_CONTEXT_ANCHOR* shapeStart = findAnchorById( index.Anchors(), shapeStartId );
    const AI_CONTEXT_ANCHOR* shapeEnd = findAnchorById( index.Anchors(), shapeEndId );

    BOOST_REQUIRE( trackStart );
    BOOST_REQUIRE( trackEnd );
    BOOST_REQUIRE( shapeStart );
    BOOST_REQUIRE( shapeEnd );
    BOOST_CHECK_EQUAL( static_cast<int>( trackStart->m_Kind ),
                       static_cast<int>( AI_CONTEXT_ANCHOR_KIND::RouteStart ) );
    BOOST_CHECK_EQUAL( static_cast<int>( trackEnd->m_Kind ),
                       static_cast<int>( AI_CONTEXT_ANCHOR_KIND::RouteTarget ) );
    BOOST_CHECK_EQUAL( static_cast<int>( shapeStart->m_Kind ),
                       static_cast<int>( AI_CONTEXT_ANCHOR_KIND::ShapeCorner ) );
    BOOST_CHECK_EQUAL( trackStart->m_Position.x, 10 );
    BOOST_CHECK_EQUAL( trackEnd->m_Position.y, 120 );
    BOOST_CHECK_EQUAL( shapeStart->m_Position.x, 500 );
    BOOST_CHECK_EQUAL( shapeEnd->m_Position.y, 800 );

    nlohmann::json routeDetails = anchorDetails( *trackStart );
    BOOST_CHECK_EQUAL( routeDetails["role"].get<std::string>(), "start" );
    BOOST_CHECK_EQUAL( routeDetails["layer"].get<std::string>(), "F.Cu" );
    BOOST_CHECK_EQUAL( routeDetails["net_name"].get<std::string>(), "/CLK" );
}

BOOST_AUTO_TEST_SUITE_END()
