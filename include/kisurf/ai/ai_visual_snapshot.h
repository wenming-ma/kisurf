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

class EDA_DRAW_PANEL_GAL;
class wxImage;

struct KICOMMON_API AI_VISUAL_CAPTURE_OPTIONS
{
    int m_MaxEdgePixels = 1024;
};

KICOMMON_API AI_VISUAL_SNAPSHOT MakeAiVisualSnapshotFromImage(
        const wxImage& aImage,
        const wxString& aSource,
        const AI_VISUAL_CAPTURE_OPTIONS& aOptions = AI_VISUAL_CAPTURE_OPTIONS() );

bool CaptureAiVisualSnapshotFromCanvas(
        EDA_DRAW_PANEL_GAL& aCanvas,
        AI_VISUAL_SNAPSHOT& aSnapshot,
        const AI_VISUAL_CAPTURE_OPTIONS& aOptions = AI_VISUAL_CAPTURE_OPTIONS() );
