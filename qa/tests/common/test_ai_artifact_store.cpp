#include <boost/test/unit_test.hpp>
#include <json_common.h>
#include <kisurf/ai/ai_accept_applier.h>
#include <kisurf/ai/ai_artifact_store.h>
#include <kisurf/ai/ai_local_text_memory.h>
#include <kisurf/ai/ai_provider_input_compiler.h>
#include <kisurf/ai/ai_visual_snapshot.h>

#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/utils.h>


BOOST_AUTO_TEST_SUITE( AiArtifactStore )

namespace
{
wxString uniqueArtifactManifestPath( const wxString& aSuffix )
{
    wxString base = wxFileName::CreateTempFileName( wxS( "ksa" ) );

    if( wxFileExists( base ) )
        wxRemoveFile( base );

    wxFileName path( base );
    path.SetFullName( wxString::Format( wxS( "kisurf_artifacts_%s_%lu.jsonl" ),
                                        aSuffix,
                                        static_cast<unsigned long>( wxGetProcessId() ) ) );

    if( wxFileExists( path.GetFullPath() ) )
        wxRemoveFile( path.GetFullPath() );

    return path.GetFullPath();
}

class RECORDING_PROVIDER_RECOVERY_ACCEPT_ADAPTER : public AI_ACCEPT_APPLY_ADAPTER
{
public:
    bool BeginTransaction( const AI_EXECUTION_SESSION& aSession,
                           wxString& aError ) override
    {
        ++m_BeginCount;
        m_BoardId = aSession.BoardId();
        m_SessionOperationCount = aSession.Journal().Operations().size();
        aError.clear();
        return true;
    }

    bool ApplyOperation( const AI_SESSION_OPERATION_RECORD& aOperation,
                         wxString& aError ) override
    {
        m_OperationKinds.push_back( aOperation.m_Kind );
        m_OperationArguments.push_back( aOperation.m_ArgumentsJson );
        aError.clear();
        return true;
    }

    bool CommitTransaction( wxString& aError ) override
    {
        ++m_CommitCount;
        aError.clear();
        return true;
    }

    bool HasBoardChanges() const override { return m_HasBoardChanges; }

    void AbortTransaction() override { ++m_AbortCount; }

    int                                    m_BeginCount = 0;
    int                                    m_CommitCount = 0;
    int                                    m_AbortCount = 0;
    bool                                   m_HasBoardChanges = true;
    wxString                               m_BoardId;
    size_t                                 m_SessionOperationCount = 0;
    std::vector<AI_SESSION_OPERATION_KIND> m_OperationKinds;
    std::vector<wxString>                  m_OperationArguments;
};
}


BOOST_AUTO_TEST_CASE( StoreLoadAndReadPayloadByStableUri )
{
    wxString manifestPath = uniqueArtifactManifestPath( wxS( "append" ) );

    AI_ARTIFACT_STORE store( manifestPath );

    AI_ARTIFACT_RECORD artifact;
    artifact.m_Kind = wxS( "tool_result" );
    artifact.m_ProjectId = wxS( "project-a" );
    artifact.m_DocumentId = wxS( "board-1" );
    artifact.m_AgentKind = wxS( "chat" );
    artifact.m_Source = wxS( "script_run_bounded_plan" );
    artifact.m_MimeType = wxS( "application/json" );
    artifact.m_Summary = wxS( "Long script output was archived." );
    artifact.m_RetentionClass = wxS( "trace" );
    artifact.m_MetadataJson = wxS( "{\"tool_call_id\":\"call-1\"}" );

    wxString error;
    wxString payload = wxS( "{\"stdout\":\"long output body\"}" );

    BOOST_REQUIRE( store.StorePayload( artifact, payload, error ) );
    BOOST_CHECK( artifact.m_Uri.StartsWith( wxS( "kisurf-artifact://tool-result/" ) ) );
    BOOST_CHECK_EQUAL( artifact.m_ByteSize, payload.length() );
    BOOST_CHECK( wxFileExists( artifact.m_BlobPath ) );

    std::vector<AI_ARTIFACT_RECORD> records = store.LoadAll( error );

    BOOST_REQUIRE_EQUAL( records.size(), 1 );
    BOOST_CHECK_EQUAL( records.front().m_Uri, artifact.m_Uri );
    BOOST_CHECK_EQUAL( records.front().m_ProjectId, wxString( wxS( "project-a" ) ) );
    BOOST_CHECK_EQUAL( records.front().m_RetentionClass, wxString( wxS( "trace" ) ) );
    BOOST_CHECK_EQUAL( records.front().m_MetadataJson,
                       wxString( wxS( "{\"tool_call_id\":\"call-1\"}" ) ) );

    wxString loadedPayload;
    BOOST_REQUIRE( store.ReadPayload( artifact.m_Uri, loadedPayload, error ) );
    BOOST_CHECK_EQUAL( loadedPayload, payload );

    wxRemoveFile( manifestPath );
    wxRemoveFile( artifact.m_BlobPath );
}


BOOST_AUTO_TEST_CASE( ExportArtifactSummariesToLocalTextIndex )
{
    wxString manifestPath = uniqueArtifactManifestPath( wxS( "local_text_export" ) );

    AI_ARTIFACT_STORE store( manifestPath );
    wxString          error;

    AI_ARTIFACT_RECORD validationArtifact;
    validationArtifact.m_Kind = wxS( "validation_report" );
    validationArtifact.m_ProjectId = wxS( "project-a" );
    validationArtifact.m_DocumentId = wxS( "board-1" );
    validationArtifact.m_AgentKind = wxS( "next_action" );
    validationArtifact.m_Source = wxS( "validate_hidden_attempt" );
    validationArtifact.m_MimeType = wxS( "application/json" );
    validationArtifact.m_Summary =
            wxS( "Archived validation report for validate_hidden_attempt" );
    validationArtifact.m_RetentionClass = wxS( "trace" );
    validationArtifact.m_MetadataJson =
            wxS( "{\"backend\":\"native_drc\",\"issue_count\":2,"
                 "\"net\":\"USB_DP\"}" );
    BOOST_REQUIRE( store.StorePayload(
            validationArtifact,
            wxS( "{\"issues\":[{\"kind\":\"clearance\",\"net\":\"USB_DP\"}]}" ),
            error ) );

    AI_ARTIFACT_RECORD otherArtifact = validationArtifact;
    otherArtifact.m_ProjectId = wxS( "project-b" );
    otherArtifact.m_DocumentId = wxS( "board-2" );
    otherArtifact.m_Summary = wxS( "Archived validation report for another board" );
    BOOST_REQUIRE( store.StorePayload( otherArtifact,
                                       wxS( "{\"issues\":[]}" ), error ) );

    AI_LOCAL_TEXT_MEMORY_INDEX index;
    AI_ARTIFACT_QUERY          exportQuery;
    exportQuery.m_ProjectId = wxS( "project-a" );
    exportQuery.m_DocumentId = wxS( "board-1" );

    BOOST_REQUIRE( AiExportArtifactSummariesToLocalTextIndex(
            store, exportQuery, index, error ) );

    AI_LOCAL_TEXT_MEMORY_QUERY search;
    search.m_Text = wxS( "native_drc USB_DP clearance" );
    search.m_ProjectId = wxS( "project-a" );
    search.m_DocumentId = wxS( "board-1" );
    search.m_AgentKind = wxS( "next_action" );
    search.m_Types.push_back( wxS( "validation_report" ) );

    std::vector<AI_LOCAL_TEXT_MEMORY_RESULT> results = index.Search( search, 4 );

    BOOST_REQUIRE_EQUAL( results.size(), 1 );
    BOOST_CHECK_EQUAL( results.front().m_Record.m_Id,
                       validationArtifact.m_Uri );
    BOOST_CHECK_EQUAL( results.front().m_Record.m_Type,
                       wxString( wxS( "validation_report" ) ) );
    BOOST_CHECK_EQUAL( results.front().m_Record.m_Source,
                       wxString( wxS( "validate_hidden_attempt" ) ) );
    BOOST_CHECK( results.front().m_Record.m_Text.Contains(
            wxS( "Archived validation report" ) ) );
    BOOST_CHECK( results.front().m_Record.m_Text.Contains(
            wxS( "native_drc" ) ) );
    BOOST_CHECK( results.front().m_Record.m_ProvenanceJson.Contains(
            validationArtifact.m_Uri ) );
    BOOST_CHECK( results.front().m_Record.m_ProvenanceJson.Contains(
            wxS( "artifact_summary" ) ) );

    wxRemoveFile( manifestPath );
    wxRemoveFile( validationArtifact.m_BlobPath );
    wxRemoveFile( otherArtifact.m_BlobPath );
}


BOOST_AUTO_TEST_CASE( RetentionKeepsNewestRecordsAndDeletesPrunedBlobs )
{
    wxString manifestPath = uniqueArtifactManifestPath( wxS( "retention" ) );

    AI_ARTIFACT_RETENTION_POLICY retention;
    retention.m_MaxRecords = 2;

    AI_ARTIFACT_STORE store( manifestPath, retention );

    wxString firstBlobPath;
    wxString error;

    for( int i = 1; i <= 3; ++i )
    {
        AI_ARTIFACT_RECORD artifact;
        artifact.m_Kind = wxS( "tool_result" );
        artifact.m_Source = wxS( "tool" );
        artifact.m_RetentionClass = wxS( "trace" );
        artifact.m_Summary = wxString::Format( wxS( "artifact %d" ), i );
        artifact.m_CreatedAtUnixSeconds = i;

        BOOST_REQUIRE( store.StorePayload(
                artifact,
                wxString::Format( wxS( "{\"value\":%d}" ), i ),
                error ) );

        if( i == 1 )
            firstBlobPath = artifact.m_BlobPath;
    }

    std::vector<AI_ARTIFACT_RECORD> records = store.LoadAll( error );

    BOOST_REQUIRE_EQUAL( records.size(), 2 );
    BOOST_CHECK_EQUAL( records.at( 0 ).m_Summary, wxString( wxS( "artifact 2" ) ) );
    BOOST_CHECK_EQUAL( records.at( 1 ).m_Summary, wxString( wxS( "artifact 3" ) ) );
    BOOST_CHECK( !wxFileExists( firstBlobPath ) );

    for( const AI_ARTIFACT_RECORD& record : records )
        wxRemoveFile( record.m_BlobPath );

    wxRemoveFile( manifestPath );
}


BOOST_AUTO_TEST_CASE( DefaultPathUsesAiArtifactsManifest )
{
    wxFileName path( AI_ARTIFACT_STORE::DefaultManifestPath() );

    BOOST_CHECK_EQUAL( path.GetFullName(), wxString( wxS( "artifacts.jsonl" ) ) );
    BOOST_CHECK( path.GetPath().Contains( wxS( "ai" ) ) );
}


BOOST_AUTO_TEST_CASE( ToolResultArtifactUsesProviderCompilerReferenceUri )
{
    wxString manifestPath = uniqueArtifactManifestPath( wxS( "tool_result" ) );

    AI_TOOL_CALL_RECORD result;
    result.m_RequestId = 901;
    result.m_ToolCallId = wxS( "call-tool-artifact" );
    result.m_ToolName = wxS( "generic_verbose_tool" );
    result.m_ArgumentsJson = wxS( "{\"mode\":\"long\"}" );

    wxString rawPayload;

    for( int i = 0; i < 5000; ++i )
        rawPayload << wxS( "z" );

    result.m_ResultJson = wxS( "{\"stdout\":\"" ) + rawPayload + wxS( "\"}" );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 901;
    request.m_UserText = wxS( "continue after script" );
    request.m_MaxToolResultChars = 200;
    request.m_ToolResults.push_back( result );

    AI_PROVIDER_REQUEST compiled = AiCompileProviderInput( request );
    nlohmann::json compressed = nlohmann::json::parse(
            compiled.m_ToolResults.front().m_ResultJson.ToStdString() );
    const std::string referenceUri =
            compressed["artifact_ref"]["uri"].get<std::string>();

    AI_ARTIFACT_STORE  store( manifestPath );
    AI_ARTIFACT_RECORD artifact;
    wxString           error;

    BOOST_REQUIRE( AiStoreToolResultArtifact(
            wxS( "project-a" ), wxS( "board-1" ), wxS( "chat" ), wxS( "trace" ),
            result.m_ToolCallId, result.m_ToolName, result.m_ResultJson,
            store, artifact, error ) );

    BOOST_CHECK_EQUAL( artifact.m_Uri, wxString::FromUTF8( referenceUri.c_str() ) );
    BOOST_CHECK_EQUAL( artifact.m_ProjectId, wxString( wxS( "project-a" ) ) );
    BOOST_CHECK_EQUAL( artifact.m_DocumentId, wxString( wxS( "board-1" ) ) );
    BOOST_CHECK( artifact.m_MetadataJson.Contains( wxS( "call-tool-artifact" ) ) );

    wxString loadedPayload;
    BOOST_REQUIRE( store.ReadPayload( artifact.m_Uri, loadedPayload, error ) );
    BOOST_CHECK_EQUAL( loadedPayload, result.m_ResultJson );

    wxRemoveFile( manifestPath );
    wxRemoveFile( artifact.m_BlobPath );
}


BOOST_AUTO_TEST_CASE( ScriptOutputArtifactUsesProviderCompilerReferenceUri )
{
    wxString manifestPath = uniqueArtifactManifestPath( wxS( "script_output" ) );

    AI_TOOL_CALL_RECORD result;
    result.m_RequestId = 902;
    result.m_ToolCallId = wxS( "call-script-artifact" );
    result.m_ToolName = wxS( "kisurf_run_cell" );
    result.m_ArgumentsJson =
            wxS( "{\"cell_id\":\"long-script\",\"cell_text\":\"print('x')\"}" );

    wxString rawPayload;

    for( int i = 0; i < 5000; ++i )
        rawPayload << wxS( "s" );

    result.m_ResultJson = wxS( "{\"stdout\":\"" ) + rawPayload
                          + wxS( "\",\"stderr\":\"warning\"}" );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 902;
    request.m_UserText = wxS( "continue after script" );
    request.m_MaxToolResultChars = 200;
    request.m_ToolResults.push_back( result );

    AI_PROVIDER_REQUEST compiled = AiCompileProviderInput( request );
    nlohmann::json compressed = nlohmann::json::parse(
            compiled.m_ToolResults.front().m_ResultJson.ToStdString() );
    const std::string referenceUri =
            compressed["artifact_ref"]["uri"].get<std::string>();

    BOOST_CHECK_EQUAL( compressed["artifact_ref"]["kind"].get<std::string>(),
                       "script_output" );
    BOOST_CHECK( referenceUri.find( "kisurf-artifact://script-output/" ) == 0 );

    AI_ARTIFACT_STORE  store( manifestPath );
    AI_ARTIFACT_RECORD artifact;
    wxString           error;

    BOOST_REQUIRE( AiStoreScriptOutputArtifact(
            wxS( "project-a" ), wxS( "board-1" ), wxS( "chat" ), wxS( "trace" ),
            result.m_ToolCallId, result.m_ToolName, result.m_ArgumentsJson,
            result.m_ResultJson, store, artifact, error ) );

    BOOST_CHECK_EQUAL( artifact.m_Uri, wxString::FromUTF8( referenceUri.c_str() ) );
    BOOST_CHECK_EQUAL( artifact.m_Kind, wxString( wxS( "script_output" ) ) );
    BOOST_CHECK_EQUAL( artifact.m_Source, wxString( wxS( "kisurf_run_cell" ) ) );
    BOOST_CHECK( artifact.m_MetadataJson.Contains( wxS( "call-script-artifact" ) ) );
    BOOST_CHECK( artifact.m_MetadataJson.Contains( wxS( "stdout_chars" ) ) );
    BOOST_CHECK( artifact.m_MetadataJson.Contains( wxS( "stderr_chars" ) ) );

    wxString loadedPayload;
    BOOST_REQUIRE( store.ReadPayload( artifact.m_Uri, loadedPayload, error ) );
    BOOST_CHECK_EQUAL( loadedPayload, result.m_ResultJson );

    wxRemoveFile( manifestPath );
    wxRemoveFile( artifact.m_BlobPath );
}


BOOST_AUTO_TEST_CASE( FailedHiddenAttemptArtifactStoresAuditPayload )
{
    wxString manifestPath = uniqueArtifactManifestPath( wxS( "failed_attempt" ) );

    nlohmann::json payload = {
        { "schema",
          { { "name", "kisurf.ai.failed_hidden_attempt" },
            { "version", 1 } } },
        { "runtime_step_id", 12 },
        { "attempt_id", 34 },
        { "terminal_status", "abandoned" },
        { "attempt_journal", { { "operation_count", 1 } } },
        { "review_decision", { { "reason_code", "render_freshness_failed" } } }
    };

    AI_ARTIFACT_STORE  store( manifestPath );
    AI_ARTIFACT_RECORD artifact;
    wxString           error;

    BOOST_REQUIRE( AiStoreFailedHiddenAttemptArtifact(
            wxS( "project-a" ), wxS( "board-1" ), wxS( "next_action" ),
            wxS( "trace" ), 12, 34, wxS( "abandoned" ),
            wxS( "render_freshness_failed" ),
            wxString::FromUTF8( payload.dump().c_str() ),
            store, artifact, error ) );

    BOOST_CHECK_EQUAL( artifact.m_Kind,
                       wxString( wxS( "failed_hidden_attempt" ) ) );
    BOOST_CHECK( artifact.m_Uri.StartsWith(
            wxS( "kisurf-artifact://failed-hidden-attempt/" ) ) );
    BOOST_CHECK_EQUAL( artifact.m_ProjectId, wxString( wxS( "project-a" ) ) );
    BOOST_CHECK_EQUAL( artifact.m_DocumentId, wxString( wxS( "board-1" ) ) );
    BOOST_CHECK_EQUAL( artifact.m_AgentKind, wxString( wxS( "next_action" ) ) );
    BOOST_CHECK( artifact.m_MetadataJson.Contains( wxS( "render_freshness_failed" ) ) );

    wxString loadedPayload;
    BOOST_REQUIRE( store.ReadPayload( artifact.m_Uri, loadedPayload, error ) );
    BOOST_CHECK( loadedPayload.Contains( wxS( "failed_hidden_attempt" ) ) );
    BOOST_CHECK( loadedPayload.Contains( wxS( "attempt_journal" ) ) );

    wxRemoveFile( manifestPath );
    wxRemoveFile( artifact.m_BlobPath );
}


BOOST_AUTO_TEST_CASE( LatestProviderRecoveryPolicyRequiresCheckpointOrJournalResume )
{
    wxString manifestPath = uniqueArtifactManifestPath( wxS( "provider_recovery_policy" ) );

    AI_ARTIFACT_STORE store( manifestPath );
    wxString          error;

    nlohmann::json firstTrace = {
        { "runtime_guard",
          { { "reason", "post_side_effect_ambiguity" },
            { "action", "checkpoint_or_journal_recovery_required" },
            { "replay_policy", "do_not_blindly_reexecute_tools" },
            { "recovery_basis",
              { { "board_state_version", { { "document_revision", 1 } } },
                { "tool_results",
                  nlohmann::json::array( {
                          { { "tool_call_id", "call-old" },
                            { "checkpoint_id", 7 },
                            { "session_journal",
                              { { "operations",
                                  nlohmann::json::array( { { { "kind", "old" } } } ) } } } }
                  } ) } } } } } };

    AI_ARTIFACT_RECORD firstArtifact;
    BOOST_REQUIRE( AiStoreProviderRecoveryArtifact(
            wxS( "project-a" ), wxS( "board-1" ), wxS( "chat" ),
            wxS( "trace" ), wxS( "AI_RUNTIME" ), 1001,
            wxString::FromUTF8( firstTrace.dump().c_str() ),
            store, firstArtifact, error ) );

    nlohmann::json secondTrace = {
        { "runtime_guard",
          { { "reason", "post_side_effect_ambiguity" },
            { "action", "checkpoint_or_journal_recovery_required" },
            { "replay_policy", "do_not_blindly_reexecute_tools" },
            { "recovery_basis",
              { { "board_state_version", { { "document_revision", 2 } } },
                { "tool_results",
                  nlohmann::json::array( {
                          { { "tool_call_id", "call-new" },
                            { "checkpoint_id", 42 },
                            { "session_journal",
                              { { "operations",
                                  nlohmann::json::array( { { { "kind", "new" } } } ) } } } }
                  } ) } } } } } };

    AI_ARTIFACT_RECORD secondArtifact;
    BOOST_REQUIRE( AiStoreProviderRecoveryArtifact(
            wxS( "project-a" ), wxS( "board-1" ), wxS( "chat" ),
            wxS( "trace" ), wxS( "AI_RUNTIME" ), 1002,
            wxString::FromUTF8( secondTrace.dump().c_str() ),
            store, secondArtifact, error ) );

    AI_PROVIDER_RECOVERY_QUERY query;
    query.m_ProjectId = wxS( "project-a" );
    query.m_DocumentId = wxS( "board-1" );
    query.m_AgentKind = wxS( "chat" );

    AI_PROVIDER_RECOVERY_POLICY policy =
            AiEvaluateLatestProviderRecovery( store, query, error );

    BOOST_CHECK( policy.m_Available );
    BOOST_CHECK( !policy.m_BlindToolReplayAllowed );
    BOOST_CHECK( policy.m_CheckpointOrJournalResumeRequired );
    BOOST_CHECK_EQUAL( policy.m_ArtifactUri, secondArtifact.m_Uri );
    BOOST_CHECK_EQUAL( policy.m_RequestId, 1002 );
    BOOST_CHECK_EQUAL( policy.m_Reason,
                       wxString( wxS( "post_side_effect_ambiguity" ) ) );
    BOOST_CHECK_EQUAL( policy.m_Action,
                       wxString( wxS( "checkpoint_or_journal_recovery_required" ) ) );
    BOOST_CHECK( policy.m_RecoveryBasisJson.Contains( wxS( "call-new" ) ) );
    BOOST_CHECK( !policy.m_RecoveryBasisJson.Contains( wxS( "call-old" ) ) );

    AI_PROVIDER_RECOVERY_QUERY otherQuery = query;
    otherQuery.m_DocumentId = wxS( "other-board" );
    AI_PROVIDER_RECOVERY_POLICY missing =
            AiEvaluateLatestProviderRecovery( store, otherQuery, error );

    BOOST_CHECK( !missing.m_Available );
    BOOST_CHECK( !missing.m_BlindToolReplayAllowed );
    BOOST_CHECK( missing.m_CheckpointOrJournalResumeRequired );

    wxRemoveFile( manifestPath );
    wxRemoveFile( firstArtifact.m_BlobPath );
    wxRemoveFile( secondArtifact.m_BlobPath );
}


BOOST_AUTO_TEST_CASE( ProviderRecoveryResumePreflightRejectsStaleDocumentRevision )
{
    wxString manifestPath = uniqueArtifactManifestPath( wxS( "provider_recovery_preflight" ) );

    AI_ARTIFACT_STORE store( manifestPath );
    wxString          error;

    nlohmann::json trace = {
        { "runtime_guard",
          { { "reason", "post_side_effect_ambiguity" },
            { "action", "checkpoint_or_journal_recovery_required" },
            { "replay_policy", "do_not_blindly_reexecute_tools" },
            { "recovery_basis",
              { { "board_state_version", { { "document_revision", 42 } } },
                { "tool_results",
                  nlohmann::json::array( {
                          { { "tool_call_id", "call-replay" },
                            { "checkpoint_id", 18 },
                            { "session_journal",
                              { { "operations",
                                  nlohmann::json::array( {
                                          { { "kind", "create_via" } }
                                  } ) } } } }
                  } ) } } } } } };

    AI_ARTIFACT_RECORD artifact;
    BOOST_REQUIRE( AiStoreProviderRecoveryArtifact(
            wxS( "project-a" ), wxS( "board-1" ), wxS( "chat" ),
            wxS( "trace" ), wxS( "AI_RUNTIME" ), 1003,
            wxString::FromUTF8( trace.dump().c_str() ),
            store, artifact, error ) );

    AI_PROVIDER_RECOVERY_QUERY query;
    query.m_ProjectId = wxS( "project-a" );
    query.m_DocumentId = wxS( "board-1" );
    query.m_AgentKind = wxS( "chat" );

    AI_PROVIDER_RECOVERY_RESUME_PLAN plan =
            AiBuildLatestProviderRecoveryResumePlan( store, query, error );

    BOOST_REQUIRE( plan.m_Available );

    AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT staleContext;
    staleContext.m_DocumentRevision = 41;

    AI_PROVIDER_RECOVERY_PREFLIGHT_RESULT stale =
            AiPreflightProviderRecoveryResumePlan( plan, staleContext );

    BOOST_CHECK( !stale.m_Allowed );
    BOOST_CHECK_EQUAL( stale.m_Reason,
                       wxString( wxS( "document_revision_mismatch" ) ) );
    BOOST_CHECK_EQUAL( stale.m_ExpectedDocumentRevision, 42 );
    BOOST_CHECK_EQUAL( stale.m_CurrentDocumentRevision, 41 );
    BOOST_CHECK( stale.m_ResultJson.Contains(
            wxS( "\"reason\":\"document_revision_mismatch\"" ) ) );
    BOOST_CHECK( stale.m_ResultJson.Contains(
            wxS( "\"expected_document_revision\":42" ) ) );
    BOOST_CHECK( stale.m_ResultJson.Contains(
            wxS( "\"current_document_revision\":41" ) ) );

    AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT matchingContext;
    matchingContext.m_DocumentRevision = 42;

    AI_PROVIDER_RECOVERY_PREFLIGHT_RESULT ready =
            AiPreflightProviderRecoveryResumePlan( plan, matchingContext );

    BOOST_CHECK( ready.m_Allowed );
    BOOST_CHECK( !ready.m_BlindToolReplayAllowed );
    BOOST_CHECK( ready.m_CheckpointOrJournalReplayRequired );
    BOOST_CHECK_EQUAL( ready.m_Reason,
                       wxString( wxS( "ready_for_replay_review" ) ) );
    BOOST_CHECK_EQUAL( ready.m_ReplayCandidateCount, 1 );
    BOOST_CHECK( ready.m_ResultJson.Contains(
            wxS( "\"reason\":\"ready_for_replay_review\"" ) ) );
    BOOST_CHECK( ready.m_ResultJson.Contains(
            wxS( "\"replay_candidate_count\":1" ) ) );
    BOOST_CHECK( ready.m_ResultJson.Contains(
            wxS( "review_effectful_tool_results_before_replay" ) ) );

    wxRemoveFile( manifestPath );
    wxRemoveFile( artifact.m_BlobPath );
}


BOOST_AUTO_TEST_CASE( ProviderRecoveryReplayRequestSelectsCheckpointJournalCandidate )
{
    wxString manifestPath =
            uniqueArtifactManifestPath( wxS( "provider_recovery_replay_request" ) );

    AI_ARTIFACT_STORE store( manifestPath );
    wxString          error;

    nlohmann::json trace = {
        { "runtime_guard",
          { { "reason", "post_side_effect_ambiguity" },
            { "action", "checkpoint_or_journal_recovery_required" },
            { "replay_policy", "do_not_blindly_reexecute_tools" },
            { "recovery_basis",
              { { "board_state_version", { { "document_revision", 42 } } },
                { "tool_results",
                  nlohmann::json::array( {
                          { { "tool_call_id", "call-replay" },
                            { "tool_name", "kisurf_run_cell" },
                            { "session_id", "chat-session-1" },
                            { "checkpoint_id", 18 },
                            { "session_journal",
                              { { "operations",
                                  nlohmann::json::array( {
                                          { { "kind", "create_via" },
                                            { "net", "GND" } }
                                  } ) } } } }
                  } ) } } } } } };

    AI_ARTIFACT_RECORD artifact;
    BOOST_REQUIRE( AiStoreProviderRecoveryArtifact(
            wxS( "project-a" ), wxS( "board-1" ), wxS( "chat" ),
            wxS( "trace" ), wxS( "AI_RUNTIME" ), 1004,
            wxString::FromUTF8( trace.dump().c_str() ),
            store, artifact, error ) );

    AI_PROVIDER_RECOVERY_QUERY query;
    query.m_ProjectId = wxS( "project-a" );
    query.m_DocumentId = wxS( "board-1" );
    query.m_AgentKind = wxS( "chat" );

    AI_PROVIDER_RECOVERY_RESUME_PLAN plan =
            AiBuildLatestProviderRecoveryResumePlan( store, query, error );

    BOOST_REQUIRE( plan.m_Available );

    AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT staleContext;
    staleContext.m_DocumentRevision = 41;

    AI_PROVIDER_RECOVERY_REPLAY_REQUEST stale =
            AiBuildProviderRecoveryReplayRequest( plan, staleContext );

    BOOST_CHECK( !stale.m_Available );
    BOOST_CHECK( !stale.m_Allowed );
    BOOST_CHECK_EQUAL( stale.m_Reason,
                       wxString( wxS( "document_revision_mismatch" ) ) );
    BOOST_CHECK( stale.m_RequestJson.Contains(
            wxS( "\"status\":\"blocked_by_preflight\"" ) ) );

    AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT matchingContext;
    matchingContext.m_DocumentRevision = 42;

    AI_PROVIDER_RECOVERY_REPLAY_REQUEST replay =
            AiBuildProviderRecoveryReplayRequest( plan, matchingContext );

    BOOST_CHECK( replay.m_Available );
    BOOST_CHECK( replay.m_Allowed );
    BOOST_CHECK( !replay.m_BlindToolReplayAllowed );
    BOOST_CHECK( replay.m_UserReviewRequired );
    BOOST_CHECK( replay.m_CheckpointOrJournalReplayRequired );
    BOOST_CHECK_EQUAL( replay.m_Reason,
                       wxString( wxS( "ready_for_user_review" ) ) );
    BOOST_CHECK_EQUAL( replay.m_RequestId, 1004 );
    BOOST_CHECK_EQUAL( replay.m_ToolCallId, wxString( wxS( "call-replay" ) ) );
    BOOST_CHECK_EQUAL( replay.m_ToolName, wxString( wxS( "kisurf_run_cell" ) ) );
    BOOST_CHECK_EQUAL( replay.m_SessionId, wxString( wxS( "chat-session-1" ) ) );
    BOOST_CHECK_EQUAL( replay.m_CheckpointId, 18 );
    BOOST_CHECK_EQUAL( replay.m_JournalOperationCount, 1 );
    BOOST_CHECK( replay.m_JournalJson.Contains( wxS( "create_via" ) ) );
    BOOST_CHECK( replay.m_RequestJson.Contains(
            wxS( "kisurf.ai.provider_recovery_replay_request" ) ) );
    BOOST_CHECK( replay.m_RequestJson.Contains(
            wxS( "\"status\":\"ready_for_user_review\"" ) ) );
    BOOST_CHECK( replay.m_RequestJson.Contains(
            wxS( "\"replay_source\":\"session_journal\"" ) ) );
    BOOST_CHECK( replay.m_RequestJson.Contains(
            wxS( "\"blind_tool_replay_allowed\":false" ) ) );

    wxRemoveFile( manifestPath );
    wxRemoveFile( artifact.m_BlobPath );
}


BOOST_AUTO_TEST_CASE( ProviderRecoveryReplayExecutionRequiresUserReviewBeforeAdapterMutation )
{
    wxString manifestPath =
            uniqueArtifactManifestPath( wxS( "provider_recovery_replay_execute" ) );

    AI_ARTIFACT_STORE store( manifestPath );
    wxString          error;

    nlohmann::json trace = {
        { "runtime_guard",
          { { "reason", "post_side_effect_ambiguity" },
            { "action", "checkpoint_or_journal_recovery_required" },
            { "replay_policy", "do_not_blindly_reexecute_tools" },
            { "recovery_basis",
              { { "board_state_version", { { "document_revision", 42 } } },
                { "tool_results",
                  nlohmann::json::array( {
                          { { "tool_call_id", "call-replay" },
                            { "tool_name", "kisurf_run_cell" },
                            { "session_id", "chat-session-1" },
                            { "checkpoint_id", 18 },
                            { "session_journal",
                              { { "operations",
                                  nlohmann::json::array( {
                                          { { "id", 7 },
                                            { "kind", "pcb.create_via" },
                                            { "arguments",
                                              { { "position", { { "x", 10 }, { "y", 20 } } },
                                                { "net", "GND" } } },
                                            { "result", { { "handle", "via-1" } } } }
                                  } ) } } } }
                  } ) } } } } } };

    AI_ARTIFACT_RECORD artifact;
    BOOST_REQUIRE( AiStoreProviderRecoveryArtifact(
            wxS( "project-a" ), wxS( "board-1" ), wxS( "chat" ),
            wxS( "trace" ), wxS( "AI_RUNTIME" ), 1005,
            wxString::FromUTF8( trace.dump().c_str() ),
            store, artifact, error ) );

    AI_PROVIDER_RECOVERY_QUERY query;
    query.m_ProjectId = wxS( "project-a" );
    query.m_DocumentId = wxS( "board-1" );
    query.m_AgentKind = wxS( "chat" );

    AI_PROVIDER_RECOVERY_RESUME_PLAN plan =
            AiBuildLatestProviderRecoveryResumePlan( store, query, error );

    BOOST_REQUIRE( plan.m_Available );

    AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT currentContext;
    currentContext.m_DocumentRevision = 42;

    AI_PROVIDER_RECOVERY_REPLAY_REQUEST replay =
            AiBuildProviderRecoveryReplayRequest( plan, currentContext );

    BOOST_REQUIRE( replay.m_Available );
    BOOST_REQUIRE( replay.m_Allowed );
    BOOST_REQUIRE_EQUAL( replay.m_JournalOperationCount, 1 );

    RECORDING_PROVIDER_RECOVERY_ACCEPT_ADAPTER adapter;
    AI_PROVIDER_RECOVERY_REPLAY_EXECUTION_OPTIONS unreviewed;

    AI_PROVIDER_RECOVERY_REPLAY_EXECUTION_RESULT blocked =
            AiExecuteProviderRecoveryReplayRequest( replay, unreviewed, adapter );

    BOOST_CHECK( !blocked.m_Ok );
    BOOST_CHECK_EQUAL( blocked.m_ErrorCode,
                       wxString( wxS( "user_review_required" ) ) );
    BOOST_CHECK( blocked.m_UserReviewRequired );
    BOOST_CHECK_EQUAL( adapter.m_BeginCount, 0 );
    BOOST_CHECK_EQUAL( adapter.m_CommitCount, 0 );
    BOOST_CHECK( adapter.m_OperationKinds.empty() );

    AI_PROVIDER_RECOVERY_REPLAY_EXECUTION_OPTIONS reviewed;
    reviewed.m_UserReviewed = true;
    reviewed.m_Reviewer = wxS( "engineer" );
    reviewed.m_ReviewNote = wxS( "journal operation was inspected" );

    AI_PROVIDER_RECOVERY_REPLAY_EXECUTION_RESULT applied =
            AiExecuteProviderRecoveryReplayRequest( replay, reviewed, adapter );

    BOOST_CHECK( applied.m_Ok );
    BOOST_CHECK( applied.m_BoardMutated );
    BOOST_CHECK( !applied.m_UserReviewRequired );
    BOOST_CHECK_EQUAL( applied.m_AppliedOperationCount, 1 );
    BOOST_CHECK_EQUAL( adapter.m_BeginCount, 1 );
    BOOST_CHECK_EQUAL( adapter.m_CommitCount, 1 );
    BOOST_CHECK_EQUAL( adapter.m_AbortCount, 0 );
    BOOST_REQUIRE_EQUAL( adapter.m_OperationKinds.size(), 1 );
    BOOST_CHECK( adapter.m_OperationKinds.front()
                 == AI_SESSION_OPERATION_KIND::CreateVia );
    BOOST_REQUIRE_EQUAL( adapter.m_OperationArguments.size(), 1 );
    BOOST_CHECK( adapter.m_OperationArguments.front().Contains( wxS( "\"net\":\"GND\"" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_SessionOperationCount, 1 );
    BOOST_CHECK( applied.m_ResultJson.Contains(
            wxS( "kisurf.ai.provider_recovery_replay_execution" ) ) );
    BOOST_CHECK( applied.m_ResultJson.Contains( wxS( "\"status\":\"applied\"" ) ) );
    BOOST_CHECK( applied.m_ResultJson.Contains( wxS( "\"user_reviewed\":true" ) ) );

    wxRemoveFile( manifestPath );
    wxRemoveFile( artifact.m_BlobPath );
}


BOOST_AUTO_TEST_CASE( VisualObservationArtifactStoresPixelsAndGroundingSidecar )
{
    wxString manifestPath = uniqueArtifactManifestPath( wxS( "visual" ) );

    AI_VISUAL_SNAPSHOT snapshot;
    snapshot.m_Source = wxS( "pcbnew.canvas.roi" );
    snapshot.m_MimeType = wxS( "image/png" );
    snapshot.m_DataUri = wxS( "data:image/png;base64,dmlzdWFsLXJvaQ==" );
    snapshot.m_WidthPx = 320;
    snapshot.m_HeightPx = 240;
    snapshot.m_ByteSize = 10;

    AI_VISUAL_OBSERVATION_REQUEST request;
    request.m_FrameId = wxS( "frame-visual-1" );
    request.m_FrameKind = wxS( "annotated_roi" );
    request.m_AttemptId = wxS( "attempt-42" );
    request.m_PreviewId = wxS( "preview-7" );
    request.m_DocumentRevision = 123;
    request.m_WorldBounds = AI_VISUAL_BOUNDS{ 10.0, 20.0, 30.0, 40.0 };
    request.m_PixelBounds = AI_VISUAL_BOUNDS{ 0.0, 0.0, 320.0, 240.0 };

    AI_VISUAL_ANCHOR_RECORD anchor;
    anchor.m_AnchorId = wxS( "anchor-U1" );
    anchor.m_ObjectId = wxS( "U1" );
    anchor.m_Handle = wxS( "handle-U1" );
    anchor.m_Layer = wxS( "F.Cu" );
    anchor.m_WorldX = 12.0;
    anchor.m_WorldY = 24.0;
    request.m_Anchors.push_back( anchor );

    AI_VISUAL_OBSERVATION_ARTIFACT visual =
            BuildAiVisualObservationArtifact( snapshot, request );

    AI_ARTIFACT_STORE  store( manifestPath );
    AI_ARTIFACT_RECORD artifact;
    wxString           error;

    BOOST_REQUIRE( AiStoreVisualObservationArtifact(
            wxS( "project-a" ), wxS( "board-1" ), wxS( "next_action" ),
            wxS( "trace" ), visual, store, artifact, error ) );

    BOOST_CHECK( artifact.m_Uri.StartsWith(
            wxS( "kisurf-artifact://visual-observation/" ) ) );
    BOOST_CHECK_EQUAL( artifact.m_Kind, wxString( wxS( "visual_observation" ) ) );
    BOOST_CHECK_EQUAL( artifact.m_Source,
                       wxString( wxS( "pcbnew.canvas.roi" ) ) );
    BOOST_CHECK_EQUAL( artifact.m_MimeType,
                       wxString( wxS( "application/json" ) ) );
    BOOST_CHECK( artifact.m_MetadataJson.Contains( wxS( "frame-visual-1" ) ) );
    BOOST_CHECK( artifact.m_MetadataJson.Contains( wxS( "annotated_roi" ) ) );
    BOOST_CHECK( artifact.m_MetadataJson.Contains( wxS( "attempt-42" ) ) );
    BOOST_CHECK( artifact.m_MetadataJson.Contains( wxS( "preview-7" ) ) );

    wxString payload;
    BOOST_REQUIRE( store.ReadPayload( artifact.m_Uri, payload, error ) );
    BOOST_CHECK( payload.Contains( wxS( "visual_observation_artifact" ) ) );
    BOOST_CHECK( payload.Contains( wxS( "data:image/png;base64,dmlzdWFsLXJvaQ==" ) ) );
    BOOST_CHECK( payload.Contains( wxS( "anchor-U1" ) ) );
    BOOST_CHECK( payload.Contains( wxS( "\"document_revision\":123" ) ) );

    wxRemoveFile( manifestPath );
    wxRemoveFile( artifact.m_BlobPath );
}


BOOST_AUTO_TEST_CASE( ValidationReportArtifactStoresDrcFactsWithStableReference )
{
    wxString manifestPath = uniqueArtifactManifestPath( wxS( "validation" ) );

    wxString validationJson =
            wxS( "{\"status\":\"validated\","
                 "\"validation\":{\"backend\":\"native_drc\","
                 "\"scope\":\"preview\",\"grade\":\"accept\","
                 "\"issue_count\":1,"
                 "\"issues\":[{\"id\":\"drc-1\","
                 "\"severity\":\"error\",\"message\":\"clearance\"}]}}" );

    AI_ARTIFACT_STORE  store( manifestPath );
    AI_ARTIFACT_RECORD artifact;
    wxString           error;

    BOOST_REQUIRE( AiStoreValidationReportArtifact(
            wxS( "project-a" ), wxS( "board-1" ), wxS( "next_action" ),
            wxS( "trace" ), wxS( "call-validate" ), wxS( "validate_hidden_attempt" ),
            validationJson, store, artifact, error ) );

    BOOST_CHECK( artifact.m_Uri.StartsWith(
            wxS( "kisurf-artifact://validation-report/" ) ) );
    BOOST_CHECK_EQUAL( artifact.m_Kind, wxString( wxS( "validation_report" ) ) );
    BOOST_CHECK_EQUAL( artifact.m_Source,
                       wxString( wxS( "validate_hidden_attempt" ) ) );
    BOOST_CHECK_EQUAL( artifact.m_MimeType,
                       wxString( wxS( "application/json" ) ) );
    BOOST_CHECK( artifact.m_MetadataJson.Contains( wxS( "call-validate" ) ) );
    BOOST_CHECK( artifact.m_MetadataJson.Contains( wxS( "native_drc" ) ) );
    BOOST_CHECK( artifact.m_MetadataJson.Contains( wxS( "issue_count" ) ) );

    wxString payload;
    BOOST_REQUIRE( store.ReadPayload( artifact.m_Uri, payload, error ) );
    BOOST_CHECK_EQUAL( payload, validationJson );

    wxRemoveFile( manifestPath );
    wxRemoveFile( artifact.m_BlobPath );
}


BOOST_AUTO_TEST_SUITE_END()
