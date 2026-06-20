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
#include <math/vector2d.h>

#include <optional>
#include <vector>
#include <wx/string.h>

enum class AI_SUGGESTION_OPERATION_KIND
{
    Unknown,
    Move,
    MoveSelected,
    RouteSegmentPreview,
    PlaceViaPreview,
    CreateShapePreview,
    CreateCopperZonePreview,
    PanelFillColumnPreview,
    AnchorFocusPreview
};

struct KICOMMON_API AI_SUGGESTION_OPERATION
{
    AI_SUGGESTION_OPERATION_KIND m_Kind = AI_SUGGESTION_OPERATION_KIND::Unknown;
    VECTOR2I                     m_MoveDelta = VECTOR2I( 0, 0 );
    VECTOR2I                     m_Start = VECTOR2I( 0, 0 );
    VECTOR2I                     m_End = VECTOR2I( 0, 0 );
    VECTOR2I                     m_Position = VECTOR2I( 0, 0 );
    std::vector<VECTOR2I>         m_Points;
    std::vector<std::vector<VECTOR2I>> m_Holes;
    int                          m_Width = 0;
    int                          m_Diameter = 0;
    int                          m_Drill = 0;
    wxString                     m_NetName;
    wxString                     m_LayerName;
    wxString                     m_Shape;
    wxString                     m_PanelId;
    wxString                     m_TableId;
    wxString                     m_ColumnId;
    wxString                     m_Value;
    wxString                     m_AnchorId;
    wxString                     m_FocusLayer;
    wxString                     m_FocusNet;
    bool                         m_DimUnfocusedLayers = false;
    std::vector<wxString>        m_TargetRowIds;

    bool IsMove() const;
    bool IsMoveSelected() const;
    bool IsRouteSegmentPreview() const;
    bool IsPlaceViaPreview() const;
    bool IsCreateShapePreview() const;
    bool IsCreateCopperZonePreview() const;
    bool IsPanelFillColumnPreview() const;
    bool IsAnchorFocusPreview() const;
};

KICOMMON_API std::optional<AI_SUGGESTION_OPERATION> ParseAiSuggestionOperation(
        const wxString& aArgumentsJson );

KICOMMON_API std::optional<VECTOR2I> ParseAiSuggestionMoveDelta(
        const wxString& aArgumentsJson );
