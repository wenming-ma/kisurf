/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 */

#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_types.h>

#include <cstdint>
#include <vector>

class EDA_DRAW_PANEL_GAL;
class wxImage;

struct KICOMMON_API AI_VISUAL_CAPTURE_OPTIONS
{
    int m_MaxEdgePixels = 1024;
};

struct KICOMMON_API AI_VISUAL_FRAME_POLICY
{
    bool   m_AllowPixels = true;
    size_t m_MaxDataUriChars = 1500000;
};

struct KICOMMON_API AI_VISUAL_FRAME_POLICY_RESULT
{
    AI_VISUAL_SNAPSHOT m_Snapshot;
    bool               m_PixelsIncluded = false;
    wxString           m_OmissionReason;
    wxString           m_SidecarJson;
};

struct KICOMMON_API AI_VISUAL_BOUNDS
{
    double m_Left = 0.0;
    double m_Top = 0.0;
    double m_Right = 0.0;
    double m_Bottom = 0.0;
};

struct KICOMMON_API AI_VISUAL_ANCHOR_RECORD
{
    wxString         m_AnchorId;
    wxString         m_ObjectId;
    wxString         m_Handle;
    wxString         m_Layer;
    wxString         m_NetName;
    double           m_WorldX = 0.0;
    double           m_WorldY = 0.0;
    AI_VISUAL_BOUNDS m_WorldBounds;
    AI_VISUAL_BOUNDS m_PixelBounds;
};

struct KICOMMON_API AI_VISUAL_ANCHOR_RESOLUTION
{
    bool             m_Resolved = false;
    wxString         m_Kind;
    wxString         m_AnchorId;
    wxString         m_ObjectId;
    wxString         m_Handle;
    wxString         m_Layer;
    wxString         m_NetName;
    double           m_WorldX = 0.0;
    double           m_WorldY = 0.0;
    AI_VISUAL_BOUNDS m_WorldBounds;
    AI_VISUAL_BOUNDS m_PixelBounds;
    wxString         m_ErrorCode;
    wxString         m_Message;
};

struct KICOMMON_API AI_VISUAL_PIXEL_WORLD_TRANSFORM
{
    double m_WorldOriginX = 0.0;
    double m_WorldOriginY = 0.0;
    double m_WorldXPerPixelX = 0.0;
    double m_WorldXPerPixelY = 0.0;
    double m_WorldYPerPixelX = 0.0;
    double m_WorldYPerPixelY = 0.0;
};

struct KICOMMON_API AI_VISUAL_FRAME_RELATION_RECORD
{
    wxString m_Relation;
    wxString m_FrameId;
    wxString m_FrameKind;
    wxString m_IssueId;
};

struct KICOMMON_API AI_VISUAL_OBSERVATION_REQUEST
{
    wxString                             m_FrameId;
    wxString                             m_FrameKind;
    wxString                             m_AnnotationMode;
    wxString                             m_AttemptId;
    wxString                             m_PreviewId;
    uint64_t                             m_DocumentRevision = 0;
    uint64_t                             m_PreviewRevision = 0;
    AI_VISUAL_BOUNDS                     m_WorldBounds;
    AI_VISUAL_BOUNDS                     m_PixelBounds;
    AI_VISUAL_PIXEL_WORLD_TRANSFORM      m_PixelWorldTransform;
    std::vector<AI_VISUAL_ANCHOR_RECORD> m_Anchors;
    std::vector<AI_VISUAL_FRAME_RELATION_RECORD> m_RelatedFrames;
    wxString                             m_IssueId;
    wxString                             m_IssueKind;
    wxString                             m_IssueSeverity;
    wxString                             m_IssueTitle;
    wxString                             m_IssueMessage;
    AI_VISUAL_BOUNDS                     m_IssueWorldBounds;
    AI_VISUAL_BOUNDS                     m_IssuePixelBounds;
};

struct KICOMMON_API AI_VISUAL_CONTEXT_FRAME_REQUEST
{
    wxString                             m_FrameId;
    wxString                             m_FrameKind;
    wxString                             m_Source;
    wxString                             m_AnnotationMode;
    wxString                             m_AttemptId;
    wxString                             m_PreviewId;
    uint64_t                             m_DocumentRevision = 0;
    uint64_t                             m_PreviewRevision = 0;
    AI_VISUAL_BOUNDS                     m_WorldBounds;
    AI_VISUAL_BOUNDS                     m_PixelBounds;
    AI_VISUAL_PIXEL_WORLD_TRANSFORM      m_PixelWorldTransform;
    std::vector<AI_VISUAL_ANCHOR_RECORD> m_Anchors;
    std::vector<AI_VISUAL_FRAME_RELATION_RECORD> m_RelatedFrames;
};

struct KICOMMON_API AI_VISUAL_ISSUE_CROP_REQUEST
{
    wxString                             m_FrameId;
    wxString                             m_Source;
    wxString                             m_ParentFrameId;
    wxString                             m_ParentFrameKind;
    wxString                             m_IssueId;
    wxString                             m_IssueKind;
    wxString                             m_IssueSeverity;
    wxString                             m_IssueTitle;
    wxString                             m_IssueMessage;
    uint64_t                             m_DocumentRevision = 0;
    uint64_t                             m_PreviewRevision = 0;
    int                                  m_ContextPaddingPx = 24;
    AI_VISUAL_BOUNDS                     m_IssueWorldBounds;
    AI_VISUAL_BOUNDS                     m_IssuePixelBounds;
    AI_VISUAL_PIXEL_WORLD_TRANSFORM      m_PixelWorldTransform;
    std::vector<AI_VISUAL_ANCHOR_RECORD> m_Anchors;
};

struct KICOMMON_API AI_VISUAL_OBSERVATION_ARTIFACT
{
    AI_VISUAL_SNAPSHOT m_Snapshot;
    wxString           m_SidecarJson;
};

KICOMMON_API AI_VISUAL_SNAPSHOT MakeAiVisualSnapshotFromImage(
        const wxImage& aImage,
        const wxString& aSource,
        const AI_VISUAL_CAPTURE_OPTIONS& aOptions = AI_VISUAL_CAPTURE_OPTIONS() );

KICOMMON_API AI_VISUAL_SNAPSHOT BuildAiViewportVisualSnapshotFromImage(
        const wxImage& aImage,
        const wxString& aSource,
        const AI_VISUAL_CAPTURE_OPTIONS& aOptions = AI_VISUAL_CAPTURE_OPTIONS() );

KICOMMON_API AI_VISUAL_OBSERVATION_ARTIFACT BuildAiVisualObservationArtifact(
        const AI_VISUAL_SNAPSHOT& aSnapshot,
        const AI_VISUAL_OBSERVATION_REQUEST& aRequest );

KICOMMON_API AI_VISUAL_OBSERVATION_ARTIFACT BuildAiVisualContextFrameFromImage(
        const wxImage& aImage,
        const AI_VISUAL_CONTEXT_FRAME_REQUEST& aRequest,
        const AI_VISUAL_CAPTURE_OPTIONS& aOptions = AI_VISUAL_CAPTURE_OPTIONS() );

KICOMMON_API AI_VISUAL_OBSERVATION_ARTIFACT BuildAiVisualIssueCropFromImage(
        const wxImage& aImage,
        const AI_VISUAL_ISSUE_CROP_REQUEST& aRequest,
        const AI_VISUAL_CAPTURE_OPTIONS& aOptions = AI_VISUAL_CAPTURE_OPTIONS() );

KICOMMON_API AI_VISUAL_ANCHOR_RESOLUTION ResolveAiVisualReferenceJson(
        const wxString& aReferenceJson,
        const std::vector<AI_VISUAL_ANCHOR_RECORD>& aAnchors );

bool BuildAiVisualContextFrameFromCanvas(
        EDA_DRAW_PANEL_GAL& aCanvas,
        AI_VISUAL_OBSERVATION_ARTIFACT& aArtifact,
        const AI_VISUAL_CONTEXT_FRAME_REQUEST& aRequest,
        const AI_VISUAL_CAPTURE_OPTIONS& aOptions = AI_VISUAL_CAPTURE_OPTIONS() );

KICOMMON_API AI_VISUAL_FRAME_POLICY_RESULT ApplyAiVisualFramePolicy(
        const AI_VISUAL_SNAPSHOT& aSnapshot, const AI_VISUAL_FRAME_POLICY& aPolicy );

bool CaptureAiVisualSnapshotFromCanvas(
        EDA_DRAW_PANEL_GAL& aCanvas,
        AI_VISUAL_SNAPSHOT& aSnapshot,
        const AI_VISUAL_CAPTURE_OPTIONS& aOptions = AI_VISUAL_CAPTURE_OPTIONS() );
