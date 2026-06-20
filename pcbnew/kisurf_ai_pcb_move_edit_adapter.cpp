#include <kisurf_ai_pcb_move_edit_adapter.h>

#include <board_item.h>
#include <commit.h>
#include <kisurf_ai_pcb_object_resolver.h>

#include <utility>

KISURF_AI_PCB_MOVE_EDIT_ADAPTER::KISURF_AI_PCB_MOVE_EDIT_ADAPTER(
        KISURF_AI_PCB_OBJECT_RESOLVER& aResolver, COMMIT& aCommit,
        const VECTOR2I& aDelta, wxString aCommitMessage ) :
        m_Resolver( aResolver ),
        m_Commit( aCommit ),
        m_Delta( aDelta ),
        m_CommitMessage( std::move( aCommitMessage ) )
{
}


bool KISURF_AI_PCB_MOVE_EDIT_ADAPTER::BeginApply( const AI_VALIDATION_SUMMARY&,
                                                  size_t aObjectCount )
{
    m_MovedItems.clear();
    m_FailedObjects.clear();
    m_WasCommitted = false;
    m_WasReverted = false;
    return aObjectCount > 0;
}


bool KISURF_AI_PCB_MOVE_EDIT_ADAPTER::ApplyObject( const AI_OBJECT_REF& aObject )
{
    BOARD_ITEM* item = m_Resolver.Resolve( aObject );

    if( !item )
    {
        m_FailedObjects.push_back( aObject );
        return false;
    }

    m_Commit.Modify( item );
    item->Move( m_Delta );
    m_MovedItems.push_back( item );
    return true;
}


bool KISURF_AI_PCB_MOVE_EDIT_ADAPTER::EndApply()
{
    if( m_MovedItems.empty() )
        return false;

    m_Commit.Push( m_CommitMessage );
    m_WasCommitted = true;
    return true;
}


void KISURF_AI_PCB_MOVE_EDIT_ADAPTER::AbortApply()
{
    m_Commit.Revert();
    m_WasReverted = true;
    m_MovedItems.clear();
}
