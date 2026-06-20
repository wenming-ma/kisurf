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

#include <wx/bitmap.h>
#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <wx/image.h>


bool CaptureAiVisualSnapshotFromCanvas(
        EDA_DRAW_PANEL_GAL& aCanvas,
        AI_VISUAL_SNAPSHOT& aSnapshot,
        const AI_VISUAL_CAPTURE_OPTIONS& aOptions )
{
    wxImage image;
    wxString source = wxS( "canvas.opengl" );

    if( !aCanvas.GetScreenshot( image ) )
    {
        const wxSize imageSize = aCanvas.GetClientSize();

        if( imageSize.x <= 0 || imageSize.y <= 0 )
        {
            aSnapshot = AI_VISUAL_SNAPSHOT();
            aSnapshot.m_Source = wxS( "canvas" );
            aSnapshot.m_UnavailableReason = wxS( "canvas_size_unavailable" );
            return false;
        }

        wxClientDC dc( &aCanvas );
        wxBitmap   bitmap( imageSize.x, imageSize.y );
        wxMemoryDC memdc;

        memdc.SelectObject( bitmap );
        memdc.Blit( 0, 0, imageSize.x, imageSize.y, &dc, 0, 0 );
        memdc.SelectObject( wxNullBitmap );

        image = bitmap.ConvertToImage();
        source = wxS( "canvas.dc" );
    }

    aSnapshot = MakeAiVisualSnapshotFromImage( image, source, aOptions );
    return aSnapshot.HasPixels();
}
