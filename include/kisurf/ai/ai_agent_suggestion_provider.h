#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_provider.h>
#include <kisurf/ai/ai_suggestion_orchestrator.h>

#include <memory>

class KICOMMON_API AI_AGENT_SUGGESTION_PROVIDER : public AI_SUGGESTION_PROVIDER
{
public:
    AI_AGENT_SUGGESTION_PROVIDER();
    explicit AI_AGENT_SUGGESTION_PROVIDER( std::unique_ptr<AI_PROVIDER> aProvider );

    std::optional<AI_SUGGESTION_RECORD> Suggest(
            const AI_SUGGESTION_TRIGGER& aTrigger ) override;

private:
    std::unique_ptr<AI_PROVIDER> m_Provider;
};
