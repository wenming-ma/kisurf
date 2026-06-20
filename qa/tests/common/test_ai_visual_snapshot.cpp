/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 */

#include <boost/test/unit_test.hpp>
#include <kisurf/ai/ai_visual_snapshot.h>

#include <wx/image.h>


BOOST_AUTO_TEST_SUITE( AiVisualSnapshot )


BOOST_AUTO_TEST_CASE( ValidImageEncodesAsPngDataUri )
{
    wxImage image( 4, 2, false );
    image.SetRGB( wxRect( 0, 0, 4, 2 ), 255, 0, 0 );

    AI_VISUAL_SNAPSHOT snapshot =
            MakeAiVisualSnapshotFromImage( image, wxS( "test.image" ) );

    BOOST_CHECK( snapshot.HasPixels() );
    BOOST_CHECK_EQUAL( snapshot.m_Source, wxString( wxS( "test.image" ) ) );
    BOOST_CHECK_EQUAL( snapshot.m_MimeType, wxString( wxS( "image/png" ) ) );
    BOOST_CHECK( snapshot.m_DataUri.StartsWith( wxS( "data:image/png;base64," ) ) );
    BOOST_CHECK_EQUAL( snapshot.m_WidthPx, 4 );
    BOOST_CHECK_EQUAL( snapshot.m_HeightPx, 2 );
    BOOST_CHECK_GT( snapshot.m_ByteSize, 8 );
    BOOST_CHECK( snapshot.m_UnavailableReason.IsEmpty() );
}


BOOST_AUTO_TEST_CASE( OversizedImageIsDownscaledToMaxEdge )
{
    wxImage image( 8, 4, false );
    image.SetRGB( wxRect( 0, 0, 8, 4 ), 0, 128, 255 );

    AI_VISUAL_CAPTURE_OPTIONS options;
    options.m_MaxEdgePixels = 4;

    AI_VISUAL_SNAPSHOT snapshot =
            MakeAiVisualSnapshotFromImage( image, wxS( "test.image" ), options );

    BOOST_CHECK( snapshot.HasPixels() );
    BOOST_CHECK_EQUAL( snapshot.m_WidthPx, 4 );
    BOOST_CHECK_EQUAL( snapshot.m_HeightPx, 2 );
}


BOOST_AUTO_TEST_CASE( InvalidImageReturnsUnavailableReason )
{
    wxImage image;

    AI_VISUAL_SNAPSHOT snapshot =
            MakeAiVisualSnapshotFromImage( image, wxS( "test.image" ) );

    BOOST_CHECK( !snapshot.HasPixels() );
    BOOST_CHECK_EQUAL( snapshot.m_Source, wxString( wxS( "test.image" ) ) );
    BOOST_CHECK( snapshot.m_DataUri.IsEmpty() );
    BOOST_CHECK_EQUAL( snapshot.m_WidthPx, 0 );
    BOOST_CHECK_EQUAL( snapshot.m_HeightPx, 0 );
    BOOST_CHECK_EQUAL( snapshot.m_UnavailableReason,
                       wxString( wxS( "invalid_image" ) ) );
}


BOOST_AUTO_TEST_SUITE_END()
