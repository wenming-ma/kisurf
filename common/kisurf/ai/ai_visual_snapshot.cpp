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

#include <algorithm>
#include <cmath>
#include <wx/base64.h>
#include <wx/buffer.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/sstream.h>


namespace
{
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
