#include <kisurf/ai/ai_preview_manager.h>
#include <kisurf/ai/ai_suggestion_operations.h>

#include <utility>

void AI_PREVIEW_ADAPTER::ShowOperation( uint64_t aPreviewId,
                                        const AI_SUGGESTION_OPERATION& aOperation )
{
    wxUnusedVar( aPreviewId );
    wxUnusedVar( aOperation );
}


void AI_PREVIEW_ADAPTER::ShowOverlay( uint64_t aPreviewId,
                                      const AI_PREVIEW_ITEM_OVERLAY& aOverlay )
{
    wxUnusedVar( aPreviewId );
    wxUnusedVar( aOverlay );
}


AI_PREVIEW_MANAGER::AI_PREVIEW_MANAGER( AI_PREVIEW_ADAPTER& aAdapter ) :
        m_Adapter( aAdapter )
{
}


uint64_t AI_PREVIEW_MANAGER::BeginPreview( wxString aProvenanceJson )
{
    m_CurrentPreviewId = m_NextPreviewId++;
    m_CurrentProvenanceJson = std::move( aProvenanceJson );
    m_CurrentPreviewItems.clear();
    m_CurrentPreviewOverlays.clear();
    m_Adapter.BeginPreview( m_CurrentPreviewId );
    return m_CurrentPreviewId;
}


void AI_PREVIEW_MANAGER::ShowObject( const AI_OBJECT_REF& aObject )
{
    if( m_CurrentPreviewId == 0 )
        BeginPreview();

    AI_PREVIEW_ITEM item;
    item.m_PreviewId = m_CurrentPreviewId;
    item.m_ItemKind = wxS( "object" );
    item.m_Label = aObject.m_Label;
    item.m_ProvenanceJson = m_CurrentProvenanceJson;
    m_CurrentPreviewItems.push_back( std::move( item ) );

    m_Adapter.ShowObject( m_CurrentPreviewId, aObject );
}


void AI_PREVIEW_MANAGER::ShowOperation( const AI_SUGGESTION_OPERATION& aOperation )
{
    if( m_CurrentPreviewId == 0 )
        BeginPreview();

    AI_PREVIEW_ITEM item;
    item.m_PreviewId = m_CurrentPreviewId;
    item.m_ItemKind = wxS( "operation" );
    item.m_Label = aOperation.m_AnchorId;
    item.m_ProvenanceJson = m_CurrentProvenanceJson;
    m_CurrentPreviewItems.push_back( std::move( item ) );

    m_Adapter.ShowOperation( m_CurrentPreviewId, aOperation );
}


void AI_PREVIEW_MANAGER::ShowItemOverlay( wxString aItemLabel, wxString aOverlayKind,
                                          wxString aSeverity, wxString aMessage,
                                          wxString aGeometryJson, wxString aLayer )
{
    if( m_CurrentPreviewId == 0 )
        BeginPreview();

    AI_PREVIEW_ITEM_OVERLAY overlay;
    overlay.m_PreviewId = m_CurrentPreviewId;
    overlay.m_ItemLabel = std::move( aItemLabel );
    overlay.m_OverlayKind = std::move( aOverlayKind );
    overlay.m_Severity = std::move( aSeverity );
    overlay.m_Message = std::move( aMessage );
    overlay.m_ProvenanceJson = m_CurrentProvenanceJson;
    overlay.m_GeometryJson = std::move( aGeometryJson );
    overlay.m_Layer = std::move( aLayer );

    for( AI_PREVIEW_ITEM& item : m_CurrentPreviewItems )
    {
        if( item.m_Label == overlay.m_ItemLabel )
            item.m_ValidationStatus = overlay.m_Severity;
    }

    m_CurrentPreviewOverlays.push_back( overlay );
    m_Adapter.ShowOverlay( m_CurrentPreviewId, overlay );
}


AI_PREVIEW_BATCH_ITEM AI_PREVIEW_MANAGER::CurrentPreviewBatchItem() const
{
    AI_PREVIEW_BATCH_ITEM batch;
    batch.m_PreviewId = m_CurrentPreviewId;
    batch.m_ProvenanceJson = m_CurrentProvenanceJson;
    batch.m_Items = m_CurrentPreviewItems;
    batch.m_Overlays = m_CurrentPreviewOverlays;
    return batch;
}


void AI_PREVIEW_MANAGER::ClearPreview()
{
    if( m_CurrentPreviewId == 0 )
        return;

    m_Adapter.ClearPreview( m_CurrentPreviewId );
    m_CurrentPreviewId = 0;
    m_CurrentProvenanceJson.Clear();
    m_CurrentPreviewItems.clear();
    m_CurrentPreviewOverlays.clear();
}
