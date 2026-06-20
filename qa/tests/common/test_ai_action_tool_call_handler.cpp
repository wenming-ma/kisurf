#include <boost/test/unit_test.hpp>

#include <json_common.h>
#include <kisurf/ai/ai_action_tool_call_handler.h>
#include <kisurf/ai/ai_activity_log.h>
#include <kisurf/ai/ai_tool_execution.h>

#include <optional>
#include <wx/arrstr.h>

namespace
{

class CAPTURING_ACTION_RUNNER : public AI_ACTION_RUNNER
{
public:
    bool RunActionByName( const wxString& aActionName, wxString& aError ) override
    {
        wxUnusedVar( aError );
        m_Calls.push_back( aActionName );
        return true;
    }

    wxArrayString m_Calls;
};


AI_ACTION_DESCRIPTOR actionDescriptor( const wxString& aName, AI_ACTION_SAFETY aSafety,
                                       bool aEnabled = true )
{
    AI_ACTION_DESCRIPTOR descriptor;
    descriptor.m_Name = aName;
    descriptor.m_FriendlyName = aName;
    descriptor.m_EditorKind = AI_EDITOR_KIND::Pcb;
    descriptor.m_Safety = aSafety;
    descriptor.m_Enabled = aEnabled;
    return descriptor;
}


AI_PROVIDER_REQUEST requestWithActions( std::vector<AI_ACTION_DESCRIPTOR> aActions )
{
    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 31;
    request.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_ContextSnapshot.m_Actions = std::move( aActions );
    return request;
}


AI_TOOL_CALL_RECORD toolCall( const wxString& aToolName, const wxString& aArguments )
{
    AI_TOOL_CALL_RECORD call;
    call.m_RequestId = 31;
    call.m_ToolCallId = wxS( "call_action" );
    call.m_ToolName = aToolName;
    call.m_ArgumentsJson = aArguments;
    return call;
}

} // namespace


BOOST_AUTO_TEST_SUITE( AiActionToolCallHandler )


BOOST_AUTO_TEST_CASE( CheckActionForcesDryRunAndDoesNotCallRunner )
{
    AI_ACTIVITY_LOG          log( 16 );
    AI_TOOL_EXECUTION_POLICY policy;
    CAPTURING_ACTION_RUNNER  runner;
    policy.AllowAction( wxS( "common.Control.showAgentPanel" ) );

    AI_ACTION_TOOL_CALL_HANDLER handler( policy, runner, log );

    AI_PROVIDER_REQUEST request = requestWithActions(
            { actionDescriptor( wxS( "common.Control.showAgentPanel" ),
                                AI_ACTION_SAFETY::ReadOnly ) } );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            request, toolCall( wxS( "kisurf_check_action" ),
                               wxS( "{\"action\":\"common.Control.showAgentPanel\"}" ) ) );

    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK( result.m_ResultJson.Contains( wxS( "dry_run" ) ) );
    BOOST_CHECK( runner.m_Calls.empty() );
    BOOST_REQUIRE_EQUAL( log.Records().size(), 3 );
}


BOOST_AUTO_TEST_CASE( RunActionDefaultsToDryRunForAllowlistedReadOnlyAction )
{
    AI_ACTIVITY_LOG          log( 16 );
    AI_TOOL_EXECUTION_POLICY policy;
    CAPTURING_ACTION_RUNNER  runner;
    policy.AllowAction( wxS( "common.Control.showAgentPanel" ) );

    AI_ACTION_TOOL_CALL_HANDLER handler( policy, runner, log );

    AI_PROVIDER_REQUEST request = requestWithActions(
            { actionDescriptor( wxS( "common.Control.showAgentPanel" ),
                                AI_ACTION_SAFETY::ReadOnly ) } );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            request, toolCall( wxS( "kisurf_run_action" ),
                               wxS( "{\"action\":\"common.Control.showAgentPanel\"}" ) ) );

    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK( runner.m_Calls.empty() );

    nlohmann::json resultJson = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK( resultJson["dry_run"].get<bool>() );
    BOOST_CHECK_EQUAL( resultJson["status"].get<std::string>(), "allowed" );
}


BOOST_AUTO_TEST_CASE( RunActionCreatesPendingActionPreviewSuggestion )
{
    AI_ACTIVITY_LOG          log( 16 );
    AI_TOOL_EXECUTION_POLICY policy;
    CAPTURING_ACTION_RUNNER  runner;
    policy.AllowAction( wxS( "common.Control.showAgentPanel" ) );

    std::optional<AI_SUGGESTION_RECORD> storedSuggestion;
    AI_ACTION_TOOL_CALL_HANDLER handler(
            policy, runner, log,
            [&]( AI_SUGGESTION_RECORD aSuggestion )
            {
                aSuggestion.m_Id = 77;
                storedSuggestion = aSuggestion;
                return storedSuggestion;
            } );

    AI_PROVIDER_REQUEST request = requestWithActions(
            { actionDescriptor( wxS( "common.Control.showAgentPanel" ),
                                AI_ACTION_SAFETY::ReadOnly ) } );
    request.m_ContextVersion.m_DocumentRevision = 4;
    request.m_ContextSnapshot.m_Version = request.m_ContextVersion;

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            request, toolCall( wxS( "kisurf_run_action" ),
                               wxS( "{\"action\":\"common.Control.showAgentPanel\"}" ) ) );

    BOOST_REQUIRE( storedSuggestion.has_value() );
    BOOST_CHECK_EQUAL( storedSuggestion->m_Title,
                       wxString( wxS( "Preview action" ) ) );
    BOOST_CHECK( storedSuggestion->m_ArgumentsJson.Contains(
            wxS( "\"operation\":\"action_preview\"" ) ) );
    BOOST_CHECK( storedSuggestion->m_ArgumentsJson.Contains(
            wxS( "\"action\":\"common.Control.showAgentPanel\"" ) ) );
    BOOST_CHECK( storedSuggestion->m_EditObjects.empty() );
    BOOST_CHECK( runner.m_Calls.empty() );

    nlohmann::json resultJson = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( resultJson["status"].get<std::string>(), "preview_ready" );
    BOOST_CHECK_EQUAL( resultJson["suggestion_id"].get<int>(), 77 );
    BOOST_CHECK( resultJson["preview_required"].get<bool>() );
}


BOOST_AUTO_TEST_CASE( RunActionIgnoresModelRequestedExecution )
{
    AI_ACTIVITY_LOG          log( 16 );
    AI_TOOL_EXECUTION_POLICY policy;
    CAPTURING_ACTION_RUNNER  runner;
    policy.AllowAction( wxS( "common.Control.showAgentPanel" ) );

    AI_ACTION_TOOL_CALL_HANDLER handler( policy, runner, log );

    AI_PROVIDER_REQUEST request = requestWithActions(
            { actionDescriptor( wxS( "common.Control.showAgentPanel" ),
                                AI_ACTION_SAFETY::ReadOnly ) } );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            request, toolCall( wxS( "kisurf_run_action" ),
                               wxS( "{\"action\":\"common.Control.showAgentPanel\","
                                    "\"dry_run\":false}" ) ) );

    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK( runner.m_Calls.empty() );

    nlohmann::json resultJson = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK( resultJson["dry_run"].get<bool>() );
}


BOOST_AUTO_TEST_CASE( RequestContextDescriptorWinsOverFallbackDescriptor )
{
    AI_ACTIVITY_LOG          log( 16 );
    AI_TOOL_EXECUTION_POLICY policy;
    CAPTURING_ACTION_RUNNER  runner;
    policy.AllowAction( wxS( "common.Control.showAgentPanel" ) );

    AI_ACTION_TOOL_CALL_HANDLER handler( policy, runner, log );
    handler.SetFallbackActions(
            { actionDescriptor( wxS( "common.Control.showAgentPanel" ),
                                AI_ACTION_SAFETY::ReadOnly, false ) } );

    AI_PROVIDER_REQUEST request = requestWithActions(
            { actionDescriptor( wxS( "common.Control.showAgentPanel" ),
                                AI_ACTION_SAFETY::ReadOnly, true ) } );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            request, toolCall( wxS( "kisurf_run_action" ),
                               wxS( "{\"action\":\"common.Control.showAgentPanel\"}" ) ) );

    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK( runner.m_Calls.empty() );
}


BOOST_AUTO_TEST_CASE( UnknownToolFailsClosed )
{
    AI_ACTIVITY_LOG          log( 16 );
    AI_TOOL_EXECUTION_POLICY policy;
    CAPTURING_ACTION_RUNNER  runner;
    AI_ACTION_TOOL_CALL_HANDLER handler( policy, runner, log );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithActions( {} ),
            toolCall( wxS( "kisurf_delete_everything" ), wxS( "{}" ) ) );

    BOOST_CHECK( !result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString( wxS( "unknown_tool" ) ) );
    nlohmann::json resultJson = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( resultJson["action"].get<std::string>(), "kisurf_delete_everything" );
    BOOST_CHECK( !resultJson["allowed"].get<bool>() );
    BOOST_CHECK( !resultJson["executed"].get<bool>() );
    BOOST_CHECK_EQUAL( resultJson["status"].get<std::string>(), "denied" );
    BOOST_CHECK_EQUAL( resultJson["error_code"].get<std::string>(), "unknown_tool" );
    BOOST_CHECK( runner.m_Calls.empty() );
}


BOOST_AUTO_TEST_CASE( MalformedJsonFailsClosed )
{
    AI_ACTIVITY_LOG          log( 16 );
    AI_TOOL_EXECUTION_POLICY policy;
    CAPTURING_ACTION_RUNNER  runner;
    AI_ACTION_TOOL_CALL_HANDLER handler( policy, runner, log );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithActions( {} ),
            toolCall( wxS( "kisurf_run_action" ), wxS( "{" ) ) );

    BOOST_CHECK( !result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString( wxS( "malformed_arguments" ) ) );
    nlohmann::json resultJson = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK( !resultJson["allowed"].get<bool>() );
    BOOST_CHECK( !resultJson["executed"].get<bool>() );
    BOOST_CHECK_EQUAL( resultJson["status"].get<std::string>(), "denied" );
    BOOST_CHECK_EQUAL( resultJson["error_code"].get<std::string>(), "malformed_arguments" );
    BOOST_CHECK( runner.m_Calls.empty() );
}


BOOST_AUTO_TEST_CASE( UnknownActionFailsClosed )
{
    AI_ACTIVITY_LOG          log( 16 );
    AI_TOOL_EXECUTION_POLICY policy;
    CAPTURING_ACTION_RUNNER  runner;
    AI_ACTION_TOOL_CALL_HANDLER handler( policy, runner, log );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithActions( {} ),
            toolCall( wxS( "kisurf_run_action" ), wxS( "{\"action\":\"missing.action\"}" ) ) );

    BOOST_CHECK( !result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString( wxS( "unknown_action" ) ) );
    nlohmann::json resultJson = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( resultJson["action"].get<std::string>(), "missing.action" );
    BOOST_CHECK( !resultJson["allowed"].get<bool>() );
    BOOST_CHECK( !resultJson["executed"].get<bool>() );
    BOOST_CHECK_EQUAL( resultJson["status"].get<std::string>(), "denied" );
    BOOST_CHECK_EQUAL( resultJson["error_code"].get<std::string>(), "unknown_action" );
    BOOST_CHECK( runner.m_Calls.empty() );
}


BOOST_AUTO_TEST_CASE( ModifyingActionIsDeniedByPolicy )
{
    AI_ACTIVITY_LOG          log( 16 );
    AI_TOOL_EXECUTION_POLICY policy;
    CAPTURING_ACTION_RUNNER  runner;
    policy.AllowAction( wxS( "pcbnew.Place.move" ) );

    AI_ACTION_TOOL_CALL_HANDLER handler( policy, runner, log );

    AI_PROVIDER_REQUEST request = requestWithActions(
            { actionDescriptor( wxS( "pcbnew.Place.move" ),
                                AI_ACTION_SAFETY::Modifying ) } );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            request, toolCall( wxS( "kisurf_run_action" ),
                               wxS( "{\"action\":\"pcbnew.Place.move\"}" ) ) );

    BOOST_CHECK( !result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString( wxS( "requires_preview" ) ) );
    BOOST_CHECK( runner.m_Calls.empty() );
}


BOOST_AUTO_TEST_SUITE_END()
