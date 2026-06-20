/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 */

#include <kisurf_ai_pcb_session_preview_service.h>

#include <board.h>
#include <kisurf/ai/ai_shadow_board.h>
#include <nlohmann/json.hpp>

#include <optional>
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


nlohmann::json parseObjectJson( const wxString& aText )
{
    nlohmann::json parsed = nlohmann::json::parse( toUtf8String( aText ), nullptr, false );

    if( parsed.is_discarded() || !parsed.is_object() )
        return nlohmann::json::object();

    return parsed;
}


std::string stringOrFallback( const wxString& aText, const char* aFallback )
{
    if( aText.IsEmpty() )
        return aFallback;

    return toUtf8String( aText );
}


std::string layerOrFallback( const AI_SHADOW_ITEM& aItem )
{
    if( !aItem.m_Layer.IsEmpty() )
        return toUtf8String( aItem.m_Layer );

    if( !aItem.m_Layers.empty() && !aItem.m_Layers.front().IsEmpty() )
        return toUtf8String( aItem.m_Layers.front() );

    return "F.Cu";
}


wxString shadowItemPreviewLabel( const AI_SHADOW_ITEM& aItem )
{
    return aItem.m_Alias.IsEmpty()
                   ? wxString::Format( wxS( "session:%llu" ),
                                       static_cast<unsigned long long>(
                                               aItem.m_Handle.m_HandleId ) )
                   : aItem.m_Alias;
}


wxString metadataValue( const AI_SHADOW_ITEM& aItem, const wxString& aKey )
{
    auto it = aItem.m_Metadata.find( aKey );

    if( it == aItem.m_Metadata.end() )
        return wxEmptyString;

    return it->second;
}


wxString validationOverlayGeometry( const AI_SHADOW_ITEM& aItem )
{
    wxString geometry = metadataValue( aItem, wxS( "validation_geometry" ) );

    if( !geometry.IsEmpty() )
        return geometry;

    wxString position = metadataValue( aItem, wxS( "validation_position" ) );

    if( position.IsEmpty() )
        return wxEmptyString;

    nlohmann::json parsed =
            nlohmann::json::parse( toUtf8String( position ), nullptr, false );

    if( parsed.is_discarded() || !parsed.is_object() )
        return wxEmptyString;

    return fromJson( { { "position", std::move( parsed ) } } );
}


wxString validationOverlayLayer( const AI_SHADOW_ITEM& aItem )
{
    wxString layer = metadataValue( aItem, wxS( "validation_layer" ) );

    if( !layer.IsEmpty() )
        return layer;

    return wxString::FromUTF8( layerOrFallback( aItem ).c_str() );
}


int integerFieldOr( const nlohmann::json& aJson, const char* aName, int aFallback )
{
    if( !aJson.contains( aName ) || !aJson[aName].is_number() )
        return aFallback;

    return static_cast<int>( aJson[aName].get<double>() );
}


std::optional<nlohmann::json> previewPoint( const nlohmann::json& aPoint )
{
    if( !aPoint.is_object() || !aPoint.contains( "x" ) || !aPoint.contains( "y" )
        || !aPoint["x"].is_number() || !aPoint["y"].is_number() )
    {
        return std::nullopt;
    }

    return nlohmann::json{
        { "x", static_cast<int>( aPoint["x"].get<double>() ) },
        { "y", static_cast<int>( aPoint["y"].get<double>() ) }
    };
}


const nlohmann::json* pointRing( const nlohmann::json& aGeometry )
{
    if( aGeometry.contains( "points" ) && aGeometry["points"].is_array() )
        return &aGeometry["points"];

    if( aGeometry.contains( "outer" ) && aGeometry["outer"].is_array() )
        return &aGeometry["outer"];

    if( aGeometry.contains( "outline" ) && aGeometry["outline"].is_object() )
    {
        const nlohmann::json& outline = aGeometry["outline"];

        if( outline.contains( "points" ) && outline["points"].is_array() )
            return &outline["points"];

        if( outline.contains( "outer" ) && outline["outer"].is_array() )
            return &outline["outer"];
    }

    return nullptr;
}


std::optional<nlohmann::json> previewPointsFromRing( const nlohmann::json& aRing )
{
    if( !aRing.is_array() || aRing.size() < 3 )
        return std::nullopt;

    nlohmann::json result = nlohmann::json::array();

    for( const nlohmann::json& point : aRing )
    {
        std::optional<nlohmann::json> parsed = previewPoint( point );

        if( !parsed )
            return std::nullopt;

        result.push_back( *parsed );
    }

    return result;
}


std::optional<nlohmann::json> previewPoints( const nlohmann::json& aGeometry )
{
    const nlohmann::json* points = pointRing( aGeometry );

    if( !points )
        return std::nullopt;

    return previewPointsFromRing( *points );
}


bool appendPreviewHoleRing( const nlohmann::json& aRing, nlohmann::json& aHoles )
{
    std::optional<nlohmann::json> parsed = previewPointsFromRing( aRing );

    if( !parsed )
        return false;

    aHoles.push_back( *parsed );
    return true;
}


bool appendPreviewHolesFromGeometry( const nlohmann::json& aGeometry, nlohmann::json& aHoles )
{
    if( aGeometry.contains( "inner" ) && !appendPreviewHoleRing( aGeometry["inner"], aHoles ) )
        return false;

    if( aGeometry.contains( "holes" ) )
    {
        if( !aGeometry["holes"].is_array() )
            return false;

        for( const nlohmann::json& hole : aGeometry["holes"] )
        {
            if( !appendPreviewHoleRing( hole, aHoles ) )
                return false;
        }
    }

    return true;
}


std::optional<nlohmann::json> previewHoles( const nlohmann::json& aGeometry )
{
    nlohmann::json holes = nlohmann::json::array();

    if( !appendPreviewHolesFromGeometry( aGeometry, holes ) )
        return std::nullopt;

    if( aGeometry.contains( "outline" ) && aGeometry["outline"].is_object()
        && !appendPreviewHolesFromGeometry( aGeometry["outline"], holes ) )
    {
        return std::nullopt;
    }

    return holes;
}


std::optional<AI_OBJECT_REF> previewRefForShadowItem( const AI_SHADOW_ITEM& aItem )
{
    nlohmann::json geometry = parseObjectJson( aItem.m_GeometryJson );
    nlohmann::json details = nlohmann::json::object();
    KICAD_T        type = TYPE_NOT_INIT;

    if( aItem.m_Type == wxS( "via" ) )
    {
        std::optional<nlohmann::json> position =
                geometry.contains( "position" ) ? previewPoint( geometry["position"] )
                                                : std::nullopt;

        if( !position )
            return std::nullopt;

        details = {
            { "operation", "place_via_preview" },
            { "net", stringOrFallback( aItem.m_Net, "AI_PREVIEW" ) },
            { "diameter", integerFieldOr( geometry, "diameter", 600000 ) },
            { "drill", integerFieldOr( geometry, "drill", 300000 ) },
            { "position", *position }
        };
        type = PCB_VIA_T;
    }
    else if( aItem.m_Type == wxS( "track_segment" ) )
    {
        std::optional<nlohmann::json> start =
                geometry.contains( "start" ) ? previewPoint( geometry["start"] )
                                             : std::nullopt;
        std::optional<nlohmann::json> end =
                geometry.contains( "end" ) ? previewPoint( geometry["end"] )
                                           : std::nullopt;

        if( !start || !end )
            return std::nullopt;

        details = {
            { "operation", "route_segment_preview" },
            { "net", stringOrFallback( aItem.m_Net, "AI_PREVIEW" ) },
            { "layer", layerOrFallback( aItem ) },
            { "width", integerFieldOr( geometry, "width", 100000 ) },
            { "start", *start },
            { "end", *end }
        };
        type = PCB_TRACE_T;
    }
    else if( aItem.m_Type == wxS( "shape" ) )
    {
        std::optional<nlohmann::json> start =
                geometry.contains( "start" ) ? previewPoint( geometry["start"] )
                                             : std::nullopt;
        std::optional<nlohmann::json> end =
                geometry.contains( "end" ) ? previewPoint( geometry["end"] )
                                           : std::nullopt;

        if( !start || !end )
            return std::nullopt;

        std::string shape = "segment";

        if( geometry.contains( "shape" ) && geometry["shape"].is_string() )
            shape = geometry["shape"].get<std::string>();
        else if( geometry.contains( "shape_type" ) && geometry["shape_type"].is_string() )
            shape = geometry["shape_type"].get<std::string>();

        details = {
            { "operation", "create_shape_preview" },
            { "shape", shape },
            { "layer", layerOrFallback( aItem ) },
            { "width", integerFieldOr( geometry, "width", 100000 ) },
            { "start", *start },
            { "end", *end }
        };
        type = PCB_SHAPE_T;
    }
    else if( aItem.m_Type == wxS( "zone" ) )
    {
        std::optional<nlohmann::json> points = previewPoints( geometry );
        std::optional<nlohmann::json> holes = previewHoles( geometry );

        if( !points || points->size() < 3 || !holes )
            return std::nullopt;

        details = {
            { "operation", "create_copper_zone_preview" },
            { "net", stringOrFallback( aItem.m_Net, "AI_PREVIEW" ) },
            { "layer", layerOrFallback( aItem ) },
            { "points", *points }
        };

        if( !holes->empty() )
            details["holes"] = *holes;

        type = PCB_ZONE_T;
    }
    else
    {
        return std::nullopt;
    }

    return AI_OBJECT_REF( KIID(), type, shadowItemPreviewLabel( aItem ),
                          fromJson( details ) );
}
} // namespace


KISURF_AI_PCB_SESSION_PREVIEW_SERVICE::KISURF_AI_PCB_SESSION_PREVIEW_SERVICE(
        BOARD& aBoard, KIGFX::VIEW& aView ) :
        m_Board( aBoard ),
        m_Resolver( aBoard ),
        m_Adapter( m_Resolver, aView ),
        m_PreviewManager( m_Adapter )
{
}


AI_SESSION_PREVIEW_RESULT KISURF_AI_PCB_SESSION_PREVIEW_SERVICE::RenderPreview(
        const AI_EXECUTION_SESSION& aSession, const wxString& aArgumentsJson )
{
    wxUnusedVar( m_Board );

    AI_SESSION_PREVIEW_RESULT result;

    if( aSession.EditorKind() != AI_EDITOR_KIND::Pcb )
    {
        result.m_Ok = false;
        result.m_ErrorCode = wxS( "wrong_editor" );
        result.m_Message = wxS( "PCB session preview service can only render PCB sessions." );
        return result;
    }

    nlohmann::json provenance = {
        { "source", "ai_session" },
        { "session_id", aSession.SessionId() },
        { "board_id", toUtf8String( aSession.BoardId() ) },
        { "epoch", aSession.Epoch() },
        { "arguments", parseObjectJson( aArgumentsJson ) }
    };

    m_ActiveSessionId = aSession.SessionId();
    const uint64_t previewId = m_PreviewManager.BeginPreview( fromJson( provenance ) );
    size_t renderedItemCount = 0;
    size_t renderedOverlayCount = 0;

    for( const AI_SHADOW_ITEM& item : aSession.ShadowBoard().QueryItems() )
    {
        std::optional<AI_OBJECT_REF> ref = previewRefForShadowItem( item );

        if( !ref )
            continue;

        const size_t beforeCount = m_Adapter.PreviewedItems().size();
        m_PreviewManager.ShowObject( *ref );

        if( m_Adapter.PreviewedItems().size() > beforeCount )
            ++renderedItemCount;

        wxString severity = metadataValue( item, wxS( "validation_status" ) );

        if( severity.IsEmpty() )
            severity = metadataValue( item, wxS( "validation_severity" ) );

        if( !severity.IsEmpty() )
        {
            wxString message = metadataValue( item, wxS( "validation_message" ) );

            if( message.IsEmpty() )
                message = severity;

            const size_t beforeOverlayCount = m_Adapter.PreviewedItems().size();
            m_PreviewManager.ShowItemOverlay( shadowItemPreviewLabel( item ),
                                              wxS( "validation" ), severity, message,
                                              validationOverlayGeometry( item ),
                                              validationOverlayLayer( item ) );

            if( m_Adapter.PreviewedItems().size() > beforeOverlayCount )
                ++renderedOverlayCount;
        }
    }

    result.m_Ok = true;
    result.m_PreviewId = previewId;
    result.m_RenderedItemCount = renderedItemCount;
    result.m_ResultJson = fromJson( {
        { "status", "preview_rendered" },
        { "native_preview", true },
        { "preview_id", previewId },
        { "rendered_item_count", renderedItemCount },
        { "rendered_overlay_count", renderedOverlayCount }
    } );
    return result;
}


void KISURF_AI_PCB_SESSION_PREVIEW_SERVICE::ClearPreview( uint64_t aSessionId )
{
    if( m_ActiveSessionId != aSessionId )
        return;

    m_PreviewManager.ClearPreview();
    m_ActiveSessionId = 0;
}
