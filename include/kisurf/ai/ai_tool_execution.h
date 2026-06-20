#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_activity_log.h>
#include <kisurf/ai/ai_types.h>

#include <set>

class KICOMMON_API AI_ACTION_RUNNER
{
public:
    virtual ~AI_ACTION_RUNNER() = default;

    virtual bool RunActionByName( const wxString& aActionName, wxString& aError ) = 0;
};

class KICOMMON_API AI_TOOL_EXECUTION_POLICY
{
public:
    void AllowAction( const wxString& aActionName );
    bool IsAllowlisted( const wxString& aActionName ) const;

    AI_TOOL_INVOCATION_RESULT Evaluate( const AI_TOOL_INVOCATION_REQUEST& aRequest ) const;

private:
    std::set<wxString> m_Allowlist;
};

class KICOMMON_API AI_TOOL_EXECUTOR
{
public:
    AI_TOOL_EXECUTOR( const AI_TOOL_EXECUTION_POLICY& aPolicy,
                      AI_ACTION_RUNNER& aRunner,
                      AI_ACTIVITY_LOG& aActivityLog );

    AI_TOOL_INVOCATION_RESULT Invoke( const AI_TOOL_INVOCATION_REQUEST& aRequest );

private:
    void recordRequest( const AI_TOOL_INVOCATION_REQUEST& aRequest );
    void recordResult( AI_ACTIVITY_KIND aKind, const AI_TOOL_INVOCATION_RESULT& aResult );

    const AI_TOOL_EXECUTION_POLICY& m_Policy;
    AI_ACTION_RUNNER&               m_Runner;
    AI_ACTIVITY_LOG&                m_ActivityLog;
};
