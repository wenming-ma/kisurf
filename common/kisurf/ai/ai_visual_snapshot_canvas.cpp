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

#include <class_draw_panel_gal.h>
#include <math/vector2d.h>
#include <view/view.h>

#include <wx/bitmap.h>
#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <wx/image.h>


namespace
{
bool hasPixelWorldTransform( const AI_VISUAL_PIXEL_WORLD_TRANSFORM& aTransform )
{
    return aTransform.m_WorldXPerPixelX != 0.0
           || aTransform.m_WorldXPerPixelY != 0.0
           || aTransform.m_WorldYPerPixelX != 0.0
           || aTransform.m_WorldYPerPixelY != 0.0;
}


void bindCanvasPixelWorldTransform( EDA_DRAW_PANEL_GAL& aCanvas,
                                    AI_VISUAL_CONTEXT_FRAME_REQUEST& aRequest )
{
    if( hasPixelWorldTransform( aRequest.m_PixelWorldTransform ) )
        return;

    KIGFX::VIEW* view = aCanvas.GetView();

    if( !view )
        return;

    const VECTOR2D origin = view->ToWorld( VECTOR2D( 0.0, 0.0 ) );
    const VECTOR2D pixelX = view->ToWorld( VECTOR2D( 1.0, 0.0 ) );
    const VECTOR2D pixelY = view->ToWorld( VECTOR2D( 0.0, 1.0 ) );

    aRequest.m_PixelWorldTransform.m_WorldOriginX = origin.x;
    aRequest.m_PixelWorldTransform.m_WorldOriginY = origin.y;
    aRequest.m_PixelWorldTransform.m_WorldXPerPixelX = pixelX.x - origin.x;
    aRequest.m_PixelWorldTransform.m_WorldYPerPixelX = pixelX.y - origin.y;
    aRequest.m_PixelWorldTransform.m_WorldXPerPixelY = pixelY.x - origin.x;
    aRequest.m_PixelWorldTransform.m_WorldYPerPixelY = pixelY.y - origin.y;
}


bool captureCanvasImage( EDA_DRAW_PANEL_GAL& aCanvas, wxImage& aImage,
                         wxString& aSource, wxString& aUnavailableReason )
{
    aSource = wxS( "canvas.opengl" );

    if( aCanvas.GetScreenshot( aImage ) )
        return true;

    const wxSize imageSize = aCanvas.GetClientSize();

    if( imageSize.x <= 0 || imageSize.y <= 0 )
    {
        aSource = wxS( "canvas" );
        aUnavailableReason = wxS( "canvas_size_unavailable" );
        return false;
    }

    wxClientDC dc( &aCanvas );
    wxBitmap   bitmap( imageSize.x, imageSize.y );
    wxMemoryDC memdc;

    memdc.SelectObject( bitmap );
    memdc.Blit( 0, 0, imageSize.x, imageSize.y, &dc, 0, 0 );
    memdc.SelectObject( wxNullBitmap );

    aImage = bitmap.ConvertToImage();
    aSource = wxS( "canvas.dc" );
    return aImage.IsOk();
}
} // namespace


bool CaptureAiVisualSnapshotFromCanvas(
        EDA_DRAW_PANEL_GAL& aCanvas,
        AI_VISUAL_SNAPSHOT& aSnapshot,
        const AI_VISUAL_CAPTURE_OPTIONS& aOptions )
{
    wxImage image;
    wxString source;
    wxString unavailableReason;
    AI_VISUAL_OBSERVATION_ARTIFACT artifact;
    AI_VISUAL_CONTEXT_FRAME_REQUEST request;

    request.m_FrameId = wxS( "current_viewport" );
    request.m_FrameKind = wxS( "viewport_raw" );

    if( !captureCanvasImage( aCanvas, image, source, unavailableReason ) )
    {
        aSnapshot = AI_VISUAL_SNAPSHOT();
        aSnapshot.m_Source = source;
        aSnapshot.m_UnavailableReason = unavailableReason;
        return false;
    }

    request.m_Source = source;
    bindCanvasPixelWorldTransform( aCanvas, request );

    artifact = BuildAiVisualContextFrameFromImage( image, request, aOptions );
    aSnapshot = artifact.m_Snapshot;
    return aSnapshot.HasPixels();
}


bool BuildAiVisualContextFrameFromCanvas(
        EDA_DRAW_PANEL_GAL& aCanvas,
        AI_VISUAL_OBSERVATION_ARTIFACT& aArtifact,
        const AI_VISUAL_CONTEXT_FRAME_REQUEST& aRequest,
        const AI_VISUAL_CAPTURE_OPTIONS& aOptions )
{
    wxImage  image;
    wxString source;
    wxString unavailableReason;

    AI_VISUAL_CONTEXT_FRAME_REQUEST request = aRequest;
    bindCanvasPixelWorldTransform( aCanvas, request );

    if( !captureCanvasImage( aCanvas, image, source, unavailableReason ) )
    {
        if( request.m_Source.IsEmpty() )
            request.m_Source = source;

        aArtifact = BuildAiVisualContextFrameFromImage( image, request, aOptions );
        aArtifact.m_Snapshot.m_UnavailableReason = unavailableReason;
        return false;
    }

    if( request.m_Source.IsEmpty() )
        request.m_Source = source;

    aArtifact = BuildAiVisualContextFrameFromImage( image, request, aOptions );
    return aArtifact.m_Snapshot.HasPixels();
}
