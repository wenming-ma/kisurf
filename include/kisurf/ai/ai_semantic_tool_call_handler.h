#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_runtime.h>
#include <kisurf/ai/ai_semantic_ui.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

using AI_SEMANTIC_UI_TREE_PROVIDER = std::function<AI_SEMANTIC_UI_TREE()>;

using AI_SEMANTIC_UI_ACTION_INVOKER =
        std::function<AI_SEMANTIC_UI_ACTION_RESULT( const AI_SEMANTIC_UI_ACTION_REQUEST& )>;

class KICOMMON_API AI_SEMANTIC_TOOL_CALL_HANDLER : public AI_TOOL_CALL_HANDLER
{
public:
    AI_SEMANTIC_TOOL_CALL_HANDLER() = default;
    AI_SEMANTIC_TOOL_CALL_HANDLER( AI_SEMANTIC_UI_TREE_PROVIDER aSemanticUiTreeProvider,
                                   AI_SEMANTIC_UI_ACTION_INVOKER aSemanticUiActionInvoker );

    AI_TOOL_INVOCATION_RESULT HandleToolCall(
            const AI_PROVIDER_REQUEST& aRequest,
            const AI_TOOL_CALL_RECORD& aToolCall ) override;

private:
    AI_SEMANTIC_UI_TREE_PROVIDER  m_SemanticUiTreeProvider;
    AI_SEMANTIC_UI_ACTION_INVOKER m_SemanticUiActionInvoker;
};

class KICOMMON_API AI_TOOL_CALL_DISPATCHER : public AI_TOOL_CALL_HANDLER
{
public:
    AI_TOOL_CALL_DISPATCHER() = default;
    AI_TOOL_CALL_DISPATCHER( const AI_TOOL_CALL_DISPATCHER& ) = delete;
    AI_TOOL_CALL_DISPATCHER& operator=( const AI_TOOL_CALL_DISPATCHER& ) = delete;

    void AddHandler( std::unique_ptr<AI_TOOL_CALL_HANDLER> aHandler );

    AI_TOOL_INVOCATION_RESULT HandleToolCall(
            const AI_PROVIDER_REQUEST& aRequest,
            const AI_TOOL_CALL_RECORD& aToolCall ) override;

private:
    std::vector<std::unique_ptr<AI_TOOL_CALL_HANDLER>> m_Handlers;
};
