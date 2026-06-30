#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_runtime.h>
#include <kisurf/ai/ai_session_tool_call_handler.h>

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

class KICOMMON_API AI_MARSHALLED_SESSION_PREVIEW_SERVICE :
        public AI_SESSION_PREVIEW_SERVICE
{
public:
    using DISPATCH_TO_TARGET_THREAD =
            AI_MARSHALLED_TOOL_CALL_HANDLER::DISPATCH_TO_TARGET_THREAD;
    using IS_TARGET_THREAD = AI_MARSHALLED_TOOL_CALL_HANDLER::IS_TARGET_THREAD;

    AI_MARSHALLED_SESSION_PREVIEW_SERVICE(
            AI_SESSION_PREVIEW_SERVICE& aTarget,
            DISPATCH_TO_TARGET_THREAD aDispatchToTargetThread,
            IS_TARGET_THREAD aIsTargetThread );

    AI_SESSION_PREVIEW_RESULT RenderPreview(
            const AI_EXECUTION_SESSION& aSession,
            const wxString& aArgumentsJson ) override;
    void ClearPreview( uint64_t aSessionId ) override;

private:
    AI_SESSION_PREVIEW_SERVICE& m_Target;
    DISPATCH_TO_TARGET_THREAD   m_DispatchToTargetThread;
    IS_TARGET_THREAD            m_IsTargetThread;
};

class KICOMMON_API AI_MARSHALLED_SESSION_VALIDATION_SERVICE :
        public AI_SESSION_VALIDATION_SERVICE
{
public:
    using DISPATCH_TO_TARGET_THREAD =
            AI_MARSHALLED_TOOL_CALL_HANDLER::DISPATCH_TO_TARGET_THREAD;
    using IS_TARGET_THREAD = AI_MARSHALLED_TOOL_CALL_HANDLER::IS_TARGET_THREAD;

    AI_MARSHALLED_SESSION_VALIDATION_SERVICE(
            AI_SESSION_VALIDATION_SERVICE& aTarget,
            DISPATCH_TO_TARGET_THREAD aDispatchToTargetThread,
            IS_TARGET_THREAD aIsTargetThread );

    AI_SESSION_VALIDATION_RESULT RunValidation(
            const AI_EXECUTION_SESSION& aSession,
            const wxString& aArgumentsJson,
            const wxString& aCurrentResultJson ) override;

private:
    AI_SESSION_VALIDATION_SERVICE& m_Target;
    DISPATCH_TO_TARGET_THREAD      m_DispatchToTargetThread;
    IS_TARGET_THREAD               m_IsTargetThread;
};
