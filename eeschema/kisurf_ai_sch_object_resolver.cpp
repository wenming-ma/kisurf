#include <kisurf_ai_sch_object_resolver.h>

#include <sch_item.h>
#include <sch_screen.h>

KISURF_AI_SCH_OBJECT_RESOLVER::KISURF_AI_SCH_OBJECT_RESOLVER( SCH_SCREEN& aScreen ) :
        m_Screen( aScreen )
{
}


SCH_ITEM* KISURF_AI_SCH_OBJECT_RESOLVER::Resolve( const AI_OBJECT_REF& aRef ) const
{
    if( !aRef.IsValid() )
        return nullptr;

    for( SCH_ITEM* item : m_Screen.Items() )
    {
        if( item->m_Uuid == aRef.m_Uuid && item->Type() == aRef.m_Type )
            return item;
    }

    return nullptr;
}


std::vector<SCH_ITEM*> KISURF_AI_SCH_OBJECT_RESOLVER::ResolveAll(
        const std::vector<AI_OBJECT_REF>& aRefs ) const
{
    std::vector<SCH_ITEM*> resolved;

    for( const AI_OBJECT_REF& ref : aRefs )
    {
        if( SCH_ITEM* item = Resolve( ref ) )
            resolved.push_back( item );
    }

    return resolved;
}
