#include <boost/test/unit_test.hpp>

#include <json_common.h>
#include <kisurf/ai/ai_activity_log.h>
#include <kisurf/ai/ai_tool_execution.h>

#include <vector>
#include <wx/arrstr.h>
#include <wx/string.h>

class FAKE_ACTION_RUNNER : public AI_ACTION_RUNNER
{
public:
    bool RunActionByName( const wxString& aActionName, wxString& aError ) override
    {
        m_Calls.push_back( aActionName );

        if( !m_ShouldSucceed )
        {
            aError = wxS( "runner failed" );
            return false;
        }

        return true;
    }

    bool                  m_ShouldSucceed = true;
    std::vector<wxString> m_Calls;
};


static AI_ACTION_DESCRIPTOR actionDescriptor( const wxString& aName, AI_ACTION_SAFETY aSafety,
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


BOOST_AUTO_TEST_SUITE( AiToolExecution )


BOOST_AUTO_TEST_CASE( PolicyAllowsReadonlyActionWithoutAllowlist )
{
    AI_TOOL_EXECUTION_POLICY policy;

    AI_TOOL_INVOCATION_REQUEST request;
    request.m_Action =
            actionDescriptor( wxS( "common.Control.showAgentPanel" ),
                              AI_ACTION_SAFETY::ReadOnly );

    AI_TOOL_INVOCATION_RESULT result = policy.Evaluate( request );

    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( result.m_ErrorCode.IsEmpty() );
}


BOOST_AUTO_TEST_CASE( PolicyDeniesUnsafeActions )
{
    AI_TOOL_EXECUTION_POLICY policy;
    policy.AllowAction( wxS( "common.Control.showAgentPanel" ) );
    policy.AllowAction( wxS( "pcbnew.Edit.delete" ) );

    AI_TOOL_INVOCATION_REQUEST unknown;
    unknown.m_Action = AI_ACTION_DESCRIPTOR();
    BOOST_CHECK_EQUAL( policy.Evaluate( unknown ).m_ErrorCode,
                       wxString( wxS( "unknown_action" ) ) );

    AI_TOOL_INVOCATION_REQUEST notAllowlisted;
    notAllowlisted.m_Action =
            actionDescriptor( wxS( "pcbnew.Interactive.dragPan" ),
                              AI_ACTION_SAFETY::Interactive );
    BOOST_CHECK_EQUAL( policy.Evaluate( notAllowlisted ).m_ErrorCode,
                       wxString( wxS( "not_allowlisted" ) ) );

    AI_TOOL_INVOCATION_REQUEST disabled;
    disabled.m_Action =
            actionDescriptor( wxS( "common.Control.showAgentPanel" ),
                              AI_ACTION_SAFETY::ReadOnly, false );
    BOOST_CHECK_EQUAL( policy.Evaluate( disabled ).m_ErrorCode,
                       wxString( wxS( "disabled_action" ) ) );

    AI_TOOL_INVOCATION_REQUEST modifying;
    modifying.m_Action = actionDescriptor( wxS( "pcbnew.Place.move" ),
                                           AI_ACTION_SAFETY::Modifying );
    policy.AllowAction( modifying.m_Action.m_Name );
    AI_TOOL_INVOCATION_RESULT modifyingResult = policy.Evaluate( modifying );
    BOOST_CHECK_EQUAL( modifyingResult.m_ErrorCode,
                       wxString( wxS( "modifying_action_not_available" ) ) );
    BOOST_CHECK( modifyingResult.m_Message.Contains( wxS( "current-board atomic" ) ) );

    AI_TOOL_INVOCATION_REQUEST destructive;
    destructive.m_Action = actionDescriptor( wxS( "pcbnew.Edit.delete" ),
                                             AI_ACTION_SAFETY::Destructive );
    BOOST_CHECK_EQUAL( policy.Evaluate( destructive ).m_ErrorCode,
                       wxString( wxS( "destructive_denied" ) ) );
}


BOOST_AUTO_TEST_CASE( ExecutorAuditsAndRunsOnlyAllowedCalls )
{
    AI_ACTIVITY_LOG          log( 16 );
    AI_TOOL_EXECUTION_POLICY policy;
    FAKE_ACTION_RUNNER       runner;

    AI_TOOL_EXECUTOR executor( policy, runner, log );

    AI_TOOL_INVOCATION_REQUEST request;
    request.m_RequestId = 9;
    request.m_ToolCallId = wxS( "call_9" );
    request.m_Action =
            actionDescriptor( wxS( "common.Control.showAgentPanel" ),
                              AI_ACTION_SAFETY::ReadOnly );

    AI_TOOL_INVOCATION_RESULT result = executor.Invoke( request );

    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( result.m_Executed );
    BOOST_REQUIRE_EQUAL( runner.m_Calls.size(), 1 );
    BOOST_CHECK_EQUAL( runner.m_Calls.front(),
                       wxString( wxS( "common.Control.showAgentPanel" ) ) );

    std::vector<AI_ACTIVITY_RECORD> records = log.Records();
    BOOST_REQUIRE_EQUAL( records.size(), 3 );
    BOOST_CHECK( records.at( 0 ).m_Kind == AI_ACTIVITY_KIND::ModelToolRequest );
    BOOST_CHECK( records.at( 1 ).m_Kind == AI_ACTIVITY_KIND::PolicyDecision );
    BOOST_CHECK( records.at( 2 ).m_Kind == AI_ACTIVITY_KIND::ToolResult );
}


BOOST_AUTO_TEST_CASE( ExecutorDoesNotRunDeniedOrDryRunCalls )
{
    AI_ACTIVITY_LOG          log( 16 );
    AI_TOOL_EXECUTION_POLICY policy;
    FAKE_ACTION_RUNNER       runner;
    AI_TOOL_EXECUTOR         executor( policy, runner, log );

    AI_TOOL_INVOCATION_REQUEST denied;
    denied.m_Action =
            actionDescriptor( wxS( "common.Interactive.someTool" ),
                              AI_ACTION_SAFETY::Interactive );

    AI_TOOL_INVOCATION_RESULT deniedResult = executor.Invoke( denied );
    BOOST_CHECK( !deniedResult.m_Allowed );
    BOOST_CHECK( !deniedResult.m_Executed );
    BOOST_CHECK( runner.m_Calls.empty() );

    nlohmann::json deniedJson = nlohmann::json::parse( deniedResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( deniedJson["action"].get<std::string>(), "common.Interactive.someTool" );
    BOOST_CHECK( !deniedJson["allowed"].get<bool>() );
    BOOST_CHECK( !deniedJson["executed"].get<bool>() );
    BOOST_CHECK_EQUAL( deniedJson["status"].get<std::string>(), "denied" );
    BOOST_CHECK_EQUAL( deniedJson["error_code"].get<std::string>(), "not_allowlisted" );

    policy.AllowAction( wxS( "common.Control.showAgentPanel" ) );

    AI_TOOL_INVOCATION_REQUEST dryRun;
    dryRun.m_Action =
            actionDescriptor( wxS( "common.Control.showAgentPanel" ),
                              AI_ACTION_SAFETY::ReadOnly );
    dryRun.m_DryRun = true;

    AI_TOOL_INVOCATION_RESULT dryRunResult = executor.Invoke( dryRun );
    BOOST_CHECK( dryRunResult.m_Allowed );
    BOOST_CHECK( !dryRunResult.m_Executed );
    BOOST_CHECK( runner.m_Calls.empty() );

    nlohmann::json dryRunJson =
            nlohmann::json::parse( dryRunResult.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( dryRunJson["action"].get<std::string>(),
                       "common.Control.showAgentPanel" );
    BOOST_CHECK( dryRunJson["allowed"].get<bool>() );
    BOOST_CHECK( !dryRunJson["executed"].get<bool>() );
    BOOST_CHECK( dryRunJson["dry_run"].get<bool>() );
    BOOST_CHECK_EQUAL( dryRunJson["status"].get<std::string>(), "allowed" );
    BOOST_CHECK_EQUAL( dryRunJson["message"].get<std::string>(), "Dry run allowed." );
}


BOOST_AUTO_TEST_CASE( ExecutorReturnsStructuredExecutedResultJson )
{
    AI_ACTIVITY_LOG          log( 16 );
    AI_TOOL_EXECUTION_POLICY policy;
    FAKE_ACTION_RUNNER       runner;
    AI_TOOL_EXECUTOR         executor( policy, runner, log );

    AI_TOOL_INVOCATION_REQUEST request;
    request.m_Action =
            actionDescriptor( wxS( "common.Control.showAgentPanel" ),
                              AI_ACTION_SAFETY::ReadOnly );

    AI_TOOL_INVOCATION_RESULT result = executor.Invoke( request );

    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( result.m_Executed );

    nlohmann::json resultJson = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( resultJson["action"].get<std::string>(),
                       "common.Control.showAgentPanel" );
    BOOST_CHECK( resultJson["allowed"].get<bool>() );
    BOOST_CHECK( resultJson["executed"].get<bool>() );
    BOOST_CHECK( !resultJson["dry_run"].get<bool>() );
    BOOST_CHECK_EQUAL( resultJson["status"].get<std::string>(), "executed" );
    BOOST_CHECK_EQUAL( resultJson["error_code"].get<std::string>(), "" );
    BOOST_CHECK_EQUAL( resultJson["message"].get<std::string>(), "Action executed." );
}


BOOST_AUTO_TEST_SUITE_END()
