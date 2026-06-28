#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_types.h>

#include <cstddef>


struct KICOMMON_API AI_TOKEN_BUDGET_POLICY
{
    size_t m_TargetInputChars = 24000;
    size_t m_OutputHeadroomChars = 4000;
    size_t m_ToolHeadroomChars = 2000;
    size_t m_VisualHeadroomChars = 6000;
    size_t m_MinCompiledContextChars = 1200;
    size_t m_MaxContextActivityRecords = 24;
    size_t m_MaxToolResultChars = 4096;
    size_t m_MaxRetrievedMemoryChars = 4096;
    size_t m_MaxVisualDataUriChars = 1500000;
    bool   m_AllowVisualPixels = true;
};


struct KICOMMON_API AI_TOKEN_BUDGET_PLAN
{
    bool     m_ShouldShrink = false;
    wxString m_Reason;
    size_t   m_MaxProviderInputChars = 24000;
    size_t   m_MaxContextActivityRecords = 24;
    size_t   m_MaxToolResultChars = 4096;
    size_t   m_MaxRetrievedMemoryChars = 4096;
    size_t   m_MaxVisualDataUriChars = 1500000;
    bool     m_AllowVisualPixels = true;
};


KICOMMON_API AI_TOKEN_BUDGET_POLICY AiDefaultProviderInputBudgetPolicy();

KICOMMON_API AI_TOKEN_BUDGET_POLICY AiProviderInputBudgetPolicyForRequestKind(
        AI_PROVIDER_REQUEST_KIND aRequestKind );

KICOMMON_API AI_TOKEN_BUDGET_POLICY AiProviderShrinkRetryBudgetPolicy();

KICOMMON_API AI_TOKEN_BUDGET_PLAN AiPlanProviderInputBudget(
        const AI_PROVIDER_REQUEST& aRequest, const AI_TOKEN_BUDGET_POLICY& aPolicy );

KICOMMON_API AI_TOKEN_BUDGET_PLAN AiPlanProviderInputBudgetForRequest(
        const AI_PROVIDER_REQUEST& aRequest );

KICOMMON_API AI_PROVIDER_REQUEST AiApplyProviderInputBudgetPlan(
        const AI_PROVIDER_REQUEST& aRequest, const AI_TOKEN_BUDGET_PLAN& aPlan );
