/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 */

#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_preview_manager.h>

#include <cstddef>
#include <cstdint>
#include <vector>
#include <wx/colour.h>
#include <wx/string.h>

class wxGrid;
class wxPropertyGrid;

struct KICOMMON_API AI_STRUCTURED_SURFACE_PREVIEW_TARGET
{
    uint64_t m_PreviewId = 0;
    wxString m_ItemLabel;
    wxString m_SurfaceId;
    wxString m_TableId;
    wxString m_RowId;
    wxString m_ColumnId;
    wxString m_FieldId;
    wxString m_TargetPath;
    wxString m_PreviousValue;
    wxString m_ProposedValue;
    wxString m_Severity;
    wxString m_Message;
    bool     m_HasPreviousValue = false;
    bool     m_ValueChanged = false;
};

class KICOMMON_API AI_STRUCTURED_SURFACE_PREVIEW_OVERLAY_IO
{
public:
    virtual ~AI_STRUCTURED_SURFACE_PREVIEW_OVERLAY_IO() = default;

    virtual bool ShowCellPreview(
            const AI_STRUCTURED_SURFACE_PREVIEW_TARGET& aTarget,
            wxString& aError ) = 0;
    virtual bool ShowFieldPreview(
            const AI_STRUCTURED_SURFACE_PREVIEW_TARGET& aTarget,
            wxString& aError ) = 0;
    virtual void ClearPreview( uint64_t aPreviewId ) = 0;
};

class KICOMMON_API AI_STRUCTURED_SURFACE_PREVIEW_ADAPTER :
        public AI_PREVIEW_ADAPTER
{
public:
    AI_STRUCTURED_SURFACE_PREVIEW_ADAPTER(
            AI_STRUCTURED_SURFACE_PREVIEW_OVERLAY_IO& aOverlayIo,
            wxString aSurfaceId = wxEmptyString,
            wxString aTableId = wxEmptyString );

    void BeginPreview( uint64_t aPreviewId ) override;
    void ShowObject( uint64_t aPreviewId, const AI_OBJECT_REF& aObject ) override;
    void ShowOverlay( uint64_t aPreviewId,
                      const AI_PREVIEW_ITEM_OVERLAY& aOverlay ) override;
    void ClearPreview( uint64_t aPreviewId ) override;

    uint64_t ActivePreviewId() const { return m_ActivePreviewId; }
    size_t   ShownTargetCount() const { return m_ShownTargetCount; }
    size_t   IgnoredOverlayCount() const { return m_IgnoredOverlayCount; }
    size_t   FailedOverlayCount() const { return m_FailedOverlayCount; }
    const wxString& LastError() const { return m_LastError; }

private:
    bool targetMatchesFilter( const AI_STRUCTURED_SURFACE_PREVIEW_TARGET& aTarget ) const;

    AI_STRUCTURED_SURFACE_PREVIEW_OVERLAY_IO& m_OverlayIo;
    wxString                                  m_SurfaceId;
    wxString                                  m_TableId;
    uint64_t                                  m_ActivePreviewId = 0;
    size_t                                    m_ShownTargetCount = 0;
    size_t                                    m_IgnoredOverlayCount = 0;
    size_t                                    m_FailedOverlayCount = 0;
    wxString                                  m_LastError;
};

class KICOMMON_API AI_STRUCTURED_SURFACE_WX_GRID_PREVIEW_OVERLAY_IO :
        public AI_STRUCTURED_SURFACE_PREVIEW_OVERLAY_IO
{
public:
    explicit AI_STRUCTURED_SURFACE_WX_GRID_PREVIEW_OVERLAY_IO(
            wxGrid& aGrid,
            wxColour aPreviewBackground = wxColour( 255, 244, 188 ),
            wxColour aPreviewText = wxColour( 25, 22, 15 ) );

    bool ShowCellPreview(
            const AI_STRUCTURED_SURFACE_PREVIEW_TARGET& aTarget,
            wxString& aError ) override;
    bool ShowFieldPreview(
            const AI_STRUCTURED_SURFACE_PREVIEW_TARGET& aTarget,
            wxString& aError ) override;
    void ClearPreview( uint64_t aPreviewId ) override;

private:
    struct CELL_STYLE
    {
        uint64_t m_PreviewId = 0;
        int      m_Row = 0;
        int      m_Column = 0;
        wxColour m_Background;
        wxColour m_Text;
    };

    bool resolveCell( const AI_STRUCTURED_SURFACE_PREVIEW_TARGET& aTarget,
                      int& aRow, int& aColumn ) const;

    wxGrid&                 m_Grid;
    wxColour                m_PreviewBackground;
    wxColour                m_PreviewText;
    std::vector<CELL_STYLE> m_AppliedCells;
};

class KICOMMON_API AI_STRUCTURED_SURFACE_WX_PROPERTY_GRID_PREVIEW_OVERLAY_IO :
        public AI_STRUCTURED_SURFACE_PREVIEW_OVERLAY_IO
{
public:
    explicit AI_STRUCTURED_SURFACE_WX_PROPERTY_GRID_PREVIEW_OVERLAY_IO(
            wxPropertyGrid& aPropertyGrid,
            wxColour aPreviewBackground = wxColour( 255, 244, 188 ),
            wxColour aPreviewText = wxColour( 25, 22, 15 ) );

    bool ShowCellPreview(
            const AI_STRUCTURED_SURFACE_PREVIEW_TARGET& aTarget,
            wxString& aError ) override;
    bool ShowFieldPreview(
            const AI_STRUCTURED_SURFACE_PREVIEW_TARGET& aTarget,
            wxString& aError ) override;
    void ClearPreview( uint64_t aPreviewId ) override;

private:
    struct FIELD_STYLE
    {
        uint64_t m_PreviewId = 0;
        wxString m_FieldId;
        wxColour m_Background;
        wxColour m_Text;
    };

    wxPropertyGrid&          m_PropertyGrid;
    wxColour                 m_PreviewBackground;
    wxColour                 m_PreviewText;
    std::vector<FIELD_STYLE> m_AppliedFields;
};
