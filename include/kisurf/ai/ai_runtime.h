#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_activity_log.h>
#include <kisurf/ai/ai_provider.h>
#include <kisurf/ai/ai_types.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

class KICOMMON_API AI_TOOL_CALL_HANDLER
{
public:
    virtual ~AI_TOOL_CALL_HANDLER() = default;

    virtual AI_TOOL_INVOCATION_RESULT HandleToolCall(
            const AI_PROVIDER_REQUEST& aRequest,
            const AI_TOOL_CALL_RECORD& aToolCall ) = 0;
};

class AI_PROMPT_TRACE_STORE;
class AI_ARTIFACT_STORE;

class KICOMMON_API AI_RUNTIME
{
public:
    explicit AI_RUNTIME( std::unique_ptr<AI_PROVIDER> aProvider );
    AI_RUNTIME( std::unique_ptr<AI_PROVIDER> aProvider, AI_ACTIVITY_LOG& aActivityLog );

    AI_PROVIDER_RESPONSE Submit( AI_PROVIDER_REQUEST aRequest );
    bool Cancel( uint64_t aRequestId );

    void SetProvider( std::unique_ptr<AI_PROVIDER> aProvider );
    void SetToolCallHandler( AI_TOOL_CALL_HANDLER* aHandler );
    void SetPromptTraceStore( AI_PROMPT_TRACE_STORE* aStore );
    void SetArtifactStore( AI_ARTIFACT_STORE* aStore );
    std::vector<AI_TRACE_RECORD> TraceRecords() const;
    std::vector<AI_ACTIVITY_RECORD> ActivityRecords() const;

private:
    std::unique_ptr<AI_PROVIDER> m_Provider;
    std::atomic<uint64_t>        m_NextRequestId;
    AI_ACTIVITY_LOG              m_OwnedActivityLog;
    AI_ACTIVITY_LOG*             m_ActivityLog = nullptr;
    AI_TOOL_CALL_HANDLER*        m_ToolCallHandler = nullptr;
    AI_PROMPT_TRACE_STORE*       m_PromptTraceStore = nullptr;
    AI_ARTIFACT_STORE*           m_ArtifactStore = nullptr;
    mutable std::mutex           m_Mutex;
    std::vector<AI_TRACE_RECORD> m_TraceRecords;
};
