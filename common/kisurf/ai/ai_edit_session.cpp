#include <kisurf/ai/ai_edit_session.h>

AI_EDIT_SESSION::AI_EDIT_SESSION( AI_EDIT_ADAPTER& aAdapter ) :
        m_Adapter( aAdapter )
{
}


bool AI_EDIT_SESSION::Apply( const std::vector<AI_OBJECT_REF>& aObjects,
                             const AI_VALIDATION_SUMMARY& aValidation )
{
    if( aValidation.HasBlockingIssue() )
        return false;

    if( !m_Adapter.BeginApply( aValidation, aObjects.size() ) )
        return false;

    for( const AI_OBJECT_REF& object : aObjects )
    {
        if( !m_Adapter.ApplyObject( object ) )
        {
            m_Adapter.AbortApply();
            return false;
        }
    }

    if( !m_Adapter.EndApply() )
    {
        m_Adapter.AbortApply();
        return false;
    }

    m_LastValidation = aValidation;
    return true;
}
