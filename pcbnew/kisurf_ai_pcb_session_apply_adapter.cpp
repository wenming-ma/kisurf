#include <kisurf_ai_pcb_session_apply_adapter.h>

#include <board.h>
#include <board_commit.h>
#include <board_item.h>
#include <eda_shape.h>
#include <footprint.h>
#include <kiid.h>
#include <kisurf/ai/ai_shadow_board.h>
#include <lset.h>
#include <netinfo.h>
#include <nlohmann/json.hpp>
#include <pcb_edit_frame.h>
#include <pcb_shape.h>
#include <pcb_track.h>
#include <tool/tool_manager.h>
#include <zone.h>
#include <zone_filler.h>

#include <optional>
#include <string>
#include <vector>

namespace
{
std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


wxString stringField( const nlohmann::json& aArgs, const char* aName )
{
    if( !aArgs.contains( aName ) || !aArgs[aName].is_string() )
        return wxEmptyString;

    return wxString::FromUTF8( aArgs[aName].get_ref<const std::string&>().c_str() );
}


std::optional<int> intField( const nlohmann::json& aArgs, const char* aName )
{
    if( !aArgs.contains( aName ) || !aArgs[aName].is_number() )
        return std::nullopt;

    return static_cast<int>( aArgs[aName].get<double>() );
}


std::optional<VECTOR2I> pointFromJson( const nlohmann::json& aPoint )
{
    if( !aPoint.is_object() || !aPoint.contains( "x" ) || !aPoint.contains( "y" )
        || !aPoint["x"].is_number() || !aPoint["y"].is_number() )
    {
        return std::nullopt;
    }

    return VECTOR2I( static_cast<int>( aPoint["x"].get<double>() ),
                     static_cast<int>( aPoint["y"].get<double>() ) );
}


std::optional<VECTOR2I> targetPositionForHandle(
        const nlohmann::json& aTargetPositions,
        const AI_SESSION_HANDLE& aHandle,
        size_t aIndex,
        size_t aHandleCount )
{
    if( aTargetPositions.is_object() && aTargetPositions.contains( "x" )
        && aTargetPositions.contains( "y" ) && aHandleCount == 1 )
    {
        return pointFromJson( aTargetPositions );
    }

    if( aTargetPositions.is_array() && aIndex < aTargetPositions.size() )
        return pointFromJson( aTargetPositions[aIndex] );

    if( aTargetPositions.is_object() )
    {
        const std::string handleId =
                std::to_string( static_cast<unsigned long long>( aHandle.m_HandleId ) );

        if( aTargetPositions.contains( handleId ) )
            return pointFromJson( aTargetPositions[handleId] );

        if( !aHandle.m_Alias.IsEmpty() )
        {
            const std::string alias = toUtf8String( aHandle.m_Alias );

            if( aTargetPositions.contains( alias ) )
                return pointFromJson( aTargetPositions[alias] );
        }
    }

    return std::nullopt;
}


std::optional<PCB_LAYER_ID> resolveLayerName( const BOARD& aBoard,
                                              const wxString& aLayerName )
{
    wxString layerName = aLayerName;
    const int standardLayer = LSET::NameToLayer( layerName );

    if( standardLayer >= 0 && standardLayer < PCB_LAYER_ID_COUNT )
        return static_cast<PCB_LAYER_ID>( standardLayer );

    for( int layer = 0; layer < PCB_LAYER_ID_COUNT; ++layer )
    {
        PCB_LAYER_ID pcbLayer = static_cast<PCB_LAYER_ID>( layer );

        if( aBoard.GetLayerName( pcbLayer ) == aLayerName )
            return pcbLayer;
    }

    return std::nullopt;
}


std::vector<PCB_LAYER_ID> layerListFromJson( const BOARD& aBoard,
                                             const nlohmann::json& aArgs,
                                             const char* aField )
{
    std::vector<PCB_LAYER_ID> layers;

    if( !aArgs.contains( aField ) )
        return layers;

    const nlohmann::json& value = aArgs[aField];

    if( value.is_string() )
    {
        if( std::optional<PCB_LAYER_ID> layer =
                    resolveLayerName( aBoard, stringField( aArgs, aField ) ) )
        {
            layers.push_back( *layer );
        }

        return layers;
    }

    if( !value.is_array() )
        return layers;

    for( const nlohmann::json& entry : value )
    {
        if( !entry.is_string() )
            continue;

        wxString layerName =
                wxString::FromUTF8( entry.get_ref<const std::string&>().c_str() );

        if( std::optional<PCB_LAYER_ID> layer = resolveLayerName( aBoard, layerName ) )
            layers.push_back( *layer );
    }

    return layers;
}


std::optional<BOX2I> boxFromJson( const nlohmann::json& aBox )
{
    if( !aBox.is_object() )
        return std::nullopt;

    if( aBox.contains( "x" ) && aBox.contains( "y" ) && aBox.contains( "width" )
        && aBox.contains( "height" ) && aBox["x"].is_number()
        && aBox["y"].is_number() && aBox["width"].is_number()
        && aBox["height"].is_number() )
    {
        const int x = static_cast<int>( aBox["x"].get<double>() );
        const int y = static_cast<int>( aBox["y"].get<double>() );
        const int width = static_cast<int>( aBox["width"].get<double>() );
        const int height = static_cast<int>( aBox["height"].get<double>() );

        if( width <= 0 || height <= 0 )
            return std::nullopt;

        return BOX2I( VECTOR2I( x, y ), VECTOR2I( width, height ) );
    }

    if( aBox.contains( "min" ) && aBox.contains( "max" ) )
    {
        std::optional<VECTOR2I> minPoint = pointFromJson( aBox["min"] );
        std::optional<VECTOR2I> maxPoint = pointFromJson( aBox["max"] );

        if( !minPoint || !maxPoint || maxPoint->x <= minPoint->x
            || maxPoint->y <= minPoint->y )
        {
            return std::nullopt;
        }

        return BOX2I( *minPoint, *maxPoint - *minPoint );
    }

    return std::nullopt;
}


NETINFO_ITEM* resolveOptionalNet( BOARD& aBoard, const nlohmann::json& aArgs,
                                  wxString& aError )
{
    wxString netName = stringField( aArgs, "net" );

    if( netName.IsEmpty() )
        return nullptr;

    NETINFO_ITEM* net = aBoard.FindNet( netName );

    if( !net )
        aError = wxString::Format( wxS( "Net '%s' was not found." ), netName );

    return net;
}


BOARD_ITEM* buildVia( BOARD& aBoard, const nlohmann::json& aArgs, wxString& aError )
{
    std::optional<VECTOR2I> position =
            aArgs.contains( "position" ) ? pointFromJson( aArgs["position"] ) : std::nullopt;

    if( !position )
    {
        aError = wxS( "CreateVia requires a numeric position." );
        return nullptr;
    }

    std::vector<PCB_LAYER_ID> layers = layerListFromJson( aBoard, aArgs, "layer_pair" );

    PCB_VIA* via = new PCB_VIA( &aBoard );
    via->SetPosition( *position );

    if( std::optional<int> diameter = intField( aArgs, "diameter" ) )
        via->SetWidth( *diameter );

    if( std::optional<int> drill = intField( aArgs, "drill" ) )
        via->SetPrimaryDrillSize( VECTOR2I( *drill, *drill ) );

    if( layers.size() >= 2 )
        via->SetLayerPair( layers.front(), layers.back() );
    else
        via->SetLayerPair( F_Cu, B_Cu );

    if( NETINFO_ITEM* net = resolveOptionalNet( aBoard, aArgs, aError ) )
    {
        via->SetNet( net );
        via->SetIsFree( via->GetNetCode() > 0 );
    }
    else if( !aError.IsEmpty() )
    {
        delete via;
        return nullptr;
    }

    return via;
}


BOARD_ITEM* buildTrackSegment( BOARD& aBoard, const nlohmann::json& aArgs,
                               wxString& aError )
{
    std::optional<VECTOR2I> start =
            aArgs.contains( "start" ) ? pointFromJson( aArgs["start"] ) : std::nullopt;
    std::optional<VECTOR2I> end =
            aArgs.contains( "end" ) ? pointFromJson( aArgs["end"] ) : std::nullopt;

    if( !start || !end )
    {
        aError = wxS( "CreateTrackSegment requires numeric start and end points." );
        return nullptr;
    }

    std::optional<PCB_LAYER_ID> layer = resolveLayerName( aBoard, stringField( aArgs, "layer" ) );

    if( !layer )
    {
        aError = wxS( "CreateTrackSegment requires a valid PCB layer." );
        return nullptr;
    }

    PCB_TRACK* track = new PCB_TRACK( &aBoard );
    track->SetStart( *start );
    track->SetEnd( *end );
    track->SetLayer( *layer );

    if( std::optional<int> width = intField( aArgs, "width" ) )
        track->SetWidth( *width );

    if( NETINFO_ITEM* net = resolveOptionalNet( aBoard, aArgs, aError ) )
        track->SetNet( net );
    else if( !aError.IsEmpty() )
    {
        delete track;
        return nullptr;
    }

    return track;
}


std::optional<SHAPE_T> shapeTypeFromName( const wxString& aShape )
{
    if( aShape.CmpNoCase( wxS( "segment" ) ) == 0
        || aShape.CmpNoCase( wxS( "line" ) ) == 0 )
    {
        return SHAPE_T::SEGMENT;
    }

    if( aShape.CmpNoCase( wxS( "rectangle" ) ) == 0
        || aShape.CmpNoCase( wxS( "rect" ) ) == 0 )
    {
        return SHAPE_T::RECTANGLE;
    }

    if( aShape.CmpNoCase( wxS( "circle" ) ) == 0 )
        return SHAPE_T::CIRCLE;

    if( aShape.CmpNoCase( wxS( "arc" ) ) == 0 )
        return SHAPE_T::ARC;

    return std::nullopt;
}


std::optional<ZONE_FILL_MODE> zoneFillModeFromName( const wxString& aFillMode )
{
    wxString mode = aFillMode.Lower();
    mode.Replace( wxS( "-" ), wxS( "_" ) );
    mode.Replace( wxS( " " ), wxS( "_" ) );

    if( mode == wxS( "solid" ) || mode == wxS( "polygon" )
        || mode == wxS( "polygons" ) )
    {
        return ZONE_FILL_MODE::POLYGONS;
    }

    if( mode == wxS( "hatch" ) || mode == wxS( "hatched" )
        || mode == wxS( "hatch_pattern" ) )
    {
        return ZONE_FILL_MODE::HATCH_PATTERN;
    }

    if( mode == wxS( "copper_thieving" ) || mode == wxS( "thieving" ) )
        return ZONE_FILL_MODE::COPPER_THIEVING;

    return std::nullopt;
}


BOARD_ITEM* buildShape( BOARD& aBoard, const nlohmann::json& aArgs, wxString& aError )
{
    const nlohmann::json geometry =
            aArgs.contains( "geometry" ) && aArgs["geometry"].is_object()
                    ? aArgs["geometry"]
                    : aArgs;
    wxString shapeName = stringField( aArgs, "shape_type" );

    if( shapeName.IsEmpty() )
        shapeName = stringField( aArgs, "shape" );

    std::optional<SHAPE_T> shapeType = shapeTypeFromName( shapeName );
    std::optional<PCB_LAYER_ID> layer = resolveLayerName( aBoard, stringField( aArgs, "layer" ) );
    std::optional<VECTOR2I> start =
            geometry.contains( "start" ) ? pointFromJson( geometry["start"] ) : std::nullopt;
    std::optional<VECTOR2I> end =
            geometry.contains( "end" ) ? pointFromJson( geometry["end"] ) : std::nullopt;
    std::optional<VECTOR2I> mid =
            geometry.contains( "mid" ) ? pointFromJson( geometry["mid"] ) : std::nullopt;
    std::optional<VECTOR2I> center =
            geometry.contains( "center" ) ? pointFromJson( geometry["center"] ) : std::nullopt;
    std::optional<int> radius = intField( geometry, "radius" );

    if( !shapeType || !layer )
    {
        aError = wxS( "CreateShape requires shape_type and layer." );
        return nullptr;
    }

    if( *shapeType == SHAPE_T::CIRCLE )
    {
        if( !center || !radius || *radius <= 0 )
        {
            aError = wxS( "CreateShape circle requires center and positive radius." );
            return nullptr;
        }
    }
    else if( *shapeType == SHAPE_T::ARC )
    {
        if( !start || !mid || !end )
        {
            aError = wxS( "CreateShape arc requires start, mid, and end." );
            return nullptr;
        }
    }
    else if( !start || !end )
    {
        aError = wxS( "CreateShape requires start and end for this shape type." );
        return nullptr;
    }

    PCB_SHAPE* shape = new PCB_SHAPE( &aBoard, *shapeType );
    shape->SetLayer( *layer );

    if( *shapeType == SHAPE_T::CIRCLE )
    {
        shape->SetStart( *center );
        shape->SetEnd( VECTOR2I( center->x + *radius, center->y ) );
    }
    else if( *shapeType == SHAPE_T::ARC )
    {
        shape->SetArcGeometry( *start, *mid, *end );
    }
    else
    {
        shape->SetStart( *start );
        shape->SetEnd( *end );
    }

    if( std::optional<int> width = intField( aArgs, "width" ) )
        shape->SetWidth( *width );

    if( aArgs.contains( "fill" ) && aArgs["fill"].is_boolean() )
        shape->SetFilled( aArgs["fill"].get<bool>() );

    return shape;
}


std::vector<VECTOR2I> pointsFromJsonArray( const nlohmann::json& aPoints )
{
    std::vector<VECTOR2I> result;

    if( !aPoints.is_array() )
        return result;

    for( const nlohmann::json& point : aPoints )
    {
        if( std::optional<VECTOR2I> parsed = pointFromJson( point ) )
            result.push_back( *parsed );
    }

    return result;
}


std::vector<std::vector<VECTOR2I>> outlineRingsFromJson( const nlohmann::json& aArgs )
{
    const nlohmann::json* outline = nullptr;

    if( aArgs.contains( "outline" ) )
        outline = &aArgs["outline"];
    else if( aArgs.contains( "points" ) )
        outline = &aArgs["points"];

    if( !outline )
    {
        if( aArgs.contains( "outer" ) )
            outline = &aArgs;
    }

    if( !outline )
        return {};

    std::vector<std::vector<VECTOR2I>> rings;

    if( outline->is_array() )
    {
        rings.push_back( pointsFromJsonArray( *outline ) );
        return rings;
    }

    if( !outline->is_object() )
        return {};

    if( outline->contains( "outer" ) )
    {
        rings.push_back( pointsFromJsonArray( ( *outline )["outer"] ) );

        if( outline->contains( "inner" ) )
            rings.push_back( pointsFromJsonArray( ( *outline )["inner"] ) );

        if( outline->contains( "holes" ) && ( *outline )["holes"].is_array() )
        {
            for( const nlohmann::json& hole : ( *outline )["holes"] )
                rings.push_back( pointsFromJsonArray( hole ) );
        }

        return rings;
    }

    if( outline->contains( "points" ) )
        rings.push_back( pointsFromJsonArray( ( *outline )["points"] ) );

    return rings;
}


SHAPE_LINE_CHAIN lineChainFromPoints( const std::vector<VECTOR2I>& aPoints )
{
    SHAPE_LINE_CHAIN chain;

    for( const VECTOR2I& point : aPoints )
        chain.Append( point );

    chain.SetClosed( true );
    return chain;
}


BOARD_ITEM* buildZone( BOARD& aBoard, const nlohmann::json& aArgs, wxString& aError )
{
    std::vector<std::vector<VECTOR2I>> rings = outlineRingsFromJson( aArgs );

    if( rings.empty() || rings.front().size() < 3 )
    {
        aError = wxS( "CreateZone requires an outline with at least three points." );
        return nullptr;
    }

    std::vector<PCB_LAYER_ID> layers = layerListFromJson( aBoard, aArgs, "layer_set" );

    if( layers.empty() )
        layers = layerListFromJson( aBoard, aArgs, "layer" );

    if( layers.empty() )
    {
        aError = wxS( "CreateZone requires at least one valid layer." );
        return nullptr;
    }

    ZONE* zone = new ZONE( &aBoard );
    LSET  layerSet;

    for( PCB_LAYER_ID layer : layers )
        layerSet.set( layer );

    zone->SetLayerSet( layerSet );

    if( NETINFO_ITEM* net = resolveOptionalNet( aBoard, aArgs, aError ) )
        zone->SetNet( net );
    else if( !aError.IsEmpty() )
    {
        delete zone;
        return nullptr;
    }

    if( aArgs.contains( "clearance" ) )
    {
        std::optional<int> clearance = intField( aArgs, "clearance" );

        if( !clearance || *clearance < 0 )
        {
            aError = wxS( "CreateZone clearance must be a non-negative number." );
            delete zone;
            return nullptr;
        }

        zone->SetLocalClearance( *clearance );
    }

    if( aArgs.contains( "priority" ) )
    {
        std::optional<int> priority = intField( aArgs, "priority" );

        if( !priority || *priority < 0 )
        {
            aError = wxS( "CreateZone priority must be a non-negative number." );
            delete zone;
            return nullptr;
        }

        zone->SetAssignedPriority( static_cast<unsigned>( *priority ) );
    }

    if( aArgs.contains( "fill_mode" ) )
    {
        wxString fillModeName = stringField( aArgs, "fill_mode" );
        std::optional<ZONE_FILL_MODE> fillMode =
                zoneFillModeFromName( fillModeName );

        if( !fillMode )
        {
            aError = wxS( "CreateZone fill_mode must be solid, hatch_pattern, or copper_thieving." );
            delete zone;
            return nullptr;
        }

        zone->SetFillMode( *fillMode );
    }

    zone->AddPolygon( lineChainFromPoints( rings.front() ) );

    for( size_t i = 1; i < rings.size(); ++i )
    {
        if( rings[i].size() >= 3 )
            zone->AddPolygon( lineChainFromPoints( rings[i] ) );
    }

    zone->SetNeedRefill( true );
    return zone;
}


bool replaceZoneOutline( ZONE& aZone, const nlohmann::json& aPatch, wxString& aError )
{
    std::vector<std::vector<VECTOR2I>> rings = outlineRingsFromJson( aPatch );

    if( rings.empty() || rings.front().size() < 3 )
    {
        aError = wxS( "Zone geometry patch requires an outline with at least three points." );
        return false;
    }

    for( size_t i = 1; i < rings.size(); ++i )
    {
        if( rings[i].size() < 3 )
        {
            aError = wxS( "Zone geometry patch holes must have at least three points." );
            return false;
        }
    }

    aZone.RemoveAllContours();
    aZone.AddPolygon( lineChainFromPoints( rings.front() ) );

    for( size_t i = 1; i < rings.size(); ++i )
        aZone.AddPolygon( lineChainFromPoints( rings[i] ) );

    aZone.SetNeedRefill( true );
    return true;
}


bool updateItemGeometry( BOARD_ITEM& aItem, const nlohmann::json& aPatch,
                         wxString& aError )
{
    if( PCB_VIA* via = dynamic_cast<PCB_VIA*>( &aItem ) )
    {
        if( aPatch.contains( "position" ) )
        {
            std::optional<VECTOR2I> position = pointFromJson( aPatch["position"] );

            if( !position )
            {
                aError = wxS( "Via geometry patch has an invalid position." );
                return false;
            }

            via->SetPosition( *position );
        }

        if( std::optional<int> diameter = intField( aPatch, "diameter" ) )
            via->SetWidth( *diameter );

        if( std::optional<int> drill = intField( aPatch, "drill" ) )
            via->SetPrimaryDrillSize( VECTOR2I( *drill, *drill ) );

        return true;
    }

    if( PCB_TRACK* track = dynamic_cast<PCB_TRACK*>( &aItem ) )
    {
        if( aPatch.contains( "start" ) )
        {
            std::optional<VECTOR2I> start = pointFromJson( aPatch["start"] );

            if( !start )
            {
                aError = wxS( "Track geometry patch has an invalid start." );
                return false;
            }

            track->SetStart( *start );
        }

        if( aPatch.contains( "end" ) )
        {
            std::optional<VECTOR2I> end = pointFromJson( aPatch["end"] );

            if( !end )
            {
                aError = wxS( "Track geometry patch has an invalid end." );
                return false;
            }

            track->SetEnd( *end );
        }

        if( std::optional<int> width = intField( aPatch, "width" ) )
            track->SetWidth( *width );

        return true;
    }

    if( ZONE* zone = dynamic_cast<ZONE*>( &aItem ) )
        return replaceZoneOutline( *zone, aPatch, aError );

    if( PCB_SHAPE* shape = dynamic_cast<PCB_SHAPE*>( &aItem ) )
    {
        if( aPatch.contains( "start" ) )
        {
            std::optional<VECTOR2I> start = pointFromJson( aPatch["start"] );

            if( !start )
            {
                aError = wxS( "Shape geometry patch has an invalid start." );
                return false;
            }

            shape->SetStart( *start );
        }

        if( aPatch.contains( "end" ) )
        {
            std::optional<VECTOR2I> end = pointFromJson( aPatch["end"] );

            if( !end )
            {
                aError = wxS( "Shape geometry patch has an invalid end." );
                return false;
            }

            shape->SetEnd( *end );
        }

        if( std::optional<int> width = intField( aPatch, "width" ) )
            shape->SetWidth( *width );

        return true;
    }

    aError = wxS( "Item type does not support geometry patching yet." );
    return false;
}


bool applyTypedProperties( BOARD_ITEM& aItem, const nlohmann::json& aProps,
                           wxString& aError )
{
    if( PCB_VIA* via = dynamic_cast<PCB_VIA*>( &aItem ) )
    {
        if( aProps.contains( "diameter" ) )
        {
            std::optional<int> diameter = intField( aProps, "diameter" );

            if( !diameter || *diameter <= 0 )
            {
                aError = wxS( "Via diameter property must be a positive number." );
                return false;
            }

            via->SetWidth( *diameter );
        }

        if( aProps.contains( "drill" ) )
        {
            std::optional<int> drill = intField( aProps, "drill" );

            if( !drill || *drill <= 0 )
            {
                aError = wxS( "Via drill property must be a positive number." );
                return false;
            }

            via->SetPrimaryDrillSize( VECTOR2I( *drill, *drill ) );
        }

        return true;
    }

    if( PCB_TRACK* track = dynamic_cast<PCB_TRACK*>( &aItem ) )
    {
        if( aProps.contains( "width" ) )
        {
            std::optional<int> width = intField( aProps, "width" );

            if( !width || *width <= 0 )
            {
                aError = wxS( "Track width property must be a positive number." );
                return false;
            }

            track->SetWidth( *width );
        }

        return true;
    }

    if( ZONE* zone = dynamic_cast<ZONE*>( &aItem ) )
    {
        if( aProps.contains( "clearance" ) )
        {
            std::optional<int> clearance = intField( aProps, "clearance" );

            if( !clearance || *clearance < 0 )
            {
                aError = wxS( "Zone clearance property must be a non-negative number." );
                return false;
            }

            zone->SetLocalClearance( *clearance );
        }

        if( aProps.contains( "priority" ) )
        {
            std::optional<int> priority = intField( aProps, "priority" );

            if( !priority || *priority < 0 )
            {
                aError = wxS( "Zone priority property must be a non-negative number." );
                return false;
            }

            zone->SetAssignedPriority( static_cast<unsigned>( *priority ) );
        }

        if( aProps.contains( "fill_mode" ) )
        {
            wxString fillModeName = stringField( aProps, "fill_mode" );
            std::optional<ZONE_FILL_MODE> fillMode =
                    zoneFillModeFromName( fillModeName );

            if( !fillMode )
            {
                aError = wxS( "Zone fill_mode property must be solid, hatch_pattern, "
                              "or copper_thieving." );
                return false;
            }

            zone->SetFillMode( *fillMode );
        }

        zone->SetNeedRefill( true );
        return true;
    }

    if( FOOTPRINT* footprint = dynamic_cast<FOOTPRINT*>( &aItem ) )
    {
        if( aProps.contains( "reference" ) )
        {
            if( !aProps["reference"].is_string() )
            {
                aError = wxS( "Footprint reference property must be a string." );
                return false;
            }

            footprint->SetReference( stringField( aProps, "reference" ) );
        }

        if( aProps.contains( "value" ) )
        {
            if( !aProps["value"].is_string() )
            {
                aError = wxS( "Footprint value property must be a string." );
                return false;
            }

            footprint->SetValue( stringField( aProps, "value" ) );
        }

        if( aProps.contains( "side" ) )
        {
            if( !aProps["side"].is_string() )
            {
                aError = wxS( "Footprint side property must be a layer name string." );
                return false;
            }

            std::optional<PCB_LAYER_ID> side =
                    resolveLayerName( *footprint->GetBoard(), stringField( aProps, "side" ) );

            if( !side || ( *side != F_Cu && *side != B_Cu ) )
            {
                aError = wxS( "Footprint side property must resolve to F.Cu or B.Cu." );
                return false;
            }

            footprint->SetLayerAndFlip( *side );
        }

        if( aProps.contains( "orientation_degrees" ) )
        {
            if( !aProps["orientation_degrees"].is_number() )
            {
                aError = wxS( "Footprint orientation_degrees property must be numeric." );
                return false;
            }

            footprint->SetOrientation(
                    EDA_ANGLE( aProps["orientation_degrees"].get<double>(),
                               DEGREES_T ) );
        }

        return true;
    }

    if( PCB_SHAPE* shape = dynamic_cast<PCB_SHAPE*>( &aItem ) )
    {
        if( aProps.contains( "width" ) )
        {
            std::optional<int> width = intField( aProps, "width" );

            if( !width || *width < 0 )
            {
                aError = wxS( "Shape width property must be a non-negative number." );
                return false;
            }

            shape->SetWidth( *width );
        }

        if( aProps.contains( "fill" ) )
        {
            if( !aProps["fill"].is_boolean() )
            {
                aError = wxS( "Shape fill property must be a boolean." );
                return false;
            }

            shape->SetFilled( aProps["fill"].get<bool>() );
        }

        return true;
    }

    aError = wxS( "Item type does not support typed property patching yet." );
    return false;
}


} // namespace


KISURF_AI_PCB_SESSION_APPLY_ADAPTER::KISURF_AI_PCB_SESSION_APPLY_ADAPTER(
        PCB_EDIT_FRAME& aFrame ) :
        m_Frame( &aFrame ),
        m_Board( aFrame.GetBoard() ),
        m_ToolManager( aFrame.GetToolManager() )
{
}


KISURF_AI_PCB_SESSION_APPLY_ADAPTER::KISURF_AI_PCB_SESSION_APPLY_ADAPTER(
        BOARD& aBoard, TOOL_MANAGER& aToolManager ) :
        m_Board( &aBoard ),
        m_ToolManager( &aToolManager )
{
}


KISURF_AI_PCB_SESSION_APPLY_ADAPTER::KISURF_AI_PCB_SESSION_APPLY_ADAPTER(
        BOARD& aPreviewBoard ) :
        m_Board( &aPreviewBoard ),
        m_DirectBoardApply( true )
{
}


KISURF_AI_PCB_SESSION_APPLY_ADAPTER::~KISURF_AI_PCB_SESSION_APPLY_ADAPTER() = default;


KISURF_AI_PCB_SESSION_APPLY_ADAPTER::HANDLE_KEY
KISURF_AI_PCB_SESSION_APPLY_ADAPTER::keyForHandle( const AI_SESSION_HANDLE& aHandle )
{
    return { aHandle.m_HandleId, aHandle.m_Generation };
}


void KISURF_AI_PCB_SESSION_APPLY_ADAPTER::seedLiveItemHandleMap(
        const AI_EXECUTION_SESSION& aSession )
{
    if( !m_Board )
        return;

    for( const AI_SHADOW_ITEM& item : aSession.ShadowBoard().QueryItems() )
    {
        const auto it = item.m_Metadata.find( wxS( "live_uuid" ) );

        if( it == item.m_Metadata.end() || it->second.IsEmpty() )
            continue;

        if( BOARD_ITEM* liveItem = m_Board->ResolveItem( KIID( it->second ), true ) )
            m_ItemsByHandle[keyForHandle( item.m_Handle )] = liveItem;
    }
}


void KISURF_AI_PCB_SESSION_APPLY_ADAPTER::stageModify( BOARD_ITEM* aItem )
{
    if( m_DirectBoardApply )
    {
        m_DirectBoardMutated = true;
        return;
    }

    m_Commit->Modify( aItem );
}


void KISURF_AI_PCB_SESSION_APPLY_ADAPTER::addCreatedItem( BOARD_ITEM* aItem )
{
    if( m_DirectBoardApply )
    {
        m_Board->Add( aItem );
        m_DirectBoardMutated = true;
        return;
    }

    m_Commit->Add( aItem );
}


void KISURF_AI_PCB_SESSION_APPLY_ADAPTER::removeItem( BOARD_ITEM* aItem )
{
    if( m_DirectBoardApply )
    {
        m_Board->Remove( aItem );
        delete aItem;
        m_DirectBoardMutated = true;
        return;
    }

    m_Commit->Remove( aItem );
}


bool KISURF_AI_PCB_SESSION_APPLY_ADAPTER::BeginTransaction(
        const AI_EXECUTION_SESSION& aSession, wxString& aError )
{
    wxUnusedVar( aSession );

    if( m_Frame )
    {
        m_Board = m_Frame->GetBoard();
        m_ToolManager = m_Frame->GetToolManager();
    }

    if( !m_Board || ( !m_DirectBoardApply && !m_ToolManager ) )
    {
        aError = wxS( "PCB editor is not ready for AI session accept." );
        return false;
    }

    m_ItemsByHandle.clear();
    m_DirectBoardMutated = false;

    if( !m_DirectBoardApply )
        m_Commit = std::make_unique<BOARD_COMMIT>( m_ToolManager, true, false );

    seedLiveItemHandleMap( aSession );
    return true;
}


bool KISURF_AI_PCB_SESSION_APPLY_ADAPTER::ApplyOperation(
        const AI_SESSION_OPERATION_RECORD& aOperation, wxString& aError )
{
    if( !m_Board || ( !m_DirectBoardApply && !m_Commit ) )
    {
        aError = wxS( "AI session accept transaction is not open." );
        return false;
    }

    nlohmann::json args =
            nlohmann::json::parse( toUtf8String( aOperation.m_ArgumentsJson ), nullptr, false );

    if( args.is_discarded() || !args.is_object() )
    {
        aError = wxS( "Journal operation arguments are not a JSON object." );
        return false;
    }

    BOARD&      board = *m_Board;
    BOARD_ITEM* created = nullptr;

    switch( aOperation.m_Kind )
    {
    case AI_SESSION_OPERATION_KIND::CreateVia:
        created = buildVia( board, args, aError );
        break;

    case AI_SESSION_OPERATION_KIND::CreateTrackSegment:
        created = buildTrackSegment( board, args, aError );
        break;

    case AI_SESSION_OPERATION_KIND::CreateZone:
        created = buildZone( board, args, aError );
        break;

    case AI_SESSION_OPERATION_KIND::CreateShape:
        created = buildShape( board, args, aError );
        break;

    case AI_SESSION_OPERATION_KIND::MoveItems:
    {
        std::optional<VECTOR2I> delta =
                args.contains( "delta" ) ? pointFromJson( args["delta"] ) : std::nullopt;

        const bool hasTargetPositions = args.contains( "target_positions" );

        if( !delta && !hasTargetPositions )
        {
            aError = wxS( "MoveItems requires a numeric delta or target_positions." );
            return false;
        }

        for( size_t i = 0; i < aOperation.m_ResolvedHandles.size(); ++i )
        {
            const AI_SESSION_HANDLE& handle = aOperation.m_ResolvedHandles[i];
            auto it = m_ItemsByHandle.find( keyForHandle( handle ) );

            if( it == m_ItemsByHandle.end() || !it->second )
            {
                aError = wxS( "MoveItems references an unknown session handle." );
                return false;
            }

            stageModify( it->second );

            if( delta )
            {
                it->second->Move( *delta );
            }
            else
            {
                std::optional<VECTOR2I> target = targetPositionForHandle(
                        args["target_positions"], handle, i,
                        aOperation.m_ResolvedHandles.size() );

                if( !target )
                {
                    aError = wxS( "MoveItems target_positions did not resolve a target." );
                    return false;
                }

                it->second->Move( *target - it->second->GetPosition() );
            }
        }

        return true;
    }

    case AI_SESSION_OPERATION_KIND::DeleteItems:
        for( const AI_SESSION_HANDLE& handle : aOperation.m_ResolvedHandles )
        {
            auto it = m_ItemsByHandle.find( keyForHandle( handle ) );

            if( it == m_ItemsByHandle.end() || !it->second )
            {
                aError = wxS( "DeleteItems references an unknown session handle." );
                return false;
            }

            removeItem( it->second );
            m_ItemsByHandle.erase( it );
        }

        return true;

    case AI_SESSION_OPERATION_KIND::UpdateItemGeometry:
        if( !args.contains( "geometry_patch" ) || !args["geometry_patch"].is_object() )
        {
            aError = wxS( "UpdateItemGeometry requires geometry_patch." );
            return false;
        }

        for( const AI_SESSION_HANDLE& handle : aOperation.m_ResolvedHandles )
        {
            auto it = m_ItemsByHandle.find( keyForHandle( handle ) );

            if( it == m_ItemsByHandle.end() || !it->second )
            {
                aError = wxS( "UpdateItemGeometry references an unknown session handle." );
                return false;
            }

            stageModify( it->second );

            if( !updateItemGeometry( *it->second, args["geometry_patch"], aError ) )
                return false;
        }

        return true;

    case AI_SESSION_OPERATION_KIND::SetItemNet:
        for( const AI_SESSION_HANDLE& handle : aOperation.m_ResolvedHandles )
        {
            auto it = m_ItemsByHandle.find( keyForHandle( handle ) );

            if( it == m_ItemsByHandle.end() || !it->second )
            {
                aError = wxS( "SetItemNet references an unknown session handle." );
                return false;
            }

            BOARD_CONNECTED_ITEM* connected =
                    dynamic_cast<BOARD_CONNECTED_ITEM*>( it->second );

            if( !connected )
            {
                aError = wxS( "SetItemNet target is not a connected board item." );
                return false;
            }

            NETINFO_ITEM* net = resolveOptionalNet( board, args, aError );

            if( !net && !aError.IsEmpty() )
                return false;

            stageModify( it->second );
            connected->SetNet( net );
        }

        return true;

    case AI_SESSION_OPERATION_KIND::SetItemLayer:
        for( const AI_SESSION_HANDLE& handle : aOperation.m_ResolvedHandles )
        {
            auto it = m_ItemsByHandle.find( keyForHandle( handle ) );

            if( it == m_ItemsByHandle.end() || !it->second )
            {
                aError = wxS( "SetItemLayer references an unknown session handle." );
                return false;
            }

            if( args.contains( "layer_set" ) )
            {
                std::vector<PCB_LAYER_ID> layers =
                        layerListFromJson( board, args, "layer_set" );

                if( layers.empty() )
                {
                    aError = wxS( "SetItemLayer requires a valid layer_set." );
                    return false;
                }

                ZONE* zone = dynamic_cast<ZONE*>( it->second );

                if( !zone )
                {
                    aError = wxS( "SetItemLayer layer_set target must be a zone." );
                    return false;
                }

                LSET layerSet;

                for( PCB_LAYER_ID layer : layers )
                    layerSet.set( layer );

                stageModify( it->second );
                zone->SetLayerSet( layerSet );
                continue;
            }

            std::optional<PCB_LAYER_ID> layer =
                    resolveLayerName( board, stringField( args, "layer" ) );

            if( !layer )
            {
                aError = wxS( "SetItemLayer requires a valid layer." );
                return false;
            }

            stageModify( it->second );
            it->second->SetLayer( *layer );
        }

        return true;

    case AI_SESSION_OPERATION_KIND::SetItemProperties:
        if( !args.contains( "typed_props" ) || !args["typed_props"].is_object() )
        {
            aError = wxS( "SetItemProperties requires typed_props." );
            return false;
        }

        for( const AI_SESSION_HANDLE& handle : aOperation.m_ResolvedHandles )
        {
            auto it = m_ItemsByHandle.find( keyForHandle( handle ) );

            if( it == m_ItemsByHandle.end() || !it->second )
            {
                aError = wxS( "SetItemProperties references an unknown session handle." );
                return false;
            }

            stageModify( it->second );

            if( !applyTypedProperties( *it->second, args["typed_props"], aError ) )
                return false;
        }

        return true;

    case AI_SESSION_OPERATION_KIND::RefillZones:
    {
        std::vector<ZONE*> zones;
        const bool fillAll = args.contains( "all" ) && args["all"].is_boolean()
                             && args["all"].get<bool>();
        std::optional<BOX2I> affectedArea;

        if( args.contains( "affected_area" ) )
        {
            affectedArea = boxFromJson( args["affected_area"] );

            if( !affectedArea )
            {
                aError = wxS( "RefillZones affected_area must be a valid box." );
                return false;
            }
        }

        if( fillAll )
        {
            for( ZONE* zone : board.Zones() )
                zones.push_back( zone );
        }
        else if( affectedArea && aOperation.m_ResolvedHandles.empty() )
        {
            for( ZONE* zone : board.Zones() )
            {
                if( zone->GetBoundingBox().Intersects( *affectedArea ) )
                    zones.push_back( zone );
            }
        }
        else if( aOperation.m_ResolvedHandles.empty() )
        {
            for( ZONE* zone : board.Zones() )
                zones.push_back( zone );
        }
        else
        {
            for( const AI_SESSION_HANDLE& handle : aOperation.m_ResolvedHandles )
            {
                auto it = m_ItemsByHandle.find( keyForHandle( handle ) );

                if( it == m_ItemsByHandle.end() || !it->second )
                {
                    aError = wxS( "RefillZones references an unknown session handle." );
                    return false;
                }

                if( ZONE* zone = dynamic_cast<ZONE*>( it->second ) )
                    zones.push_back( zone );
            }
        }

        if( zones.empty() )
            return true;

        for( ZONE* zone : zones )
            zone->SetNeedRefill( true );

        ZONE_FILLER filler( &board, m_Commit.get() );

        if( !filler.Fill( zones, false, nullptr ) )
        {
            aError = wxS( "Native zone refill failed." );
            return false;
        }

        board.BuildConnectivity();
        return true;
    }

    case AI_SESSION_OPERATION_KIND::RebuildConnectivity:
        board.BuildConnectivity();
        return true;

    case AI_SESSION_OPERATION_KIND::SetMetadata:
    case AI_SESSION_OPERATION_KIND::RunValidation:
        return true;

    default:
        aError = wxString::Format( wxS( "Operation '%s' is not replayable by PCB accept." ),
                                   aOperation.OperationId() );
        return false;
    }

    if( !created )
        return false;

    addCreatedItem( created );

    for( const AI_SESSION_HANDLE& handle : aOperation.m_CreatedHandles )
        m_ItemsByHandle[keyForHandle( handle )] = created;

    return true;
}


bool KISURF_AI_PCB_SESSION_APPLY_ADAPTER::CommitTransaction( wxString& aError )
{
    if( m_DirectBoardApply )
    {
        m_ItemsByHandle.clear();
        return true;
    }

    if( !m_Commit )
    {
        aError = wxS( "AI session accept transaction is not open." );
        return false;
    }

    if( m_Commit->Empty() )
    {
        m_Commit.reset();
        m_ItemsByHandle.clear();
        return true;
    }

    m_Commit->Push( wxS( "Accept AI session" ) );
    m_Commit.reset();
    m_ItemsByHandle.clear();
    return true;
}


bool KISURF_AI_PCB_SESSION_APPLY_ADAPTER::HasBoardChanges() const
{
    if( m_DirectBoardApply )
        return m_DirectBoardMutated;

    return m_Commit && !m_Commit->Empty();
}


void KISURF_AI_PCB_SESSION_APPLY_ADAPTER::AbortTransaction()
{
    if( m_Commit )
    {
        m_Commit->Revert();
        m_Commit.reset();
    }

    m_ItemsByHandle.clear();
    m_DirectBoardMutated = false;
}
