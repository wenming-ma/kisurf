#include <boost/test/unit_test.hpp>

#include <kisurf/ai/ai_agent_suggestion_provider.h>
#include <kisurf/ai/ai_provider.h>

namespace
{
class CAPTURING_SUGGESTION_AI_PROVIDER : public AI_PROVIDER
{
public:
    AI_PROVIDER_RESPONSE Generate( const AI_PROVIDER_REQUEST& aRequest ) override
    {
        ++m_CallCount;
        m_LastRequest = aRequest;

        AI_PROVIDER_RESPONSE response = m_Response;
        response.m_RequestId = aRequest.m_RequestId;
        return response;
    }

    int                  m_CallCount = 0;
    AI_PROVIDER_REQUEST  m_LastRequest;
    AI_PROVIDER_RESPONSE m_Response;
};


AI_SUGGESTION_TRIGGER makeSelectedTrigger()
{
    AI_SUGGESTION_TRIGGER trigger;
    trigger.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_Version.m_DocumentRevision = 7;
    trigger.m_ContextSnapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::Selecting;
    trigger.m_ContextSnapshot.m_SelectedObjects.push_back(
            AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ) ) );
    trigger.m_Activity.m_Sequence = 3;
    trigger.m_Activity.m_ActionName = wxS( "common.Interactive.selected" );
    trigger.m_Activity.m_Message = wxS( "selection changed" );
    return trigger;
}


AI_SUGGESTION_TRIGGER makeRoutingSelectedTrigger()
{
    AI_SUGGESTION_TRIGGER trigger = makeSelectedTrigger();
    trigger.m_ContextSnapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::RoutingTrack;
    trigger.m_ContextSnapshot.m_ToolState.m_ActiveActionName =
            wxS( "pcbnew.InteractiveRoute" );
    trigger.m_Activity.m_ActionName = wxS( "pcbnew.Interactive.routeTrack" );
    return trigger;
}
} // namespace

BOOST_AUTO_TEST_SUITE( AiAgentSuggestionProvider )


BOOST_AUTO_TEST_CASE( SelectedContextCreatesPreviewableSuggestion )
{
    AI_AGENT_SUGGESTION_PROVIDER provider;

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            provider.Suggest( makeSelectedTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK( suggestion->m_EditorKind == AI_EDITOR_KIND::Pcb );
    BOOST_CHECK( suggestion->m_Kind == AI_SUGGESTION_KIND::Preview );
    BOOST_CHECK( suggestion->m_Title.Contains( wxS( "U1.1" ) ) );
    BOOST_CHECK( suggestion->m_Body.Contains( wxS( "Preview" ) ) );
    BOOST_REQUIRE_EQUAL( suggestion->m_PreviewObjects.size(), 1 );
    BOOST_REQUIRE_EQUAL( suggestion->m_EditObjects.size(), 1 );
    BOOST_CHECK_EQUAL( suggestion->m_PreviewObjects.front().m_Label,
                       wxString( wxS( "U1.1" ) ) );
    BOOST_CHECK_EQUAL( suggestion->m_ContextVersion.m_DocumentRevision, 7 );
    BOOST_CHECK_EQUAL( suggestion->m_TriggerActivitySequence, 3 );
    BOOST_CHECK_EQUAL( suggestion->m_ContextKind, wxString( wxS( "general" ) ) );
    BOOST_CHECK( suggestion->m_ContextDetailsJson.Contains(
            wxS( "deterministic_selection" ) ) );
    BOOST_CHECK( suggestion->m_ContextDetailsJson.Contains( wxS( "selecting" ) ) );
}


BOOST_AUTO_TEST_CASE( ModelJsonCreatesGroundedSuggestion )
{
    auto* fakeProvider = new CAPTURING_SUGGESTION_AI_PROVIDER();
    fakeProvider->m_Response.m_Body =
            wxS( "{\"kind\":\"preview\","
                 "\"title\":\"Inspect U1 pad clearance\","
                 "\"body\":\"U1.1 was selected after routing activity.\","
                 "\"fingerprint\":\"model:u1.1\","
                 "\"arguments\":{\"intent\":\"clearance-preview\"},"
                 "\"preview_objects\":[{\"label\":\"U1.1\"}]}" );

    AI_AGENT_SUGGESTION_PROVIDER provider{ std::unique_ptr<AI_PROVIDER>( fakeProvider ) };

    AI_SUGGESTION_TRIGGER trigger = makeRoutingSelectedTrigger();
    trigger.m_Reason = wxS( "selection changed" );

    std::optional<AI_SUGGESTION_RECORD> suggestion = provider.Suggest( trigger );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK_EQUAL( fakeProvider->m_CallCount, 1 );
    BOOST_CHECK_EQUAL( suggestion->m_Title,
                       wxString( wxS( "Inspect U1 pad clearance" ) ) );
    BOOST_CHECK_EQUAL( suggestion->m_Body,
                       wxString( wxS( "U1.1 was selected after routing activity." ) ) );
    BOOST_CHECK_EQUAL( suggestion->m_Fingerprint, wxString( wxS( "model:u1.1" ) ) );
    BOOST_CHECK( suggestion->m_ArgumentsJson.Contains( wxS( "clearance-preview" ) ) );
    BOOST_REQUIRE_EQUAL( suggestion->m_PreviewObjects.size(), 1 );
    BOOST_REQUIRE_EQUAL( suggestion->m_EditObjects.size(), 1 );
    BOOST_CHECK_EQUAL( suggestion->m_PreviewObjects.front().m_Label,
                       wxString( wxS( "U1.1" ) ) );
    BOOST_CHECK_EQUAL( suggestion->m_EditObjects.front().m_Label,
                       wxString( wxS( "U1.1" ) ) );
    BOOST_CHECK_EQUAL( suggestion->m_ContextVersion.m_DocumentRevision, 7 );
    BOOST_CHECK_EQUAL( suggestion->m_TriggerActivitySequence, 3 );
    BOOST_CHECK_EQUAL( suggestion->m_ContextKind, wxString( wxS( "routing" ) ) );
    BOOST_CHECK( suggestion->m_ContextDetailsJson.Contains( wxS( "model_suggestion" ) ) );
    BOOST_CHECK( suggestion->m_ContextDetailsJson.Contains( wxS( "routing_track" ) ) );

    BOOST_CHECK( fakeProvider->m_LastRequest.m_EditorKind == AI_EDITOR_KIND::Pcb );
    BOOST_CHECK_EQUAL( fakeProvider->m_LastRequest.m_ContextVersion.m_DocumentRevision, 7 );
    BOOST_CHECK( fakeProvider->m_LastRequest.m_UserText.Contains( wxS( "JSON object" ) ) );
    BOOST_CHECK( fakeProvider->m_LastRequest.m_UserText.Contains( wxS( "preview_objects" ) ) );
    BOOST_CHECK( fakeProvider->m_LastRequest.m_UserText.Contains(
            wxS( "selection changed" ) ) );
    BOOST_CHECK( fakeProvider->m_LastRequest.m_UserText.Contains(
            wxS( "pcbnew.Interactive.routeTrack" ) ) );
    BOOST_CHECK( fakeProvider->m_LastRequest.m_UserText.Contains( wxS( "U1.1" ) ) );
}


BOOST_AUTO_TEST_CASE( ModelSuggestionRejectsUnknownObjectLabels )
{
    auto* fakeProvider = new CAPTURING_SUGGESTION_AI_PROVIDER();
    fakeProvider->m_Response.m_Body =
            wxS( "{\"title\":\"Inspect unknown item\","
                 "\"body\":\"This object is not in context.\","
                 "\"preview_objects\":[{\"label\":\"U99.1\"}]}" );

    AI_AGENT_SUGGESTION_PROVIDER provider{ std::unique_ptr<AI_PROVIDER>( fakeProvider ) };

    BOOST_CHECK( !provider.Suggest( makeSelectedTrigger() ).has_value() );
    BOOST_CHECK_EQUAL( fakeProvider->m_CallCount, 1 );
}


BOOST_AUTO_TEST_CASE( ModelCanExplicitlyReturnNoSuggestion )
{
    auto* fakeProvider = new CAPTURING_SUGGESTION_AI_PROVIDER();
    fakeProvider->m_Response.m_Body = wxS( "{\"no_suggestion\":true}" );

    AI_AGENT_SUGGESTION_PROVIDER provider{ std::unique_ptr<AI_PROVIDER>( fakeProvider ) };

    BOOST_CHECK( !provider.Suggest( makeSelectedTrigger() ).has_value() );
    BOOST_CHECK_EQUAL( fakeProvider->m_CallCount, 1 );
}


BOOST_AUTO_TEST_CASE( ModelProviderFallsBackForUnstructuredText )
{
    auto* fakeProvider = new CAPTURING_SUGGESTION_AI_PROVIDER();
    fakeProvider->m_Response.m_Body = wxS( "I would inspect the selected pad." );

    AI_AGENT_SUGGESTION_PROVIDER provider{ std::unique_ptr<AI_PROVIDER>( fakeProvider ) };

    std::optional<AI_SUGGESTION_RECORD> suggestion = provider.Suggest( makeSelectedTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK_EQUAL( fakeProvider->m_CallCount, 1 );
    BOOST_CHECK( suggestion->m_Title.Contains( wxS( "U1.1" ) ) );
    BOOST_REQUIRE_EQUAL( suggestion->m_PreviewObjects.size(), 1 );
}


BOOST_AUTO_TEST_CASE( MissingSelectionCreatesNoSuggestion )
{
    AI_AGENT_SUGGESTION_PROVIDER provider;
    AI_SUGGESTION_TRIGGER        trigger = makeSelectedTrigger();
    trigger.m_ContextSnapshot.m_SelectedObjects.clear();

    BOOST_CHECK( !provider.Suggest( trigger ).has_value() );
}


BOOST_AUTO_TEST_CASE( MissingActivityCreatesNoSuggestion )
{
    AI_AGENT_SUGGESTION_PROVIDER provider;
    AI_SUGGESTION_TRIGGER        trigger = makeSelectedTrigger();
    trigger.m_Activity = AI_ACTIVITY_RECORD();

    BOOST_CHECK( !provider.Suggest( trigger ).has_value() );
}


BOOST_AUTO_TEST_SUITE_END()
