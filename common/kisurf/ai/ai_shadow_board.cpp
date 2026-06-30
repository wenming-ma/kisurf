#include <kisurf/ai/ai_shadow_board.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

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


wxString stringFromJson( const nlohmann::json& aValue )
{
    if( aValue.is_string() )
        return wxString::FromUTF8( aValue.get_ref<const std::string&>().c_str() );

    return fromJson( aValue );
}


void appendUniqueType( std::vector<wxString>& aTypes, const wxString& aType )
{
    if( aType.IsEmpty() )
        return;

    if( std::find( aTypes.begin(), aTypes.end(), aType ) == aTypes.end() )
        aTypes.push_back( aType );
}


std::vector<wxString> canonicalItemTypeFilters( const nlohmann::json& aValue )
{
    std::vector<wxString> types;

    if( aValue.is_array() )
    {
        for( const nlohmann::json& entry : aValue )
        {
            for( const wxString& type : canonicalItemTypeFilters( entry ) )
                appendUniqueType( types, type );
        }

        return types;
    }

    wxString type = stringFromJson( aValue );
    type.Trim( true );
    type.Trim( false );
    type.MakeLower();
    type.Replace( wxS( "-" ), wxS( "_" ) );
    type.Replace( wxS( " " ), wxS( "_" ) );

    if( type == wxS( "route" ) || type == wxS( "routes" )
        || type == wxS( "routing" ) || type == wxS( "all_routing" )
        || type == wxS( "routed_items" ) )
    {
        appendUniqueType( types, wxS( "track_segment" ) );
        appendUniqueType( types, wxS( "via" ) );
        return types;
    }

    if( type == wxS( "track" ) || type == wxS( "tracks" )
        || type == wxS( "trace" ) || type == wxS( "traces" )
        || type == wxS( "pcb_track" )
        || type == wxS( "pcb_tracks" ) || type == wxS( "track_segments" ) )
    {
        appendUniqueType( types, wxS( "track_segment" ) );
        return types;
    }

    if( type == wxS( "vias" ) || type == wxS( "pcb_via" )
        || type == wxS( "pcb_vias" ) )
    {
        appendUniqueType( types, wxS( "via" ) );
        return types;
    }

    if( type == wxS( "footprints" ) || type == wxS( "component" )
        || type == wxS( "components" ) || type == wxS( "part" )
        || type == wxS( "parts" ) )
    {
        appendUniqueType( types, wxS( "footprint" ) );
        return types;
    }

    if( type == wxS( "pads" ) )
    {
        appendUniqueType( types, wxS( "pad" ) );
        return types;
    }

    if( type == wxS( "zones" ) || type == wxS( "copper_zone" )
        || type == wxS( "copper_zones" ) || type == wxS( "copper_pour" )
        || type == wxS( "copper_pours" ) )
    {
        appendUniqueType( types, wxS( "zone" ) );
        return types;
    }

    if( type == wxS( "shapes" ) || type == wxS( "graphic" )
        || type == wxS( "graphics" ) )
    {
        appendUniqueType( types, wxS( "shape" ) );
        return types;
    }

    appendUniqueType( types, type );
    return types;
}


bool itemMatchesTypeFilter( const AI_SHADOW_ITEM& aItem,
                            const nlohmann::json& aTypeFilter )
{
    const std::vector<wxString> types = canonicalItemTypeFilters( aTypeFilter );
    return std::find( types.begin(), types.end(), aItem.m_Type ) != types.end();
}


bool sameHandle( const AI_SESSION_HANDLE& aLeft, const AI_SESSION_HANDLE& aRight )
{
    return aLeft.m_SessionId == aRight.m_SessionId
           && aLeft.m_HandleId == aRight.m_HandleId
           && aLeft.m_Generation == aRight.m_Generation;
}


bool layerMatches( const AI_SHADOW_ITEM& aItem, const wxString& aLayer )
{
    if( aItem.m_Layer == aLayer )
        return true;

    return std::find( aItem.m_Layers.begin(), aItem.m_Layers.end(), aLayer )
           != aItem.m_Layers.end();
}


bool itemIsSelected( const AI_SHADOW_ITEM& aItem )
{
    const auto it = aItem.m_Metadata.find( wxS( "selected" ) );

    if( it == aItem.m_Metadata.end() )
        return false;

    const wxString selected = it->second.Lower();
    return selected == wxS( "true" ) || selected == wxS( "1" )
           || selected == wxS( "yes" );
}


bool itemMatchesHandleFilter( const AI_SHADOW_ITEM& aItem,
                              const nlohmann::json& aHandleFilter )
{
    if( aHandleFilter.is_string() )
        return aItem.m_Alias == stringFromJson( aHandleFilter );

    if( aHandleFilter.is_number_unsigned() || aHandleFilter.is_number_integer() )
    {
        if( !aHandleFilter.is_number_unsigned() && aHandleFilter.get<int64_t>() < 0 )
            return false;

        return aItem.m_Handle.m_HandleId == aHandleFilter.get<uint64_t>();
    }

    if( !aHandleFilter.is_object() )
        return false;

    if( aHandleFilter.contains( "alias" ) && aHandleFilter["alias"].is_string()
        && aItem.m_Alias != stringFromJson( aHandleFilter["alias"] ) )
    {
        return false;
    }

    if( aHandleFilter.contains( "session_id" )
        && aHandleFilter["session_id"].is_number_unsigned()
        && aItem.m_Handle.m_SessionId != aHandleFilter["session_id"].get<uint64_t>() )
    {
        return false;
    }

    if( !aHandleFilter.contains( "handle_id" )
        || !aHandleFilter["handle_id"].is_number_unsigned()
        || aItem.m_Handle.m_HandleId != aHandleFilter["handle_id"].get<uint64_t>() )
    {
        return false;
    }

    if( aHandleFilter.contains( "generation" )
        && aHandleFilter["generation"].is_number_unsigned()
        && aItem.m_Handle.m_Generation != aHandleFilter["generation"].get<uint64_t>() )
    {
        return false;
    }

    return true;
}


bool metadataValueMatches( const wxString& aActual,
                           const nlohmann::json& aExpected )
{
    if( aExpected.is_boolean() )
    {
        const wxString actual = aActual.Lower();
        const bool actualBool = actual == wxS( "true" ) || actual == wxS( "1" )
                                || actual == wxS( "yes" );

        return actualBool == aExpected.get<bool>();
    }

    return aActual == stringFromJson( aExpected );
}


bool metadataMatches( const AI_SHADOW_ITEM& aItem,
                      const nlohmann::json& aMetadataFilter )
{
    if( !aMetadataFilter.is_object() )
        return false;

    for( const auto& [key, expected] : aMetadataFilter.items() )
    {
        const wxString metadataKey = wxString::FromUTF8( key.c_str() );
        const auto metadataIt = aItem.m_Metadata.find( metadataKey );

        if( metadataIt == aItem.m_Metadata.end() )
            return false;

        if( !metadataValueMatches( metadataIt->second, expected ) )
            return false;
    }

    return true;
}


struct SHADOW_BOX
{
    long long m_MinX = 0;
    long long m_MinY = 0;
    long long m_MaxX = 0;
    long long m_MaxY = 0;
};


std::optional<std::pair<long long, long long>> pointFromJson( const nlohmann::json& aPoint )
{
    if( !aPoint.is_object() || !aPoint.contains( "x" ) || !aPoint.contains( "y" )
        || !aPoint["x"].is_number() || !aPoint["y"].is_number() )
    {
        return std::nullopt;
    }

    return std::make_pair( static_cast<long long>( aPoint["x"].get<double>() ),
                           static_cast<long long>( aPoint["y"].get<double>() ) );
}


SHADOW_BOX normalizedBox( long long aX0, long long aY0, long long aX1, long long aY1 )
{
    SHADOW_BOX box;
    box.m_MinX = std::min( aX0, aX1 );
    box.m_MinY = std::min( aY0, aY1 );
    box.m_MaxX = std::max( aX0, aX1 );
    box.m_MaxY = std::max( aY0, aY1 );
    return box;
}


std::optional<SHADOW_BOX> boxFromJson( const nlohmann::json& aBox )
{
    if( !aBox.is_object() )
        return std::nullopt;

    if( aBox.contains( "x" ) && aBox.contains( "y" ) && aBox.contains( "width" )
        && aBox.contains( "height" ) && aBox["x"].is_number() && aBox["y"].is_number()
        && aBox["width"].is_number() && aBox["height"].is_number() )
    {
        const long long x = static_cast<long long>( aBox["x"].get<double>() );
        const long long y = static_cast<long long>( aBox["y"].get<double>() );
        const long long width = static_cast<long long>( aBox["width"].get<double>() );
        const long long height = static_cast<long long>( aBox["height"].get<double>() );
        return normalizedBox( x, y, x + width, y + height );
    }

    if( aBox.contains( "min" ) && aBox.contains( "max" ) )
    {
        std::optional<std::pair<long long, long long>> minPoint = pointFromJson( aBox["min"] );
        std::optional<std::pair<long long, long long>> maxPoint = pointFromJson( aBox["max"] );

        if( minPoint && maxPoint )
        {
            return normalizedBox( minPoint->first, minPoint->second,
                                  maxPoint->first, maxPoint->second );
        }
    }

    return std::nullopt;
}


void includePointInBox( SHADOW_BOX& aBox, bool& aHasPoint, long long aX, long long aY )
{
    if( !aHasPoint )
    {
        aBox = normalizedBox( aX, aY, aX, aY );
        aHasPoint = true;
        return;
    }

    aBox.m_MinX = std::min( aBox.m_MinX, aX );
    aBox.m_MinY = std::min( aBox.m_MinY, aY );
    aBox.m_MaxX = std::max( aBox.m_MaxX, aX );
    aBox.m_MaxY = std::max( aBox.m_MaxY, aY );
}


void collectGeometryBox( const nlohmann::json& aGeometry, SHADOW_BOX& aBox,
                         bool& aHasPoint )
{
    if( std::optional<std::pair<long long, long long>> point = pointFromJson( aGeometry ) )
        includePointInBox( aBox, aHasPoint, point->first, point->second );

    if( aGeometry.is_object() )
    {
        for( const auto& [key, value] : aGeometry.items() )
        {
            wxUnusedVar( key );
            collectGeometryBox( value, aBox, aHasPoint );
        }
    }
    else if( aGeometry.is_array() )
    {
        for( const nlohmann::json& value : aGeometry )
            collectGeometryBox( value, aBox, aHasPoint );
    }
}


std::optional<SHADOW_BOX> itemGeometryBox( const AI_SHADOW_ITEM& aItem )
{
    nlohmann::json geometry =
            nlohmann::json::parse( toUtf8String( aItem.m_GeometryJson ), nullptr, false );

    if( geometry.is_discarded() )
        return std::nullopt;

    SHADOW_BOX box;
    bool hasPoint = false;
    collectGeometryBox( geometry, box, hasPoint );

    if( !hasPoint )
        return std::nullopt;

    return box;
}


bool boxesIntersect( const SHADOW_BOX& aLeft, const SHADOW_BOX& aRight )
{
    return aLeft.m_MinX <= aRight.m_MaxX && aLeft.m_MaxX >= aRight.m_MinX
           && aLeft.m_MinY <= aRight.m_MaxY && aLeft.m_MaxY >= aRight.m_MinY;
}


void moveCoordinate( nlohmann::json& aCoordinate, long long aDelta )
{
    if( aCoordinate.is_number_integer() || aCoordinate.is_number_unsigned() )
        aCoordinate = aCoordinate.get<long long>() + aDelta;
    else if( aCoordinate.is_number() )
        aCoordinate = aCoordinate.get<double>() + static_cast<double>( aDelta );
}


void movePoint( nlohmann::json& aPoint, long long aDx, long long aDy )
{
    if( !aPoint.is_object() || !aPoint.contains( "x" ) || !aPoint.contains( "y" )
        || !aPoint["x"].is_number() || !aPoint["y"].is_number() )
    {
        return;
    }

    moveCoordinate( aPoint["x"], aDx );
    moveCoordinate( aPoint["y"], aDy );
}


void movePointArray( nlohmann::json& aPoints, long long aDx, long long aDy )
{
    if( !aPoints.is_array() )
        return;

    for( nlohmann::json& point : aPoints )
        movePoint( point, aDx, aDy );
}


void moveGeometryCoordinates( nlohmann::json& aGeometry, long long aDx, long long aDy )
{
    if( !aGeometry.is_object() )
        return;

    for( const char* key : { "position", "start", "end" } )
    {
        if( aGeometry.contains( key ) )
            movePoint( aGeometry[key], aDx, aDy );
    }

    for( const char* key : { "points", "outer", "inner" } )
    {
        if( aGeometry.contains( key ) )
            movePointArray( aGeometry[key], aDx, aDy );
    }

    if( aGeometry.contains( "holes" ) && aGeometry["holes"].is_array() )
    {
        for( nlohmann::json& hole : aGeometry["holes"] )
            movePointArray( hole, aDx, aDy );
    }

    if( aGeometry.contains( "outline" ) && aGeometry["outline"].is_object() )
        moveGeometryCoordinates( aGeometry["outline"], aDx, aDy );
}


void mergeObjectPatch( nlohmann::json& aTarget, const nlohmann::json& aPatch )
{
    for( const auto& [key, value] : aPatch.items() )
    {
        if( value.is_object() && aTarget.contains( key )
            && aTarget[key].is_object() )
        {
            mergeObjectPatch( aTarget[key], value );
        }
        else
        {
            aTarget[key] = value;
        }
    }
}
} // namespace


void AI_SHADOW_BOARD::UpsertItem( AI_SHADOW_ITEM aItem )
{
    if( !aItem.m_Handle.IsValid() )
        return;

    m_Items[aItem.m_Handle.m_HandleId] = std::move( aItem );
}


const AI_SHADOW_ITEM* AI_SHADOW_BOARD::FindItem(
        const AI_SESSION_HANDLE& aHandle ) const
{
    const auto it = m_Items.find( aHandle.m_HandleId );

    if( it == m_Items.end() || it->second.m_Deleted
        || !sameHandle( it->second.m_Handle, aHandle ) )
    {
        return nullptr;
    }

    return &it->second;
}


AI_SHADOW_ITEM* AI_SHADOW_BOARD::FindMutableItem(
        const AI_SESSION_HANDLE& aHandle )
{
    auto it = m_Items.find( aHandle.m_HandleId );

    if( it == m_Items.end() || it->second.m_Deleted
        || !sameHandle( it->second.m_Handle, aHandle ) )
    {
        return nullptr;
    }

    return &it->second;
}


std::vector<AI_SHADOW_ITEM> AI_SHADOW_BOARD::QueryItems(
        const wxString& aFilterJson ) const
{
    nlohmann::json filter = nlohmann::json::object();
    const std::string filterText = toUtf8String( aFilterJson );

    if( !filterText.empty() )
        filter = nlohmann::json::parse( filterText, nullptr, false );

    if( filter.is_discarded() || !filter.is_object() )
        filter = nlohmann::json::object();

    std::optional<SHADOW_BOX> filterBox;
    const bool hasBoxFilter = filter.contains( "bbox" );

    if( hasBoxFilter )
        filterBox = boxFromJson( filter["bbox"] );

    std::optional<bool> selectionFilter;

    if( filter.contains( "selection" ) && filter["selection"].is_boolean() )
        selectionFilter = filter["selection"].get<bool>();

    std::vector<AI_SHADOW_ITEM> result;

    for( const auto& [id, item] : m_Items )
    {
        wxUnusedVar( id );

        if( item.m_Deleted )
            continue;

        if( filter.contains( "type" )
            && !itemMatchesTypeFilter( item, filter["type"] ) )
        {
            continue;
        }

        if( filter.contains( "net" ) && item.m_Net != stringFromJson( filter["net"] ) )
            continue;

        if( filter.contains( "layer" )
            && !layerMatches( item, stringFromJson( filter["layer"] ) ) )
        {
            continue;
        }

        if( filter.contains( "alias" )
            && item.m_Alias != stringFromJson( filter["alias"] ) )
        {
            continue;
        }

        if( filter.contains( "handle" )
            && !itemMatchesHandleFilter( item, filter["handle"] ) )
        {
            continue;
        }

        if( filter.contains( "metadata" )
            && !metadataMatches( item, filter["metadata"] ) )
        {
            continue;
        }

        if( selectionFilter && itemIsSelected( item ) != *selectionFilter )
            continue;

        if( hasBoxFilter )
        {
            std::optional<SHADOW_BOX> itemBox = itemGeometryBox( item );

            if( !filterBox || !itemBox || !boxesIntersect( *itemBox, *filterBox ) )
                continue;
        }

        result.push_back( item );
    }

    return result;
}


wxString AI_SHADOW_BOARD::QueryBoardSummary() const
{
    nlohmann::json payload = {
        { "items_total", LiveItemCount() },
        { "vias", LiveItemCountByType( wxS( "via" ) ) },
        { "track_segments", LiveItemCountByType( wxS( "track_segment" ) ) },
        { "zones", LiveItemCountByType( wxS( "zone" ) ) },
        { "shapes", LiveItemCountByType( wxS( "shape" ) ) },
        { "footprints", LiveItemCountByType( wxS( "footprint" ) ) },
        { "pads", LiveItemCountByType( wxS( "pad" ) ) },
        { "nets", QueryNets().size() }
    };

    return fromJson( payload );
}


void AI_SHADOW_BOARD::SetNets( std::vector<wxString> aNets )
{
    std::vector<wxString> nets;

    for( wxString net : aNets )
    {
        net.Trim( true );
        net.Trim( false );

        if( net.IsEmpty() )
            continue;

        if( std::find( nets.begin(), nets.end(), net ) == nets.end() )
            nets.push_back( net );
    }

    std::sort( nets.begin(), nets.end() );
    m_Nets = std::move( nets );
}


std::vector<wxString> AI_SHADOW_BOARD::QueryNets() const
{
    std::vector<wxString> nets = m_Nets;

    for( const auto& [id, item] : m_Items )
    {
        wxUnusedVar( id );

        if( item.m_Deleted || item.m_Net.IsEmpty() )
            continue;

        if( std::find( nets.begin(), nets.end(), item.m_Net ) == nets.end() )
            nets.push_back( item.m_Net );
    }

    std::sort( nets.begin(), nets.end() );
    return nets;
}


size_t AI_SHADOW_BOARD::LiveItemCount() const
{
    size_t count = 0;

    for( const auto& [id, item] : m_Items )
    {
        wxUnusedVar( id );

        if( !item.m_Deleted )
            ++count;
    }

    return count;
}


size_t AI_SHADOW_BOARD::LiveItemCountByType( const wxString& aType ) const
{
    size_t count = 0;

    for( const auto& [id, item] : m_Items )
    {
        wxUnusedVar( id );

        if( !item.m_Deleted && item.m_Type == aType )
            ++count;
    }

    return count;
}


bool AI_SHADOW_BOARD::UpdateGeometry( const AI_SESSION_HANDLE& aHandle,
                                      wxString aGeometryJson, uint64_t aEpoch )
{
    AI_SHADOW_ITEM* item = FindMutableItem( aHandle );

    if( !item )
        return false;

    nlohmann::json existing =
            nlohmann::json::parse( toUtf8String( item->m_GeometryJson ), nullptr, false );
    nlohmann::json patch =
            nlohmann::json::parse( toUtf8String( aGeometryJson ), nullptr, false );

    if( patch.is_discarded() || !patch.is_object() )
        return false;

    if( existing.is_discarded() || !existing.is_object() )
        existing = nlohmann::json::object();

    mergeObjectPatch( existing, patch );

    item->m_GeometryJson = fromJson( existing );
    item->m_UpdatedEpoch = aEpoch;
    return true;
}


bool AI_SHADOW_BOARD::MoveItem( const AI_SESSION_HANDLE& aHandle,
                                const wxString& aDeltaJson, uint64_t aEpoch )
{
    AI_SHADOW_ITEM* item = FindMutableItem( aHandle );

    if( !item )
        return false;

    nlohmann::json geometry =
            nlohmann::json::parse( toUtf8String( item->m_GeometryJson ), nullptr, false );
    nlohmann::json delta =
            nlohmann::json::parse( toUtf8String( aDeltaJson ), nullptr, false );

    if( geometry.is_discarded() || !geometry.is_object() || delta.is_discarded()
        || !delta.is_object() || !delta.contains( "x" ) || !delta.contains( "y" ) )
    {
        return false;
    }

    const long long dx = delta["x"].get<long long>();
    const long long dy = delta["y"].get<long long>();

    moveGeometryCoordinates( geometry, dx, dy );

    item->m_GeometryJson = fromJson( geometry );
    item->m_UpdatedEpoch = aEpoch;
    return true;
}


bool AI_SHADOW_BOARD::MoveItemTo( const AI_SESSION_HANDLE& aHandle,
                                  const wxString& aTargetJson, uint64_t aEpoch )
{
    AI_SHADOW_ITEM* item = FindMutableItem( aHandle );

    if( !item )
        return false;

    nlohmann::json geometry =
            nlohmann::json::parse( toUtf8String( item->m_GeometryJson ), nullptr, false );
    nlohmann::json target =
            nlohmann::json::parse( toUtf8String( aTargetJson ), nullptr, false );

    if( geometry.is_discarded() || !geometry.is_object() || target.is_discarded()
        || !target.is_object() || !target.contains( "x" ) || !target.contains( "y" ) )
    {
        return false;
    }

    if( !geometry.contains( "position" ) || !geometry["position"].is_object() )
        return false;

    geometry["position"] = target;
    item->m_GeometryJson = fromJson( geometry );
    item->m_UpdatedEpoch = aEpoch;
    return true;
}


bool AI_SHADOW_BOARD::DeleteItem( const AI_SESSION_HANDLE& aHandle, uint64_t aEpoch )
{
    AI_SHADOW_ITEM* item = FindMutableItem( aHandle );

    if( !item )
        return false;

    item->m_Deleted = true;
    item->m_UpdatedEpoch = aEpoch;
    return true;
}


bool AI_SHADOW_BOARD::SetItemNet( const AI_SESSION_HANDLE& aHandle, wxString aNet,
                                  uint64_t aEpoch )
{
    AI_SHADOW_ITEM* item = FindMutableItem( aHandle );

    if( !item )
        return false;

    item->m_Net = std::move( aNet );
    item->m_UpdatedEpoch = aEpoch;
    return true;
}


bool AI_SHADOW_BOARD::SetItemLayer( const AI_SESSION_HANDLE& aHandle, wxString aLayer,
                                    uint64_t aEpoch )
{
    AI_SHADOW_ITEM* item = FindMutableItem( aHandle );

    if( !item )
        return false;

    item->m_Layer = std::move( aLayer );
    item->m_Layers.clear();
    item->m_Layers.push_back( item->m_Layer );
    item->m_UpdatedEpoch = aEpoch;
    return true;
}


bool AI_SHADOW_BOARD::SetItemLayers( const AI_SESSION_HANDLE& aHandle,
                                     std::vector<wxString> aLayers, uint64_t aEpoch )
{
    AI_SHADOW_ITEM* item = FindMutableItem( aHandle );

    if( !item || aLayers.empty() )
        return false;

    item->m_Layers = std::move( aLayers );
    item->m_Layer = item->m_Layers.front();
    item->m_UpdatedEpoch = aEpoch;
    return true;
}


bool AI_SHADOW_BOARD::SetItemProperties( const AI_SESSION_HANDLE& aHandle,
                                         wxString aPropertiesJson, uint64_t aEpoch )
{
    AI_SHADOW_ITEM* item = FindMutableItem( aHandle );

    if( !item )
        return false;

    nlohmann::json existing =
            nlohmann::json::parse( toUtf8String( item->m_PropertiesJson ), nullptr, false );
    nlohmann::json patch =
            nlohmann::json::parse( toUtf8String( aPropertiesJson ), nullptr, false );

    if( patch.is_discarded() || !patch.is_object() )
        return false;

    if( existing.is_discarded() || !existing.is_object() )
        existing = nlohmann::json::object();

    for( const auto& [key, value] : patch.items() )
        existing[key] = value;

    item->m_PropertiesJson = fromJson( existing );
    item->m_UpdatedEpoch = aEpoch;
    return true;
}


bool AI_SHADOW_BOARD::SetMetadata( const AI_SESSION_HANDLE& aHandle,
                                   const std::map<wxString, wxString>& aKeyValues,
                                   uint64_t aEpoch )
{
    AI_SHADOW_ITEM* item = FindMutableItem( aHandle );

    if( !item )
        return false;

    for( const auto& [key, value] : aKeyValues )
        item->m_Metadata[key] = value;

    item->m_UpdatedEpoch = aEpoch;
    return true;
}


void AI_SHADOW_BOARD::CaptureCheckpoint( const AI_SESSION_CHECKPOINT& aCheckpoint )
{
    CHECKPOINT_STATE state;
    state.m_Checkpoint = aCheckpoint;
    state.m_Items = m_Items;
    m_Checkpoints.push_back( std::move( state ) );
}


bool AI_SHADOW_BOARD::RollbackTo( const AI_SESSION_CHECKPOINT& aCheckpoint )
{
    const auto it = std::find_if( m_Checkpoints.begin(), m_Checkpoints.end(),
                                  [&]( const CHECKPOINT_STATE& aState )
                                  {
                                      return aState.m_Checkpoint.m_Id == aCheckpoint.m_Id;
                                  } );

    if( it == m_Checkpoints.end() )
        return false;

    m_Items = it->m_Items;
    m_Checkpoints.erase(
            std::remove_if( m_Checkpoints.begin(), m_Checkpoints.end(),
                            [&]( const CHECKPOINT_STATE& aState )
                            {
                                return aState.m_Checkpoint.m_Epoch
                                       > aCheckpoint.m_Epoch;
                            } ),
            m_Checkpoints.end() );
    return true;
}
