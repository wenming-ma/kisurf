#include <boost/test/unit_test.hpp>

#include <kisurf/ai/ai_next_action_runtime.h>
#include <kisurf/ai/ai_suggestion_operations.h>

#include <deque>
#include <memory>

namespace
{
class SCRIPTED_NEXT_ACTION_PROVIDER : public AI_PROVIDER
{
public:
    explicit SCRIPTED_NEXT_ACTION_PROVIDER( std::deque<wxString> aBodies ) :
            m_Bodies( std::move( aBodies ) )
    {
    }

    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_Requests.push_back( aRequest );

        AI_PROVIDER_RESPONSE response;
        response.m_RequestId = aRequest.m_RequestId;
        response.m_Title = wxS( "scripted next action" );

        if( m_Bodies.empty() )
        {
            response.m_Body = wxS( "{\"decision_kind\":\"abandon\"}" );
            return response;
        }

        response.m_Body = m_Bodies.front();
        m_Bodies.pop_front();
        return response;
    }

    int                              m_CallCount = 0;
    std::vector<AI_PROVIDER_REQUEST> m_Requests;
    std::deque<wxString>             m_Bodies;
};


wxString viaDetails( int aX, int aY, const wxString& aNetName,
                     int aDiameter = 600000 )
{
    wxString details;
    details << wxS( "{\"kind\":\"via\",\"position\":{\"x\":" ) << aX
            << wxS( ",\"y\":" ) << aY << wxS( "},\"diameter\":" )
            << aDiameter << wxS( ",\"net_name\":\"" ) << aNetName << wxS( "\"}" );
    return details;
}


AI_OBJECT_REF viaRef( int aX, int aY, const wxString& aNetName = wxS( "GND" ) )
{
    return AI_OBJECT_REF( KIID(), PCB_VIA_T,
                          wxString::Format( wxS( "via:%d,%d" ), aX, aY ),
                          viaDetails( aX, aY, aNetName ) );
}


AI_SUGGESTION_TRIGGER makeViaTrigger()
{
    AI_SUGGESTION_TRIGGER trigger;
    trigger.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextVersion.m_DocumentRevision = 12;
    trigger.m_ContextVersion.m_ViewRevision = 5;
    trigger.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_Version = trigger.m_ContextVersion;
    trigger.m_ContextSnapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::PlacingVia;
    trigger.m_ContextSnapshot.m_ToolState.m_ContextVersion = trigger.m_ContextVersion;
    trigger.m_ContextSnapshot.m_VisibleObjects.push_back( viaRef( 100, 50 ) );
    trigger.m_ContextSnapshot.m_VisibleObjects.push_back( viaRef( 200, 50 ) );
    trigger.m_ContextSnapshot.m_VisibleObjects.push_back( viaRef( 300, 50 ) );
    trigger.m_Activity.m_Sequence = 44;
    trigger.m_Activity.m_ActionName = wxS( "pcbnew.Interactive.placeVia" );
    trigger.m_Reason = wxS( "cursor paused" );
    trigger.m_PreviewOnly = true;
    return trigger;
}


AI_SUGGESTION_TRIGGER makeMouseMoveTrigger()
{
    AI_SUGGESTION_TRIGGER trigger = makeViaTrigger();
    trigger.m_Activity.m_Sequence = 45;
    trigger.m_Activity.m_ActionName = wxS( "mouse.move" );
    return trigger;
}
} // namespace

BOOST_AUTO_TEST_SUITE( AiNextActionRuntime )


BOOST_AUTO_TEST_CASE( SchedulerSuppressesRawMouseMove )
{
    AI_NEXT_ACTION_SCHEDULER scheduler;

    BOOST_CHECK( !scheduler.BuildSemanticEvent( makeMouseMoveTrigger() ).has_value() );
    BOOST_CHECK( scheduler.BuildSemanticEvent( makeViaTrigger() ).has_value() );
}


BOOST_AUTO_TEST_CASE( SchedulerDebouncesSameSemanticSlot )
{
    AI_NEXT_ACTION_SCHEDULER scheduler;

    BOOST_CHECK( scheduler.BuildSemanticEvent( makeViaTrigger() ).has_value() );
    BOOST_CHECK( !scheduler.BuildSemanticEvent( makeViaTrigger() ).has_value() );

    AI_SUGGESTION_TRIGGER changed = makeViaTrigger();
    changed.m_ContextVersion.m_ViewRevision = 99;
    changed.m_ContextSnapshot.m_Version = changed.m_ContextVersion;
    changed.m_ContextSnapshot.m_ToolState.m_ContextVersion = changed.m_ContextVersion;

    BOOST_CHECK( scheduler.BuildSemanticEvent( changed ).has_value() );
}


BOOST_AUTO_TEST_CASE( RuntimePublishesOnlyAfterDecisionAndReviewTurns )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\","
                   "\"reason_code\":\"likely_helpful\"}" ),
              wxS( "{\"decision_kind\":\"publish\","
                   "\"reason_code\":\"acceptable\"}" ) } );

    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ) };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK_EQUAL( provider->m_CallCount, 2 );
    BOOST_REQUIRE_EQUAL( provider->m_Requests.size(), 2 );
    BOOST_CHECK( provider->m_Requests.at( 0 ).m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionDecision );
    BOOST_CHECK( provider->m_Requests.at( 1 ).m_RequestKind
                 == AI_PROVIDER_REQUEST_KIND::NextActionReview );
    BOOST_CHECK( provider->m_Requests.at( 0 ).m_DisableDefaultTools );
    BOOST_CHECK( provider->m_Requests.at( 0 ).m_UserText.Contains(
            wxS( "candidate.generate" ) ) );
    BOOST_CHECK( provider->m_Requests.at( 0 ).m_ToolCatalogJson.Contains(
            wxS( "publish.preview" ) ) );
    BOOST_CHECK( provider->m_Requests.at( 1 ).m_UserText.Contains(
            wxS( "validate.hidden_attempt" ) ) );
    BOOST_CHECK( suggestion->m_Title.Contains( wxS( "next via" ) ) );
    BOOST_CHECK( !suggestion->m_PreviewOnly );
    BOOST_CHECK( !suggestion->m_EditObjects.empty() );
    BOOST_CHECK( runtime.CanAccept( suggestion->m_Id ) );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains( wxS( "attempt_id" ) ) );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains( wxS( "preview_lease" ) ) );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains( wxS( "preview_id" ) ) );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains( wxS( "tool_results" ) ) );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains( wxS( "session_journal" ) ) );
    BOOST_CHECK( suggestion->m_RuntimeProvenanceJson.Contains( wxS( "pcb.create_via" ) ) );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK_NE( runtime.Attempts().front().m_HiddenSessionId, 0 );
    BOOST_CHECK_NE( runtime.Attempts().front().m_HiddenStepId, 0 );
    BOOST_CHECK_NE( runtime.Attempts().front().m_BaseCheckpointId, 0 );
    BOOST_CHECK( runtime.Attempts().front().m_JournalJson.Contains(
            wxS( "pcb.create_via" ) ) );

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( suggestion->m_ArgumentsJson );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsPlaceViaPreview() );
    BOOST_CHECK_EQUAL( operation->m_Position.x, 400 );
    BOOST_CHECK_EQUAL( operation->m_Position.y, 50 );
}


BOOST_AUTO_TEST_CASE( RuntimeRollbackRetryRecordsCheckpointRollback )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              wxS( "{\"decision_kind\":\"rollback_retry\","
                   "\"reason_code\":\"try_next_candidate\"}" ) } );

    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ) };

    BOOST_CHECK( !runtime.Update( makeViaTrigger() ).has_value() );
    BOOST_REQUIRE_EQUAL( runtime.Attempts().size(), 1 );
    BOOST_CHECK( runtime.Attempts().front().m_RollbackJson.Contains(
            wxS( "rollback.attempt" ) ) );
    BOOST_CHECK( runtime.Attempts().front().m_RollbackJson.Contains(
            wxS( "rolled_back" ) ) );
}


BOOST_AUTO_TEST_CASE( RuntimeDoesNotPublishWithoutReviewApproval )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              wxS( "{\"decision_kind\":\"abandon\","
                   "\"reason_code\":\"low_confidence\"}" ) } );

    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ) };

    BOOST_CHECK( !runtime.Update( makeViaTrigger() ).has_value() );
    BOOST_CHECK_EQUAL( provider->m_CallCount, 2 );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
}


BOOST_AUTO_TEST_CASE( RuntimeWaitDecisionDoesNotRunAttemptOrReview )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"wait\","
                   "\"reason_code\":\"user_busy\"}" ) } );

    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ) };

    BOOST_CHECK( !runtime.Update( makeViaTrigger() ).has_value() );
    BOOST_CHECK_EQUAL( provider->m_CallCount, 1 );
    BOOST_REQUIRE_EQUAL( runtime.Steps().size(), 1 );
    BOOST_CHECK( runtime.Steps().front().m_AttemptIds.empty() );
    BOOST_CHECK( runtime.Steps().front().m_Status
                 == AI_NEXT_ACTION_STEP_STATUS::Abandoned );
}


BOOST_AUTO_TEST_CASE( RuntimeExpiresPublishedPreviewWhenContextChanges )
{
    auto* provider = new SCRIPTED_NEXT_ACTION_PROVIDER(
            { wxS( "{\"decision_kind\":\"attempt\","
                   "\"opportunity_type\":\"placement\"}" ),
              wxS( "{\"decision_kind\":\"publish\","
                   "\"reason_code\":\"acceptable\"}" ) } );

    AI_NEXT_ACTION_RUNTIME runtime{ std::unique_ptr<AI_PROVIDER>( provider ) };

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            runtime.Update( makeViaTrigger() );
    BOOST_REQUIRE( suggestion.has_value() );

    AI_CONTEXT_VERSION current = suggestion->m_ContextVersion;
    current.m_DocumentRevision = 99;

    BOOST_CHECK_EQUAL( runtime.ExpireStale( current ), 1 );

    std::optional<AI_SUGGESTION_RECORD> stored = runtime.FindSuggestion( suggestion->m_Id );
    BOOST_REQUIRE( stored.has_value() );
    BOOST_CHECK( stored->m_Status == AI_SUGGESTION_STATUS::Expired );
}


BOOST_AUTO_TEST_SUITE_END()
