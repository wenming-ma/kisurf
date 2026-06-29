/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_types.h>

class AI_STRUCTURED_SURFACE_GRID_IO;
class wxWindow;

KICOMMON_API AI_PANEL_STATE_RECORD AiPanelStateRecordFromGridIo(
        const wxString& aPanelId, const wxString& aPanelTitle,
        const wxString& aTableId, const wxString& aTableTitle,
        AI_STRUCTURED_SURFACE_GRID_IO& aGridIo );

KICOMMON_API AI_PANEL_STATE_RECORD AiPanelStateRecordFromWxWindowGrids(
        const wxWindow& aRoot, const wxString& aPanelId,
        const wxString& aPanelTitle, const wxString& aTableIdPrefix,
        const wxString& aTableTitlePrefix );
