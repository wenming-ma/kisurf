/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 */

#include <kisurf/ai/ai_visual_snapshot.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <wx/base64.h>
#include <wx/buffer.h>
#include <wx/gdicmn.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/sstream.h>


namespace
{
std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


wxString fromUtf8String( const std::string& aText )
{
    return wxString::FromUTF8( aText.c_str() );
}


wxImage scaledImage( const wxImage& aImage, int aMaxEdgePixels )
{
    if( aMaxEdgePixels <= 0 )
        return wxImage( aImage );

    const int width = aImage.GetWidth();
    const int height = aImage.GetHeight();
    const int maxEdge = std::max( width, height );

    if( maxEdge <= aMaxEdgePixels )
        return wxImage( aImage );

    const double scale = static_cast<double>( aMaxEdgePixels ) / maxEdge;
    const int    scaledWidth = std::max( 1, static_cast<int>( std::lround( width * scale ) ) );
    const int    scaledHeight = std::max( 1, static_cast<int>( std::lround( height * scale ) ) );

    return aImage.Scale( scaledWidth, scaledHeight, wxIMAGE_QUALITY_HIGH );
}


struct CROP_RECT
{
    wxRect m_Rect;
    bool   m_UsesFullImage = true;
};


CROP_RECT cropRectForBounds( const wxImage& aImage, const AI_VISUAL_BOUNDS& aBounds )
{
    CROP_RECT result;

    if( !aImage.IsOk() || aImage.GetWidth() <= 0 || aImage.GetHeight() <= 0 )
        return result;

    if( aBounds.m_Right <= aBounds.m_Left || aBounds.m_Bottom <= aBounds.m_Top )
    {
        result.m_Rect = wxRect( 0, 0, aImage.GetWidth(), aImage.GetHeight() );
        result.m_UsesFullImage = true;
        return result;
    }

    const int left = std::clamp( static_cast<int>( std::floor( aBounds.m_Left ) ),
                                 0, aImage.GetWidth() );
    const int top = std::clamp( static_cast<int>( std::floor( aBounds.m_Top ) ),
                                0, aImage.GetHeight() );
    const int right = std::clamp( static_cast<int>( std::ceil( aBounds.m_Right ) ),
                                  0, aImage.GetWidth() );
    const int bottom = std::clamp( static_cast<int>( std::ceil( aBounds.m_Bottom ) ),
                                   0, aImage.GetHeight() );

    if( right <= left || bottom <= top )
    {
        result.m_Rect = wxRect( 0, 0, aImage.GetWidth(), aImage.GetHeight() );
        result.m_UsesFullImage = true;
        return result;
    }

    result.m_Rect = wxRect( left, top, right - left, bottom - top );
    result.m_UsesFullImage = result.m_Rect.GetX() == 0 && result.m_Rect.GetY() == 0
                             && result.m_Rect.GetWidth() == aImage.GetWidth()
                             && result.m_Rect.GetHeight() == aImage.GetHeight();
    return result;
}


wxImage cropImageForBounds( const wxImage& aImage, const CROP_RECT& aCrop )
{
    if( !aImage.IsOk() )
        return wxImage( aImage );

    if( aCrop.m_UsesFullImage )
        return wxImage( aImage );

    return aImage.GetSubImage( aCrop.m_Rect );
}


void setPixelIfInside( wxImage& aImage, int aX, int aY,
                       unsigned char aRed, unsigned char aGreen, unsigned char aBlue )
{
    if( !aImage.IsOk() || aX < 0 || aY < 0 || aX >= aImage.GetWidth()
            || aY >= aImage.GetHeight() )
    {
        return;
    }

    aImage.SetRGB( aX, aY, aRed, aGreen, aBlue );
}


void drawAnchorOverlay( wxImage& aImage, const AI_VISUAL_ANCHOR_RECORD& aAnchor,
                        const wxRect& aCropRect )
{
    if( !aImage.IsOk() || aImage.GetWidth() <= 0 || aImage.GetHeight() <= 0 )
        return;

    const int left = static_cast<int>( std::floor( aAnchor.m_PixelBounds.m_Left ) )
                     - aCropRect.GetX();
    const int top = static_cast<int>( std::floor( aAnchor.m_PixelBounds.m_Top ) )
                    - aCropRect.GetY();
    const int right = static_cast<int>( std::ceil( aAnchor.m_PixelBounds.m_Right ) )
                      - aCropRect.GetX() - 1;
    const int bottom = static_cast<int>( std::ceil( aAnchor.m_PixelBounds.m_Bottom ) )
                       - aCropRect.GetY() - 1;

    if( right < 0 || bottom < 0 || left >= aImage.GetWidth() || top >= aImage.GetHeight() )
        return;

    const int clippedLeft = std::clamp( left, 0, aImage.GetWidth() - 1 );
    const int clippedTop = std::clamp( top, 0, aImage.GetHeight() - 1 );
    const int clippedRight = std::clamp( right, 0, aImage.GetWidth() - 1 );
    const int clippedBottom = std::clamp( bottom, 0, aImage.GetHeight() - 1 );

    for( int x = clippedLeft; x <= clippedRight; ++x )
    {
        setPixelIfInside( aImage, x, clippedTop, 220, 32, 32 );
        setPixelIfInside( aImage, x, clippedBottom, 220, 32, 32 );
    }

    for( int y = clippedTop; y <= clippedBottom; ++y )
    {
        setPixelIfInside( aImage, clippedLeft, y, 220, 32, 32 );
        setPixelIfInside( aImage, clippedRight, y, 220, 32, 32 );
    }
}


void drawAnchorOverlays( wxImage& aImage,
                         const std::vector<AI_VISUAL_ANCHOR_RECORD>& aAnchors,
                         const wxRect& aCropRect )
{
    for( const AI_VISUAL_ANCHOR_RECORD& anchor : aAnchors )
        drawAnchorOverlay( aImage, anchor, aCropRect );
}


wxString contextFrameSource( const AI_VISUAL_CONTEXT_FRAME_REQUEST& aRequest )
{
    if( aRequest.m_Source.IsEmpty() )
    {
        if( aRequest.m_FrameKind.IsEmpty() )
            return wxS( "visual_context_frame" );

        return aRequest.m_FrameKind;
    }

    if( aRequest.m_FrameKind.IsEmpty() )
        return aRequest.m_Source;

    return aRequest.m_Source + wxS( "." ) + aRequest.m_FrameKind;
}


bool encodeImageToPng( const wxImage& aImage, wxMemoryBuffer& aPngData )
{
    wxImage imageCopy( aImage );

    imageCopy.SetOption( wxIMAGE_OPTION_PNG_COMPRESSION_LEVEL, 1 );
    imageCopy.SetOption( wxIMAGE_OPTION_PNG_COMPRESSION_STRATEGY, 3 );
    imageCopy.SetOption( wxIMAGE_OPTION_PNG_FILTER, 0x08 );

    wxMemoryOutputStream   memStream;
    wxBufferedOutputStream bufferedStream( memStream );

    if( !imageCopy.SaveFile( bufferedStream, wxBITMAP_TYPE_PNG ) )
        return false;

    bufferedStream.Close();

    auto* buffer = memStream.GetOutputStreamBuffer();
    const size_t byteCount = static_cast<size_t>( buffer->GetIntPosition() );

    if( byteCount == 0 )
        return false;

    aPngData.SetDataLen( 0 );
    aPngData.AppendData( buffer->GetBufferStart(), byteCount );
    return true;
}


AI_VISUAL_SNAPSHOT unavailableSnapshot( const wxString& aSource,
                                        const wxString& aReason )
{
    AI_VISUAL_SNAPSHOT snapshot;
    snapshot.m_Source = aSource;
    snapshot.m_UnavailableReason = aReason;
    return snapshot;
}


nlohmann::json boundsJson( const AI_VISUAL_BOUNDS& aBounds )
{
    return {
        { "left", aBounds.m_Left },
        { "top", aBounds.m_Top },
        { "right", aBounds.m_Right },
        { "bottom", aBounds.m_Bottom }
    };
}


bool validBounds( const AI_VISUAL_BOUNDS& aBounds )
{
    return aBounds.m_Right > aBounds.m_Left && aBounds.m_Bottom > aBounds.m_Top;
}


AI_VISUAL_BOUNDS expandedPixelBounds( const AI_VISUAL_BOUNDS& aBounds, int aPaddingPx )
{
    const double padding = std::max( 0, aPaddingPx );

    return {
        aBounds.m_Left - padding,
        aBounds.m_Top - padding,
        aBounds.m_Right + padding,
        aBounds.m_Bottom + padding
    };
}


double transformWorldX( const AI_VISUAL_PIXEL_WORLD_TRANSFORM& aTransform,
                        double aPixelX, double aPixelY )
{
    return aTransform.m_WorldOriginX
           + aPixelX * aTransform.m_WorldXPerPixelX
           + aPixelY * aTransform.m_WorldXPerPixelY;
}


double transformWorldY( const AI_VISUAL_PIXEL_WORLD_TRANSFORM& aTransform,
                        double aPixelX, double aPixelY )
{
    return aTransform.m_WorldOriginY
           + aPixelX * aTransform.m_WorldYPerPixelX
           + aPixelY * aTransform.m_WorldYPerPixelY;
}


bool hasPixelWorldTransform( const AI_VISUAL_PIXEL_WORLD_TRANSFORM& aTransform )
{
    return aTransform.m_WorldXPerPixelX != 0.0
           || aTransform.m_WorldXPerPixelY != 0.0
           || aTransform.m_WorldYPerPixelX != 0.0
           || aTransform.m_WorldYPerPixelY != 0.0;
}


AI_VISUAL_BOUNDS worldBoundsForPixelBounds(
        const AI_VISUAL_BOUNDS& aPixelBounds,
        const AI_VISUAL_PIXEL_WORLD_TRANSFORM& aTransform )
{
    const double xs[] = {
        transformWorldX( aTransform, aPixelBounds.m_Left, aPixelBounds.m_Top ),
        transformWorldX( aTransform, aPixelBounds.m_Right, aPixelBounds.m_Top ),
        transformWorldX( aTransform, aPixelBounds.m_Left, aPixelBounds.m_Bottom ),
        transformWorldX( aTransform, aPixelBounds.m_Right, aPixelBounds.m_Bottom )
    };

    const double ys[] = {
        transformWorldY( aTransform, aPixelBounds.m_Left, aPixelBounds.m_Top ),
        transformWorldY( aTransform, aPixelBounds.m_Right, aPixelBounds.m_Top ),
        transformWorldY( aTransform, aPixelBounds.m_Left, aPixelBounds.m_Bottom ),
        transformWorldY( aTransform, aPixelBounds.m_Right, aPixelBounds.m_Bottom )
    };

    return {
        *std::min_element( std::begin( xs ), std::end( xs ) ),
        *std::min_element( std::begin( ys ), std::end( ys ) ),
        *std::max_element( std::begin( xs ), std::end( xs ) ),
        *std::max_element( std::begin( ys ), std::end( ys ) )
    };
}


nlohmann::json anchorJson( const AI_VISUAL_ANCHOR_RECORD& aAnchor )
{
    return {
        { "anchor_id", toUtf8String( aAnchor.m_AnchorId ) },
        { "object_id", toUtf8String( aAnchor.m_ObjectId ) },
        { "handle", toUtf8String( aAnchor.m_Handle ) },
        { "layer", toUtf8String( aAnchor.m_Layer ) },
        { "net_name", toUtf8String( aAnchor.m_NetName ) },
        { "world_xy", { { "x", aAnchor.m_WorldX }, { "y", aAnchor.m_WorldY } } },
        { "world_bounds", boundsJson( aAnchor.m_WorldBounds ) },
        { "pixel_bounds", boundsJson( aAnchor.m_PixelBounds ) }
    };
}


nlohmann::json issueJson( const AI_VISUAL_OBSERVATION_REQUEST& aRequest )
{
    nlohmann::json issue = {
        { "id", toUtf8String( aRequest.m_IssueId ) },
        { "kind", toUtf8String( aRequest.m_IssueKind ) },
        { "severity", toUtf8String( aRequest.m_IssueSeverity ) },
        { "world_bounds", boundsJson( aRequest.m_IssueWorldBounds ) },
        { "pixel_bounds", boundsJson( aRequest.m_IssuePixelBounds ) }
    };

    if( !aRequest.m_IssueTitle.IsEmpty() )
        issue["title"] = toUtf8String( aRequest.m_IssueTitle );

    if( !aRequest.m_IssueMessage.IsEmpty() )
        issue["message"] = toUtf8String( aRequest.m_IssueMessage );

    return issue;
}


nlohmann::json pixelWorldTransformJson(
        const AI_VISUAL_PIXEL_WORLD_TRANSFORM& aTransform )
{
    return {
        { "world_origin", { { "x", aTransform.m_WorldOriginX },
                            { "y", aTransform.m_WorldOriginY } } },
        { "world_x_per_pixel_x", aTransform.m_WorldXPerPixelX },
        { "world_x_per_pixel_y", aTransform.m_WorldXPerPixelY },
        { "world_y_per_pixel_x", aTransform.m_WorldYPerPixelX },
        { "world_y_per_pixel_y", aTransform.m_WorldYPerPixelY }
    };
}


nlohmann::json relatedFrameJson( const AI_VISUAL_FRAME_RELATION_RECORD& aRelation )
{
    nlohmann::json relation = {
        { "relation", toUtf8String( aRelation.m_Relation ) },
        { "frame_id", toUtf8String( aRelation.m_FrameId ) },
        { "frame_kind", toUtf8String( aRelation.m_FrameKind ) }
    };

    if( !aRelation.m_IssueId.IsEmpty() )
        relation["issue_id"] = toUtf8String( aRelation.m_IssueId );

    return relation;
}


AI_VISUAL_ANCHOR_RESOLUTION anchorResolutionError( const wxString& aErrorCode,
                                                   const wxString& aMessage )
{
    AI_VISUAL_ANCHOR_RESOLUTION result;
    result.m_ErrorCode = aErrorCode;
    result.m_Message = aMessage;
    return result;
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


bool jsonPoint( const nlohmann::json& aObject, double& aX, double& aY )
{
    return jsonNumberField( aObject, "x", aX )
        && jsonNumberField( aObject, "y", aY );
}


bool hasPixelOnlyReference( const nlohmann::json& aReference )
{
    if( !aReference.is_object() )
        return false;

    if( aReference.contains( "pixel_position" )
        || aReference.contains( "pixel" )
        || aReference.contains( "pixel_bounds" ) )
    {
        return true;
    }

    if( aReference.contains( "coordinate_space" )
        && aReference["coordinate_space"].is_string()
        && aReference["coordinate_space"].get<std::string>() == "pixel" )
    {
        return true;
    }

    return aReference.contains( "x" ) && aReference.contains( "y" )
        && !aReference.contains( "world_position" )
        && !aReference.contains( "world" )
        && !aReference.contains( "anchor_id" );
}


AI_VISUAL_ANCHOR_RESOLUTION resolutionFromAnchor(
        const AI_VISUAL_ANCHOR_RECORD& aAnchor )
{
    AI_VISUAL_ANCHOR_RESOLUTION result;
    result.m_Resolved = true;
    result.m_Kind = wxS( "anchor" );
    result.m_AnchorId = aAnchor.m_AnchorId;
    result.m_ObjectId = aAnchor.m_ObjectId;
    result.m_Handle = aAnchor.m_Handle;
    result.m_Layer = aAnchor.m_Layer;
    result.m_NetName = aAnchor.m_NetName;
    result.m_WorldX = aAnchor.m_WorldX;
    result.m_WorldY = aAnchor.m_WorldY;
    result.m_WorldBounds = aAnchor.m_WorldBounds;
    result.m_PixelBounds = aAnchor.m_PixelBounds;
    return result;
}


nlohmann::json parseJsonObjectOrEmpty( const wxString& aText )
{
    if( aText.IsEmpty() )
        return nlohmann::json::object();

    nlohmann::json parsed = nlohmann::json::parse( toUtf8String( aText ), nullptr, false );

    if( parsed.is_discarded() || !parsed.is_object() )
        return nlohmann::json::object();

    return parsed;
}
} // namespace


AI_VISUAL_SNAPSHOT MakeAiVisualSnapshotFromImage(
        const wxImage& aImage,
        const wxString& aSource,
        const AI_VISUAL_CAPTURE_OPTIONS& aOptions )
{
    AI_VISUAL_SNAPSHOT snapshot;

    if( !aImage.IsOk() || aImage.GetWidth() <= 0 || aImage.GetHeight() <= 0 )
        return unavailableSnapshot( aSource, wxS( "invalid_image" ) );

    wxImage image = scaledImage( aImage, aOptions.m_MaxEdgePixels );
    wxMemoryBuffer pngData;

    if( !encodeImageToPng( image, pngData ) )
    {
        snapshot = unavailableSnapshot( aSource, wxS( "png_encode_failed" ) );
        snapshot.m_MimeType = wxS( "image/png" );
        snapshot.m_WidthPx = image.GetWidth();
        snapshot.m_HeightPx = image.GetHeight();
        return snapshot;
    }

    wxString encoded = wxBase64Encode( pngData.GetData(), pngData.GetDataLen() );

    if( encoded.IsEmpty() )
    {
        snapshot = unavailableSnapshot( aSource, wxS( "base64_encode_failed" ) );
        snapshot.m_MimeType = wxS( "image/png" );
        snapshot.m_WidthPx = image.GetWidth();
        snapshot.m_HeightPx = image.GetHeight();
        snapshot.m_ByteSize = pngData.GetDataLen();
        return snapshot;
    }

    snapshot.m_Source = aSource;
    snapshot.m_MimeType = wxS( "image/png" );
    snapshot.m_DataUri = wxS( "data:image/png;base64," ) + encoded;
    snapshot.m_WidthPx = image.GetWidth();
    snapshot.m_HeightPx = image.GetHeight();
    snapshot.m_ByteSize = pngData.GetDataLen();
    return snapshot;
}


AI_VISUAL_SNAPSHOT BuildAiViewportVisualSnapshotFromImage(
        const wxImage& aImage,
        const wxString& aSource,
        const AI_VISUAL_CAPTURE_OPTIONS& aOptions )
{
    AI_VISUAL_SNAPSHOT snapshot =
            MakeAiVisualSnapshotFromImage( aImage, aSource, aOptions );

    if( !snapshot.HasPixels() )
        return snapshot;

    AI_VISUAL_OBSERVATION_REQUEST request;
    request.m_FrameId = wxS( "current_viewport" );
    request.m_FrameKind = wxS( "viewport_raw" );
    request.m_PixelBounds = AI_VISUAL_BOUNDS{
        0.0,
        0.0,
        static_cast<double>( snapshot.m_WidthPx ),
        static_cast<double>( snapshot.m_HeightPx )
    };

    return BuildAiVisualObservationArtifact( snapshot, request ).m_Snapshot;
}


AI_VISUAL_OBSERVATION_ARTIFACT BuildAiVisualObservationArtifact(
        const AI_VISUAL_SNAPSHOT& aSnapshot,
        const AI_VISUAL_OBSERVATION_REQUEST& aRequest )
{
    AI_VISUAL_OBSERVATION_ARTIFACT artifact;
    artifact.m_Snapshot = aSnapshot;
    artifact.m_Snapshot.m_FrameId = aRequest.m_FrameId;
    artifact.m_Snapshot.m_FrameKind = aRequest.m_FrameKind;

    nlohmann::json anchors = nlohmann::json::array();

    for( const AI_VISUAL_ANCHOR_RECORD& anchor : aRequest.m_Anchors )
        anchors.push_back( anchorJson( anchor ) );

    nlohmann::json relatedFrames = nlohmann::json::array();

    for( const AI_VISUAL_FRAME_RELATION_RECORD& relation : aRequest.m_RelatedFrames )
        relatedFrames.push_back( relatedFrameJson( relation ) );

    nlohmann::json sidecar = {
        { "schema", { { "name", "kisurf.ai.visual_observation_artifact" },
                      { "version", 1 } } },
        { "frame_id", toUtf8String( aRequest.m_FrameId ) },
        { "frame_kind", toUtf8String( aRequest.m_FrameKind ) },
        { "document_revision", aRequest.m_DocumentRevision },
        { "preview_revision", aRequest.m_PreviewRevision },
        { "source", toUtf8String( aSnapshot.m_Source ) },
        { "mime_type", toUtf8String( aSnapshot.m_MimeType ) },
        { "width_px", aSnapshot.m_WidthPx },
        { "height_px", aSnapshot.m_HeightPx },
        { "byte_size", aSnapshot.m_ByteSize },
        { "world_bounds", boundsJson( aRequest.m_WorldBounds ) },
        { "pixel_bounds", boundsJson( aRequest.m_PixelBounds ) },
        { "pixel_world_transform",
          pixelWorldTransformJson( aRequest.m_PixelWorldTransform ) },
        { "related_frames", std::move( relatedFrames ) },
        { "anchors", std::move( anchors ) }
    };

    if( !aRequest.m_AnnotationMode.IsEmpty() )
        sidecar["annotation_mode"] = toUtf8String( aRequest.m_AnnotationMode );

    if( !aRequest.m_AttemptId.IsEmpty() )
        sidecar["attempt_id"] = toUtf8String( aRequest.m_AttemptId );

    if( !aRequest.m_PreviewId.IsEmpty() )
        sidecar["preview_id"] = toUtf8String( aRequest.m_PreviewId );

    if( !aRequest.m_IssueId.IsEmpty() || !aRequest.m_IssueKind.IsEmpty()
        || !aRequest.m_IssueSeverity.IsEmpty() )
    {
        sidecar["issue"] = issueJson( aRequest );
    }

    artifact.m_SidecarJson = fromUtf8String( sidecar.dump() );
    artifact.m_Snapshot.m_SidecarJson = artifact.m_SidecarJson;
    return artifact;
}


AI_VISUAL_OBSERVATION_ARTIFACT BuildAiVisualContextFrameFromImage(
        const wxImage& aImage,
        const AI_VISUAL_CONTEXT_FRAME_REQUEST& aRequest,
        const AI_VISUAL_CAPTURE_OPTIONS& aOptions )
{
    CROP_RECT crop = cropRectForBounds( aImage, aRequest.m_PixelBounds );
    wxImage   frameImage = cropImageForBounds( aImage, crop );
    AI_VISUAL_BOUNDS pixelBounds = aRequest.m_PixelBounds;

    if( !validBounds( pixelBounds ) && crop.m_Rect.GetWidth() > 0
        && crop.m_Rect.GetHeight() > 0 )
    {
        pixelBounds = AI_VISUAL_BOUNDS{
            static_cast<double>( crop.m_Rect.GetX() ),
            static_cast<double>( crop.m_Rect.GetY() ),
            static_cast<double>( crop.m_Rect.GetX() + crop.m_Rect.GetWidth() ),
            static_cast<double>( crop.m_Rect.GetY() + crop.m_Rect.GetHeight() )
        };
    }

    AI_VISUAL_BOUNDS worldBounds = aRequest.m_WorldBounds;

    if( !validBounds( worldBounds ) && validBounds( pixelBounds )
        && hasPixelWorldTransform( aRequest.m_PixelWorldTransform ) )
    {
        worldBounds = worldBoundsForPixelBounds( pixelBounds,
                                                 aRequest.m_PixelWorldTransform );
    }

    if( !aRequest.m_AnnotationMode.IsEmpty() )
        drawAnchorOverlays( frameImage, aRequest.m_Anchors, crop.m_Rect );

    AI_VISUAL_SNAPSHOT snapshot = MakeAiVisualSnapshotFromImage(
            frameImage, contextFrameSource( aRequest ), aOptions );

    AI_VISUAL_OBSERVATION_REQUEST observation;
    observation.m_FrameId = aRequest.m_FrameId;
    observation.m_FrameKind = aRequest.m_FrameKind;
    observation.m_AnnotationMode = aRequest.m_AnnotationMode;
    observation.m_AttemptId = aRequest.m_AttemptId;
    observation.m_PreviewId = aRequest.m_PreviewId;
    observation.m_DocumentRevision = aRequest.m_DocumentRevision;
    observation.m_PreviewRevision = aRequest.m_PreviewRevision;
    observation.m_WorldBounds = worldBounds;
    observation.m_PixelBounds = pixelBounds;
    observation.m_PixelWorldTransform = aRequest.m_PixelWorldTransform;
    observation.m_Anchors = aRequest.m_Anchors;
    observation.m_RelatedFrames = aRequest.m_RelatedFrames;

    return BuildAiVisualObservationArtifact( snapshot, observation );
}


AI_VISUAL_OBSERVATION_ARTIFACT BuildAiVisualIssueCropFromImage(
        const wxImage& aImage,
        const AI_VISUAL_ISSUE_CROP_REQUEST& aRequest,
        const AI_VISUAL_CAPTURE_OPTIONS& aOptions )
{
    AI_VISUAL_CONTEXT_FRAME_REQUEST frameRequest;
    frameRequest.m_FrameId = aRequest.m_FrameId;
    frameRequest.m_FrameKind = wxS( "issue_crop" );
    frameRequest.m_Source = aRequest.m_Source;
    frameRequest.m_DocumentRevision = aRequest.m_DocumentRevision;
    frameRequest.m_PreviewRevision = aRequest.m_PreviewRevision;
    frameRequest.m_PixelWorldTransform = aRequest.m_PixelWorldTransform;
    frameRequest.m_PixelBounds = validBounds( aRequest.m_IssuePixelBounds )
            ? expandedPixelBounds( aRequest.m_IssuePixelBounds, aRequest.m_ContextPaddingPx )
            : aRequest.m_IssuePixelBounds;
    frameRequest.m_WorldBounds = aRequest.m_IssueWorldBounds;
    frameRequest.m_Anchors = aRequest.m_Anchors;

    if( hasPixelWorldTransform( aRequest.m_PixelWorldTransform )
        && validBounds( frameRequest.m_PixelBounds ) )
    {
        frameRequest.m_WorldBounds = worldBoundsForPixelBounds(
                frameRequest.m_PixelBounds, aRequest.m_PixelWorldTransform );
    }

    if( !frameRequest.m_Anchors.empty() )
        frameRequest.m_AnnotationMode = wxS( "issue_anchor_overlay" );

    if( !aRequest.m_ParentFrameId.IsEmpty() )
    {
        AI_VISUAL_FRAME_RELATION_RECORD relation;
        relation.m_Relation = wxS( "issue_crop_for" );
        relation.m_FrameId = aRequest.m_ParentFrameId;
        relation.m_FrameKind = aRequest.m_ParentFrameKind;
        relation.m_IssueId = aRequest.m_IssueId;
        frameRequest.m_RelatedFrames.push_back( relation );
    }

    AI_VISUAL_OBSERVATION_ARTIFACT artifact =
            BuildAiVisualContextFrameFromImage( aImage, frameRequest, aOptions );

    AI_VISUAL_OBSERVATION_REQUEST observation;
    observation.m_FrameId = frameRequest.m_FrameId;
    observation.m_FrameKind = frameRequest.m_FrameKind;
    observation.m_AnnotationMode = frameRequest.m_AnnotationMode;
    observation.m_DocumentRevision = frameRequest.m_DocumentRevision;
    observation.m_PreviewRevision = frameRequest.m_PreviewRevision;
    observation.m_WorldBounds = frameRequest.m_WorldBounds;
    observation.m_PixelBounds = frameRequest.m_PixelBounds;
    observation.m_PixelWorldTransform = frameRequest.m_PixelWorldTransform;
    observation.m_Anchors = frameRequest.m_Anchors;
    observation.m_RelatedFrames = frameRequest.m_RelatedFrames;
    observation.m_IssueId = aRequest.m_IssueId;
    observation.m_IssueKind = aRequest.m_IssueKind;
    observation.m_IssueSeverity = aRequest.m_IssueSeverity;
    observation.m_IssueTitle = aRequest.m_IssueTitle;
    observation.m_IssueMessage = aRequest.m_IssueMessage;
    observation.m_IssueWorldBounds = aRequest.m_IssueWorldBounds;
    observation.m_IssuePixelBounds = aRequest.m_IssuePixelBounds;

    return BuildAiVisualObservationArtifact( artifact.m_Snapshot, observation );
}


AI_VISUAL_ANCHOR_RESOLUTION ResolveAiVisualReferenceJson(
        const wxString& aReferenceJson,
        const std::vector<AI_VISUAL_ANCHOR_RECORD>& aAnchors )
{
    nlohmann::json reference =
            nlohmann::json::parse( toUtf8String( aReferenceJson ), nullptr, false );

    if( reference.is_discarded() || !reference.is_object() )
    {
        return anchorResolutionError(
                wxS( "invalid_reference_json" ),
                wxS( "Visual reference must be a JSON object." ) );
    }

    if( reference.contains( "anchor_id" ) && reference["anchor_id"].is_string() )
    {
        const wxString anchorId =
                fromUtf8String( reference["anchor_id"].get<std::string>() );

        for( const AI_VISUAL_ANCHOR_RECORD& anchor : aAnchors )
        {
            if( anchor.m_AnchorId == anchorId )
                return resolutionFromAnchor( anchor );
        }

        return anchorResolutionError(
                wxS( "anchor_not_found" ),
                wxS( "Visual anchor id was not found in the current observation." ) );
    }

    double worldX = 0.0;
    double worldY = 0.0;

    const nlohmann::json* worldPoint = nullptr;

    if( reference.contains( "world_position" ) && reference["world_position"].is_object() )
        worldPoint = &reference["world_position"];
    else if( reference.contains( "world" ) && reference["world"].is_object() )
        worldPoint = &reference["world"];

    if( worldPoint && jsonPoint( *worldPoint, worldX, worldY ) )
    {
        AI_VISUAL_ANCHOR_RESOLUTION result;
        result.m_Resolved = true;
        result.m_Kind = wxS( "world_position" );
        result.m_WorldX = worldX;
        result.m_WorldY = worldY;

        if( reference.contains( "layer" ) && reference["layer"].is_string() )
            result.m_Layer = fromUtf8String( reference["layer"].get<std::string>() );

        if( reference.contains( "net" ) && reference["net"].is_string() )
            result.m_NetName = fromUtf8String( reference["net"].get<std::string>() );
        else if( reference.contains( "net_name" ) && reference["net_name"].is_string() )
            result.m_NetName = fromUtf8String( reference["net_name"].get<std::string>() );

        return result;
    }

    if( hasPixelOnlyReference( reference ) )
    {
        return anchorResolutionError(
                wxS( "pixel_only_reference" ),
                wxS( "Pixel-only visual references must be grounded through an "
                     "anchor id or explicit world coordinate before mutation." ) );
    }

    return anchorResolutionError(
            wxS( "missing_grounding" ),
            wxS( "Visual reference must include anchor_id or world_position." ) );
}


AI_VISUAL_FRAME_POLICY_RESULT ApplyAiVisualFramePolicy(
        const AI_VISUAL_SNAPSHOT& aSnapshot, const AI_VISUAL_FRAME_POLICY& aPolicy )
{
    AI_VISUAL_FRAME_POLICY_RESULT result;
    result.m_Snapshot = aSnapshot;

    if( !aSnapshot.HasPixels() )
    {
        result.m_PixelsIncluded = false;
        result.m_OmissionReason = aSnapshot.m_UnavailableReason;
    }
    else if( !aPolicy.m_AllowPixels )
    {
        result.m_Snapshot.m_DataUri.Clear();
        result.m_Snapshot.m_UnavailableReason = wxS( "visual_pixels_disabled" );
        result.m_OmissionReason = wxS( "visual_pixels_disabled" );
    }
    else if( aSnapshot.m_DataUri.length() > aPolicy.m_MaxDataUriChars )
    {
        result.m_Snapshot.m_DataUri.Clear();
        result.m_Snapshot.m_UnavailableReason = wxS( "visual_payload_budget" );
        result.m_OmissionReason = wxS( "visual_payload_budget" );
    }
    else
    {
        result.m_PixelsIncluded = true;
    }

    nlohmann::json sidecar = {
        { "source", toUtf8String( result.m_Snapshot.m_Source ) },
        { "mime_type", toUtf8String( result.m_Snapshot.m_MimeType ) },
        { "frame_id", toUtf8String( result.m_Snapshot.m_FrameId ) },
        { "frame_kind", toUtf8String( result.m_Snapshot.m_FrameKind ) },
        { "width_px", result.m_Snapshot.m_WidthPx },
        { "height_px", result.m_Snapshot.m_HeightPx },
        { "byte_size", result.m_Snapshot.m_ByteSize },
        { "pixels_included", result.m_PixelsIncluded }
    };

    nlohmann::json artifactSidecar = parseJsonObjectOrEmpty( result.m_Snapshot.m_SidecarJson );

    if( !artifactSidecar.empty() )
        sidecar["visual_observation_artifact"] = std::move( artifactSidecar );

    if( !result.m_OmissionReason.IsEmpty() )
        sidecar["omission_reason"] = toUtf8String( result.m_OmissionReason );

    result.m_SidecarJson = fromUtf8String( sidecar.dump() );
    return result;
}
