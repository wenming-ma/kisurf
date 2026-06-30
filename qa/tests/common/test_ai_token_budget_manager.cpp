#include <boost/test/unit_test.hpp>
#include <kisurf/ai/ai_token_budget_manager.h>

BOOST_AUTO_TEST_SUITE( AiTokenBudgetManager )


BOOST_AUTO_TEST_CASE( PlansShrinkAndDisablesOversizedVisualPayload )
{
    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 91;
    request.m_MaxProviderInputChars = 10000;
    request.m_MaxContextActivityRecords = 24;
    request.m_MaxToolResultChars = 4096;
    request.m_MaxRetrievedMemoryChars = 4096;
    request.m_MaxVisualDataUriChars = 2000000;
    request.m_AllowVisualPixels = true;
    request.m_ContextSnapshot.m_Visual.m_Source = wxS( "canvas" );
    request.m_ContextSnapshot.m_Visual.m_MimeType = wxS( "image/png" );

    wxString dataUri = wxS( "data:image/png;base64," );

    for( int i = 0; i < 3000; ++i )
        dataUri << wxS( "a" );

    request.m_ContextSnapshot.m_Visual.m_DataUri = dataUri;

    AI_TOKEN_BUDGET_POLICY policy;
    policy.m_TargetInputChars = 3000;
    policy.m_OutputHeadroomChars = 500;
    policy.m_ToolHeadroomChars = 500;
    policy.m_VisualHeadroomChars = 500;
    policy.m_MinCompiledContextChars = 600;
    policy.m_MaxContextActivityRecords = 6;
    policy.m_MaxToolResultChars = 1024;
    policy.m_MaxRetrievedMemoryChars = 768;
    policy.m_MaxVisualDataUriChars = 500;
    policy.m_AllowVisualPixels = true;

    AI_TOKEN_BUDGET_PLAN plan = AiPlanProviderInputBudget( request, policy );

    BOOST_CHECK( plan.m_ShouldShrink );
    BOOST_CHECK_EQUAL( plan.m_MaxProviderInputChars, 1500 );
    BOOST_CHECK_EQUAL( plan.m_MaxContextActivityRecords, 6 );
    BOOST_CHECK_EQUAL( plan.m_MaxToolResultChars, 1024 );
    BOOST_CHECK_EQUAL( plan.m_MaxRetrievedMemoryChars, 768 );
    BOOST_CHECK_EQUAL( plan.m_MaxVisualDataUriChars, 0 );
    BOOST_CHECK( !plan.m_AllowVisualPixels );
    BOOST_CHECK( plan.m_Reason.Contains( wxS( "visual" ) ) );
}


BOOST_AUTO_TEST_CASE( AppliesPlanClearsCompiledStateAndPreservesInputBlocks )
{
    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 92;
    request.m_MaxProviderInputChars = 10000;
    request.m_MaxContextActivityRecords = 24;
    request.m_MaxToolResultChars = 4096;
    request.m_MaxRetrievedMemoryChars = 4096;
    request.m_MaxVisualDataUriChars = 2000000;
    request.m_AllowVisualPixels = true;
    request.m_ContextCompiled = true;
    request.m_ProviderInputWasShrunk = true;
    request.m_ContextEstimatedChars = 9999;
    request.m_CompiledUserMessageText = wxS( "already compiled" );
    request.m_PromptTraceJson = wxS( "{}" );

    AI_PROVIDER_INPUT_BLOCK block;
    block.m_Id = wxS( "old" );
    request.m_ProviderInputBlocks.push_back( block );

    AI_TOKEN_BUDGET_PLAN plan;
    plan.m_ShouldShrink = true;
    plan.m_MaxProviderInputChars = 1200;
    plan.m_MaxContextActivityRecords = 4;
    plan.m_MaxToolResultChars = 512;
    plan.m_MaxRetrievedMemoryChars = 256;
    plan.m_MaxVisualDataUriChars = 0;
    plan.m_AllowVisualPixels = false;
    plan.m_Reason = wxS( "unit-test" );

    AI_PROVIDER_REQUEST applied = AiApplyProviderInputBudgetPlan( request, plan );

    BOOST_CHECK_EQUAL( applied.m_MaxProviderInputChars, 1200 );
    BOOST_CHECK_EQUAL( applied.m_MaxContextActivityRecords, 4 );
    BOOST_CHECK_EQUAL( applied.m_MaxToolResultChars, 512 );
    BOOST_CHECK_EQUAL( applied.m_MaxRetrievedMemoryChars, 256 );
    BOOST_CHECK_EQUAL( applied.m_MaxVisualDataUriChars, 0 );
    BOOST_CHECK( !applied.m_AllowVisualPixels );
    BOOST_CHECK( !applied.m_ContextCompiled );
    BOOST_CHECK( !applied.m_ProviderInputWasShrunk );
    BOOST_CHECK_EQUAL( applied.m_ContextEstimatedChars, 0 );
    BOOST_CHECK( applied.m_CompiledUserMessageText.IsEmpty() );
    BOOST_CHECK( applied.m_PromptTraceJson.IsEmpty() );
    BOOST_REQUIRE_EQUAL( applied.m_ProviderInputBlocks.size(), 1 );
    BOOST_CHECK_EQUAL( applied.m_ProviderInputBlocks.front().m_Id,
                       wxString( wxS( "old" ) ) );
}


BOOST_AUTO_TEST_CASE( RequestKindDefaultPoliciesSeparateChatAndNextAction )
{
    const AI_TOKEN_BUDGET_POLICY chat =
            AiProviderInputBudgetPolicyForRequestKind( AI_PROVIDER_REQUEST_KIND::Chat );
    const AI_TOKEN_BUDGET_POLICY decision =
            AiProviderInputBudgetPolicyForRequestKind(
                    AI_PROVIDER_REQUEST_KIND::NextActionDecision );
    const AI_TOKEN_BUDGET_POLICY review =
            AiProviderInputBudgetPolicyForRequestKind(
                    AI_PROVIDER_REQUEST_KIND::NextActionReview );

    BOOST_CHECK_GT( chat.m_MaxContextActivityRecords,
                    decision.m_MaxContextActivityRecords );
    BOOST_CHECK_GT( chat.m_MaxRetrievedMemoryChars,
                    decision.m_MaxRetrievedMemoryChars );
    BOOST_CHECK_GE( decision.m_ToolHeadroomChars, chat.m_ToolHeadroomChars );

    BOOST_CHECK_LE( review.m_MaxContextActivityRecords,
                    decision.m_MaxContextActivityRecords );
    BOOST_CHECK_GT( review.m_MaxToolResultChars,
                    decision.m_MaxToolResultChars );
    BOOST_CHECK_GE( review.m_VisualHeadroomChars,
                    decision.m_VisualHeadroomChars );
    BOOST_CHECK( chat.m_AllowVisualPixels );
    BOOST_CHECK( decision.m_AllowVisualPixels );
    BOOST_CHECK( review.m_AllowVisualPixels );
}


BOOST_AUTO_TEST_CASE( NextActionBudgetUsesConfiguredContextWindow )
{
    AI_PROVIDER_REQUEST decisionRequest;
    decisionRequest.m_RequestKind = AI_PROVIDER_REQUEST_KIND::NextActionDecision;
    decisionRequest.m_MaxProviderInputChars = 160000;
    decisionRequest.m_MaxContextActivityRecords = 8;
    decisionRequest.m_MaxToolResultChars = 16384;
    decisionRequest.m_MaxRetrievedMemoryChars = 2048;

    AI_PROVIDER_REQUEST reviewRequest = decisionRequest;
    reviewRequest.m_RequestKind = AI_PROVIDER_REQUEST_KIND::NextActionReview;
    reviewRequest.m_MaxToolResultChars = 24576;
    reviewRequest.m_MaxRetrievedMemoryChars = 1536;

    const AI_TOKEN_BUDGET_PLAN decisionPlan =
            AiPlanProviderInputBudgetForRequest( decisionRequest );
    const AI_TOKEN_BUDGET_PLAN reviewPlan =
            AiPlanProviderInputBudgetForRequest( reviewRequest );

    BOOST_CHECK_EQUAL( decisionPlan.m_MaxProviderInputChars, 160000 );
    BOOST_CHECK_EQUAL( reviewPlan.m_MaxProviderInputChars, 160000 );
    BOOST_CHECK( !decisionPlan.m_Reason.Contains( wxS( "compiled_input_budget" ) ) );
    BOOST_CHECK( !reviewPlan.m_Reason.Contains( wxS( "compiled_input_budget" ) ) );
}


BOOST_AUTO_TEST_CASE( ChatPolicyKeepsToolResultsWithinDefaultBudget )
{
    const AI_TOKEN_BUDGET_POLICY baseline;
    const AI_TOKEN_BUDGET_POLICY chat =
            AiProviderInputBudgetPolicyForRequestKind( AI_PROVIDER_REQUEST_KIND::Chat );

    BOOST_CHECK_LE( chat.m_MaxToolResultChars,
                    baseline.m_MaxToolResultChars );
}


BOOST_AUTO_TEST_CASE( ChatDefaultBudgetDoesNotShrinkNormalRequest )
{
    AI_PROVIDER_REQUEST request;
    request.m_RequestKind = AI_PROVIDER_REQUEST_KIND::Chat;
    request.m_MaxProviderInputChars = 24000;
    request.m_MaxContextActivityRecords = 24;
    request.m_MaxToolResultChars = 4096;
    request.m_MaxRetrievedMemoryChars = 4096;

    const AI_TOKEN_BUDGET_PLAN plan =
            AiPlanProviderInputBudgetForRequest( request );

    BOOST_CHECK( !plan.m_ShouldShrink );
    BOOST_CHECK_EQUAL( plan.m_MaxProviderInputChars,
                       request.m_MaxProviderInputChars );
    BOOST_CHECK_EQUAL( plan.m_MaxContextActivityRecords,
                       request.m_MaxContextActivityRecords );
}


BOOST_AUTO_TEST_CASE( PlansProviderInputBudgetFromRequestKind )
{
    AI_PROVIDER_REQUEST chatRequest;
    chatRequest.m_RequestKind = AI_PROVIDER_REQUEST_KIND::Chat;
    chatRequest.m_MaxProviderInputChars = 50000;
    chatRequest.m_MaxContextActivityRecords = 50;
    chatRequest.m_MaxToolResultChars = 50000;
    chatRequest.m_MaxRetrievedMemoryChars = 20000;

    AI_PROVIDER_REQUEST nextActionRequest = chatRequest;
    nextActionRequest.m_RequestKind = AI_PROVIDER_REQUEST_KIND::NextActionDecision;

    const AI_TOKEN_BUDGET_PLAN chatPlan =
            AiPlanProviderInputBudgetForRequest( chatRequest );
    const AI_TOKEN_BUDGET_PLAN decisionPlan =
            AiPlanProviderInputBudgetForRequest( nextActionRequest );

    BOOST_CHECK_GT( chatPlan.m_MaxContextActivityRecords,
                    decisionPlan.m_MaxContextActivityRecords );
    BOOST_CHECK_GT( chatPlan.m_MaxRetrievedMemoryChars,
                    decisionPlan.m_MaxRetrievedMemoryChars );
    BOOST_CHECK_LT( decisionPlan.m_MaxToolResultChars,
                    nextActionRequest.m_MaxToolResultChars );
    BOOST_CHECK( decisionPlan.m_ShouldShrink );
    BOOST_CHECK( decisionPlan.m_Reason.Contains( wxS( "activity_record_budget" ) ) );
}


BOOST_AUTO_TEST_SUITE_END()
