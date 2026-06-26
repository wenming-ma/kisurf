#include <kisurf_ai_pcb_session_shadow_seeder.h>

#include <board.h>
#include <board_connected_item.h>
#include <footprint.h>
#include <kisurf/ai/ai_execution_session.h>
#include <kisurf/ai/ai_shadow_board.h>
#include <lset.h>
#include <nlohmann/json.hpp>
#include <pad.h>
#include <pcb_shape.h>
#include <pcb_track.h>
#include <zone.h>

#include <map>
#include <string>

namespace
{
std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


wxString fromJson( const nlohmann::json& aJson )
{
    return wxString::FromUTF8( aJson.dump().c_str() );
}


nlohmann::json pointJson( const VECTOR2I& aPoint )
{
    return { { "x", aPoint.x }, { "y", aPoint.y } };
}


nlohmann::json lineChainJson( const SHAPE_LINE_CHAIN& aChain )
{
    nlohmann::json points = nlohmann::json::array();

    for( int i = 0; i < aChain.PointCount(); ++i )
        points.push_back( pointJson( aChain.CPoint( i ) ) );

    return points;
}


wxString shapeName( SHAPE_T aShape )
{
    switch( aShape )
    {
    case SHAPE_T::SEGMENT:
        return wxS( "segment" );

    case SHAPE_T::RECTANGLE:
        return wxS( "rectangle" );

    case SHAPE_T::CIRCLE:
        return wxS( "circle" );

    case SHAPE_T::ARC:
        return wxS( "arc" );

    case SHAPE_T::POLY:
        return wxS( "poly" );

    case SHAPE_T::BEZIER:
        return wxS( "bezier" );

    default:
        return wxS( "shape" );
    }
}


void applyLiveItemMetadata( AI_SHADOW_ITEM& aShadowItem, const BOARD_ITEM& aItem )
{
    const wxString uuid = aItem.m_Uuid.AsString();
    aShadowItem.m_Metadata[wxS( "live_uuid" )] = uuid;
    aShadowItem.m_Metadata[wxS( "live_kicad_type" )] =
            wxString::Format( wxS( "%d" ), static_cast<int>( aItem.Type() ) );

    if( aItem.IsSelected() )
        aShadowItem.m_Metadata[wxS( "selected" )] = wxS( "true" );
}


void applyNet( AI_SHADOW_ITEM& aShadowItem, const BOARD_ITEM& aItem )
{
    if( const BOARD_CONNECTED_ITEM* connected =
                dynamic_cast<const BOARD_CONNECTED_ITEM*>( &aItem ) )
    {
        aShadowItem.m_Net = connected->GetNetname();
    }
}


void upsertLiveItem( AI_EXECUTION_SESSION& aSession, BOARD_ITEM& aItem,
                     const wxString& aType, const nlohmann::json& aGeometry,
                     const wxString& aLayer = wxEmptyString,
                     const std::vector<wxString>& aLayers = {},
                     const std::map<wxString, wxString>& aMetadata = {} )
{
    const wxString alias =
            wxString::Format( wxS( "live:%s" ), aItem.m_Uuid.AsString() );
    AI_SESSION_HANDLE handle = aSession.CreateHandle( alias );

    AI_SHADOW_ITEM shadowItem;
    shadowItem.m_Handle = handle;
    shadowItem.m_Type = aType;
    shadowItem.m_Alias = handle.m_Alias;
    shadowItem.m_Layer = aLayer;
    shadowItem.m_Layers = aLayers;
    shadowItem.m_GeometryJson = fromJson( aGeometry );
    shadowItem.m_CreatedEpoch = aSession.Epoch();
    shadowItem.m_UpdatedEpoch = aSession.Epoch();
    applyNet( shadowItem, aItem );
    applyLiveItemMetadata( shadowItem, aItem );

    for( const auto& [key, value] : aMetadata )
        shadowItem.m_Metadata[key] = value;

    aSession.ShadowBoard().UpsertItem( std::move( shadowItem ) );
}


std::vector<wxString> layerNames( const LSET& aLayers )
{
    std::vector<wxString> layers;

    for( PCB_LAYER_ID layer : aLayers )
        layers.push_back( LSET::Name( layer ) );

    return layers;
}


wxString firstLayerName( const std::vector<wxString>& aLayers )
{
    return aLayers.empty() ? wxString() : aLayers.front();
}


PCB_LAYER_ID primaryPadLayer( const PAD& aPad )
{
    const LSET layers = aPad.GetLayerSet();

    if( layers.test( F_Cu ) )
        return F_Cu;

    if( layers.test( B_Cu ) )
        return B_Cu;

    LSEQ seq = layers.Seq();

    if( !seq.empty() )
        return seq.front();

    return F_Cu;
}


nlohmann::json footprintGeometry( const FOOTPRINT& aFootprint )
{
    return {
        { "position", pointJson( aFootprint.GetPosition() ) },
        { "reference", toUtf8String( aFootprint.GetReference() ) },
        { "value", toUtf8String( aFootprint.GetValue() ) },
        { "fp_id", toUtf8String( aFootprint.GetFPIDAsString() ) },
        { "orientation_degrees", aFootprint.GetOrientationDegrees() },
        { "pad_count", aFootprint.Pads().size() }
    };
}


nlohmann::json padGeometry( const PAD& aPad, PCB_LAYER_ID aPrimaryLayer )
{
    const VECTOR2I size = aPad.GetSize( aPrimaryLayer );

    nlohmann::json layers = nlohmann::json::array();

    for( const wxString& layer : layerNames( aPad.GetLayerSet() ) )
        layers.push_back( toUtf8String( layer ) );

    return {
        { "position", pointJson( aPad.GetPosition() ) },
        { "size", { { "x", size.x }, { "y", size.y } } },
        { "number", toUtf8String( aPad.GetNumber() ) },
        { "orientation_degrees", aPad.GetOrientationDegrees() },
        { "layer_set", std::move( layers ) },
        { "has_hole", aPad.HasHole() },
        { "drill", { { "x", aPad.GetDrillSizeX() }, { "y", aPad.GetDrillSizeY() } } }
    };
}


nlohmann::json zoneOutlineGeometry( const ZONE& aZone )
{
    nlohmann::json outline = nlohmann::json::object();

    if( !aZone.Outline() || aZone.Outline()->OutlineCount() == 0 )
        return outline;

    outline["points"] = lineChainJson( aZone.Outline()->COutline( 0 ) );

    nlohmann::json holes = nlohmann::json::array();

    for( int i = 0; i < aZone.Outline()->HoleCount( 0 ); ++i )
        holes.push_back( lineChainJson( aZone.Outline()->CHole( 0, i ) ) );

    if( !holes.empty() )
        outline["holes"] = std::move( holes );

    return outline;
}
} // namespace


KISURF_AI_PCB_SESSION_SHADOW_SEEDER::KISURF_AI_PCB_SESSION_SHADOW_SEEDER(
        BOARD& aBoard ) :
        m_Board( aBoard )
{
}


void KISURF_AI_PCB_SESSION_SHADOW_SEEDER::Seed( AI_EXECUTION_SESSION& aSession )
{
    for( PCB_TRACK* track : m_Board.Tracks() )
    {
        if( PCB_VIA* via = dynamic_cast<PCB_VIA*>( track ) )
        {
            PCB_LAYER_ID topLayer = F_Cu;
            PCB_LAYER_ID bottomLayer = B_Cu;
            via->LayerPair( &topLayer, &bottomLayer );

            nlohmann::json geometry = {
                { "position", pointJson( via->GetPosition() ) },
                { "diameter", via->GetWidth( topLayer ) },
                { "drill", via->GetDrillValue() },
                { "layer_pair",
                  nlohmann::json::array( { toUtf8String( LSET::Name( topLayer ) ),
                                            toUtf8String( LSET::Name( bottomLayer ) ) } ) }
            };
            upsertLiveItem( aSession, *via, wxS( "via" ), geometry, wxEmptyString,
                            { LSET::Name( topLayer ), LSET::Name( bottomLayer ) } );
            continue;
        }

        nlohmann::json geometry = {
            { "start", pointJson( track->GetStart() ) },
            { "end", pointJson( track->GetEnd() ) },
            { "width", track->GetWidth() }
        };
        upsertLiveItem( aSession, *track, wxS( "track_segment" ), geometry,
                        LSET::Name( track->GetLayer() ),
                        { LSET::Name( track->GetLayer() ) } );
    }

    for( ZONE* zone : m_Board.Zones() )
    {
        std::vector<wxString> layers = layerNames( zone->GetLayerSet() );
        wxString layer = firstLayerName( layers );
        upsertLiveItem( aSession, *zone, wxS( "zone" ), zoneOutlineGeometry( *zone ),
                        layer, layers );
    }

    for( FOOTPRINT* footprint : m_Board.Footprints() )
    {
        std::vector<wxString> footprintLayers = { LSET::Name( footprint->GetLayer() ) };
        std::map<wxString, wxString> footprintMetadata = {
            { wxS( "footprint_reference" ), footprint->GetReference() },
            { wxS( "footprint_value" ), footprint->GetValue() },
            { wxS( "fp_id" ), footprint->GetFPIDAsString() },
            { wxS( "pad_count" ),
              wxString::Format( wxS( "%zu" ), footprint->Pads().size() ) }
        };

        upsertLiveItem( aSession, *footprint, wxS( "footprint" ),
                        footprintGeometry( *footprint ), LSET::Name( footprint->GetLayer() ),
                        footprintLayers, footprintMetadata );

        for( PAD* pad : footprint->Pads() )
        {
            const PCB_LAYER_ID primaryLayer = primaryPadLayer( *pad );
            std::vector<wxString> padLayers = layerNames( pad->GetLayerSet() );
            std::map<wxString, wxString> padMetadata = {
                { wxS( "footprint_reference" ), footprint->GetReference() },
                { wxS( "footprint_value" ), footprint->GetValue() },
                { wxS( "footprint_uuid" ), footprint->m_Uuid.AsString() },
                { wxS( "pad_number" ), pad->GetNumber() }
            };

            if( footprint->IsSelected() )
            {
                padMetadata[wxS( "selected" )] = wxS( "true" );
                padMetadata[wxS( "selection_inherited_from" )] =
                        footprint->m_Uuid.AsString();
                padMetadata[wxS( "selection_inherited_from_type" )] =
                        wxS( "footprint" );
            }

            upsertLiveItem( aSession, *pad, wxS( "pad" ),
                            padGeometry( *pad, primaryLayer ),
                            LSET::Name( primaryLayer ), padLayers, padMetadata );
        }
    }

    for( BOARD_ITEM* drawing : m_Board.Drawings() )
    {
        PCB_SHAPE* shape = dynamic_cast<PCB_SHAPE*>( drawing );

        if( !shape )
            continue;

        nlohmann::json geometry = {
            { "shape", toUtf8String( shapeName( shape->GetShape() ) ) },
            { "start", pointJson( shape->GetStart() ) },
            { "end", pointJson( shape->GetEnd() ) },
            { "width", shape->GetWidth() },
            { "fill", shape->IsAnyFill() }
        };
        upsertLiveItem( aSession, *shape, wxS( "shape" ), geometry,
                        LSET::Name( shape->GetLayer() ),
                        { LSET::Name( shape->GetLayer() ) } );
    }
}
