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
#include <utility>
#include <algorithm>
#include <wx/thread.h>

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


nlohmann::json visualBoundsJson( const AI_VISUAL_BOUNDS& aBounds )
{
    return {
        { "left", aBounds.m_Left },
        { "top", aBounds.m_Top },
        { "right", aBounds.m_Right },
        { "bottom", aBounds.m_Bottom }
    };
}


std::string stringOrFallback( const wxString& aText, const char* aFallback )
{
    if( aText.IsEmpty() )
        return aFallback;

    return toUtf8String( aText );
}


nlohmann::json snapshotSidecarJson( const AI_VISUAL_SNAPSHOT& aSnapshot )
{
    if( aSnapshot.m_SidecarJson.IsEmpty() )
        return nlohmann::json::object();

    return parseObjectJson( aSnapshot.m_SidecarJson );
}


bool visualBoundsValid( const AI_VISUAL_BOUNDS& aBounds )
{
    return aBounds.m_Right > aBounds.m_Left && aBounds.m_Bottom > aBounds.m_Top;
}


bool jsonNumberField( const nlohmann::json& aObject, const char* aKey,
                      double& aValue )
{
    if( !aObject.is_object() || !aObject.contains( aKey )
        || !aObject[aKey].is_number() )
    {
        return false;
    }

    aValue = aObject[aKey].get<double>();
    return true;
}


bool pixelWorldTransformFromSidecar( const nlohmann::json& aSidecar,
                                     AI_VISUAL_PIXEL_WORLD_TRANSFORM& aTransform )
{
    if( !aSidecar.is_object() || !aSidecar.contains( "pixel_world_transform" )
        || !aSidecar["pixel_world_transform"].is_object() )
    {
        return false;
    }

    const nlohmann::json& transform = aSidecar["pixel_world_transform"];

    if( !transform.contains( "world_origin" )
        || !transform["world_origin"].is_object()
        || !jsonNumberField( transform["world_origin"], "x",
                             aTransform.m_WorldOriginX )
        || !jsonNumberField( transform["world_origin"], "y",
                             aTransform.m_WorldOriginY )
        || !jsonNumberField( transform, "world_x_per_pixel_x",
                             aTransform.m_WorldXPerPixelX )
        || !jsonNumberField( transform, "world_x_per_pixel_y",
                             aTransform.m_WorldXPerPixelY )
        || !jsonNumberField( transform, "world_y_per_pixel_x",
                             aTransform.m_WorldYPerPixelX )
        || !jsonNumberField( transform, "world_y_per_pixel_y",
                             aTransform.m_WorldYPerPixelY ) )
    {
        return false;
    }

    const double determinant =
            aTransform.m_WorldXPerPixelX * aTransform.m_WorldYPerPixelY
            - aTransform.m_WorldXPerPixelY * aTransform.m_WorldYPerPixelX;

    return determinant != 0.0;
}


std::optional<std::pair<double, double>> pixelPointFromWorldPoint(
        const AI_VISUAL_PIXEL_WORLD_TRANSFORM& aTransform,
        double aWorldX, double aWorldY )
{
    const double determinant =
            aTransform.m_WorldXPerPixelX * aTransform.m_WorldYPerPixelY
            - aTransform.m_WorldXPerPixelY * aTransform.m_WorldYPerPixelX;

    if( determinant == 0.0 )
        return std::nullopt;

    const double dx = aWorldX - aTransform.m_WorldOriginX;
    const double dy = aWorldY - aTransform.m_WorldOriginY;
    const double pixelX = ( dx * aTransform.m_WorldYPerPixelY
                            - aTransform.m_WorldXPerPixelY * dy )
                          / determinant;
    const double pixelY = ( aTransform.m_WorldXPerPixelX * dy
                            - dx * aTransform.m_WorldYPerPixelX )
                          / determinant;

    return std::make_pair( pixelX, pixelY );
}


std::optional<AI_VISUAL_BOUNDS> pixelBoundsFromWorldBounds(
        const AI_VISUAL_BOUNDS& aWorldBounds,
        const AI_VISUAL_PIXEL_WORLD_TRANSFORM& aTransform )
{
    if( !visualBoundsValid( aWorldBounds ) )
        return std::nullopt;

    const std::optional<std::pair<double, double>> corners[] = {
        pixelPointFromWorldPoint( aTransform, aWorldBounds.m_Left, aWorldBounds.m_Top ),
        pixelPointFromWorldPoint( aTransform, aWorldBounds.m_Right, aWorldBounds.m_Top ),
        pixelPointFromWorldPoint( aTransform, aWorldBounds.m_Left, aWorldBounds.m_Bottom ),
        pixelPointFromWorldPoint( aTransform, aWorldBounds.m_Right, aWorldBounds.m_Bottom )
    };

    if( !corners[0] || !corners[1] || !corners[2] || !corners[3] )
        return std::nullopt;

    const double xs[] = {
        corners[0]->first,
        corners[1]->first,
        corners[2]->first,
        corners[3]->first
    };
    const double ys[] = {
        corners[0]->second,
        corners[1]->second,
        corners[2]->second,
        corners[3]->second
    };

    return AI_VISUAL_BOUNDS{
        *std::min_element( std::begin( xs ), std::end( xs ) ),
        *std::min_element( std::begin( ys ), std::end( ys ) ),
        *std::max_element( std::begin( xs ), std::end( xs ) ),
        *std::max_element( std::begin( ys ), std::end( ys ) )
    };
}


nlohmann::json visualAnchorJson(
        const AI_VISUAL_ANCHOR_RECORD& aAnchor,
        const AI_VISUAL_PIXEL_WORLD_TRANSFORM* aPixelWorldTransform = nullptr )
{
    AI_VISUAL_BOUNDS pixelBounds = aAnchor.m_PixelBounds;

    if( aPixelWorldTransform && !visualBoundsValid( pixelBounds )
        && visualBoundsValid( aAnchor.m_WorldBounds ) )
    {
        if( std::optional<AI_VISUAL_BOUNDS> projected =
                    pixelBoundsFromWorldBounds( aAnchor.m_WorldBounds,
                                                *aPixelWorldTransform ) )
        {
            pixelBounds = *projected;
        }
    }

    return {
        { "anchor_id", toUtf8String( aAnchor.m_AnchorId ) },
        { "object_id", toUtf8String( aAnchor.m_ObjectId ) },
        { "handle", toUtf8String( aAnchor.m_Handle ) },
        { "layer", toUtf8String( aAnchor.m_Layer ) },
        { "net_name", toUtf8String( aAnchor.m_NetName ) },
        { "world_xy", { { "x", aAnchor.m_WorldX }, { "y", aAnchor.m_WorldY } } },
        { "world_bounds", visualBoundsJson( aAnchor.m_WorldBounds ) },
        { "pixel_bounds", visualBoundsJson( pixelBounds ) }
    };
}


nlohmann::json visualAnchorsJson(
        const std::vector<AI_VISUAL_ANCHOR_RECORD>& aAnchors,
        const AI_VISUAL_PIXEL_WORLD_TRANSFORM* aPixelWorldTransform = nullptr )
{
    nlohmann::json anchors = nlohmann::json::array();

    for( const AI_VISUAL_ANCHOR_RECORD& anchor : aAnchors )
        anchors.push_back( visualAnchorJson( anchor, aPixelWorldTransform ) );

    return anchors;
}


nlohmann::json previewFrameJson( const AI_VISUAL_SNAPSHOT& aSnapshot,
                                 uint64_t aPreviewId,
                                 const AI_EXECUTION_SESSION& aSession,
                                 size_t aRenderedItemCount,
                                 size_t aRenderedOverlayCount,
                                 const std::vector<AI_VISUAL_ANCHOR_RECORD>& aAnchors )
{
    const std::string fallbackFrameId =
            "preview_after_" + std::to_string( aPreviewId );

    nlohmann::json frame = {
        { "frame_id",
          stringOrFallback( aSnapshot.m_FrameId, fallbackFrameId.c_str() ) },
        { "frame_kind",
          stringOrFallback( aSnapshot.m_FrameKind, "preview_after" ) },
        { "source",
          stringOrFallback( aSnapshot.m_Source,
                            "pcbnew.native_preview_scene" ) },
        { "has_pixels", aSnapshot.HasPixels() },
        { "preview_id", aPreviewId },
        { "session_id", aSession.SessionId() },
        { "epoch", aSession.Epoch() },
        { "rendered_item_count", aRenderedItemCount },
        { "rendered_overlay_count", aRenderedOverlayCount }
    };

    if( aSnapshot.m_WidthPx > 0 )
        frame["width_px"] = aSnapshot.m_WidthPx;

    if( aSnapshot.m_HeightPx > 0 )
        frame["height_px"] = aSnapshot.m_HeightPx;

    if( aSnapshot.m_ByteSize > 0 )
        frame["byte_size"] = aSnapshot.m_ByteSize;

    if( !aSnapshot.m_MimeType.IsEmpty() )
        frame["mime_type"] = toUtf8String( aSnapshot.m_MimeType );

    if( !aSnapshot.m_UnavailableReason.IsEmpty() )
        frame["unavailable_reason"] =
                toUtf8String( aSnapshot.m_UnavailableReason );
    else if( !aSnapshot.HasPixels() )
        frame["unavailable_reason"] = "preview_scene_pixels_not_captured";

    const nlohmann::json sidecar = snapshotSidecarJson( aSnapshot );
    AI_VISUAL_PIXEL_WORLD_TRANSFORM pixelWorldTransform;
    const bool hasPixelWorldTransform =
            pixelWorldTransformFromSidecar( sidecar, pixelWorldTransform );

    if( !sidecar.empty() )
    {
        frame["sidecar"] = sidecar;

        if( sidecar.contains( "document_revision" ) )
            frame["document_revision"] = sidecar["document_revision"];

        if( sidecar.contains( "preview_revision" ) )
            frame["preview_revision"] = sidecar["preview_revision"];
    }

    if( !aAnchors.empty() )
    {
        if( !frame.contains( "sidecar" ) || !frame["sidecar"].is_object() )
            frame["sidecar"] = nlohmann::json::object();

        frame["sidecar"]["anchors"] = visualAnchorsJson(
                aAnchors, hasPixelWorldTransform ? &pixelWorldTransform : nullptr );
    }

    return frame;
}


AI_VISUAL_SNAPSHOT capturePreviewFrameFromCanvas(
        EDA_DRAW_PANEL_GAL& aCanvas, uint64_t aPreviewId,
        const AI_EXECUTION_SESSION& aSession,
        const std::vector<AI_VISUAL_ANCHOR_RECORD>& aAnchors )
{
    AI_VISUAL_CONTEXT_FRAME_REQUEST request;
    request.m_FrameId = wxString::Format(
            wxS( "preview_after_%llu" ),
            static_cast<unsigned long long>( aPreviewId ) );
    request.m_FrameKind = wxS( "preview_after" );
    request.m_Source = wxS( "pcbnew.native_preview_scene" );
    request.m_PreviewId = wxString::Format(
            wxS( "%llu" ), static_cast<unsigned long long>( aPreviewId ) );
    request.m_DocumentRevision =
            aSession.ContextVersion().m_DocumentRevision;
    request.m_PreviewRevision =
            aSession.ContextVersion().m_ViewRevision;
    request.m_Anchors = aAnchors;

    AI_VISUAL_OBSERVATION_ARTIFACT artifact;
    BuildAiVisualContextFrameFromCanvas( aCanvas, artifact, request );
    return artifact.m_Snapshot;
}


std::string layerOrFallback( const AI_SHADOW_ITEM& aItem )
{
    if( !aItem.m_Layer.IsEmpty() )
        return toUtf8String( aItem.m_Layer );

    if( !aItem.m_Layers.empty() && !aItem.m_Layers.front().IsEmpty() )
        return toUtf8String( aItem.m_Layers.front() );

    return "F.Cu";
}


const nlohmann::json* pointRing( const nlohmann::json& aGeometry );


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


struct PREVIEW_ANCHOR_GEOMETRY
{
    double           m_WorldX = 0.0;
    double           m_WorldY = 0.0;
    AI_VISUAL_BOUNDS m_WorldBounds;
};


struct PREVIEW_WORLD_BOUNDS_BUILDER
{
    bool   m_HasPoint = false;
    double m_Left = 0.0;
    double m_Top = 0.0;
    double m_Right = 0.0;
    double m_Bottom = 0.0;

    void Include( double aX, double aY )
    {
        if( !m_HasPoint )
        {
            m_Left = aX;
            m_Right = aX;
            m_Top = aY;
            m_Bottom = aY;
            m_HasPoint = true;
            return;
        }

        m_Left = std::min( m_Left, aX );
        m_Right = std::max( m_Right, aX );
        m_Top = std::min( m_Top, aY );
        m_Bottom = std::max( m_Bottom, aY );
    }

    AI_VISUAL_BOUNDS Bounds( double aPadding = 0.0 ) const
    {
        return {
            m_Left - aPadding,
            m_Top - aPadding,
            m_Right + aPadding,
            m_Bottom + aPadding
        };
    }
};


bool jsonPointD( const nlohmann::json& aPoint, double& aX, double& aY )
{
    if( !aPoint.is_object() || !aPoint.contains( "x" ) || !aPoint.contains( "y" )
        || !aPoint["x"].is_number() || !aPoint["y"].is_number() )
    {
        return false;
    }

    aX = aPoint["x"].get<double>();
    aY = aPoint["y"].get<double>();
    return true;
}


bool includePointFromField( const nlohmann::json& aGeometry, const char* aField,
                            PREVIEW_WORLD_BOUNDS_BUILDER& aBuilder,
                            double& aLastX, double& aLastY )
{
    if( !aGeometry.contains( aField ) )
        return false;

    double x = 0.0;
    double y = 0.0;

    if( !jsonPointD( aGeometry[aField], x, y ) )
        return false;

    aBuilder.Include( x, y );
    aLastX = x;
    aLastY = y;
    return true;
}


bool includePointArray( const nlohmann::json& aPoints,
                        PREVIEW_WORLD_BOUNDS_BUILDER& aBuilder )
{
    if( !aPoints.is_array() || aPoints.empty() )
        return false;

    bool any = false;

    for( const nlohmann::json& point : aPoints )
    {
        double x = 0.0;
        double y = 0.0;

        if( !jsonPointD( point, x, y ) )
            return false;

        aBuilder.Include( x, y );
        any = true;
    }

    return any;
}


double numericFieldOr( const nlohmann::json& aJson, const char* aName, double aFallback )
{
    if( !aJson.contains( aName ) || !aJson[aName].is_number() )
        return aFallback;

    return aJson[aName].get<double>();
}


std::optional<PREVIEW_ANCHOR_GEOMETRY> previewAnchorGeometry(
        const AI_SHADOW_ITEM& aItem )
{
    nlohmann::json geometry = parseObjectJson( aItem.m_GeometryJson );
    nlohmann::json properties = parseObjectJson( aItem.m_PropertiesJson );
    PREVIEW_ANCHOR_GEOMETRY result;

    if( aItem.m_Type == wxS( "via" ) )
    {
        if( !geometry.contains( "position" )
            || !jsonPointD( geometry["position"], result.m_WorldX, result.m_WorldY ) )
        {
            return std::nullopt;
        }

        const double diameter = numericFieldOr( geometry, "diameter", 600000.0 );
        const double radius = std::max( 0.0, diameter / 2.0 );
        result.m_WorldBounds = {
            result.m_WorldX - radius,
            result.m_WorldY - radius,
            result.m_WorldX + radius,
            result.m_WorldY + radius
        };
        return result;
    }

    PREVIEW_WORLD_BOUNDS_BUILDER builder;
    double lastX = 0.0;
    double lastY = 0.0;
    double padding = 0.0;

    if( aItem.m_Type == wxS( "track_segment" ) )
    {
        if( !includePointFromField( geometry, "start", builder, lastX, lastY )
            || !includePointFromField( geometry, "end", builder, lastX, lastY ) )
        {
            return std::nullopt;
        }

        padding = numericFieldOr( geometry, "width", 0.0 ) / 2.0;
    }
    else if( aItem.m_Type == wxS( "shape" ) )
    {
        std::string shape;

        if( properties.contains( "shape_type" ) && properties["shape_type"].is_string() )
            shape = properties["shape_type"].get<std::string>();
        else if( geometry.contains( "shape" ) && geometry["shape"].is_string() )
            shape = geometry["shape"].get<std::string>();
        else if( geometry.contains( "shape_type" ) && geometry["shape_type"].is_string() )
            shape = geometry["shape_type"].get<std::string>();

        if( shape == "circle" )
        {
            if( !geometry.contains( "center" )
                || !jsonPointD( geometry["center"], result.m_WorldX, result.m_WorldY ) )
            {
                return std::nullopt;
            }

            const double radius = numericFieldOr( geometry, "radius", 0.0 );
            const double width = numericFieldOr( properties, "width",
                                                 numericFieldOr( geometry, "width", 0.0 ) );
            padding = std::max( 0.0, radius + width / 2.0 );
            result.m_WorldBounds = {
                result.m_WorldX - padding,
                result.m_WorldY - padding,
                result.m_WorldX + padding,
                result.m_WorldY + padding
            };
            return result;
        }

        if( shape == "polygon" || shape == "poly" )
        {
            const nlohmann::json* points = pointRing( geometry );

            if( !points || !includePointArray( *points, builder ) )
                return std::nullopt;
        }
        else
        {
            bool any = includePointFromField( geometry, "start", builder, lastX, lastY );
            any = includePointFromField( geometry, "mid", builder, lastX, lastY ) || any;
            any = includePointFromField( geometry, "end", builder, lastX, lastY ) || any;

            if( !any )
                return std::nullopt;
        }

        padding = numericFieldOr( properties, "width",
                                  numericFieldOr( geometry, "width", 0.0 ) ) / 2.0;
    }
    else if( aItem.m_Type == wxS( "zone" ) )
    {
        const nlohmann::json* points = pointRing( geometry );

        if( !points || !includePointArray( *points, builder ) )
            return std::nullopt;
    }
    else if( aItem.m_Type == wxS( "footprint" ) )
    {
        if( !geometry.contains( "position" )
            || !jsonPointD( geometry["position"], result.m_WorldX, result.m_WorldY ) )
        {
            return std::nullopt;
        }

        result.m_WorldBounds = {
            result.m_WorldX,
            result.m_WorldY,
            result.m_WorldX,
            result.m_WorldY
        };
        return result;
    }
    else
    {
        return std::nullopt;
    }

    if( !builder.m_HasPoint )
        return std::nullopt;

    result.m_WorldBounds = builder.Bounds( padding );
    result.m_WorldX = ( result.m_WorldBounds.m_Left + result.m_WorldBounds.m_Right ) / 2.0;
    result.m_WorldY = ( result.m_WorldBounds.m_Top + result.m_WorldBounds.m_Bottom ) / 2.0;
    return result;
}


std::optional<AI_VISUAL_ANCHOR_RECORD> previewAnchorForShadowItem(
        const AI_SHADOW_ITEM& aItem )
{
    std::optional<PREVIEW_ANCHOR_GEOMETRY> geometry =
            previewAnchorGeometry( aItem );

    if( !geometry )
        return std::nullopt;

    const wxString label = shadowItemPreviewLabel( aItem );

    AI_VISUAL_ANCHOR_RECORD anchor;
    anchor.m_AnchorId = wxS( "preview_item:" ) + label;
    anchor.m_ObjectId = label;
    anchor.m_Handle = aItem.m_Handle.AsString();
    anchor.m_Layer = wxString::FromUTF8( layerOrFallback( aItem ).c_str() );
    anchor.m_NetName = aItem.m_Net;
    anchor.m_WorldX = geometry->m_WorldX;
    anchor.m_WorldY = geometry->m_WorldY;
    anchor.m_WorldBounds = geometry->m_WorldBounds;
    return anchor;
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
    nlohmann::json properties = parseObjectJson( aItem.m_PropertiesJson );
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
        std::string shape = "segment";

        if( properties.contains( "shape_type" ) && properties["shape_type"].is_string() )
            shape = properties["shape_type"].get<std::string>();
        else if( geometry.contains( "shape" ) && geometry["shape"].is_string() )
            shape = geometry["shape"].get<std::string>();
        else if( geometry.contains( "shape_type" ) && geometry["shape_type"].is_string() )
            shape = geometry["shape_type"].get<std::string>();

        const int width = properties.contains( "width" )
                                  ? integerFieldOr( properties, "width", 100000 )
                                  : integerFieldOr( geometry, "width", 100000 );

        if( shape == "circle" )
        {
            std::optional<nlohmann::json> center =
                    geometry.contains( "center" ) ? previewPoint( geometry["center"] )
                                                  : std::nullopt;

            if( !center || !geometry.contains( "radius" )
                || !geometry["radius"].is_number_integer()
                || geometry["radius"].get<int>() <= 0 )
            {
                return std::nullopt;
            }

            details = {
                { "operation", "create_shape_preview" },
                { "shape", shape },
                { "layer", layerOrFallback( aItem ) },
                { "width", width },
                { "center", *center },
                { "radius", geometry["radius"] }
            };
            type = PCB_SHAPE_T;
            return AI_OBJECT_REF( KIID(), type, shadowItemPreviewLabel( aItem ),
                                  fromJson( details ) );
        }

        if( shape == "arc" )
        {
            std::optional<nlohmann::json> start =
                    geometry.contains( "start" ) ? previewPoint( geometry["start"] )
                                                 : std::nullopt;
            std::optional<nlohmann::json> mid =
                    geometry.contains( "mid" ) ? previewPoint( geometry["mid"] )
                                               : std::nullopt;
            std::optional<nlohmann::json> end =
                    geometry.contains( "end" ) ? previewPoint( geometry["end"] )
                                               : std::nullopt;

            if( !start || !mid || !end )
                return std::nullopt;

            details = {
                { "operation", "create_shape_preview" },
                { "shape", shape },
                { "layer", layerOrFallback( aItem ) },
                { "width", width },
                { "start", *start },
                { "mid", *mid },
                { "end", *end }
            };
            type = PCB_SHAPE_T;
            return AI_OBJECT_REF( KIID(), type, shadowItemPreviewLabel( aItem ),
                                  fromJson( details ) );
        }

        if( shape == "polygon" || shape == "poly" )
        {
            std::optional<nlohmann::json> points = previewPoints( geometry );

            if( !points || points->size() < 3 )
                return std::nullopt;

            details = {
                { "operation", "create_shape_preview" },
                { "shape", shape },
                { "layer", layerOrFallback( aItem ) },
                { "width", width },
                { "points", *points }
            };
            type = PCB_SHAPE_T;
            return AI_OBJECT_REF( KIID(), type, shadowItemPreviewLabel( aItem ),
                                  fromJson( details ) );
        }

        std::optional<nlohmann::json> start =
                geometry.contains( "start" ) ? previewPoint( geometry["start"] )
                                             : std::nullopt;
        std::optional<nlohmann::json> end =
                geometry.contains( "end" ) ? previewPoint( geometry["end"] )
                                           : std::nullopt;

        if( !start || !end )
            return std::nullopt;

        details = {
            { "operation", "create_shape_preview" },
            { "shape", shape },
            { "layer", layerOrFallback( aItem ) },
            { "width", width },
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
    else if( aItem.m_Type == wxS( "footprint" ) )
    {
        std::optional<nlohmann::json> position =
                geometry.contains( "position" ) ? previewPoint( geometry["position"] )
                                                : std::nullopt;

        if( !position )
            return std::nullopt;

        wxString liveUuid = metadataValue( aItem, wxS( "live_uuid" ) );

        if( liveUuid.IsEmpty() )
            return std::nullopt;

        details = {
            { "operation", "footprint_transform_preview" },
            { "position", *position },
            { "side", layerOrFallback( aItem ) }
        };

        if( geometry.contains( "reference" ) && geometry["reference"].is_string() )
            details["reference"] = geometry["reference"];

        if( geometry.contains( "value" ) && geometry["value"].is_string() )
            details["value"] = geometry["value"];

        if( geometry.contains( "fp_id" ) && geometry["fp_id"].is_string() )
            details["fp_id"] = geometry["fp_id"];

        if( geometry.contains( "orientation_degrees" )
            && geometry["orientation_degrees"].is_number() )
        {
            details["orientation_degrees"] = geometry["orientation_degrees"];
        }

        if( properties.contains( "orientation_degrees" )
            && properties["orientation_degrees"].is_number() )
        {
            details["orientation_degrees"] = properties["orientation_degrees"];
        }

        if( properties.contains( "side" ) && properties["side"].is_string() )
            details["side"] = properties["side"];

        if( properties.contains( "reference" ) && properties["reference"].is_string() )
            details["reference"] = properties["reference"];

        if( properties.contains( "value" ) && properties["value"].is_string() )
            details["value"] = properties["value"];

        return AI_OBJECT_REF( KIID( liveUuid ), PCB_FOOTPRINT_T,
                              shadowItemPreviewLabel( aItem ), fromJson( details ) );
    }
    else
    {
        return std::nullopt;
    }

    return AI_OBJECT_REF( KIID(), type, shadowItemPreviewLabel( aItem ),
                          fromJson( details ) );
}


bool isHiddenAttemptRenderMode( const wxString& aArgumentsJson )
{
    const nlohmann::json arguments = parseObjectJson( aArgumentsJson );

    if( !arguments.contains( "mode" ) || !arguments["mode"].is_string() )
        return false;

    const std::string mode = arguments["mode"].get<std::string>();
    return mode == "hidden_attempt" || mode == "semantic_hidden_attempt";
}


bool shouldRenderSemanticOnly( const wxString& aArgumentsJson )
{
    return !wxThread::IsMain() || isHiddenAttemptRenderMode( aArgumentsJson );
}


bool shadowItemHasValidationOverlay( const AI_SHADOW_ITEM& aItem )
{
    return !metadataValue( aItem, wxS( "validation_status" ) ).IsEmpty()
           || !metadataValue( aItem, wxS( "validation_severity" ) ).IsEmpty();
}


AI_SESSION_PREVIEW_RESULT renderSemanticOnlyPreview(
        const AI_EXECUTION_SESSION& aSession, const wxString& aArgumentsJson )
{
    AI_SESSION_PREVIEW_RESULT result;
    const uint64_t previewId = aSession.SessionId();
    size_t renderedItemCount = 0;
    size_t renderedOverlayCount = 0;
    std::vector<AI_VISUAL_ANCHOR_RECORD> previewAnchors;

    for( const AI_SHADOW_ITEM& item : aSession.ShadowBoard().QueryItems() )
    {
        if( !previewRefForShadowItem( item ) )
            continue;

        ++renderedItemCount;

        if( std::optional<AI_VISUAL_ANCHOR_RECORD> anchor =
                    previewAnchorForShadowItem( item ) )
        {
            previewAnchors.push_back( *anchor );
        }

        if( shadowItemHasValidationOverlay( item ) )
            ++renderedOverlayCount;
    }

    AI_VISUAL_SNAPSHOT previewFrame;
    previewFrame.m_FrameId = wxString::Format(
            wxS( "hidden_preview_after_%llu" ),
            static_cast<unsigned long long>( previewId ) );
    previewFrame.m_FrameKind = wxS( "preview_after" );
    previewFrame.m_Source = wxS( "pcbnew.hidden_semantic_preview" );
    previewFrame.m_UnavailableReason =
            wxS( "hidden_attempt_preview_is_semantic_only" );

    result.m_Ok = true;
    result.m_PreviewId = previewId;
    result.m_RenderedItemCount = renderedItemCount;
    result.m_ResultJson = fromJson( {
        { "status", "preview_rendered" },
        { "native_preview", true },
        { "hidden_thread_safe_render", true },
        { "visible_preview_created", false },
        { "preview_id", previewId },
        { "arguments", parseObjectJson( aArgumentsJson ) },
        { "rendered_item_count", renderedItemCount },
        { "rendered_overlay_count", renderedOverlayCount },
        { "preview_frame",
          previewFrameJson( previewFrame, previewId, aSession,
                            renderedItemCount, renderedOverlayCount,
                            previewAnchors ) }
    } );

    return result;
}
} // namespace


KISURF_AI_PCB_SESSION_PREVIEW_SERVICE::KISURF_AI_PCB_SESSION_PREVIEW_SERVICE(
        BOARD& aBoard, KIGFX::VIEW& aView, EDA_DRAW_PANEL_GAL* aCanvas ) :
        KISURF_AI_PCB_SESSION_PREVIEW_SERVICE(
                [&aBoard]() { return &aBoard; },
                [&aView]() { return &aView; },
                [aCanvas]() { return aCanvas; } )
{
}


KISURF_AI_PCB_SESSION_PREVIEW_SERVICE::KISURF_AI_PCB_SESSION_PREVIEW_SERVICE(
        BOARD_PROVIDER aBoardProvider, VIEW_PROVIDER aViewProvider,
        CANVAS_PROVIDER aCanvasProvider ) :
        m_BoardProvider( std::move( aBoardProvider ) ),
        m_ViewProvider( std::move( aViewProvider ) ),
        m_CanvasProvider( std::move( aCanvasProvider ) )
{
}


void KISURF_AI_PCB_SESSION_PREVIEW_SERVICE::SetPreviewFrameCaptureProvider(
        PREVIEW_FRAME_CAPTURE_PROVIDER aProvider )
{
    m_PreviewFrameCaptureProvider = std::move( aProvider );
}


const std::vector<BOARD_ITEM*>&
KISURF_AI_PCB_SESSION_PREVIEW_SERVICE::PreviewedItems() const
{
    static const std::vector<BOARD_ITEM*> emptyItems;
    return m_Adapter ? m_Adapter->PreviewedItems() : emptyItems;
}


bool KISURF_AI_PCB_SESSION_PREVIEW_SERVICE::ensureBackend(
        AI_SESSION_PREVIEW_RESULT& aResult )
{
    BOARD* board = m_BoardProvider ? m_BoardProvider() : nullptr;
    KIGFX::VIEW* view = m_ViewProvider ? m_ViewProvider() : nullptr;

    if( !board || !view )
    {
        aResult.m_Ok = false;
        aResult.m_ErrorCode = wxS( "preview_backend_unavailable" );
        aResult.m_Message =
                wxS( "PCB session preview service cannot access the active board view." );
        return false;
    }

    if( m_PreviewManager && board == m_CurrentBoard && view == m_CurrentView )
        return true;

    if( m_PreviewManager )
        m_PreviewManager->ClearPreview();

    m_PreviewManager.reset();
    m_Adapter.reset();
    m_Resolver.reset();

    m_CurrentBoard = board;
    m_CurrentView = view;
    m_Resolver = std::make_unique<KISURF_AI_PCB_OBJECT_RESOLVER>( *m_CurrentBoard );
    m_Adapter = std::make_unique<KISURF_AI_PCB_PREVIEW_ADAPTER>(
            *m_Resolver, *m_CurrentView );
    m_PreviewManager = std::make_unique<AI_PREVIEW_MANAGER>( *m_Adapter );
    m_ActiveSessionId = 0;
    return true;
}


AI_SESSION_PREVIEW_RESULT KISURF_AI_PCB_SESSION_PREVIEW_SERVICE::RenderPreview(
        const AI_EXECUTION_SESSION& aSession, const wxString& aArgumentsJson )
{
    AI_SESSION_PREVIEW_RESULT result;

    if( aSession.EditorKind() != AI_EDITOR_KIND::Pcb )
    {
        result.m_Ok = false;
        result.m_ErrorCode = wxS( "wrong_editor" );
        result.m_Message = wxS( "PCB session preview service can only render PCB sessions." );
        return result;
    }

    if( shouldRenderSemanticOnly( aArgumentsJson ) )
        return renderSemanticOnlyPreview( aSession, aArgumentsJson );

    if( !ensureBackend( result ) )
        return result;

    nlohmann::json provenance = {
        { "source", "ai_session" },
        { "session_id", aSession.SessionId() },
        { "board_id", toUtf8String( aSession.BoardId() ) },
        { "epoch", aSession.Epoch() },
        { "arguments", parseObjectJson( aArgumentsJson ) }
    };

    m_ActiveSessionId = aSession.SessionId();
    const uint64_t previewId = m_PreviewManager->BeginPreview( fromJson( provenance ) );
    size_t renderedItemCount = 0;
    size_t renderedOverlayCount = 0;
    std::vector<AI_VISUAL_ANCHOR_RECORD> previewAnchors;

    for( const AI_SHADOW_ITEM& item : aSession.ShadowBoard().QueryItems() )
    {
        std::optional<AI_OBJECT_REF> ref = previewRefForShadowItem( item );

        if( !ref )
            continue;

        if( std::optional<AI_VISUAL_ANCHOR_RECORD> anchor = previewAnchorForShadowItem( item ) )
            previewAnchors.push_back( *anchor );

        const size_t beforeCount = m_Adapter->PreviewedItems().size();
        m_PreviewManager->ShowObject( *ref );

        if( m_Adapter->PreviewedItems().size() > beforeCount )
            ++renderedItemCount;

        wxString severity = metadataValue( item, wxS( "validation_status" ) );

        if( severity.IsEmpty() )
            severity = metadataValue( item, wxS( "validation_severity" ) );

        if( !severity.IsEmpty() )
        {
            wxString message = metadataValue( item, wxS( "validation_message" ) );

            if( message.IsEmpty() )
                message = severity;

            const size_t beforeOverlayCount = m_Adapter->PreviewedItems().size();
            m_PreviewManager->ShowItemOverlay( shadowItemPreviewLabel( item ),
                                               wxS( "validation" ), severity, message,
                                               validationOverlayGeometry( item ),
                                               validationOverlayLayer( item ) );

            if( m_Adapter->PreviewedItems().size() > beforeOverlayCount )
                ++renderedOverlayCount;
        }
    }

    AI_VISUAL_SNAPSHOT previewFrame;

    if( m_PreviewFrameCaptureProvider )
        previewFrame = m_PreviewFrameCaptureProvider( previewId, aSession );
    else if( m_CanvasProvider )
    {
        if( EDA_DRAW_PANEL_GAL* canvas = m_CanvasProvider() )
        {
            previewFrame = capturePreviewFrameFromCanvas( *canvas, previewId,
                                                          aSession, previewAnchors );
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
        { "rendered_overlay_count", renderedOverlayCount },
        { "preview_frame",
          previewFrameJson( previewFrame, previewId, aSession,
                            renderedItemCount, renderedOverlayCount,
                            previewAnchors ) }
    } );
    return result;
}


void KISURF_AI_PCB_SESSION_PREVIEW_SERVICE::ClearPreview( uint64_t aSessionId )
{
    if( m_ActiveSessionId != aSessionId )
        return;

    if( m_PreviewManager )
        m_PreviewManager->ClearPreview();

    m_ActiveSessionId = 0;
}
