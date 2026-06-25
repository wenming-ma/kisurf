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

struct AI_SUGGESTION_OPERATION;

struct KICOMMON_API AI_PREVIEW_ITEM
{
    uint64_t m_PreviewId = 0;
    wxString m_ItemKind;
    wxString m_Label;
    wxString m_ProvenanceJson;
    wxString m_SourceJson;
    wxString m_ValidationStatus;
};

struct KICOMMON_API AI_PREVIEW_ITEM_OVERLAY
{
    uint64_t m_PreviewId = 0;
    wxString m_ItemLabel;
    wxString m_OverlayKind;
    wxString m_Severity;
    wxString m_Message;
    wxString m_ProvenanceJson;
    wxString m_GeometryJson;
    wxString m_Layer;
};

struct KICOMMON_API AI_PREVIEW_BATCH_ITEM
{
    uint64_t m_PreviewId = 0;
    wxString m_ProvenanceJson;
    std::vector<AI_PREVIEW_ITEM> m_Items;
    std::vector<AI_PREVIEW_ITEM_OVERLAY> m_Overlays;
};

class KICOMMON_API AI_PREVIEW_ADAPTER
{
public:
    virtual ~AI_PREVIEW_ADAPTER() = default;

    virtual void BeginPreview( uint64_t aPreviewId ) = 0;
    virtual void ShowObject( uint64_t aPreviewId, const AI_OBJECT_REF& aObject ) = 0;
    virtual void ShowOperation( uint64_t aPreviewId,
                                const AI_SUGGESTION_OPERATION& aOperation );
    virtual void ShowOverlay( uint64_t aPreviewId,
                              const AI_PREVIEW_ITEM_OVERLAY& aOverlay );
    virtual void ClearPreview( uint64_t aPreviewId ) = 0;
};

class KICOMMON_API AI_COMPOSITE_PREVIEW_ADAPTER :
        public AI_PREVIEW_ADAPTER
{
public:
    void AddAdapter( AI_PREVIEW_ADAPTER& aAdapter );
    size_t AdapterCount() const { return m_Adapters.size(); }

    void BeginPreview( uint64_t aPreviewId ) override;
    void ShowObject( uint64_t aPreviewId,
                     const AI_OBJECT_REF& aObject ) override;
    void ShowOperation( uint64_t aPreviewId,
                        const AI_SUGGESTION_OPERATION& aOperation ) override;
    void ShowOverlay( uint64_t aPreviewId,
                      const AI_PREVIEW_ITEM_OVERLAY& aOverlay ) override;
    void ClearPreview( uint64_t aPreviewId ) override;

private:
    std::vector<AI_PREVIEW_ADAPTER*> m_Adapters;
};

class KICOMMON_API AI_PREVIEW_MANAGER
{
public:
    explicit AI_PREVIEW_MANAGER( AI_PREVIEW_ADAPTER& aAdapter );

    uint64_t BeginPreview( wxString aProvenanceJson = wxS( "{}" ) );
    void     ShowObject( const AI_OBJECT_REF& aObject );
    void     ShowOperation( const AI_SUGGESTION_OPERATION& aOperation );
    void     ShowItemOverlay( wxString aItemLabel, wxString aOverlayKind,
                              wxString aSeverity, wxString aMessage,
                              wxString aGeometryJson = wxEmptyString,
                              wxString aLayer = wxEmptyString );
    void     ClearPreview();

    uint64_t CurrentPreviewId() const { return m_CurrentPreviewId; }
    bool     HasActivePreview() const { return m_CurrentPreviewId != 0; }
    const wxString& CurrentProvenanceJson() const { return m_CurrentProvenanceJson; }
    const std::vector<AI_PREVIEW_ITEM>& CurrentPreviewItems() const
    {
        return m_CurrentPreviewItems;
    }
    const std::vector<AI_PREVIEW_ITEM_OVERLAY>& CurrentPreviewOverlays() const
    {
        return m_CurrentPreviewOverlays;
    }
    AI_PREVIEW_BATCH_ITEM CurrentPreviewBatchItem() const;

private:
    AI_PREVIEW_ADAPTER& m_Adapter;
    uint64_t            m_NextPreviewId = 1;
    uint64_t            m_CurrentPreviewId = 0;
    wxString            m_CurrentProvenanceJson;
    std::vector<AI_PREVIEW_ITEM> m_CurrentPreviewItems;
    std::vector<AI_PREVIEW_ITEM_OVERLAY> m_CurrentPreviewOverlays;
};
