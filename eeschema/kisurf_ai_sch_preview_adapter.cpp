#include <kisurf_ai_sch_preview_adapter.h>

#include <kisurf_ai_sch_object_resolver.h>
#include <sch_item.h>
#include <view/view.h>

KISURF_AI_SCH_PREVIEW_ADAPTER::KISURF_AI_SCH_PREVIEW_ADAPTER(
        KISURF_AI_SCH_OBJECT_RESOLVER& aResolver, KIGFX::VIEW& aView,
        std::optional<VECTOR2I> aMoveDelta ) :
        m_Resolver( aResolver ),
        m_View( aView ),
        m_MoveDelta( aMoveDelta )
{
}


void KISURF_AI_SCH_PREVIEW_ADAPTER::BeginPreview( uint64_t aPreviewId )
{
    m_View.ClearPreview();
    m_ActivePreviewId = aPreviewId;
    m_PreviewedItems.clear();
}


void KISURF_AI_SCH_PREVIEW_ADAPTER::ShowObject( uint64_t aPreviewId,
                                                const AI_OBJECT_REF& aObject )
{
    if( aPreviewId != m_ActivePreviewId )
        return;

    SCH_ITEM* item = m_Resolver.Resolve( aObject );

    if( !item )
        return;

    if( m_MoveDelta )
    {
        SCH_ITEM* previewItem = dynamic_cast<SCH_ITEM*>( item->Clone() );

        if( !previewItem )
            return;

        previewItem->Move( *m_MoveDelta );
        m_View.AddToPreview( previewItem, true );
        m_PreviewedItems.push_back( previewItem );
        return;
    }

    m_View.AddToPreview( item, false );
    m_PreviewedItems.push_back( item );
}


void KISURF_AI_SCH_PREVIEW_ADAPTER::ClearPreview( uint64_t aPreviewId )
{
    if( aPreviewId != m_ActivePreviewId )
        return;

    m_View.ClearPreview();
    m_ActivePreviewId = 0;
    m_PreviewedItems.clear();
}
