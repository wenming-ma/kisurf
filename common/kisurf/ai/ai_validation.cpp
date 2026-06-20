#include <kisurf/ai/ai_validation.h>

namespace
{

bool sameIssue( const AI_VALIDATION_ISSUE& aLeft, const AI_VALIDATION_ISSUE& aRight )
{
    return aLeft.m_Severity == aRight.m_Severity && aLeft.m_Message == aRight.m_Message;
}

} // namespace


AI_VALIDATION_SUMMARY AI_VALIDATION_DIFF::Classify() const
{
    AI_VALIDATION_SUMMARY summary;

    for( AI_VALIDATION_ISSUE issue : m_After )
    {
        bool existedBefore = false;

        for( const AI_VALIDATION_ISSUE& before : m_Before )
        {
            if( sameIssue( before, issue ) )
            {
                existedBefore = true;
                break;
            }
        }

        issue.m_IsNew = !existedBefore;
        summary.m_Issues.push_back( issue );
    }

    return summary;
}


bool AI_VALIDATION_POLICY::BlocksApply( AI_VALIDATION_SCOPE aScope,
                                        const AI_VALIDATION_SUMMARY& aSummary ) const
{
    if( aScope == AI_VALIDATION_SCOPE::None )
        return false;

    return aSummary.HasBlockingIssue();
}
