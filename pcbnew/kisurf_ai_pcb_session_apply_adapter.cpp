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
#include <pad.h>
#include <nlohmann/json.hpp>
#include <pcb_edit_frame.h>
#include <pcb_shape.h>
#include <pcb_track.h>
#include <kisurf_ai_pcb_session_shadow_seeder.h>
#include <tool/tool_manager.h>
#include <zone.h>
#include <zone_filler.h>

#include <algorithm>
#include <cmath>
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


std::optional<uint64_t> uint64Field( const nlohmann::json& aArgs, const char* aName )
{
    if( !aArgs.contains( aName ) || !aArgs[aName].is_number_unsigned() )
        return std::nullopt;

    return aArgs[aName].get<uint64_t>();
}


std::optional<AI_SESSION_HANDLE> handleFromJson( const nlohmann::json& aHandle )
{
    if( !aHandle.is_object() )
        return std::nullopt;

    std::optional<uint64_t> sessionId = uint64Field( aHandle, "session_id" );
    std::optional<uint64_t> handleId = uint64Field( aHandle, "handle_id" );
    std::optional<uint64_t> generation = uint64Field( aHandle, "generation" );

    if( !sessionId || !handleId || !generation )
        return std::nullopt;

    AI_SESSION_HANDLE handle;
    handle.m_SessionId = *sessionId;
    handle.m_HandleId = *handleId;
    handle.m_Generation = *generation;

    if( aHandle.contains( "alias" ) && aHandle["alias"].is_string() )
    {
        handle.m_Alias = wxString::FromUTF8(
                aHandle["alias"].get_ref<const std::string&>().c_str() );
    }

    return handle;
}


std::optional<VECTOR2I> pointFromJson( const nlohmann::json& aPoint )
{
    if( aPoint.is_array() && aPoint.size() == 2 && aPoint[0].is_number()
        && aPoint[1].is_number() )
    {
        auto coordinateFromArrayNumber = []( const nlohmann::json& aValue ) -> int
        {
            const double value = aValue.get<double>();

            if( std::abs( value ) < 100000.0 )
                return pcbIUScale.mmToIU( value );

            return static_cast<int>( value );
        };

        return VECTOR2I( coordinateFromArrayNumber( aPoint[0] ),
                         coordinateFromArrayNumber( aPoint[1] ) );
    }

    if( !aPoint.is_object() || !aPoint.contains( "x" ) || !aPoint.contains( "y" )
        || !aPoint["x"].is_number() || !aPoint["y"].is_number() )
    {
        return std::nullopt;
    }

    return VECTOR2I( static_cast<int>( aPoint["x"].get<double>() ),
                     static_cast<int>( aPoint["y"].get<double>() ) );
}


int modelFacingCoordinateToInternal( const nlohmann::json& aValue )
{
    const double value = aValue.get<double>();

    if( std::abs( value ) < 100000.0 )
        return pcbIUScale.mmToIU( value );

    return static_cast<int>( value );
}


bool modelFacingUnitsAreMillimeters( const nlohmann::json& aPoint )
{
    if( !aPoint.is_object() || !aPoint.contains( "units" )
        || !aPoint["units"].is_string() )
    {
        return false;
    }

    wxString units = wxString::FromUTF8(
            aPoint["units"].get_ref<const std::string&>().c_str() );
    units.MakeLower();

    return units == wxS( "mm" ) || units == wxS( "millimeter" )
           || units == wxS( "millimeters" );
}


bool normalizeModelFacingPointInPlace( nlohmann::json& aValue )
{
    if( aValue.is_array() && aValue.size() == 2 && aValue[0].is_number()
        && aValue[1].is_number() )
    {
        aValue = { modelFacingCoordinateToInternal( aValue[0] ),
                   modelFacingCoordinateToInternal( aValue[1] ) };
        return true;
    }

    if( !aValue.is_object() )
        return false;

    if( aValue.contains( "x_mm" ) && aValue.contains( "y_mm" )
        && aValue["x_mm"].is_number() && aValue["y_mm"].is_number() )
    {
        aValue = {
            { "x", pcbIUScale.mmToIU( aValue["x_mm"].get<double>() ) },
            { "y", pcbIUScale.mmToIU( aValue["y_mm"].get<double>() ) }
        };
        return true;
    }

    if( aValue.contains( "x" ) && aValue.contains( "y" )
        && aValue["x"].is_number() && aValue["y"].is_number()
        && !aValue.contains( "uuid" ) && !aValue.contains( "alias" )
        && !aValue.contains( "session_id" ) && !aValue.contains( "handle_id" ) )
    {
        aValue["x"] = modelFacingUnitsAreMillimeters( aValue )
                              ? pcbIUScale.mmToIU( aValue["x"].get<double>() )
                              : modelFacingCoordinateToInternal( aValue["x"] );
        aValue["y"] = modelFacingUnitsAreMillimeters( aValue )
                              ? pcbIUScale.mmToIU( aValue["y"].get<double>() )
                              : modelFacingCoordinateToInternal( aValue["y"] );
        aValue.erase( "units" );
        return true;
    }

    return false;
}


void normalizeModelFacingPointTreeInPlace( nlohmann::json& aValue )
{
    if( normalizeModelFacingPointInPlace( aValue ) )
        return;

    if( aValue.is_object() )
    {
        for( auto& [key, value] : aValue.items() )
        {
            wxUnusedVar( key );
            normalizeModelFacingPointTreeInPlace( value );
        }
    }
    else if( aValue.is_array() )
    {
        for( nlohmann::json& value : aValue )
            normalizeModelFacingPointTreeInPlace( value );
    }
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


wxString aliasForCreatedOperation( const nlohmann::json& aArgs,
                                   const AI_SESSION_OPERATION_RECORD& aOperation )
{
    wxString alias = stringField( aArgs, "alias" );

    if( !alias.IsEmpty() )
        return alias;

    for( const AI_SESSION_HANDLE& handle : aOperation.m_CreatedHandles )
    {
        if( !handle.m_Alias.IsEmpty() )
            return handle.m_Alias;
    }

    return wxEmptyString;
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

    if( aShape.CmpNoCase( wxS( "polygon" ) ) == 0
        || aShape.CmpNoCase( wxS( "poly" ) ) == 0 )
    {
        return SHAPE_T::POLY;
    }

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


std::vector<VECTOR2I> pointsFromJsonArray( const nlohmann::json& aPoints );


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
    std::vector<VECTOR2I> points =
            geometry.contains( "points" ) ? pointsFromJsonArray( geometry["points"] )
                                          : std::vector<VECTOR2I>();

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
    else if( *shapeType == SHAPE_T::POLY )
    {
        if( points.size() < 3 )
        {
            aError = wxS( "CreateShape polygon requires at least three points." );
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
    else if( *shapeType == SHAPE_T::POLY )
    {
        shape->SetPolyPoints( points );
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
        if( shape->GetShape() == SHAPE_T::CIRCLE
            && ( aPatch.contains( "center" ) || aPatch.contains( "radius" ) ) )
        {
            std::optional<VECTOR2I> center =
                    aPatch.contains( "center" ) ? pointFromJson( aPatch["center"] )
                                                 : std::optional<VECTOR2I>( shape->GetStart() );
            std::optional<int> radius =
                    aPatch.contains( "radius" ) ? intField( aPatch, "radius" )
                                                : std::optional<int>( shape->GetRadius() );

            if( !center )
            {
                aError = wxS( "Circle geometry patch has an invalid center." );
                return false;
            }

            if( !radius || *radius <= 0 )
            {
                aError = wxS( "Circle geometry patch has an invalid radius." );
                return false;
            }

            shape->SetStart( *center );
            shape->SetEnd( VECTOR2I( center->x + *radius, center->y ) );
        }
        else if( shape->GetShape() == SHAPE_T::ARC
                 && ( aPatch.contains( "start" ) || aPatch.contains( "mid" )
                      || aPatch.contains( "end" ) ) )
        {
            std::optional<VECTOR2I> start =
                    aPatch.contains( "start" ) ? pointFromJson( aPatch["start"] )
                                               : std::optional<VECTOR2I>( shape->GetStart() );
            std::optional<VECTOR2I> mid =
                    aPatch.contains( "mid" ) ? pointFromJson( aPatch["mid"] )
                                             : std::optional<VECTOR2I>( shape->GetArcMid() );
            std::optional<VECTOR2I> end =
                    aPatch.contains( "end" ) ? pointFromJson( aPatch["end"] )
                                             : std::optional<VECTOR2I>( shape->GetEnd() );

            if( !start || !mid || !end )
            {
                aError = wxS( "Arc geometry patch requires valid start, mid, and end." );
                return false;
            }

            shape->SetArcGeometry( *start, *mid, *end );
        }
        else if( shape->GetShape() == SHAPE_T::POLY && aPatch.contains( "points" ) )
        {
            std::vector<VECTOR2I> points = pointsFromJsonArray( aPatch["points"] );

            if( points.size() < 3 )
            {
                aError = wxS( "Polygon geometry patch requires at least three points." );
                return false;
            }

            shape->SetPolyPoints( points );
        }
        else
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


AI_CURRENT_BOARD_TOOL_RESULT currentBoardError( const wxString& aCode,
                                                const wxString& aMessage )
{
    AI_CURRENT_BOARD_TOOL_RESULT result;
    result.m_ErrorCode = aCode;
    result.m_Message = aMessage;
    result.m_ResultJson = wxString::FromUTF8(
            nlohmann::json( { { "status", toUtf8String( aCode ) },
                              { "error_code", toUtf8String( aCode ) },
                              { "message", toUtf8String( aMessage ) },
                              { "board_mutated", false } } ).dump().c_str() );
    return result;
}


AI_CURRENT_BOARD_TOOL_RESULT currentBoardOk( const wxString& aMessage,
                                             nlohmann::json aPayload,
                                             bool aExecuted = false )
{
    AI_CURRENT_BOARD_TOOL_RESULT result;
    result.m_Ok = true;
    result.m_Executed = aExecuted;
    result.m_Message = aMessage;
    result.m_ResultJson = wxString::FromUTF8( aPayload.dump().c_str() );
    return result;
}


nlohmann::json directPointJson( const VECTOR2I& aPoint )
{
    return { { "x", aPoint.x }, { "y", aPoint.y } };
}


wxString directItemType( const BOARD_ITEM& aItem )
{
    if( dynamic_cast<const PCB_VIA*>( &aItem ) )
        return wxS( "via" );

    if( dynamic_cast<const PCB_TRACK*>( &aItem ) )
        return wxS( "track_segment" );

    if( dynamic_cast<const ZONE*>( &aItem ) )
        return wxS( "zone" );

    if( dynamic_cast<const FOOTPRINT*>( &aItem ) )
        return wxS( "footprint" );

    if( dynamic_cast<const PAD*>( &aItem ) )
        return wxS( "pad" );

    if( dynamic_cast<const PCB_SHAPE*>( &aItem ) )
        return wxS( "shape" );

    return wxS( "item" );
}


wxString directAlias( const BOARD& aBoard, const BOARD_ITEM& aItem )
{
    wxString alias = KisurfAiPcbLiveItemAlias( aBoard, aItem );

    if( alias.IsEmpty() )
        alias = wxS( "live:" ) + aItem.m_Uuid.AsString();

    return alias;
}


nlohmann::json directHandleJson( const BOARD& aBoard, const BOARD_ITEM& aItem )
{
    return { { "uuid", toUtf8String( aItem.m_Uuid.AsString() ) },
             { "type", toUtf8String( directItemType( aItem ) ) },
             { "alias", toUtf8String( directAlias( aBoard, aItem ) ) } };
}


wxString directLayerName( const BOARD_ITEM& aItem )
{
    if( aItem.GetLayer() >= 0 && aItem.GetLayer() < PCB_LAYER_ID_COUNT )
        return LSET::Name( aItem.GetLayer() );

    return wxEmptyString;
}


nlohmann::json directGeometryJson( const BOARD_ITEM& aItem )
{
    if( const PCB_VIA* via = dynamic_cast<const PCB_VIA*>( &aItem ) )
    {
        PCB_LAYER_ID topLayer = F_Cu;
        PCB_LAYER_ID bottomLayer = B_Cu;
        via->LayerPair( &topLayer, &bottomLayer );

        return { { "position", directPointJson( via->GetPosition() ) },
                 { "diameter", via->GetWidth( topLayer ) },
                 { "drill", via->GetDrillValue() },
                 { "layer_pair",
                   nlohmann::json::array(
                           { toUtf8String( LSET::Name( topLayer ) ),
                             toUtf8String( LSET::Name( bottomLayer ) ) } ) } };
    }

    if( const PCB_TRACK* track = dynamic_cast<const PCB_TRACK*>( &aItem ) )
    {
        return { { "start", directPointJson( track->GetStart() ) },
                 { "end", directPointJson( track->GetEnd() ) },
                 { "width", track->GetWidth() } };
    }

    if( const ZONE* zone = dynamic_cast<const ZONE*>( &aItem ) )
    {
        const BOX2I bbox = zone->GetBoundingBox();
        return { { "bbox",
                   { { "x", bbox.GetX() },
                     { "y", bbox.GetY() },
                     { "width", bbox.GetWidth() },
                     { "height", bbox.GetHeight() } } } };
    }

    if( const FOOTPRINT* footprint = dynamic_cast<const FOOTPRINT*>( &aItem ) )
    {
        return { { "position", directPointJson( footprint->GetPosition() ) },
                 { "reference", toUtf8String( footprint->GetReference() ) },
                 { "value", toUtf8String( footprint->GetValue() ) },
                 { "fp_id", toUtf8String( footprint->GetFPIDAsString() ) },
                 { "orientation_degrees", footprint->GetOrientationDegrees() },
                 { "is_placed", footprint->IsPlaced() },
                 { "pad_count", footprint->Pads().size() } };
    }

    if( const PAD* pad = dynamic_cast<const PAD*>( &aItem ) )
    {
        return { { "position", directPointJson( pad->GetPosition() ) },
                 { "number", toUtf8String( pad->GetNumber() ) },
                 { "orientation_degrees", pad->GetOrientationDegrees() },
                 { "has_hole", pad->HasHole() },
                 { "drill",
                   { { "x", pad->GetDrillSizeX() },
                     { "y", pad->GetDrillSizeY() } } } };
    }

    if( const PCB_SHAPE* shape = dynamic_cast<const PCB_SHAPE*>( &aItem ) )
    {
        return { { "start", directPointJson( shape->GetStart() ) },
                 { "end", directPointJson( shape->GetEnd() ) },
                 { "width", shape->GetWidth() },
                 { "fill", shape->IsAnyFill() } };
    }

    return nlohmann::json::object();
}


nlohmann::json directItemJson( const BOARD& aBoard, const BOARD_ITEM& aItem )
{
    nlohmann::json item = {
        { "handle", directHandleJson( aBoard, aItem ) },
        { "uuid", toUtf8String( aItem.m_Uuid.AsString() ) },
        { "type", toUtf8String( directItemType( aItem ) ) },
        { "alias", toUtf8String( directAlias( aBoard, aItem ) ) },
        { "geometry", directGeometryJson( aItem ) },
        { "selected", aItem.IsSelected() }
    };

    const wxString layer = directLayerName( aItem );

    if( !layer.IsEmpty() )
        item["layer"] = toUtf8String( layer );

    if( const BOARD_CONNECTED_ITEM* connected =
                dynamic_cast<const BOARD_CONNECTED_ITEM*>( &aItem ) )
    {
        item["net"] = toUtf8String( connected->GetNetname() );
    }

    return item;
}


void appendDirectBoardItems( BOARD& aBoard, std::vector<BOARD_ITEM*>& aItems )
{
    for( PCB_TRACK* track : aBoard.Tracks() )
        aItems.push_back( track );

    for( ZONE* zone : aBoard.Zones() )
        aItems.push_back( zone );

    for( FOOTPRINT* footprint : aBoard.Footprints() )
    {
        aItems.push_back( footprint );

        for( PAD* pad : footprint->Pads() )
            aItems.push_back( pad );
    }

    for( BOARD_ITEM* drawing : aBoard.Drawings() )
    {
        if( dynamic_cast<PCB_SHAPE*>( drawing ) )
            aItems.push_back( drawing );
    }
}


std::vector<wxString> directTypeAliases( wxString aType )
{
    aType.MakeLower();
    aType.Replace( wxS( "-" ), wxS( "_" ) );
    aType.Replace( wxS( " " ), wxS( "_" ) );

    std::vector<wxString> types;

    auto add = [&]( const wxString& aValue )
    {
        if( std::find( types.begin(), types.end(), aValue ) == types.end() )
            types.push_back( aValue );
    };

    if( aType == wxS( "routing" ) || aType == wxS( "route" )
        || aType == wxS( "routes" ) || aType == wxS( "routed_items" )
        || aType == wxS( "all_routing" ) )
    {
        add( wxS( "track_segment" ) );
        add( wxS( "via" ) );
        return types;
    }

    if( aType == wxS( "track" ) || aType == wxS( "tracks" )
        || aType == wxS( "trace" ) || aType == wxS( "traces" )
        || aType == wxS( "track_segments" ) || aType == wxS( "pcb_track" )
        || aType == wxS( "pcb_tracks" ) )
    {
        add( wxS( "track_segment" ) );
        return types;
    }

    if( aType == wxS( "vias" ) || aType == wxS( "pcb_via" )
        || aType == wxS( "pcb_vias" ) )
    {
        add( wxS( "via" ) );
        return types;
    }

    add( aType );
    return types;
}


bool directItemMatchesType( const BOARD_ITEM& aItem, const nlohmann::json& aType )
{
    if( !aType.is_string() && !aType.is_array() )
        return true;

    const wxString itemType = directItemType( aItem );

    auto matchesString = [&]( const std::string& aValue )
    {
        for( const wxString& type :
             directTypeAliases( wxString::FromUTF8( aValue.c_str() ) ) )
        {
            if( itemType == type )
                return true;
        }

        return false;
    };

    if( aType.is_string() )
        return matchesString( aType.get<std::string>() );

    for( const nlohmann::json& entry : aType )
    {
        if( entry.is_string() && matchesString( entry.get<std::string>() ) )
            return true;
    }

    return false;
}


bool directItemMatchesHandle( const BOARD& aBoard, const BOARD_ITEM& aItem,
                              const nlohmann::json& aHandle )
{
    if( aHandle.is_string() )
    {
        const wxString text = wxString::FromUTF8(
                aHandle.get_ref<const std::string&>().c_str() );
        return text == aItem.m_Uuid.AsString() || text == directAlias( aBoard, aItem );
    }

    if( !aHandle.is_object() )
        return false;

    if( aHandle.contains( "uuid" ) && aHandle["uuid"].is_string()
        && wxString::FromUTF8( aHandle["uuid"].get_ref<const std::string&>().c_str() )
                   == aItem.m_Uuid.AsString() )
    {
        return true;
    }

    if( aHandle.contains( "alias" ) && aHandle["alias"].is_string()
        && wxString::FromUTF8( aHandle["alias"].get_ref<const std::string&>().c_str() )
                   == directAlias( aBoard, aItem ) )
    {
        return true;
    }

    return false;
}


bool directLayerMatches( const BOARD_ITEM& aItem, const wxString& aLayer )
{
    if( aLayer.IsEmpty() )
        return true;

    if( directLayerName( aItem ) == aLayer )
        return true;

    wxString layerName = aLayer;
    const int layer = LSET::NameToLayer( layerName );

    if( layer < 0 || layer >= PCB_LAYER_ID_COUNT )
        return false;

    if( const ZONE* zone = dynamic_cast<const ZONE*>( &aItem ) )
        return zone->GetLayerSet().test( static_cast<PCB_LAYER_ID>( layer ) );

    if( const PAD* pad = dynamic_cast<const PAD*>( &aItem ) )
        return pad->GetLayerSet().test( static_cast<PCB_LAYER_ID>( layer ) );

    return false;
}


bool directItemMatchesFilter( const BOARD& aBoard, const BOARD_ITEM& aItem,
                              const nlohmann::json& aFilter )
{
    if( !aFilter.is_object() )
        return true;

    if( aFilter.contains( "type" )
        && !directItemMatchesType( aItem, aFilter["type"] ) )
    {
        return false;
    }

    if( aFilter.contains( "alias" ) && aFilter["alias"].is_string()
        && directAlias( aBoard, aItem )
                   != wxString::FromUTF8(
                           aFilter["alias"].get_ref<const std::string&>().c_str() ) )
    {
        return false;
    }

    if( aFilter.contains( "handle" )
        && !directItemMatchesHandle( aBoard, aItem, aFilter["handle"] ) )
    {
        return false;
    }

    if( aFilter.contains( "selection" ) && aFilter["selection"].is_boolean()
        && aItem.IsSelected() != aFilter["selection"].get<bool>() )
    {
        return false;
    }

    if( aFilter.contains( "layer" ) && aFilter["layer"].is_string()
        && !directLayerMatches(
                aItem, wxString::FromUTF8(
                               aFilter["layer"].get_ref<const std::string&>().c_str() ) ) )
    {
        return false;
    }

    if( aFilter.contains( "net" ) && aFilter["net"].is_string() )
    {
        const BOARD_CONNECTED_ITEM* connected =
                dynamic_cast<const BOARD_CONNECTED_ITEM*>( &aItem );

        if( !connected
            || connected->GetNetname()
                       != wxString::FromUTF8(
                               aFilter["net"].get_ref<const std::string&>().c_str() ) )
        {
            return false;
        }
    }

    if( aFilter.contains( "bbox" ) )
    {
        std::optional<BOX2I> filterBox = boxFromJson( aFilter["bbox"] );

        if( !filterBox || !aItem.GetBoundingBox().Intersects( *filterBox ) )
            return false;
    }

    return true;
}


std::vector<BOARD_ITEM*> queryDirectBoardItems( BOARD& aBoard,
                                                const nlohmann::json& aFilter )
{
    std::vector<BOARD_ITEM*> allItems;
    appendDirectBoardItems( aBoard, allItems );

    std::vector<BOARD_ITEM*> result;

    for( BOARD_ITEM* item : allItems )
    {
        if( item && directItemMatchesFilter( aBoard, *item, aFilter ) )
            result.push_back( item );
    }

    return result;
}


std::optional<BOARD_ITEM*> resolveDirectHandle( BOARD& aBoard,
                                                const nlohmann::json& aHandle )
{
    if( aHandle.is_string() )
    {
        wxString text = wxString::FromUTF8(
                aHandle.get_ref<const std::string&>().c_str() );

        if( text.StartsWith( wxS( "live:" ), &text ) )
        {
            if( BOARD_ITEM* item = aBoard.ResolveItem( KIID( text ), true ) )
                return item;
        }

        if( BOARD_ITEM* item = aBoard.ResolveItem( KIID( text ), true ) )
            return item;

        for( BOARD_ITEM* item : queryDirectBoardItems( aBoard, nlohmann::json::object() ) )
        {
            if( item && directAlias( aBoard, *item )
                               == wxString::FromUTF8(
                                       aHandle.get_ref<const std::string&>().c_str() ) )
            {
                return item;
            }
        }

        return std::nullopt;
    }

    if( !aHandle.is_object() )
        return std::nullopt;

    if( aHandle.contains( "uuid" ) && aHandle["uuid"].is_string() )
    {
        wxString uuid = wxString::FromUTF8(
                aHandle["uuid"].get_ref<const std::string&>().c_str() );

        if( BOARD_ITEM* item = aBoard.ResolveItem( KIID( uuid ), true ) )
            return item;
    }

    if( aHandle.contains( "alias" ) && aHandle["alias"].is_string() )
    {
        nlohmann::json alias = aHandle["alias"];
        return resolveDirectHandle( aBoard, alias );
    }

    return std::nullopt;
}


void appendDirectReference( BOARD& aBoard, const nlohmann::json& aValue,
                            std::vector<BOARD_ITEM*>& aItems )
{
    auto appendUnique = [&]( BOARD_ITEM* aItem )
    {
        if( aItem && std::find( aItems.begin(), aItems.end(), aItem ) == aItems.end() )
            aItems.push_back( aItem );
    };

    if( aValue.is_array() )
    {
        for( const nlohmann::json& entry : aValue )
        {
            if( std::optional<BOARD_ITEM*> item = resolveDirectHandle( aBoard, entry ) )
            {
                appendUnique( *item );
                continue;
            }

            if( entry.is_object() )
            {
                for( const char* key : { "handle", "item", "ref", "reference" } )
                {
                    if( entry.contains( key ) )
                    {
                        if( std::optional<BOARD_ITEM*> nested =
                                    resolveDirectHandle( aBoard, entry[key] ) )
                        {
                            appendUnique( *nested );
                            break;
                        }
                    }
                }
            }
        }
        return;
    }

    if( std::optional<BOARD_ITEM*> item = resolveDirectHandle( aBoard, aValue ) )
    {
        appendUnique( *item );
        return;
    }

    if( aValue.is_object() )
    {
        for( const char* key : { "handle", "item", "ref", "reference" } )
        {
            if( aValue.contains( key ) )
            {
                if( std::optional<BOARD_ITEM*> nested =
                            resolveDirectHandle( aBoard, aValue[key] ) )
                {
                    appendUnique( *nested );
                    return;
                }
            }
        }
    }
}


std::vector<BOARD_ITEM*> resolveDirectOperationItems( BOARD& aBoard,
                                                      const nlohmann::json& aArgs )
{
    std::vector<BOARD_ITEM*> items;

    for( const char* key : { "handles", "handle", "items", "item" } )
    {
        if( aArgs.contains( key ) )
            appendDirectReference( aBoard, aArgs[key], items );
    }

    if( aArgs.contains( "alias" ) && aArgs["alias"].is_string()
        && !aArgs.contains( "handles" ) && !aArgs.contains( "handle" )
        && !aArgs.contains( "items" ) && !aArgs.contains( "item" ) )
    {
        appendDirectReference( aBoard, aArgs["alias"], items );
    }

    if( aArgs.contains( "filter" ) && aArgs["filter"].is_object() )
    {
        std::vector<BOARD_ITEM*> filtered = queryDirectBoardItems( aBoard, aArgs["filter"] );

        for( BOARD_ITEM* item : filtered )
        {
            if( item && std::find( items.begin(), items.end(), item ) == items.end() )
                items.push_back( item );
        }
    }

    return items;
}


std::optional<VECTOR2I> directDeltaFromArgs( const nlohmann::json& aArgs )
{
    if( aArgs.contains( "delta" ) )
        return pointFromJson( aArgs["delta"] );

    if( aArgs.contains( "delta_x" ) && aArgs.contains( "delta_y" ) )
        return pointFromJson( { { "x", aArgs["delta_x"] }, { "y", aArgs["delta_y"] } } );

    if( aArgs.contains( "dx" ) && aArgs.contains( "dy" ) )
        return pointFromJson( { { "x", aArgs["dx"] }, { "y", aArgs["dy"] } } );

    return std::nullopt;
}


std::optional<VECTOR2I> directTargetForItem( const nlohmann::json& aArgs,
                                             BOARD& aBoard,
                                             const BOARD_ITEM& aItem,
                                             size_t aIndex, size_t aCount )
{
    auto entryTarget = [&]( const nlohmann::json& aEntry ) -> std::optional<VECTOR2I>
    {
        if( !aEntry.is_object() )
            return std::nullopt;

        for( const char* key : { "target_position", "position", "target",
                                 "target_point", "destination", "to", "move_to" } )
        {
            if( aEntry.contains( key ) )
                return pointFromJson( aEntry[key] );
        }

        return std::nullopt;
    };

    auto entryMatchesItem = [&]( const nlohmann::json& aEntry ) -> bool
    {
        if( std::optional<BOARD_ITEM*> item = resolveDirectHandle( aBoard, aEntry ) )
        {
            return *item == &aItem;
        }

        if( !aEntry.is_object() )
            return false;

        for( const char* key : { "handle", "item", "ref", "reference" } )
        {
            if( aEntry.contains( key ) )
            {
                if( std::optional<BOARD_ITEM*> item =
                            resolveDirectHandle( aBoard, aEntry[key] ) )
                {
                    return *item == &aItem;
                }
            }
        }

        return false;
    };

    if( aArgs.contains( "items" ) )
    {
        const nlohmann::json& modelItems = aArgs["items"];

        if( modelItems.is_array() )
        {
            for( const nlohmann::json& entry : modelItems )
            {
                if( entryMatchesItem( entry ) )
                {
                    if( std::optional<VECTOR2I> target = entryTarget( entry ) )
                        return target;
                }
            }
        }
        else if( entryMatchesItem( modelItems ) )
        {
            if( std::optional<VECTOR2I> target = entryTarget( modelItems ) )
                return target;
        }
    }

    const nlohmann::json* targetPositions = nullptr;

    for( const char* key : { "target_positions", "target_position", "position",
                             "target", "target_point", "destination", "to",
                             "move_to" } )
    {
        if( aArgs.contains( key ) )
        {
            targetPositions = &aArgs[key];
            break;
        }
    }

    if( !targetPositions )
        return std::nullopt;

    if( std::optional<VECTOR2I> point = pointFromJson( *targetPositions ) )
        return point;

    if( targetPositions->is_array() && aIndex < targetPositions->size() )
        return pointFromJson( ( *targetPositions )[aIndex] );

    if( targetPositions->is_object() )
    {
        const std::string uuid = toUtf8String( aItem.m_Uuid.AsString() );
        const std::string alias = toUtf8String( directAlias( aBoard, aItem ) );

        if( targetPositions->contains( uuid ) )
            return pointFromJson( ( *targetPositions )[uuid] );

        if( targetPositions->contains( alias ) )
            return pointFromJson( ( *targetPositions )[alias] );
    }

    wxUnusedVar( aCount );
    return std::nullopt;
}


nlohmann::json directItemArrayJson( const BOARD& aBoard,
                                    const std::vector<BOARD_ITEM*>& aItems )
{
    nlohmann::json items = nlohmann::json::array();

    for( BOARD_ITEM* item : aItems )
    {
        if( item )
            items.push_back( directItemJson( aBoard, *item ) );
    }

    return items;
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


void KISURF_AI_PCB_SESSION_APPLY_ADAPTER::seedOperationLiveItemHandleMap(
        const AI_SESSION_OPERATION_RECORD& aOperation )
{
    if( !m_Board || aOperation.m_ResultJson.IsEmpty() )
        return;

    nlohmann::json result =
            nlohmann::json::parse( toUtf8String( aOperation.m_ResultJson ), nullptr,
                                   false );

    if( result.is_discarded() || !result.is_object()
        || !result.contains( "resolved_items" )
        || !result["resolved_items"].is_array() )
    {
        return;
    }

    for( const nlohmann::json& item : result["resolved_items"] )
    {
        if( !item.is_object() || !item.contains( "handle" )
            || !item.contains( "live_uuid" )
            || !item["live_uuid"].is_string() )
        {
            continue;
        }

        std::optional<AI_SESSION_HANDLE> handle = handleFromJson( item["handle"] );

        if( !handle )
            continue;

        wxString liveUuid = wxString::FromUTF8(
                item["live_uuid"].get_ref<const std::string&>().c_str() );

        if( BOARD_ITEM* liveItem = m_Board->ResolveItem( KIID( liveUuid ), true ) )
            m_ItemsByHandle[keyForHandle( *handle )] = liveItem;
    }
}


bool KISURF_AI_PCB_SESSION_APPLY_ADAPTER::beginCurrentBoardToolTransaction(
        wxString& aError )
{
    if( m_Frame )
    {
        m_Board = m_Frame->GetBoard();
        m_ToolManager = m_Frame->GetToolManager();
    }

    if( !m_Board || ( !m_DirectBoardApply && !m_ToolManager ) )
    {
        aError = wxS( "PCB editor is not ready for current-board AI tools." );
        return false;
    }

    m_ItemsByHandle.clear();
    m_DirectBoardMutated = false;

    if( !m_DirectBoardApply )
        m_Commit = std::make_unique<BOARD_COMMIT>( m_ToolManager, true, false );

    return true;
}


bool KISURF_AI_PCB_SESSION_APPLY_ADAPTER::commitCurrentBoardToolTransaction(
        wxString& aError )
{
    if( m_DirectBoardApply )
        return true;

    if( !m_Commit )
    {
        aError = wxS( "Current-board AI tool transaction is not open." );
        return false;
    }

    if( !m_Commit->Empty() )
        m_Commit->Push( wxS( "AI chat current-board edit" ) );

    m_Commit.reset();
    return true;
}


void KISURF_AI_PCB_SESSION_APPLY_ADAPTER::abortCurrentBoardToolTransaction()
{
    if( m_Commit )
    {
        if( !m_Commit->Empty() )
            m_Commit->Revert();

        m_Commit.reset();
    }

    m_DirectBoardMutated = false;
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
    if( m_Board && aItem )
        KisurfAiPcbForgetLiveItemAlias( *m_Board, *aItem );

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

    seedOperationLiveItemHandleMap( aOperation );

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

    const wxString createdAlias = aliasForCreatedOperation( args, aOperation );

    if( !createdAlias.IsEmpty() )
        KisurfAiPcbRecordLiveItemAlias( board, *created, createdAlias );

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


AI_CURRENT_BOARD_TOOL_RESULT
KISURF_AI_PCB_SESSION_APPLY_ADAPTER::QueryCurrentBoardSummary()
{
    if( m_Frame )
        m_Board = m_Frame->GetBoard();

    if( !m_Board )
        return currentBoardError( wxS( "current_board_unavailable" ),
                                  wxS( "PCB editor has no current board." ) );

    BOARD& board = *m_Board;
    const size_t trackSegments =
            queryDirectBoardItems( board, { { "type", "tracks" } } ).size();
    const size_t vias =
            queryDirectBoardItems( board, { { "type", "vias" } } ).size();
    size_t namedNets = 0;

    for( NETINFO_ITEM* net : board.GetNetInfo() )
    {
        if( net && !net->GetNetname().IsEmpty() )
            ++namedNets;
    }

    nlohmann::json payload = {
        { "status", "board_summary" },
        { "summary",
          { { "items_total", queryDirectBoardItems( board, nlohmann::json::object() ).size() },
            { "vias", vias },
            { "track_segments", trackSegments },
            { "zones", board.Zones().size() },
            { "footprints", board.Footprints().size() },
            { "pads", board.GetPads().size() },
            { "nets", namedNets } } },
        { "board_mutated", false }
    };

    return currentBoardOk( wxS( "Current board summary returned." ),
                           std::move( payload ) );
}


AI_CURRENT_BOARD_TOOL_RESULT
KISURF_AI_PCB_SESSION_APPLY_ADAPTER::QueryCurrentBoardItems(
        const wxString& aFilterJson )
{
    if( m_Frame )
        m_Board = m_Frame->GetBoard();

    if( !m_Board )
        return currentBoardError( wxS( "current_board_unavailable" ),
                                  wxS( "PCB editor has no current board." ) );

    nlohmann::json filter =
            nlohmann::json::parse( toUtf8String( aFilterJson ), nullptr, false );

    if( filter.is_discarded() || !filter.is_object() )
        filter = nlohmann::json::object();

    std::vector<BOARD_ITEM*> items = queryDirectBoardItems( *m_Board, filter );
    nlohmann::json payload = {
        { "status", "items" },
        { "total_count", items.size() },
        { "returned_count", items.size() },
        { "truncated", false },
        { "filter", filter },
        { "items", directItemArrayJson( *m_Board, items ) },
        { "board_mutated", false }
    };

    return currentBoardOk( wxS( "Current board items returned." ),
                           std::move( payload ) );
}


AI_CURRENT_BOARD_TOOL_RESULT
KISURF_AI_PCB_SESSION_APPLY_ADAPTER::QueryCurrentBoardNets()
{
    if( m_Frame )
        m_Board = m_Frame->GetBoard();

    if( !m_Board )
        return currentBoardError( wxS( "current_board_unavailable" ),
                                  wxS( "PCB editor has no current board." ) );

    nlohmann::json nets = nlohmann::json::array();

    for( NETINFO_ITEM* net : m_Board->GetNetInfo() )
    {
        if( net && !net->GetNetname().IsEmpty() )
            nets.push_back( toUtf8String( net->GetNetname() ) );
    }

    std::sort( nets.begin(), nets.end() );

    return currentBoardOk( wxS( "Current board nets returned." ),
                           { { "status", "nets" },
                             { "nets", std::move( nets ) },
                             { "board_mutated", false } } );
}


AI_CURRENT_BOARD_TOOL_RESULT
KISURF_AI_PCB_SESSION_APPLY_ADAPTER::RunCurrentBoardAtomicOperation(
        AI_SESSION_OPERATION_KIND aKind, const wxString& aArgumentsJson )
{
    nlohmann::json args =
            nlohmann::json::parse( toUtf8String( aArgumentsJson ), nullptr, false );

    if( args.is_discarded() || !args.is_object() )
        return currentBoardError( wxS( "invalid_json" ),
                                  wxS( "Arguments must be a JSON object." ) );

    normalizeModelFacingPointTreeInPlace( args );

    wxString error;

    if( !beginCurrentBoardToolTransaction( error ) )
        return currentBoardError( wxS( "current_board_unavailable" ), error );

    BOARD& board = *m_Board;
    nlohmann::json createdItems = nlohmann::json::array();
    nlohmann::json resolvedItems = nlohmann::json::array();
    size_t appliedOperationCount = 0;

    auto fail = [&]( const wxString& aCode, const wxString& aMessage )
    {
        abortCurrentBoardToolTransaction();
        return currentBoardError( aCode, aMessage );
    };

    auto addCreated = [&]( BOARD_ITEM* aItem )
    {
        addCreatedItem( aItem );

        const wxString alias = aliasForCreatedOperation( args, AI_SESSION_OPERATION_RECORD() );

        if( !alias.IsEmpty() )
            KisurfAiPcbRecordLiveItemAlias( board, *aItem, alias );

        createdItems.push_back( directItemJson( board, *aItem ) );
        ++appliedOperationCount;
    };

    switch( aKind )
    {
    case AI_SESSION_OPERATION_KIND::CreateVia:
    {
        BOARD_ITEM* created = buildVia( board, args, error );

        if( !created )
            return fail( wxS( "apply_failed" ), error );

        addCreated( created );
        break;
    }

    case AI_SESSION_OPERATION_KIND::CreateTrackSegment:
    {
        BOARD_ITEM* created = buildTrackSegment( board, args, error );

        if( !created )
            return fail( wxS( "apply_failed" ), error );

        addCreated( created );
        break;
    }

    case AI_SESSION_OPERATION_KIND::CreateTrackPolyline:
    {
        if( !args.contains( "points" ) || !args["points"].is_array()
            || args["points"].size() < 2 )
        {
            return fail( wxS( "invalid_arguments" ),
                         wxS( "CreateTrackPolyline requires at least two points." ) );
        }

        const wxString alias = stringField( args, "alias" );

        for( size_t i = 0; i + 1 < args["points"].size(); ++i )
        {
            nlohmann::json segment = args;
            segment.erase( "points" );
            segment["start"] = args["points"][i];
            segment["end"] = args["points"][i + 1];

            if( !alias.IsEmpty() )
            {
                segment["alias"] =
                        toUtf8String( wxString::Format(
                                wxS( "%s:segment:%llu" ), alias,
                                static_cast<unsigned long long>( i ) ) );
            }

            BOARD_ITEM* created = buildTrackSegment( board, segment, error );

            if( !created )
                return fail( wxS( "apply_failed" ), error );

            addCreatedItem( created );

            const wxString segmentAlias = stringField( segment, "alias" );

            if( !segmentAlias.IsEmpty() )
                KisurfAiPcbRecordLiveItemAlias( board, *created, segmentAlias );

            createdItems.push_back( directItemJson( board, *created ) );
            ++appliedOperationCount;
        }

        break;
    }

    case AI_SESSION_OPERATION_KIND::CreateZone:
    case AI_SESSION_OPERATION_KIND::CreateShape:
    {
        BOARD_ITEM* created = aKind == AI_SESSION_OPERATION_KIND::CreateZone
                                      ? buildZone( board, args, error )
                                      : buildShape( board, args, error );

        if( !created )
            return fail( wxS( "apply_failed" ), error );

        addCreated( created );
        break;
    }

    case AI_SESSION_OPERATION_KIND::MoveItems:
    {
        std::vector<BOARD_ITEM*> items = resolveDirectOperationItems( board, args );

        if( items.empty() )
            return fail( wxS( "invalid_handle" ),
                         wxS( "MoveItems did not resolve any current-board items." ) );

        std::optional<VECTOR2I> delta = directDeltaFromArgs( args );
        std::vector<VECTOR2I> targets;

        if( !delta )
        {
            targets.reserve( items.size() );

            for( size_t i = 0; i < items.size(); ++i )
            {
                std::optional<VECTOR2I> target =
                        directTargetForItem( args, board, *items[i], i, items.size() );

                if( !target )
                    return fail( wxS( "invalid_arguments" ),
                                 wxS( "MoveItems requires delta or target position." ) );

                targets.push_back( *target );
            }
        }

        for( size_t i = 0; i < items.size(); ++i )
        {
            BOARD_ITEM* item = items[i];
            stageModify( item );

            if( delta )
            {
                item->Move( *delta );
            }
            else
            {
                item->Move( targets[i] - item->GetPosition() );
            }

            resolvedItems.push_back( directItemJson( board, *item ) );
        }

        appliedOperationCount = 1;
        break;
    }

    case AI_SESSION_OPERATION_KIND::DeleteItems:
    {
        std::vector<BOARD_ITEM*> items = resolveDirectOperationItems( board, args );

        if( items.empty() )
            return fail( wxS( "invalid_handle" ),
                         wxS( "DeleteItems did not resolve any current-board items." ) );

        resolvedItems = directItemArrayJson( board, items );

        for( BOARD_ITEM* item : items )
            removeItem( item );

        appliedOperationCount = 1;
        break;
    }

    case AI_SESSION_OPERATION_KIND::UpdateItemGeometry:
    case AI_SESSION_OPERATION_KIND::SetItemNet:
    case AI_SESSION_OPERATION_KIND::SetItemLayer:
    case AI_SESSION_OPERATION_KIND::SetItemProperties:
    {
        std::vector<BOARD_ITEM*> items = resolveDirectOperationItems( board, args );

        if( items.empty() )
            return fail( wxS( "invalid_handle" ),
                         wxS( "Operation did not resolve any current-board items." ) );

        for( BOARD_ITEM* item : items )
        {
            stageModify( item );

            if( aKind == AI_SESSION_OPERATION_KIND::UpdateItemGeometry )
            {
                if( !args.contains( "geometry_patch" )
                    || !args["geometry_patch"].is_object()
                    || !updateItemGeometry( *item, args["geometry_patch"], error ) )
                {
                    return fail( wxS( "apply_failed" ),
                                 error.IsEmpty()
                                         ? wxString( wxS( "UpdateItemGeometry failed." ) )
                                         : error );
                }
            }
            else if( aKind == AI_SESSION_OPERATION_KIND::SetItemNet )
            {
                BOARD_CONNECTED_ITEM* connected =
                        dynamic_cast<BOARD_CONNECTED_ITEM*>( item );

                if( !connected )
                    return fail( wxS( "apply_failed" ),
                                 wxS( "SetItemNet target is not connected." ) );

                NETINFO_ITEM* net = resolveOptionalNet( board, args, error );

                if( !net && !error.IsEmpty() )
                    return fail( wxS( "apply_failed" ), error );

                connected->SetNet( net );
            }
            else if( aKind == AI_SESSION_OPERATION_KIND::SetItemLayer )
            {
                std::optional<PCB_LAYER_ID> layer =
                        resolveLayerName( board, stringField( args, "layer" ) );

                if( !layer )
                    return fail( wxS( "apply_failed" ),
                                 wxS( "SetItemLayer requires a valid layer." ) );

                item->SetLayer( *layer );
            }
            else if( aKind == AI_SESSION_OPERATION_KIND::SetItemProperties )
            {
                if( !args.contains( "typed_props" )
                    || !args["typed_props"].is_object()
                    || !applyTypedProperties( *item, args["typed_props"], error ) )
                {
                    return fail( wxS( "apply_failed" ),
                                 error.IsEmpty()
                                         ? wxString( wxS( "SetItemProperties failed." ) )
                                         : error );
                }
            }

            resolvedItems.push_back( directItemJson( board, *item ) );
        }

        appliedOperationCount = 1;
        break;
    }

    case AI_SESSION_OPERATION_KIND::RefillZones:
    {
        std::vector<ZONE*> zones;
        const bool fillAll = args.value( "all", false );

        if( fillAll )
        {
            for( ZONE* zone : board.Zones() )
                zones.push_back( zone );
        }
        else
        {
            for( BOARD_ITEM* item : resolveDirectOperationItems( board, args ) )
            {
                if( ZONE* zone = dynamic_cast<ZONE*>( item ) )
                    zones.push_back( zone );
            }

            if( zones.empty() && args.contains( "affected_area" ) )
            {
                nlohmann::json filter = {
                    { "type", "zone" },
                    { "bbox", args["affected_area"] }
                };

                for( BOARD_ITEM* item : queryDirectBoardItems( board, filter ) )
                {
                    if( ZONE* zone = dynamic_cast<ZONE*>( item ) )
                        zones.push_back( zone );
                }
            }
        }

        for( ZONE* zone : zones )
            zone->SetNeedRefill( true );

        if( !zones.empty() )
        {
            ZONE_FILLER filler( &board, m_Commit.get() );

            if( !filler.Fill( zones, false, nullptr ) )
                return fail( wxS( "apply_failed" ),
                             wxS( "Native zone refill failed." ) );
        }

        board.BuildConnectivity();
        appliedOperationCount = zones.empty() ? 0 : 1;
        break;
    }

    case AI_SESSION_OPERATION_KIND::RebuildConnectivity:
        board.BuildConnectivity();
        appliedOperationCount = 1;
        break;

    case AI_SESSION_OPERATION_KIND::SetMetadata:
    case AI_SESSION_OPERATION_KIND::RunValidation:
        appliedOperationCount = 0;
        break;

    default:
        return fail( wxS( "unsupported_operation" ),
                     wxString::Format(
                             wxS( "Operation '%s' is not supported by current-board tools." ),
                             AiSessionOperationKindId( aKind ) ) );
    }

    if( !commitCurrentBoardToolTransaction( error ) )
        return fail( wxS( "commit_failed" ), error );

    const bool boardMutated = HasBoardChanges() || appliedOperationCount > 0;
    nlohmann::json payload = {
        { "status", "atomic_operation_executed" },
        { "kind", toUtf8String( AiSessionOperationKindId( aKind ) ) },
        { "arguments", args },
        { "board_mutated", boardMutated },
        { "created_items", std::move( createdItems ) },
        { "resolved_items", std::move( resolvedItems ) },
        { "current_board_apply",
          { { "status", "applied" },
            { "board_mutated", boardMutated },
            { "applied_operation_count", appliedOperationCount } } }
    };

    AI_CURRENT_BOARD_TOOL_RESULT result =
            currentBoardOk( wxS( "Applied directly to the current board." ),
                            std::move( payload ), true );
    return result;
}
