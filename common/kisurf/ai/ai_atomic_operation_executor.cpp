#include <kisurf/ai/ai_atomic_operation_executor.h>
#include <kisurf/ai/ai_shadow_board.h>

#include <nlohmann/json.hpp>

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


wxString stringField( const nlohmann::json& aJson, const char* aKey )
{
    if( !aJson.contains( aKey ) || !aJson[aKey].is_string() )
        return wxEmptyString;

    return wxString::FromUTF8( aJson[aKey].get_ref<const std::string&>().c_str() );
}


wxString valueToWxString( const nlohmann::json& aValue )
{
    if( aValue.is_string() )
        return wxString::FromUTF8( aValue.get_ref<const std::string&>().c_str() );

    return fromJson( aValue );
}


AI_ATOMIC_EXECUTION_RESULT errorResult( const wxString& aCode, const wxString& aMessage )
{
    AI_ATOMIC_EXECUTION_RESULT result;
    result.m_ErrorCode = aCode;
    result.m_Message = aMessage;
    return result;
}


std::optional<AI_SESSION_HANDLE> resolveHandle( const AI_EXECUTION_SESSION& aSession,
                                                const nlohmann::json& aHandleJson )
{
    if( aHandleJson.is_string() )
        return aSession.ResolveAlias(
                wxString::FromUTF8( aHandleJson.get_ref<const std::string&>().c_str() ) );

    AI_SESSION_HANDLE handle;
    handle.m_SessionId = aSession.SessionId();
    handle.m_Generation = 1;

    if( aHandleJson.is_number_unsigned() )
    {
        handle.m_HandleId = aHandleJson.get<uint64_t>();
    }
    else if( aHandleJson.is_object() )
    {
        if( aHandleJson.contains( "session_id" ) )
            handle.m_SessionId = aHandleJson["session_id"].get<uint64_t>();

        if( !aHandleJson.contains( "handle_id" ) )
            return std::nullopt;

        handle.m_HandleId = aHandleJson["handle_id"].get<uint64_t>();

        if( aHandleJson.contains( "generation" ) )
            handle.m_Generation = aHandleJson["generation"].get<uint64_t>();

        if( aHandleJson.contains( "alias" ) && aHandleJson["alias"].is_string() )
        {
            handle.m_Alias = wxString::FromUTF8(
                    aHandleJson["alias"].get_ref<const std::string&>().c_str() );
        }
    }
    else
    {
        return std::nullopt;
    }

    if( aSession.ResolveHandle( handle ) != AI_SESSION_HANDLE_STATUS::Live )
        return std::nullopt;

    if( handle.m_Alias.IsEmpty() )
    {
        if( const AI_SHADOW_ITEM* item = aSession.ShadowBoard().FindItem( handle ) )
            handle.m_Alias = item->m_Alias;
    }

    return handle;
}


std::vector<AI_SESSION_HANDLE> resolveHandles( const AI_EXECUTION_SESSION& aSession,
                                               const nlohmann::json& aArgs )
{
    std::vector<AI_SESSION_HANDLE> handles;

    if( aArgs.contains( "handle" ) )
    {
        if( std::optional<AI_SESSION_HANDLE> handle = resolveHandle( aSession, aArgs["handle"] ) )
            handles.push_back( *handle );
    }

    if( aArgs.contains( "handles" ) && aArgs["handles"].is_array() )
    {
        for( const nlohmann::json& entry : aArgs["handles"] )
        {
            if( std::optional<AI_SESSION_HANDLE> handle = resolveHandle( aSession, entry ) )
                handles.push_back( *handle );
        }
    }

    return handles;
}


std::map<wxString, wxString> metadataFromJson( const nlohmann::json& aJson,
                                               const char* aField )
{
    std::map<wxString, wxString> metadata;

    if( !aJson.contains( aField ) || !aJson[aField].is_object() )
        return metadata;

    for( const auto& [key, value] : aJson[aField].items() )
        metadata[wxString::FromUTF8( key.c_str() )] = valueToWxString( value );

    return metadata;
}


nlohmann::json typedPropertiesFromArgs( AI_SESSION_OPERATION_KIND aKind,
                                        const nlohmann::json& aArgs )
{
    nlohmann::json properties = nlohmann::json::object();
    std::vector<const char*> keys;

    switch( aKind )
    {
    case AI_SESSION_OPERATION_KIND::CreateVia:
        keys = { "diameter", "drill", "layer_pair" };
        break;

    case AI_SESSION_OPERATION_KIND::CreateTrackSegment:
        keys = { "width" };
        break;

    case AI_SESSION_OPERATION_KIND::CreateZone:
        keys = { "clearance", "priority", "fill_mode" };
        break;

    case AI_SESSION_OPERATION_KIND::CreateShape:
        keys = { "shape_type", "width", "fill" };
        break;

    default:
        break;
    }

    for( const char* key : keys )
    {
        if( aArgs.contains( key ) )
            properties[key] = aArgs[key];
    }

    return properties;
}


bool zoneFillModeNameIsValid( wxString aMode )
{
    aMode = aMode.Lower();
    aMode.Replace( wxS( "-" ), wxS( "_" ) );
    aMode.Replace( wxS( " " ), wxS( "_" ) );

    return aMode == wxS( "solid" ) || aMode == wxS( "polygon" )
           || aMode == wxS( "polygons" ) || aMode == wxS( "hatch" )
           || aMode == wxS( "hatched" ) || aMode == wxS( "hatch_pattern" )
           || aMode == wxS( "copper_thieving" ) || aMode == wxS( "thieving" );
}


std::optional<AI_ATOMIC_EXECUTION_RESULT> validateCreateZoneTypedProperties(
        const nlohmann::json& aArgs )
{
    if( aArgs.contains( "clearance" )
        && ( !aArgs["clearance"].is_number()
             || aArgs["clearance"].get<double>() < 0.0 ) )
    {
        return errorResult( wxS( "invalid_arguments" ),
                            wxS( "CreateZone clearance must be a non-negative number." ) );
    }

    if( aArgs.contains( "priority" )
        && ( !aArgs["priority"].is_number() || aArgs["priority"].get<double>() < 0.0 ) )
    {
        return errorResult( wxS( "invalid_arguments" ),
                            wxS( "CreateZone priority must be a non-negative number." ) );
    }

    if( aArgs.contains( "fill_mode" ) )
    {
        wxString fillMode = stringField( aArgs, "fill_mode" );

        if( fillMode.IsEmpty() || !zoneFillModeNameIsValid( fillMode ) )
        {
            return errorResult(
                    wxS( "invalid_arguments" ),
                    wxS( "CreateZone fill_mode must be solid, hatch_pattern, or copper_thieving." ) );
        }
    }

    return std::nullopt;
}


std::optional<AI_ATOMIC_EXECUTION_RESULT> validateTypedPropertyPatch(
        const AI_SHADOW_ITEM& aItem, const nlohmann::json& aProps )
{
    if( !aProps.is_object() )
    {
        return errorResult( wxS( "invalid_arguments" ),
                            wxS( "typed_props must be a JSON object." ) );
    }

    if( aItem.m_Type == wxS( "zone" ) )
        return validateCreateZoneTypedProperties( aProps );

    if( aItem.m_Type == wxS( "via" ) )
    {
        for( const char* key : { "diameter", "drill" } )
        {
            if( aProps.contains( key )
                && ( !aProps[key].is_number() || aProps[key].get<double>() <= 0.0 ) )
            {
                return errorResult(
                        wxS( "invalid_arguments" ),
                        wxString::Format( wxS( "Via %s must be a positive number." ),
                                          wxString::FromUTF8( key ) ) );
            }
        }
    }
    else if( aItem.m_Type == wxS( "track_segment" ) )
    {
        if( aProps.contains( "width" )
            && ( !aProps["width"].is_number() || aProps["width"].get<double>() <= 0.0 ) )
        {
            return errorResult( wxS( "invalid_arguments" ),
                                wxS( "Track width must be a positive number." ) );
        }
    }
    else if( aItem.m_Type == wxS( "shape" ) )
    {
        if( aProps.contains( "width" )
            && ( !aProps["width"].is_number() || aProps["width"].get<double>() < 0.0 ) )
        {
            return errorResult( wxS( "invalid_arguments" ),
                                wxS( "Shape width must be a non-negative number." ) );
        }

        if( aProps.contains( "fill" ) && !aProps["fill"].is_boolean() )
        {
            return errorResult( wxS( "invalid_arguments" ),
                                wxS( "Shape fill must be a boolean." ) );
        }
    }

    return std::nullopt;
}


std::vector<wxString> layerPairFromJson( const nlohmann::json& aArgs )
{
    std::vector<wxString> layers;

    if( aArgs.contains( "layer_pair" ) && aArgs["layer_pair"].is_array() )
    {
        for( const nlohmann::json& layer : aArgs["layer_pair"] )
            layers.push_back( valueToWxString( layer ) );
    }
    else if( aArgs.contains( "layer_set" ) && aArgs["layer_set"].is_array() )
    {
        for( const nlohmann::json& layer : aArgs["layer_set"] )
            layers.push_back( valueToWxString( layer ) );
    }
    else if( aArgs.contains( "layer" ) )
    {
        layers.push_back( valueToWxString( aArgs["layer"] ) );
    }

    return layers;
}


std::vector<wxString> layerSetFromJson( const nlohmann::json& aArgs )
{
    std::vector<wxString> layers;

    if( !aArgs.contains( "layer_set" ) || !aArgs["layer_set"].is_array() )
        return layers;

    for( const nlohmann::json& layer : aArgs["layer_set"] )
        layers.push_back( valueToWxString( layer ) );

    return layers;
}


void appendRecord( AI_EXECUTION_SESSION& aSession, AI_ATOMIC_EXECUTION_RESULT& aResult,
                   AI_SESSION_OPERATION_KIND aKind, const nlohmann::json& aArgs,
                   const std::vector<AI_SESSION_HANDLE>& aResolved,
                   const std::vector<AI_SESSION_HANDLE>& aCreated,
                   std::vector<wxString> aWarnings = {},
                   wxString aResultJson = wxS( "{}" ) )
{
    AI_SESSION_OPERATION_RECORD record;
    record.m_Kind = aKind;
    record.m_ArgumentsJson = fromJson( aArgs );
    record.m_ResolvedHandles = aResolved;
    record.m_CreatedHandles = aCreated;
    record.m_Warnings = aWarnings;
    record.m_ResultJson = aResultJson;

    if( record.m_ResultJson.IsEmpty() )
        record.m_ResultJson = wxS( "{}" );

    aResult.m_Warnings = std::move( aWarnings );
    aResult.m_ResultJson = record.m_ResultJson;

    const AI_SESSION_OPERATION_RECORD& appended = aSession.AppendOperation( std::move( record ) );
    aResult.m_OperationIds.push_back( appended.m_Id );
}


AI_SHADOW_ITEM makeCreateItem( const AI_SESSION_HANDLE& aHandle,
                               AI_SESSION_OPERATION_KIND aKind, const nlohmann::json& aArgs,
                               const wxString& aType, const nlohmann::json& aGeometry,
                               uint64_t aEpoch )
{
    AI_SHADOW_ITEM item;
    item.m_Handle = aHandle;
    item.m_CreatedBy = aKind;
    item.m_Type = aType;
    item.m_Alias = aHandle.m_Alias;
    item.m_Net = stringField( aArgs, "net" );
    item.m_Layer = stringField( aArgs, "layer" );
    item.m_Layers = layerPairFromJson( aArgs );

    if( item.m_Layer.IsEmpty() && !item.m_Layers.empty() )
        item.m_Layer = item.m_Layers.front();

    item.m_GeometryJson = fromJson( aGeometry );
    item.m_PropertiesJson = fromJson( typedPropertiesFromArgs( aKind, aArgs ) );
    item.m_Metadata = metadataFromJson( aArgs, "metadata" );
    item.m_CreatedEpoch = aEpoch;
    item.m_UpdatedEpoch = aEpoch;
    return item;
}


AI_ATOMIC_EXECUTION_RESULT createVia( AI_EXECUTION_SESSION& aSession,
                                      const nlohmann::json& aArgs )
{
    if( !aArgs.contains( "position" ) || !aArgs["position"].is_object() )
        return errorResult( wxS( "invalid_arguments" ), wxS( "CreateVia requires position." ) );

    AI_ATOMIC_EXECUTION_RESULT result;
    AI_SESSION_HANDLE handle = aSession.CreateHandle( stringField( aArgs, "alias" ) );
    result.m_CreatedHandles.push_back( handle );

    nlohmann::json geometry;
    geometry["position"] = aArgs["position"];

    for( const char* key : { "diameter", "drill", "layer_pair" } )
    {
        if( aArgs.contains( key ) )
            geometry[key] = aArgs[key];
    }

    appendRecord( aSession, result, AI_SESSION_OPERATION_KIND::CreateVia, aArgs, {},
                  { handle } );
    aSession.ShadowBoard().UpsertItem(
            makeCreateItem( handle, AI_SESSION_OPERATION_KIND::CreateVia, aArgs,
                            wxS( "via" ), geometry, aSession.Epoch() ) );
    result.m_Ok = true;
    return result;
}


AI_ATOMIC_EXECUTION_RESULT createTrackSegment( AI_EXECUTION_SESSION& aSession,
                                               const nlohmann::json& aArgs )
{
    if( !aArgs.contains( "start" ) || !aArgs.contains( "end" ) )
    {
        return errorResult( wxS( "invalid_arguments" ),
                            wxS( "CreateTrackSegment requires start and end." ) );
    }

    AI_ATOMIC_EXECUTION_RESULT result;
    AI_SESSION_HANDLE handle = aSession.CreateHandle( stringField( aArgs, "alias" ) );
    result.m_CreatedHandles.push_back( handle );

    nlohmann::json geometry;
    geometry["start"] = aArgs["start"];
    geometry["end"] = aArgs["end"];

    if( aArgs.contains( "width" ) )
        geometry["width"] = aArgs["width"];

    appendRecord( aSession, result, AI_SESSION_OPERATION_KIND::CreateTrackSegment,
                  aArgs, {}, { handle } );
    aSession.ShadowBoard().UpsertItem(
            makeCreateItem( handle, AI_SESSION_OPERATION_KIND::CreateTrackSegment,
                            aArgs, wxS( "track_segment" ), geometry, aSession.Epoch() ) );
    result.m_Ok = true;
    return result;
}


AI_ATOMIC_EXECUTION_RESULT createTrackPolyline( AI_EXECUTION_SESSION& aSession,
                                                const nlohmann::json& aArgs )
{
    if( !aArgs.contains( "points" ) || !aArgs["points"].is_array()
        || aArgs["points"].size() < 2 )
    {
        return errorResult( wxS( "invalid_arguments" ),
                            wxS( "CreateTrackPolyline requires at least two points." ) );
    }

    AI_ATOMIC_EXECUTION_RESULT result;
    const wxString alias = stringField( aArgs, "alias" );

    for( size_t i = 0; i + 1 < aArgs["points"].size(); ++i )
    {
        nlohmann::json segment = aArgs;
        segment.erase( "points" );
        segment["start"] = aArgs["points"][i];
        segment["end"] = aArgs["points"][i + 1];

        if( !alias.IsEmpty() )
        {
            segment["alias"] =
                    toUtf8String( wxString::Format( wxS( "%s:segment:%llu" ),
                                                     alias,
                                                     static_cast<unsigned long long>( i ) ) );
        }

        AI_ATOMIC_EXECUTION_RESULT segmentResult = createTrackSegment( aSession, segment );

        if( !segmentResult.m_Ok )
            return segmentResult;

        result.m_CreatedHandles.insert( result.m_CreatedHandles.end(),
                                        segmentResult.m_CreatedHandles.begin(),
                                        segmentResult.m_CreatedHandles.end() );
        result.m_OperationIds.insert( result.m_OperationIds.end(),
                                      segmentResult.m_OperationIds.begin(),
                                      segmentResult.m_OperationIds.end() );
    }

    result.m_Ok = true;
    return result;
}


AI_ATOMIC_EXECUTION_RESULT createSimpleItem( AI_EXECUTION_SESSION& aSession,
                                             AI_SESSION_OPERATION_KIND aKind,
                                             const nlohmann::json& aArgs,
                                             const wxString& aType,
                                             const char* aGeometryField )
{
    if( aKind == AI_SESSION_OPERATION_KIND::CreateZone )
    {
        if( std::optional<AI_ATOMIC_EXECUTION_RESULT> error =
                    validateCreateZoneTypedProperties( aArgs ) )
        {
            return *error;
        }
    }

    if( !aArgs.contains( aGeometryField ) )
    {
        return errorResult( wxS( "invalid_arguments" ),
                            wxString::Format( wxS( "%s requires %s." ),
                                              AiSessionOperationKindId( aKind ),
                                              wxString::FromUTF8( aGeometryField ) ) );
    }

    AI_ATOMIC_EXECUTION_RESULT result;
    AI_SESSION_HANDLE handle = aSession.CreateHandle( stringField( aArgs, "alias" ) );
    result.m_CreatedHandles.push_back( handle );

    nlohmann::json geometry = aArgs.contains( aGeometryField )
                                      ? aArgs[aGeometryField]
                                      : aArgs;

    appendRecord( aSession, result, aKind, aArgs, {}, { handle } );
    aSession.ShadowBoard().UpsertItem(
            makeCreateItem( handle, aKind, aArgs, aType, geometry, aSession.Epoch() ) );
    result.m_Ok = true;
    return result;
}


std::optional<nlohmann::json> targetPositionForHandle(
        const nlohmann::json& aTargetPositions,
        const AI_SESSION_HANDLE& aHandle,
        size_t aIndex,
        size_t aHandleCount )
{
    if( aTargetPositions.is_object() && aTargetPositions.contains( "x" )
        && aTargetPositions.contains( "y" ) && aHandleCount == 1 )
    {
        return aTargetPositions;
    }

    if( aTargetPositions.is_array() && aIndex < aTargetPositions.size()
        && aTargetPositions[aIndex].is_object() )
    {
        return aTargetPositions[aIndex];
    }

    if( aTargetPositions.is_object() )
    {
        const std::string handleId =
                std::to_string( static_cast<unsigned long long>( aHandle.m_HandleId ) );

        if( aTargetPositions.contains( handleId )
            && aTargetPositions[handleId].is_object() )
        {
            return aTargetPositions[handleId];
        }

        if( !aHandle.m_Alias.IsEmpty() )
        {
            const std::string alias = toUtf8String( aHandle.m_Alias );

            if( aTargetPositions.contains( alias )
                && aTargetPositions[alias].is_object() )
            {
                return aTargetPositions[alias];
            }
        }
    }

    return std::nullopt;
}


bool jsonPointHasIntegerCoordinates( const nlohmann::json& aPoint )
{
    return aPoint.is_object() && aPoint.contains( "x" ) && aPoint.contains( "y" )
           && aPoint["x"].is_number_integer() && aPoint["y"].is_number_integer();
}


bool canMoveItemByDelta( const AI_EXECUTION_SESSION& aSession,
                         const AI_SESSION_HANDLE& aHandle,
                         const nlohmann::json& aDelta )
{
    const AI_SHADOW_ITEM* item = aSession.ShadowBoard().FindItem( aHandle );

    if( !item || !jsonPointHasIntegerCoordinates( aDelta ) )
        return false;

    nlohmann::json geometry =
            nlohmann::json::parse( toUtf8String( item->m_GeometryJson ), nullptr, false );

    return !geometry.is_discarded() && geometry.is_object();
}


bool canMoveItemToTarget( const AI_EXECUTION_SESSION& aSession,
                          const AI_SESSION_HANDLE& aHandle,
                          const nlohmann::json& aTarget )
{
    const AI_SHADOW_ITEM* item = aSession.ShadowBoard().FindItem( aHandle );

    if( !item || !jsonPointHasIntegerCoordinates( aTarget ) )
        return false;

    nlohmann::json geometry =
            nlohmann::json::parse( toUtf8String( item->m_GeometryJson ), nullptr, false );

    return !geometry.is_discarded() && geometry.is_object()
           && geometry.contains( "position" ) && geometry["position"].is_object();
}


bool canApplyHandleMutation( const AI_EXECUTION_SESSION& aSession,
                             AI_SESSION_OPERATION_KIND aKind,
                             const nlohmann::json& aArgs,
                             const AI_SESSION_HANDLE& aHandle,
                             size_t aIndex, size_t aHandleCount )
{
    if( !aSession.ShadowBoard().FindItem( aHandle ) )
        return false;

    switch( aKind )
    {
    case AI_SESSION_OPERATION_KIND::MoveItems:
        if( aArgs.contains( "delta" ) )
            return canMoveItemByDelta( aSession, aHandle, aArgs["delta"] );

        if( aArgs.contains( "target_positions" ) )
        {
            std::optional<nlohmann::json> target = targetPositionForHandle(
                    aArgs["target_positions"], aHandle, aIndex, aHandleCount );
            return target.has_value()
                   && canMoveItemToTarget( aSession, aHandle, *target );
        }

        return false;

    case AI_SESSION_OPERATION_KIND::DeleteItems:
        return true;

    case AI_SESSION_OPERATION_KIND::UpdateItemGeometry:
        return aArgs.contains( "geometry_patch" );

    case AI_SESSION_OPERATION_KIND::SetItemNet:
        return aArgs.contains( "net" );

    case AI_SESSION_OPERATION_KIND::SetItemLayer:
        return aArgs.contains( "layer" ) || !layerSetFromJson( aArgs ).empty();

    case AI_SESSION_OPERATION_KIND::SetItemProperties:
        return aArgs.contains( "typed_props" );

    case AI_SESSION_OPERATION_KIND::SetMetadata:
        return aArgs.contains( "key_values" ) && aArgs["key_values"].is_object();

    default:
        return false;
    }
}


AI_ATOMIC_EXECUTION_RESULT mutateHandles( AI_EXECUTION_SESSION& aSession,
                                          AI_SESSION_OPERATION_KIND aKind,
                                          const nlohmann::json& aArgs )
{
    std::vector<AI_SESSION_HANDLE> handles = resolveHandles( aSession, aArgs );

    if( handles.empty() )
        return errorResult( wxS( "invalid_handle" ), wxS( "No live handle resolved." ) );

    for( size_t i = 0; i < handles.size(); ++i )
    {
        if( aKind == AI_SESSION_OPERATION_KIND::UpdateItemGeometry )
        {
            if( !aArgs.contains( "geometry_patch" ) || !aArgs["geometry_patch"].is_object() )
            {
                return errorResult( wxS( "invalid_arguments" ),
                                    wxS( "UpdateItemGeometry requires geometry_patch object." ) );
            }
        }

        if( aKind == AI_SESSION_OPERATION_KIND::SetItemProperties )
        {
            if( !aArgs.contains( "typed_props" ) )
            {
                return errorResult( wxS( "invalid_arguments" ),
                                    wxS( "SetItemProperties requires typed_props." ) );
            }

            const AI_SHADOW_ITEM* item = aSession.ShadowBoard().FindItem( handles[i] );

            if( item )
            {
                if( std::optional<AI_ATOMIC_EXECUTION_RESULT> error =
                            validateTypedPropertyPatch( *item, aArgs["typed_props"] ) )
                {
                    return *error;
                }
            }
        }

        if( !canApplyHandleMutation( aSession, aKind, aArgs, handles[i], i,
                                     handles.size() ) )
        {
            return errorResult( wxS( "mutation_failed" ),
                                wxS( "Atomic mutation could not update shadow board." ) );
        }
    }

    AI_ATOMIC_EXECUTION_RESULT result;
    result.m_ResolvedHandles = handles;

    appendRecord( aSession, result, aKind, aArgs, handles, {} );
    const uint64_t epoch = aSession.Epoch();

    for( size_t i = 0; i < handles.size(); ++i )
    {
        const AI_SESSION_HANDLE& handle = handles[i];
        bool ok = false;

        switch( aKind )
        {
        case AI_SESSION_OPERATION_KIND::MoveItems:
            if( aArgs.contains( "delta" ) )
            {
                ok = aSession.ShadowBoard().MoveItem( handle, fromJson( aArgs["delta"] ),
                                                       epoch );
            }
            else if( aArgs.contains( "target_positions" ) )
            {
                std::optional<nlohmann::json> target = targetPositionForHandle(
                        aArgs["target_positions"], handle, i, handles.size() );
                ok = target.has_value()
                     && aSession.ShadowBoard().MoveItemTo( handle, fromJson( *target ),
                                                           epoch );
            }
            break;

        case AI_SESSION_OPERATION_KIND::DeleteItems:
            ok = aSession.ShadowBoard().DeleteItem( handle, epoch );
            break;

        case AI_SESSION_OPERATION_KIND::UpdateItemGeometry:
            ok = aArgs.contains( "geometry_patch" )
                 && aSession.ShadowBoard().UpdateGeometry( handle,
                         fromJson( aArgs["geometry_patch"] ), epoch );
            break;

        case AI_SESSION_OPERATION_KIND::SetItemNet:
            ok = aArgs.contains( "net" )
                 && aSession.ShadowBoard().SetItemNet( handle,
                         valueToWxString( aArgs["net"] ), epoch );
            break;

        case AI_SESSION_OPERATION_KIND::SetItemLayer:
            if( aArgs.contains( "layer_set" ) )
            {
                ok = aSession.ShadowBoard().SetItemLayers(
                        handle, layerSetFromJson( aArgs ), epoch );
            }
            else
            {
                ok = aArgs.contains( "layer" )
                     && aSession.ShadowBoard().SetItemLayer(
                             handle, valueToWxString( aArgs["layer"] ), epoch );
            }
            break;

        case AI_SESSION_OPERATION_KIND::SetItemProperties:
            ok = aArgs.contains( "typed_props" )
                 && aSession.ShadowBoard().SetItemProperties( handle,
                         fromJson( aArgs["typed_props"] ), epoch );
            break;

        case AI_SESSION_OPERATION_KIND::SetMetadata:
            ok = aSession.ShadowBoard().SetMetadata( handle,
                    metadataFromJson( aArgs, "key_values" ), epoch );
            break;

        default:
            break;
        }

        if( !ok )
        {
            return errorResult( wxS( "mutation_failed" ),
                                wxS( "Atomic mutation could not update shadow board." ) );
        }
    }

    result.m_Ok = true;
    return result;
}


wxString validationLevelFromArgs( const nlohmann::json& aArgs )
{
    if( !aArgs.contains( "level" ) )
        return wxS( "geometry" );

    return stringField( aArgs, "level" );
}


bool isSupportedValidationLevel( const wxString& aLevel )
{
    return aLevel == wxS( "geometry" ) || aLevel == wxS( "drc_lite" )
           || aLevel == wxS( "full_drc" );
}


size_t surfacePatchOperationCount( const nlohmann::json& aPatch )
{
    if( aPatch.contains( "operations" ) && aPatch["operations"].is_array() )
        return aPatch["operations"].size();

    if( aPatch.contains( "ops" ) && aPatch["ops"].is_array() )
        return aPatch["ops"].size();

    if( aPatch.contains( "changes" ) && aPatch["changes"].is_array() )
        return aPatch["changes"].size();

    return 0;
}


AI_ATOMIC_EXECUTION_RESULT applySurfacePatch( AI_EXECUTION_SESSION& aSession,
                                              const nlohmann::json& aArgs )
{
    if( stringField( aArgs, "surface_id" ).IsEmpty() )
    {
        return errorResult( wxS( "invalid_arguments" ),
                            wxS( "ApplySurfacePatch requires surface_id." ) );
    }

    if( !aArgs.contains( "patch" ) || !aArgs["patch"].is_object() )
    {
        return errorResult( wxS( "invalid_arguments" ),
                            wxS( "ApplySurfacePatch requires a patch object." ) );
    }

    const nlohmann::json& patch = aArgs["patch"];
    const size_t          operationCount = surfacePatchOperationCount( patch );

    if( operationCount == 0 )
    {
        return errorResult(
                wxS( "invalid_arguments" ),
                wxS( "ApplySurfacePatch requires at least one patch operation." ) );
    }

    nlohmann::json resultJson =
            { { "operation", "surface.apply_patch" },
              { "status", "surface_patch_recorded" },
              { "surface_id", aArgs["surface_id"] },
              { "patch", patch },
              { "patch_operation_count", operationCount },
              { "shadow_only", true },
              { "board_mutated", false },
              { "direct_publish", false },
              { "publish_allowed", false } };

    if( aArgs.contains( "table_id" ) )
        resultJson["table_id"] = aArgs["table_id"];

    if( aArgs.contains( "target_scope" ) )
        resultJson["target_scope"] = aArgs["target_scope"];

    if( aArgs.contains( "alias" ) )
        resultJson["alias"] = aArgs["alias"];

    for( const char* key : { "expected_surface_revision",
                             "expected_schema_version",
                             "expected_selection_fingerprint",
                             "expected_overlap_set" } )
    {
        if( aArgs.contains( key ) )
            resultJson[key] = aArgs[key];
    }

    std::vector<wxString> warnings = {
        wxS( "SurfacePatch is recorded in the hidden session journal; native "
             "structured-surface apply is not connected yet." )
    };

    AI_ATOMIC_EXECUTION_RESULT result;
    appendRecord( aSession, result, AI_SESSION_OPERATION_KIND::ApplySurfacePatch,
                  aArgs, {}, {}, warnings, fromJson( resultJson ) );
    result.m_Ok = true;
    return result;
}


std::vector<wxString> maintenanceWarnings( AI_SESSION_OPERATION_KIND aKind,
                                           const nlohmann::json& aArgs )
{
    std::vector<wxString> warnings;

    switch( aKind )
    {
    case AI_SESSION_OPERATION_KIND::RunValidation:
    {
        const wxString level = validationLevelFromArgs( aArgs );

        if( level == wxS( "drc_lite" ) || level == wxS( "full_drc" ) )
        {
            warnings.push_back(
                    wxString::Format(
                            wxS( "%s validation is recorded for replay; native DRC "
                                 "engine integration is not connected in this runtime yet." ),
                            level ) );
        }

        break;
    }

    case AI_SESSION_OPERATION_KIND::RefillZones:
        warnings.push_back(
                wxS( "Zone refill is recorded for accept replay; the shadow-board step "
                     "does not execute the native zone filler." ) );
        break;

    case AI_SESSION_OPERATION_KIND::RebuildConnectivity:
        warnings.push_back(
                wxS( "Connectivity rebuild is recorded for accept replay; the shadow-board "
                     "step does not rebuild native board connectivity." ) );
        break;

    case AI_SESSION_OPERATION_KIND::Unknown:
    case AI_SESSION_OPERATION_KIND::Checkpoint:
    case AI_SESSION_OPERATION_KIND::RollbackTo:
    case AI_SESSION_OPERATION_KIND::QueryBoardSummary:
    case AI_SESSION_OPERATION_KIND::QueryItems:
    case AI_SESSION_OPERATION_KIND::QueryItem:
    case AI_SESSION_OPERATION_KIND::QuerySelection:
    case AI_SESSION_OPERATION_KIND::QueryNets:
    case AI_SESSION_OPERATION_KIND::QueryLayers:
    case AI_SESSION_OPERATION_KIND::QueryDesignRules:
    case AI_SESSION_OPERATION_KIND::QueryViewport:
    case AI_SESSION_OPERATION_KIND::QueryActivityTimeline:
    case AI_SESSION_OPERATION_KIND::RenderPreview:
    case AI_SESSION_OPERATION_KIND::ObserveStep:
    case AI_SESSION_OPERATION_KIND::CreateVia:
    case AI_SESSION_OPERATION_KIND::CreateTrackSegment:
    case AI_SESSION_OPERATION_KIND::CreateTrackPolyline:
    case AI_SESSION_OPERATION_KIND::CreateZone:
    case AI_SESSION_OPERATION_KIND::CreateShape:
    case AI_SESSION_OPERATION_KIND::MoveItems:
    case AI_SESSION_OPERATION_KIND::DeleteItems:
    case AI_SESSION_OPERATION_KIND::UpdateItemGeometry:
    case AI_SESSION_OPERATION_KIND::SetItemNet:
    case AI_SESSION_OPERATION_KIND::SetItemLayer:
    case AI_SESSION_OPERATION_KIND::SetItemProperties:
    case AI_SESSION_OPERATION_KIND::SetMetadata:
    default:
        break;
    }

    return warnings;
}


nlohmann::json warningsJson( const std::vector<wxString>& aWarnings )
{
    nlohmann::json warnings = nlohmann::json::array();

    for( const wxString& warning : aWarnings )
        warnings.push_back( toUtf8String( warning ) );

    return warnings;
}


std::string stringFieldOr( const nlohmann::json& aArgs, const char* aName,
                           const char* aFallback )
{
    if( aArgs.contains( aName ) && aArgs[aName].is_string() )
        return aArgs[aName].get_ref<const std::string&>();

    return aFallback;
}


nlohmann::json handleJson( const AI_SESSION_HANDLE& aHandle )
{
    nlohmann::json handle = {
        { "session_id", aHandle.m_SessionId },
        { "handle_id", aHandle.m_HandleId },
        { "generation", aHandle.m_Generation }
    };

    if( !aHandle.m_Alias.IsEmpty() )
        handle["alias"] = toUtf8String( aHandle.m_Alias );

    return handle;
}


std::optional<std::pair<long long, long long>> validationPointFromJson(
        const nlohmann::json& aPoint )
{
    if( !aPoint.is_object() || !aPoint.contains( "x" ) || !aPoint.contains( "y" )
        || !aPoint["x"].is_number() || !aPoint["y"].is_number() )
    {
        return std::nullopt;
    }

    return std::make_pair( static_cast<long long>( aPoint["x"].get<double>() ),
                           static_cast<long long>( aPoint["y"].get<double>() ) );
}


void appendGeometryIssue( nlohmann::json& aIssues, const AI_SHADOW_ITEM& aItem,
                          const char* aCode, const char* aMessage )
{
    nlohmann::json issue = {
        { "code", aCode },
        { "severity", "error" },
        { "message", aMessage },
        { "handle", handleJson( aItem.m_Handle ) }
    };

    if( !aItem.m_Alias.IsEmpty() )
        issue["alias"] = toUtf8String( aItem.m_Alias );

    aIssues.push_back( std::move( issue ) );
}


nlohmann::json geometryValidationIssues( const AI_EXECUTION_SESSION& aSession )
{
    nlohmann::json issues = nlohmann::json::array();

    for( const AI_SHADOW_ITEM& item : aSession.ShadowBoard().QueryItems( wxS( "{}" ) ) )
    {
        nlohmann::json geometry =
                nlohmann::json::parse( toUtf8String( item.m_GeometryJson ), nullptr, false );

        if( geometry.is_discarded() || !geometry.is_object() )
        {
            appendGeometryIssue( issues, item, "invalid_geometry_json",
                                 "Item geometry is not a valid JSON object." );
            continue;
        }

        if( item.m_Type == wxS( "track_segment" ) || item.m_Type == wxS( "shape" ) )
        {
            std::optional<std::pair<long long, long long>> start =
                    geometry.contains( "start" )
                            ? validationPointFromJson( geometry["start"] )
                            : std::nullopt;
            std::optional<std::pair<long long, long long>> end =
                    geometry.contains( "end" )
                            ? validationPointFromJson( geometry["end"] )
                            : std::nullopt;

            if( !start || !end )
            {
                appendGeometryIssue( issues, item, "missing_segment_endpoint",
                                     "Segment geometry requires numeric start and end points." );
                continue;
            }

            if( *start == *end )
            {
                appendGeometryIssue(
                        issues, item,
                        item.m_Type == wxS( "track_segment" )
                                ? "zero_length_track_segment"
                                : "zero_length_shape",
                        item.m_Type == wxS( "track_segment" )
                                ? "Track segment has identical start and end points."
                                : "Shape segment has identical start and end points." );
            }
        }
        else if( item.m_Type == wxS( "via" ) )
        {
            if( !geometry.contains( "position" )
                || !validationPointFromJson( geometry["position"] ) )
            {
                appendGeometryIssue( issues, item, "missing_via_position",
                                     "Via geometry requires a numeric position." );
            }
        }
    }

    return issues;
}


nlohmann::json maintenanceResult( AI_SESSION_OPERATION_KIND aKind,
                                  const nlohmann::json& aArgs,
                                  const std::vector<wxString>& aWarnings,
                                  nlohmann::json aIssues = nlohmann::json::array() )
{
    nlohmann::json payload = {
        { "kind", toUtf8String( AiSessionOperationKindId( aKind ) ) },
        { "arguments", aArgs },
        { "board_mutated", false }
    };

    switch( aKind )
    {
    case AI_SESSION_OPERATION_KIND::RunValidation:
    {
        const std::string level = stringFieldOr( aArgs, "level", "geometry" );
        nlohmann::json validation = {
            { "scope", stringFieldOr( aArgs, "scope", "session" ) },
            { "level", level },
            { "status", "ok" },
            { "issue_count", aIssues.size() },
            { "issues", std::move( aIssues ) },
            { "warnings", warningsJson( aWarnings ) },
            { "validated_state", "semantic_shadow_board" },
            { "preview_state_exact", true }
        };

        if( level == "drc_lite" || level == "full_drc" )
        {
            validation["native_backend"] = "not_connected";
            validation["accept_validation_sufficient"] = false;
            validation["accept_validation_reason"] =
                    "native_validation_service_not_connected";
        }

        payload["status"] = "validation_completed";
        payload["validation"] = std::move( validation );
        break;
    }

    case AI_SESSION_OPERATION_KIND::RefillZones:
        payload["status"] = "zone_refill_recorded";
        payload["refill"] = {
            { "scope", aArgs },
            { "status", "recorded" },
            { "warnings", warningsJson( aWarnings ) }
        };
        break;

    case AI_SESSION_OPERATION_KIND::RebuildConnectivity:
        payload["status"] = "connectivity_rebuild_recorded";
        payload["connectivity"] = {
            { "scope", stringFieldOr( aArgs, "scope", "session" ) },
            { "status", "recorded" },
            { "warnings", warningsJson( aWarnings ) }
        };
        break;

    case AI_SESSION_OPERATION_KIND::Unknown:
    case AI_SESSION_OPERATION_KIND::Checkpoint:
    case AI_SESSION_OPERATION_KIND::RollbackTo:
    case AI_SESSION_OPERATION_KIND::QueryBoardSummary:
    case AI_SESSION_OPERATION_KIND::QueryItems:
    case AI_SESSION_OPERATION_KIND::QueryItem:
    case AI_SESSION_OPERATION_KIND::QuerySelection:
    case AI_SESSION_OPERATION_KIND::QueryNets:
    case AI_SESSION_OPERATION_KIND::QueryLayers:
    case AI_SESSION_OPERATION_KIND::QueryDesignRules:
    case AI_SESSION_OPERATION_KIND::QueryViewport:
    case AI_SESSION_OPERATION_KIND::QueryActivityTimeline:
    case AI_SESSION_OPERATION_KIND::RenderPreview:
    case AI_SESSION_OPERATION_KIND::ObserveStep:
    case AI_SESSION_OPERATION_KIND::CreateVia:
    case AI_SESSION_OPERATION_KIND::CreateTrackSegment:
    case AI_SESSION_OPERATION_KIND::CreateTrackPolyline:
    case AI_SESSION_OPERATION_KIND::CreateZone:
    case AI_SESSION_OPERATION_KIND::CreateShape:
    case AI_SESSION_OPERATION_KIND::MoveItems:
    case AI_SESSION_OPERATION_KIND::DeleteItems:
    case AI_SESSION_OPERATION_KIND::UpdateItemGeometry:
    case AI_SESSION_OPERATION_KIND::SetItemNet:
    case AI_SESSION_OPERATION_KIND::SetItemLayer:
    case AI_SESSION_OPERATION_KIND::SetItemProperties:
    case AI_SESSION_OPERATION_KIND::SetMetadata:
    default:
        payload["status"] = "not_reportable";
        break;
    }

    return payload;
}


AI_ATOMIC_EXECUTION_RESULT executeMaintenanceOperation(
        AI_EXECUTION_SESSION& aSession, AI_SESSION_OPERATION_KIND aKind,
        const nlohmann::json& aArgs )
{
    if( aKind == AI_SESSION_OPERATION_KIND::RunValidation )
    {
        const wxString level = validationLevelFromArgs( aArgs );

        if( level.IsEmpty() || !isSupportedValidationLevel( level ) )
        {
            return errorResult(
                    wxS( "unsupported_validation_level" ),
                    wxS( "Validation level must be geometry, drc_lite, or full_drc." ) );
        }
    }

    AI_ATOMIC_EXECUTION_RESULT result;
    std::vector<AI_SESSION_HANDLE> resolvedHandles;

    if( aKind == AI_SESSION_OPERATION_KIND::RefillZones
        && ( aArgs.contains( "handle" ) || aArgs.contains( "handles" ) ) )
    {
        resolvedHandles = resolveHandles( aSession, aArgs );

        if( resolvedHandles.empty() )
        {
            return errorResult( wxS( "invalid_handle" ),
                                wxS( "RefillZones did not resolve any live zone handles." ) );
        }
    }
    else if( aKind == AI_SESSION_OPERATION_KIND::RefillZones
             && aArgs.contains( "affected_area" )
             && !( aArgs.contains( "all" ) && aArgs["all"].is_boolean()
                   && aArgs["all"].get<bool>() ) )
    {
        nlohmann::json filter = {
            { "type", "zone" },
            { "bbox", aArgs["affected_area"] }
        };

        for( const AI_SHADOW_ITEM& item : aSession.ShadowBoard().QueryItems( fromJson( filter ) ) )
            resolvedHandles.push_back( item.m_Handle );
    }

    std::vector<wxString> warnings = maintenanceWarnings( aKind, aArgs );
    nlohmann::json issues = nlohmann::json::array();

    if( aKind == AI_SESSION_OPERATION_KIND::RunValidation )
        issues = geometryValidationIssues( aSession );

    const wxString resultJson =
            fromJson( maintenanceResult( aKind, aArgs, warnings, std::move( issues ) ) );
    result.m_ResolvedHandles = resolvedHandles;
    appendRecord( aSession, result, aKind, aArgs, resolvedHandles, {},
                  std::move( warnings ), resultJson );
    result.m_Ok = true;
    return result;
}
} // namespace


AI_ATOMIC_EXECUTION_RESULT AI_ATOMIC_OPERATION_EXECUTOR::Execute(
        AI_EXECUTION_SESSION& aSession, AI_SESSION_OPERATION_KIND aKind,
        const wxString& aArgumentsJson )
{
    if( aSession.Status() != AI_EXECUTION_SESSION_STATUS::Open )
        return errorResult( wxS( "session_not_open" ), wxS( "Session is not open." ) );

    if( !aSession.HasOpenStep() )
        return errorResult( wxS( "step_required" ), wxS( "Atomic ops require an open step." ) );

    nlohmann::json args = nlohmann::json::parse( toUtf8String( aArgumentsJson ), nullptr, false );

    if( args.is_discarded() || !args.is_object() )
        return errorResult( wxS( "invalid_json" ), wxS( "Arguments must be a JSON object." ) );

    switch( aKind )
    {
    case AI_SESSION_OPERATION_KIND::CreateVia:
        return createVia( aSession, args );

    case AI_SESSION_OPERATION_KIND::CreateTrackSegment:
        return createTrackSegment( aSession, args );

    case AI_SESSION_OPERATION_KIND::CreateTrackPolyline:
        return createTrackPolyline( aSession, args );

    case AI_SESSION_OPERATION_KIND::CreateZone:
        return createSimpleItem( aSession, aKind, args, wxS( "zone" ), "outline" );

    case AI_SESSION_OPERATION_KIND::CreateShape:
        return createSimpleItem( aSession, aKind, args, wxS( "shape" ), "geometry" );

    case AI_SESSION_OPERATION_KIND::MoveItems:
    case AI_SESSION_OPERATION_KIND::DeleteItems:
    case AI_SESSION_OPERATION_KIND::UpdateItemGeometry:
    case AI_SESSION_OPERATION_KIND::SetItemNet:
    case AI_SESSION_OPERATION_KIND::SetItemLayer:
    case AI_SESSION_OPERATION_KIND::SetItemProperties:
    case AI_SESSION_OPERATION_KIND::SetMetadata:
        return mutateHandles( aSession, aKind, args );

    case AI_SESSION_OPERATION_KIND::ApplySurfacePatch:
        return applySurfacePatch( aSession, args );

    case AI_SESSION_OPERATION_KIND::RefillZones:
    case AI_SESSION_OPERATION_KIND::RebuildConnectivity:
    case AI_SESSION_OPERATION_KIND::RunValidation:
        return executeMaintenanceOperation( aSession, aKind, args );

    case AI_SESSION_OPERATION_KIND::Unknown:
    case AI_SESSION_OPERATION_KIND::Checkpoint:
    case AI_SESSION_OPERATION_KIND::RollbackTo:
    case AI_SESSION_OPERATION_KIND::QueryBoardSummary:
    case AI_SESSION_OPERATION_KIND::QueryItems:
    case AI_SESSION_OPERATION_KIND::QueryItem:
    case AI_SESSION_OPERATION_KIND::QuerySelection:
    case AI_SESSION_OPERATION_KIND::QueryNets:
    case AI_SESSION_OPERATION_KIND::QueryLayers:
    case AI_SESSION_OPERATION_KIND::QueryDesignRules:
    case AI_SESSION_OPERATION_KIND::QueryViewport:
    case AI_SESSION_OPERATION_KIND::QueryActivityTimeline:
    case AI_SESSION_OPERATION_KIND::RenderPreview:
    case AI_SESSION_OPERATION_KIND::ObserveStep:
    default:
        return errorResult( wxS( "unsupported_operation" ),
                            wxS( "Operation is not a PCB mutation atomic op." ) );
    }
}
