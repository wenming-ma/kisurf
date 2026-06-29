#include <kisurf/ai/ai_token_budget_manager.h>

#include <algorithm>

namespace
{
size_t subtractHeadroom( size_t aTarget, size_t aHeadroom )
{
    return aTarget > aHeadroom ? aTarget - aHeadroom : 0;
}


void addReason( AI_TOKEN_BUDGET_PLAN& aPlan, const wxString& aReason )
{
    aPlan.m_ShouldShrink = true;

    if( !aPlan.m_Reason.IsEmpty() )
        aPlan.m_Reason << wxS( "," );

    aPlan.m_Reason << aReason;
}


size_t plannedTextBudget( const AI_PROVIDER_REQUEST& aRequest,
                          const AI_TOKEN_BUDGET_POLICY& aPolicy )
{
    size_t budget = aPolicy.m_TargetInputChars;
    budget = subtractHeadroom( budget, aPolicy.m_OutputHeadroomChars );
    budget = subtractHeadroom( budget, aPolicy.m_ToolHeadroomChars );

    if( aRequest.m_ContextSnapshot.m_Visual.HasPixels() )
        budget = subtractHeadroom( budget, aPolicy.m_VisualHeadroomChars );

    return std::max( budget, aPolicy.m_MinCompiledContextChars );
}
}


AI_TOKEN_BUDGET_POLICY AiDefaultProviderInputBudgetPolicy()
{
    return AiProviderInputBudgetPolicyForRequestKind( AI_PROVIDER_REQUEST_KIND::Chat );
}


AI_TOKEN_BUDGET_POLICY AiProviderInputBudgetPolicyForRequestKind(
        AI_PROVIDER_REQUEST_KIND aRequestKind )
{
    AI_TOKEN_BUDGET_POLICY policy;

    switch( aRequestKind )
    {
    case AI_PROVIDER_REQUEST_KIND::Chat:
        policy.m_TargetInputChars = 36000;
        return policy;

    case AI_PROVIDER_REQUEST_KIND::NextActionDecision:
        policy.m_TargetInputChars = 18000;
        policy.m_OutputHeadroomChars = 3000;
        policy.m_ToolHeadroomChars = 4000;
        policy.m_VisualHeadroomChars = 6000;
        policy.m_MinCompiledContextChars = 1200;
        policy.m_MaxContextActivityRecords = 8;
        policy.m_MaxToolResultChars = 16384;
        policy.m_MaxRetrievedMemoryChars = 2048;
        policy.m_MaxVisualDataUriChars = 1000000;
        policy.m_AllowVisualPixels = true;
        return policy;

    case AI_PROVIDER_REQUEST_KIND::NextActionReview:
        policy.m_TargetInputChars = 22000;
        policy.m_OutputHeadroomChars = 3000;
        policy.m_ToolHeadroomChars = 7000;
        policy.m_VisualHeadroomChars = 7000;
        policy.m_MinCompiledContextChars = 1200;
        policy.m_MaxContextActivityRecords = 6;
        policy.m_MaxToolResultChars = 24576;
        policy.m_MaxRetrievedMemoryChars = 1536;
        policy.m_MaxVisualDataUriChars = 1500000;
        policy.m_AllowVisualPixels = true;
        return policy;
    }

    return policy;
}


AI_TOKEN_BUDGET_POLICY AiProviderShrinkRetryBudgetPolicy()
{
    AI_TOKEN_BUDGET_POLICY policy;
    policy.m_TargetInputChars = 7000;
    policy.m_OutputHeadroomChars = 1000;
    policy.m_ToolHeadroomChars = 0;
    policy.m_VisualHeadroomChars = 0;
    policy.m_MinCompiledContextChars = 1000;
    policy.m_MaxContextActivityRecords = 6;
    policy.m_MaxToolResultChars = 1024;
    policy.m_MaxRetrievedMemoryChars = 1024;
    policy.m_MaxVisualDataUriChars = 0;
    policy.m_AllowVisualPixels = false;
    return policy;
}


AI_TOKEN_BUDGET_PLAN AiPlanProviderInputBudget(
        const AI_PROVIDER_REQUEST& aRequest, const AI_TOKEN_BUDGET_POLICY& aPolicy )
{
    AI_TOKEN_BUDGET_PLAN plan;
    plan.m_MaxProviderInputChars = std::min( aRequest.m_MaxProviderInputChars,
                                               plannedTextBudget( aRequest, aPolicy ) );
    plan.m_MaxContextActivityRecords = std::min( aRequest.m_MaxContextActivityRecords,
                                                 aPolicy.m_MaxContextActivityRecords );
    plan.m_MaxToolResultChars = std::min( aRequest.m_MaxToolResultChars,
                                          aPolicy.m_MaxToolResultChars );
    plan.m_MaxRetrievedMemoryChars = std::min( aRequest.m_MaxRetrievedMemoryChars,
                                               aPolicy.m_MaxRetrievedMemoryChars );
    plan.m_MaxVisualDataUriChars = std::min( aRequest.m_MaxVisualDataUriChars,
                                             aPolicy.m_MaxVisualDataUriChars );
    plan.m_AllowVisualPixels = aRequest.m_AllowVisualPixels && aPolicy.m_AllowVisualPixels;

    if( plan.m_MaxProviderInputChars < aRequest.m_MaxProviderInputChars )
        addReason( plan, wxS( "compiled_input_budget" ) );

    if( plan.m_MaxContextActivityRecords < aRequest.m_MaxContextActivityRecords )
        addReason( plan, wxS( "activity_record_budget" ) );

    if( plan.m_MaxToolResultChars < aRequest.m_MaxToolResultChars )
        addReason( plan, wxS( "tool_result_budget" ) );

    if( plan.m_MaxRetrievedMemoryChars < aRequest.m_MaxRetrievedMemoryChars )
        addReason( plan, wxS( "retrieved_memory_budget" ) );

    if( !plan.m_AllowVisualPixels && aRequest.m_AllowVisualPixels )
        addReason( plan, wxS( "visual_pixels_disabled" ) );

    if( aRequest.m_ContextSnapshot.m_Visual.HasPixels()
        && aRequest.m_ContextSnapshot.m_Visual.m_DataUri.length()
           > plan.m_MaxVisualDataUriChars )
    {
        plan.m_AllowVisualPixels = false;
        plan.m_MaxVisualDataUriChars = 0;
        addReason( plan, wxS( "visual_payload_budget" ) );
    }

    return plan;
}


AI_TOKEN_BUDGET_PLAN AiPlanProviderInputBudgetForRequest(
        const AI_PROVIDER_REQUEST& aRequest )
{
    return AiPlanProviderInputBudget(
            aRequest, AiProviderInputBudgetPolicyForRequestKind( aRequest.m_RequestKind ) );
}


AI_PROVIDER_REQUEST AiApplyProviderInputBudgetPlan(
        const AI_PROVIDER_REQUEST& aRequest, const AI_TOKEN_BUDGET_PLAN& aPlan )
{
    AI_PROVIDER_REQUEST applied = aRequest;
    applied.m_MaxProviderInputChars = aPlan.m_MaxProviderInputChars;
    applied.m_MaxContextActivityRecords = aPlan.m_MaxContextActivityRecords;
    applied.m_MaxToolResultChars = aPlan.m_MaxToolResultChars;
    applied.m_MaxRetrievedMemoryChars = aPlan.m_MaxRetrievedMemoryChars;
    applied.m_MaxVisualDataUriChars = aPlan.m_MaxVisualDataUriChars;
    applied.m_AllowVisualPixels = aPlan.m_AllowVisualPixels;
    applied.m_ContextCompiled = false;
    applied.m_ProviderInputWasShrunk = false;
    applied.m_ContextEstimatedChars = 0;
    applied.m_CompiledUserMessageText.Clear();
    applied.m_PromptTraceJson.Clear();
    return applied;
}
