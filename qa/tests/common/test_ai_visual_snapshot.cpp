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


BOOST_AUTO_TEST_CASE( ViewportVisualSnapshotBindsDefaultFrameMetadata )
{
    wxImage image( 6, 4, false );
    image.SetRGB( wxRect( 0, 0, 6, 4 ), 80, 90, 100 );

    AI_VISUAL_SNAPSHOT snapshot =
            BuildAiViewportVisualSnapshotFromImage( image, wxS( "canvas.opengl" ) );

    BOOST_CHECK( snapshot.HasPixels() );
    BOOST_CHECK_EQUAL( snapshot.m_Source, wxString( wxS( "canvas.opengl" ) ) );
    BOOST_CHECK_EQUAL( snapshot.m_FrameId, wxString( wxS( "current_viewport" ) ) );
    BOOST_CHECK_EQUAL( snapshot.m_FrameKind, wxString( wxS( "viewport_raw" ) ) );
    BOOST_CHECK( snapshot.m_SidecarJson.Contains( wxS( "current_viewport" ) ) );
    BOOST_CHECK( snapshot.m_SidecarJson.Contains( wxS( "viewport_raw" ) ) );
    BOOST_CHECK( snapshot.m_SidecarJson.Contains( wxS( "\"right\":6.0" ) ) );
    BOOST_CHECK( snapshot.m_SidecarJson.Contains( wxS( "\"bottom\":4.0" ) ) );
}


BOOST_AUTO_TEST_CASE( VisualPolicyOmittedPixelsKeepMetadataAndSidecar )
{
    AI_VISUAL_SNAPSHOT snapshot;
    snapshot.m_Source = wxS( "canvas.roi" );
    snapshot.m_MimeType = wxS( "image/png" );
    snapshot.m_DataUri = wxS( "data:image/png;base64,abcdefghijklmnopqrstuvwxyz" );
    snapshot.m_WidthPx = 640;
    snapshot.m_HeightPx = 480;
    snapshot.m_ByteSize = 4096;

    AI_VISUAL_FRAME_POLICY policy;
    policy.m_AllowPixels = true;
    policy.m_MaxDataUriChars = 16;

    AI_VISUAL_FRAME_POLICY_RESULT result = ApplyAiVisualFramePolicy( snapshot, policy );

    BOOST_CHECK( !result.m_Snapshot.HasPixels() );
    BOOST_CHECK_EQUAL( result.m_Snapshot.m_Source, wxString( wxS( "canvas.roi" ) ) );
    BOOST_CHECK_EQUAL( result.m_Snapshot.m_MimeType, wxString( wxS( "image/png" ) ) );
    BOOST_CHECK_EQUAL( result.m_Snapshot.m_WidthPx, 640 );
    BOOST_CHECK_EQUAL( result.m_Snapshot.m_HeightPx, 480 );
    BOOST_CHECK_EQUAL( result.m_Snapshot.m_ByteSize, 4096 );
    BOOST_CHECK_EQUAL( result.m_Snapshot.m_UnavailableReason,
                       wxString( wxS( "visual_payload_budget" ) ) );
    BOOST_CHECK( !result.m_PixelsIncluded );
    BOOST_CHECK( result.m_SidecarJson.Contains( wxS( "canvas.roi" ) ) );
    BOOST_CHECK( result.m_SidecarJson.Contains( wxS( "visual_payload_budget" ) ) );
}


BOOST_AUTO_TEST_CASE( VisualObservationArtifactBuildsGroundedSidecar )
{
    wxImage image( 4, 4, false );
    image.SetRGB( wxRect( 0, 0, 4, 4 ), 32, 64, 128 );

    AI_VISUAL_SNAPSHOT snapshot =
            MakeAiVisualSnapshotFromImage( image, wxS( "canvas.viewport" ) );

    AI_VISUAL_OBSERVATION_REQUEST request;
    request.m_FrameId = wxS( "frame-1" );
    request.m_FrameKind = wxS( "annotated_roi" );
    request.m_AttemptId = wxS( "attempt-7" );
    request.m_PreviewId = wxS( "preview-2" );
    request.m_WorldBounds = AI_VISUAL_BOUNDS{ 10.0, 20.0, 30.0, 40.0 };
    request.m_PixelBounds = AI_VISUAL_BOUNDS{ 1.0, 1.0, 3.0, 3.0 };

    AI_VISUAL_ANCHOR_RECORD anchor;
    anchor.m_AnchorId = wxS( "A1" );
    anchor.m_ObjectId = wxS( "U3" );
    anchor.m_Handle = wxS( "handle-U3" );
    anchor.m_Layer = wxS( "F.Cu" );
    anchor.m_NetName = wxS( "GND" );
    anchor.m_WorldX = 12.5;
    anchor.m_WorldY = 24.5;
    anchor.m_WorldBounds = AI_VISUAL_BOUNDS{ 12.0, 24.0, 13.0, 25.0 };
    anchor.m_PixelBounds = AI_VISUAL_BOUNDS{ 2.0, 2.0, 3.0, 3.0 };
    request.m_Anchors.push_back( anchor );

    AI_VISUAL_OBSERVATION_ARTIFACT artifact =
            BuildAiVisualObservationArtifact( snapshot, request );

    BOOST_CHECK_EQUAL( artifact.m_Snapshot.m_FrameId, wxString( wxS( "frame-1" ) ) );
    BOOST_CHECK_EQUAL( artifact.m_Snapshot.m_FrameKind,
                       wxString( wxS( "annotated_roi" ) ) );
    BOOST_CHECK( artifact.m_Snapshot.m_SidecarJson.Contains( wxS( "frame-1" ) ) );
    BOOST_CHECK( artifact.m_Snapshot.m_SidecarJson.Contains( wxS( "annotated_roi" ) ) );
    BOOST_CHECK( artifact.m_Snapshot.m_SidecarJson.Contains( wxS( "attempt-7" ) ) );
    BOOST_CHECK( artifact.m_Snapshot.m_SidecarJson.Contains( wxS( "preview-2" ) ) );
    BOOST_CHECK( artifact.m_Snapshot.m_SidecarJson.Contains( wxS( "world_bounds" ) ) );
    BOOST_CHECK( artifact.m_Snapshot.m_SidecarJson.Contains( wxS( "pixel_bounds" ) ) );
    BOOST_CHECK( artifact.m_Snapshot.m_SidecarJson.Contains( wxS( "A1" ) ) );
    BOOST_CHECK( artifact.m_Snapshot.m_SidecarJson.Contains( wxS( "handle-U3" ) ) );

    AI_VISUAL_FRAME_POLICY policy;
    policy.m_AllowPixels = false;

    AI_VISUAL_FRAME_POLICY_RESULT policyResult =
            ApplyAiVisualFramePolicy( artifact.m_Snapshot, policy );

    BOOST_CHECK( !policyResult.m_Snapshot.HasPixels() );
    BOOST_CHECK( policyResult.m_SidecarJson.Contains( wxS( "frame-1" ) ) );
    BOOST_CHECK( policyResult.m_SidecarJson.Contains( wxS( "A1" ) ) );
    BOOST_CHECK( policyResult.m_SidecarJson.Contains( wxS( "visual_pixels_disabled" ) ) );
}


BOOST_AUTO_TEST_CASE( VisualObservationArtifactBindsRevisionTransformAndFrameRelations )
{
    wxImage image( 6, 6, false );
    image.SetRGB( wxRect( 0, 0, 6, 6 ), 16, 32, 48 );

    AI_VISUAL_SNAPSHOT snapshot =
            MakeAiVisualSnapshotFromImage( image, wxS( "canvas.preview_after" ) );

    AI_VISUAL_OBSERVATION_REQUEST request;
    request.m_FrameId = wxS( "frame-after" );
    request.m_FrameKind = wxS( "preview_after" );
    request.m_AttemptId = wxS( "attempt-9" );
    request.m_PreviewId = wxS( "preview-4" );
    request.m_DocumentRevision = 42;
    request.m_PreviewRevision = 7;
    request.m_WorldBounds = AI_VISUAL_BOUNDS{ 100.0, 200.0, 160.0, 260.0 };
    request.m_PixelBounds = AI_VISUAL_BOUNDS{ 0.0, 0.0, 6.0, 6.0 };
    request.m_PixelWorldTransform.m_WorldOriginX = 100.0;
    request.m_PixelWorldTransform.m_WorldOriginY = 200.0;
    request.m_PixelWorldTransform.m_WorldXPerPixelX = 10.0;
    request.m_PixelWorldTransform.m_WorldYPerPixelY = 10.0;

    AI_VISUAL_FRAME_RELATION_RECORD beforeRelation;
    beforeRelation.m_Relation = wxS( "after_of" );
    beforeRelation.m_FrameId = wxS( "frame-before" );
    beforeRelation.m_FrameKind = wxS( "roi_raw" );
    request.m_RelatedFrames.push_back( beforeRelation );

    AI_VISUAL_FRAME_RELATION_RECORD issueRelation;
    issueRelation.m_Relation = wxS( "issue_crop_for" );
    issueRelation.m_FrameId = wxS( "frame-issue" );
    issueRelation.m_FrameKind = wxS( "issue_crop" );
    issueRelation.m_IssueId = wxS( "drc-123" );
    request.m_RelatedFrames.push_back( issueRelation );

    AI_VISUAL_OBSERVATION_ARTIFACT artifact =
            BuildAiVisualObservationArtifact( snapshot, request );

    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "\"document_revision\":42" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "\"preview_revision\":7" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "pixel_world_transform" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "world_x_per_pixel_x" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "after_of" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "frame-before" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "issue_crop_for" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "drc-123" ) ) );
}


BOOST_AUTO_TEST_CASE( VisualContextFrameBuilderCropsRoiAndBindsSidecar )
{
    wxImage image( 10, 8, false );
    image.SetRGB( wxRect( 0, 0, 10, 8 ), 220, 220, 220 );
    image.SetRGB( wxRect( 2, 1, 4, 3 ), 40, 90, 160 );

    AI_VISUAL_CONTEXT_FRAME_REQUEST request;
    request.m_FrameId = wxS( "frame-roi" );
    request.m_FrameKind = wxS( "roi_raw" );
    request.m_Source = wxS( "pcbnew.canvas" );
    request.m_DocumentRevision = 77;
    request.m_WorldBounds = AI_VISUAL_BOUNDS{ 20.0, 10.0, 60.0, 40.0 };
    request.m_PixelBounds = AI_VISUAL_BOUNDS{ 2.0, 1.0, 6.0, 4.0 };
    request.m_PixelWorldTransform.m_WorldOriginX = 20.0;
    request.m_PixelWorldTransform.m_WorldOriginY = 10.0;
    request.m_PixelWorldTransform.m_WorldXPerPixelX = 10.0;
    request.m_PixelWorldTransform.m_WorldYPerPixelY = 10.0;

    AI_VISUAL_OBSERVATION_ARTIFACT artifact =
            BuildAiVisualContextFrameFromImage( image, request );

    BOOST_CHECK( artifact.m_Snapshot.HasPixels() );
    BOOST_CHECK_EQUAL( artifact.m_Snapshot.m_Source,
                       wxString( wxS( "pcbnew.canvas.roi_raw" ) ) );
    BOOST_CHECK_EQUAL( artifact.m_Snapshot.m_FrameId,
                       wxString( wxS( "frame-roi" ) ) );
    BOOST_CHECK_EQUAL( artifact.m_Snapshot.m_FrameKind,
                       wxString( wxS( "roi_raw" ) ) );
    BOOST_CHECK_EQUAL( artifact.m_Snapshot.m_WidthPx, 4 );
    BOOST_CHECK_EQUAL( artifact.m_Snapshot.m_HeightPx, 3 );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "\"document_revision\":77" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "\"left\":2.0" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "pixel_world_transform" ) ) );
}


BOOST_AUTO_TEST_CASE( VisualContextFrameBuilderDefaultsMissingBoundsToFullImage )
{
    wxImage image( 10, 8, false );
    image.SetRGB( wxRect( 0, 0, 10, 8 ), 90, 110, 130 );

    AI_VISUAL_CONTEXT_FRAME_REQUEST request;
    request.m_FrameId = wxS( "preview-after-1" );
    request.m_FrameKind = wxS( "preview_after" );
    request.m_Source = wxS( "pcbnew.native_preview_scene" );
    request.m_DocumentRevision = 44;
    request.m_PreviewRevision = 9;

    AI_VISUAL_OBSERVATION_ARTIFACT artifact =
            BuildAiVisualContextFrameFromImage( image, request );

    BOOST_CHECK( artifact.m_Snapshot.HasPixels() );
    BOOST_CHECK_EQUAL( artifact.m_Snapshot.m_WidthPx, 10 );
    BOOST_CHECK_EQUAL( artifact.m_Snapshot.m_HeightPx, 8 );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "\"frame_id\":\"preview-after-1\"" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "\"frame_kind\":\"preview_after\"" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "\"pixel_bounds\"" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "\"left\":0.0" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "\"top\":0.0" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "\"right\":10.0" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "\"bottom\":8.0" ) ) );
}


BOOST_AUTO_TEST_CASE( VisualContextFrameBuilderDerivesWorldBoundsFromDefaultPixelBounds )
{
    wxImage image( 10, 8, false );
    image.SetRGB( wxRect( 0, 0, 10, 8 ), 140, 150, 160 );

    AI_VISUAL_CONTEXT_FRAME_REQUEST request;
    request.m_FrameId = wxS( "preview-after-transform" );
    request.m_FrameKind = wxS( "preview_after" );
    request.m_Source = wxS( "pcbnew.native_preview_scene" );
    request.m_PixelWorldTransform.m_WorldOriginX = 100.0;
    request.m_PixelWorldTransform.m_WorldOriginY = 200.0;
    request.m_PixelWorldTransform.m_WorldXPerPixelX = 5.0;
    request.m_PixelWorldTransform.m_WorldYPerPixelY = 10.0;

    AI_VISUAL_OBSERVATION_ARTIFACT artifact =
            BuildAiVisualContextFrameFromImage( image, request );

    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "\"world_bounds\"" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "\"left\":100.0" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "\"top\":200.0" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "\"right\":150.0" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "\"bottom\":280.0" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "pixel_world_transform" ) ) );
}


BOOST_AUTO_TEST_CASE( VisualContextFrameBuilderAnnotatesAnchorsAndKeepsGrounding )
{
    wxImage image( 10, 8, false );
    image.SetRGB( wxRect( 0, 0, 10, 8 ), 250, 250, 250 );

    AI_VISUAL_CONTEXT_FRAME_REQUEST rawRequest;
    rawRequest.m_FrameId = wxS( "frame-roi" );
    rawRequest.m_FrameKind = wxS( "roi_raw" );
    rawRequest.m_Source = wxS( "pcbnew.canvas" );
    rawRequest.m_WorldBounds = AI_VISUAL_BOUNDS{ 0.0, 0.0, 100.0, 80.0 };
    rawRequest.m_PixelBounds = AI_VISUAL_BOUNDS{ 0.0, 0.0, 10.0, 8.0 };

    AI_VISUAL_CONTEXT_FRAME_REQUEST annotatedRequest = rawRequest;
    annotatedRequest.m_FrameId = wxS( "frame-annotated" );
    annotatedRequest.m_FrameKind = wxS( "annotated_roi" );
    annotatedRequest.m_AnnotationMode = wxS( "anchor_overlay" );

    AI_VISUAL_ANCHOR_RECORD anchor;
    anchor.m_AnchorId = wxS( "A1" );
    anchor.m_ObjectId = wxS( "via-1" );
    anchor.m_Handle = wxS( "handle-via-1" );
    anchor.m_Layer = wxS( "F.Cu" );
    anchor.m_NetName = wxS( "GND" );
    anchor.m_WorldX = 30.0;
    anchor.m_WorldY = 20.0;
    anchor.m_PixelBounds = AI_VISUAL_BOUNDS{ 2.0, 1.0, 6.0, 5.0 };
    annotatedRequest.m_Anchors.push_back( anchor );

    AI_VISUAL_OBSERVATION_ARTIFACT raw =
            BuildAiVisualContextFrameFromImage( image, rawRequest );
    AI_VISUAL_OBSERVATION_ARTIFACT annotated =
            BuildAiVisualContextFrameFromImage( image, annotatedRequest );

    BOOST_CHECK( raw.m_Snapshot.HasPixels() );
    BOOST_CHECK( annotated.m_Snapshot.HasPixels() );
    BOOST_CHECK_NE( raw.m_Snapshot.m_DataUri, annotated.m_Snapshot.m_DataUri );
    BOOST_CHECK_EQUAL( annotated.m_Snapshot.m_Source,
                       wxString( wxS( "pcbnew.canvas.annotated_roi" ) ) );
    BOOST_CHECK( annotated.m_SidecarJson.Contains( wxS( "anchor_overlay" ) ) );
    BOOST_CHECK( annotated.m_SidecarJson.Contains( wxS( "A1" ) ) );
    BOOST_CHECK( annotated.m_SidecarJson.Contains( wxS( "handle-via-1" ) ) );
}


BOOST_AUTO_TEST_CASE( VisualIssueCropBuilderPadsIssueRegionAndBindsProvenance )
{
    wxImage image( 12, 10, false );
    image.SetRGB( wxRect( 0, 0, 12, 10 ), 230, 230, 230 );
    image.SetRGB( wxRect( 4, 3, 4, 3 ), 240, 40, 40 );

    AI_VISUAL_ISSUE_CROP_REQUEST request;
    request.m_FrameId = wxS( "issue-crop-drc-1" );
    request.m_Source = wxS( "pcbnew.validation" );
    request.m_ParentFrameId = wxS( "preview-frame-9" );
    request.m_ParentFrameKind = wxS( "preview_after" );
    request.m_IssueId = wxS( "drc-1" );
    request.m_IssueKind = wxS( "clearance" );
    request.m_IssueSeverity = wxS( "error" );
    request.m_IssueTitle = wxS( "Copper clearance" );
    request.m_DocumentRevision = 101;
    request.m_PreviewRevision = 5;
    request.m_ContextPaddingPx = 2;
    request.m_IssuePixelBounds = AI_VISUAL_BOUNDS{ 4.0, 3.0, 8.0, 6.0 };
    request.m_IssueWorldBounds = AI_VISUAL_BOUNDS{ 40.0, 30.0, 80.0, 60.0 };
    request.m_PixelWorldTransform.m_WorldOriginX = 0.0;
    request.m_PixelWorldTransform.m_WorldOriginY = 0.0;
    request.m_PixelWorldTransform.m_WorldXPerPixelX = 10.0;
    request.m_PixelWorldTransform.m_WorldYPerPixelY = 10.0;

    AI_VISUAL_ANCHOR_RECORD anchor;
    anchor.m_AnchorId = wxS( "issue-main" );
    anchor.m_ObjectId = wxS( "track-1" );
    anchor.m_Handle = wxS( "ai://session/issue/track-1" );
    anchor.m_Layer = wxS( "F.Cu" );
    anchor.m_NetName = wxS( "GND" );
    anchor.m_WorldX = 50.0;
    anchor.m_WorldY = 40.0;
    anchor.m_PixelBounds = AI_VISUAL_BOUNDS{ 4.0, 3.0, 8.0, 6.0 };
    request.m_Anchors.push_back( anchor );

    AI_VISUAL_OBSERVATION_ARTIFACT artifact =
            BuildAiVisualIssueCropFromImage( image, request );

    BOOST_CHECK( artifact.m_Snapshot.HasPixels() );
    BOOST_CHECK_EQUAL( artifact.m_Snapshot.m_Source,
                       wxString( wxS( "pcbnew.validation.issue_crop" ) ) );
    BOOST_CHECK_EQUAL( artifact.m_Snapshot.m_FrameKind,
                       wxString( wxS( "issue_crop" ) ) );
    BOOST_CHECK_EQUAL( artifact.m_Snapshot.m_WidthPx, 8 );
    BOOST_CHECK_EQUAL( artifact.m_Snapshot.m_HeightPx, 7 );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "\"document_revision\":101" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "\"preview_revision\":5" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "\"issue\"" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "drc-1" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "clearance" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "Copper clearance" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "issue_crop_for" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "preview-frame-9" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "issue-main" ) ) );
    BOOST_CHECK( artifact.m_SidecarJson.Contains( wxS( "ai://session/issue/track-1" ) ) );
}


BOOST_AUTO_TEST_CASE( VisualAnchorReferenceResolvesToGroundedObjectFacts )
{
    AI_VISUAL_ANCHOR_RECORD anchor;
    anchor.m_AnchorId = wxS( "A1" );
    anchor.m_ObjectId = wxS( "via-1" );
    anchor.m_Handle = wxS( "ai://session/7/handle/3" );
    anchor.m_Layer = wxS( "F.Cu" );
    anchor.m_NetName = wxS( "GND" );
    anchor.m_WorldX = 300.0;
    anchor.m_WorldY = 200.0;
    anchor.m_WorldBounds = AI_VISUAL_BOUNDS{ 280.0, 180.0, 320.0, 220.0 };
    anchor.m_PixelBounds = AI_VISUAL_BOUNDS{ 20.0, 10.0, 40.0, 30.0 };

    AI_VISUAL_ANCHOR_RESOLUTION resolution =
            ResolveAiVisualReferenceJson(
                    wxS( "{\"anchor_id\":\"A1\",\"role\":\"target\"}" ),
                    { anchor } );

    BOOST_CHECK( resolution.m_Resolved );
    BOOST_CHECK_EQUAL( resolution.m_Kind, wxString( wxS( "anchor" ) ) );
    BOOST_CHECK_EQUAL( resolution.m_AnchorId, wxString( wxS( "A1" ) ) );
    BOOST_CHECK_EQUAL( resolution.m_ObjectId, wxString( wxS( "via-1" ) ) );
    BOOST_CHECK_EQUAL( resolution.m_Handle,
                       wxString( wxS( "ai://session/7/handle/3" ) ) );
    BOOST_CHECK_EQUAL( resolution.m_Layer, wxString( wxS( "F.Cu" ) ) );
    BOOST_CHECK_EQUAL( resolution.m_NetName, wxString( wxS( "GND" ) ) );
    BOOST_CHECK_CLOSE( resolution.m_WorldX, 300.0, 0.001 );
    BOOST_CHECK_CLOSE( resolution.m_WorldY, 200.0, 0.001 );
    BOOST_CHECK( resolution.m_ErrorCode.IsEmpty() );
}


BOOST_AUTO_TEST_CASE( VisualAnchorReferenceRejectsPixelOnlyMutationTruth )
{
    AI_VISUAL_ANCHOR_RESOLUTION resolution =
            ResolveAiVisualReferenceJson(
                    wxS( "{\"pixel_position\":{\"x\":21,\"y\":37}}" ),
                    {} );

    BOOST_CHECK( !resolution.m_Resolved );
    BOOST_CHECK_EQUAL( resolution.m_ErrorCode,
                       wxString( wxS( "pixel_only_reference" ) ) );
    BOOST_CHECK( resolution.m_Message.Contains( wxS( "anchor" ) ) );
}


BOOST_AUTO_TEST_CASE( VisualAnchorReferenceAllowsGroundedWorldCoordinate )
{
    AI_VISUAL_ANCHOR_RESOLUTION resolution =
            ResolveAiVisualReferenceJson(
                    wxS( "{\"world_position\":{\"x\":11.5,\"y\":22.25},"
                         "\"layer\":\"F.Cu\",\"net\":\"GND\"}" ),
                    {} );

    BOOST_CHECK( resolution.m_Resolved );
    BOOST_CHECK_EQUAL( resolution.m_Kind,
                       wxString( wxS( "world_position" ) ) );
    BOOST_CHECK_CLOSE( resolution.m_WorldX, 11.5, 0.001 );
    BOOST_CHECK_CLOSE( resolution.m_WorldY, 22.25, 0.001 );
    BOOST_CHECK_EQUAL( resolution.m_Layer, wxString( wxS( "F.Cu" ) ) );
    BOOST_CHECK_EQUAL( resolution.m_NetName, wxString( wxS( "GND" ) ) );
    BOOST_CHECK( resolution.m_ErrorCode.IsEmpty() );
}


BOOST_AUTO_TEST_SUITE_END()
