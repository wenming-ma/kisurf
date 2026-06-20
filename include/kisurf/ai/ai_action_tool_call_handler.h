#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_runtime.h>
#include <kisurf/ai/ai_tool_execution.h>

#include <functional>
#include <optional>
#include <vector>

using AI_ACTION_SUGGESTION_SINK =
        std::function<std::optional<AI_SUGGESTION_RECORD>( AI_SUGGESTION_RECORD )>;

class KICOMMON_API AI_ACTION_TOOL_CALL_HANDLER : public AI_TOOL_CALL_HANDLER
{
public:
    AI_ACTION_TOOL_CALL_HANDLER( const AI_TOOL_EXECUTION_POLICY& aPolicy,
                                 AI_ACTION_RUNNER& aRunner,
                                 AI_ACTIVITY_LOG& aActivityLog,
                                 AI_ACTION_SUGGESTION_SINK aSuggestionSink = {} );

    void SetFallbackActions( std::vector<AI_ACTION_DESCRIPTOR> aActions );

    AI_TOOL_INVOCATION_RESULT HandleToolCall(
            const AI_PROVIDER_REQUEST& aRequest,
            const AI_TOOL_CALL_RECORD& aToolCall ) override;

private:
    const AI_ACTION_DESCRIPTOR* findAction( const AI_PROVIDER_REQUEST& aRequest,
                                            const wxString& aActionName ) const;

    const AI_TOOL_EXECUTION_POLICY& m_Policy;
    AI_ACTION_RUNNER&               m_Runner;
    AI_ACTIVITY_LOG&                m_ActivityLog;
    AI_ACTION_SUGGESTION_SINK       m_SuggestionSink;
    std::vector<AI_ACTION_DESCRIPTOR> m_FallbackActions;
};
