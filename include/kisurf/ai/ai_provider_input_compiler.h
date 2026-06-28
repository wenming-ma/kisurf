#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_types.h>

KICOMMON_API AI_PROVIDER_REQUEST AiCompileProviderInput(
        const AI_PROVIDER_REQUEST& aRequest );

KICOMMON_API AI_PROVIDER_REQUEST AiCompileProviderInputWithBudget(
        const AI_PROVIDER_REQUEST& aRequest );

KICOMMON_API wxString AiCompileProviderMessagesJson(
        const AI_PROVIDER_REQUEST& aRequest,
        const wxString& aDefaultSystemPrompt );
