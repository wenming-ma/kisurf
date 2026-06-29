#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_runtime.h>

#include <functional>

class KICOMMON_API AI_MARSHALLED_TOOL_CALL_HANDLER : public AI_TOOL_CALL_HANDLER
{
public:
    using DISPATCH_TO_TARGET_THREAD = std::function<void( std::function<void()> )>;
    using IS_TARGET_THREAD = std::function<bool()>;

    AI_MARSHALLED_TOOL_CALL_HANDLER(
            AI_TOOL_CALL_HANDLER& aTarget,
            DISPATCH_TO_TARGET_THREAD aDispatchToTargetThread,
            IS_TARGET_THREAD aIsTargetThread );

    AI_TOOL_INVOCATION_RESULT HandleToolCall(
            const AI_PROVIDER_REQUEST& aRequest,
            const AI_TOOL_CALL_RECORD& aToolCall ) override;

private:
    AI_TOOL_CALL_HANDLER&       m_Target;
    DISPATCH_TO_TARGET_THREAD   m_DispatchToTargetThread;
    IS_TARGET_THREAD            m_IsTargetThread;
};
