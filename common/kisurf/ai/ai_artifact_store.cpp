#include <kisurf/ai/ai_artifact_store.h>

#include <kisurf/ai/ai_accept_applier.h>
#include <kisurf/ai/ai_local_text_memory.h>
#include <kisurf/ai/ai_visual_snapshot.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

#include <wx/ffile.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/textfile.h>

namespace
{
std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


wxString fromUtf8String( const std::string& aText )
{
    return wxString::FromUTF8( aText.c_str() );
}


std::string stableHashText( const wxString& aText )
{
    const std::string text = toUtf8String( aText );
    uint64_t          hash = 1469598103934665603ull;

    for( unsigned char ch : text )
    {
        hash ^= ch;
        hash *= 1099511628211ull;
    }

    std::ostringstream stream;
    stream << std::hex << std::setw( 16 ) << std::setfill( '0' ) << hash;
    return stream.str();
}


std::string artifactKindSlug( const wxString& aKind )
{
    std::string slug = toUtf8String( aKind );

    std::transform( slug.begin(), slug.end(), slug.begin(), []( unsigned char ch )
    {
        if( std::isalnum( ch ) )
            return static_cast<char>( std::tolower( ch ) );

        return '-';
    } );

    while( slug.find( "--" ) != std::string::npos )
        slug.replace( slug.find( "--" ), 2, "-" );

    while( !slug.empty() && slug.front() == '-' )
        slug.erase( slug.begin() );

    while( !slug.empty() && slug.back() == '-' )
        slug.pop_back();

    return slug.empty() ? std::string( "artifact" ) : slug;
}


wxString jsonStringValue( const nlohmann::json& aJson, const char* aKey )
{
    const auto it = aJson.find( aKey );

    if( it == aJson.end() || !it->is_string() )
        return wxString();

    return fromUtf8String( it->get<std::string>() );
}


uint64_t jsonUint64Value( const nlohmann::json& aJson, const char* aKey )
{
    const auto it = aJson.find( aKey );

    if( it == aJson.end() )
        return 0;

    if( it->is_number_unsigned() )
        return it->get<uint64_t>();

    if( it->is_number_integer() )
    {
        const int64_t value = it->get<int64_t>();
        return value > 0 ? static_cast<uint64_t>( value ) : 0;
    }

    return 0;
}


bool jsonBoolValue( const nlohmann::json& aJson, const char* aKey, bool aDefault = false )
{
    const auto it = aJson.find( aKey );

    if( it == aJson.end() || !it->is_boolean() )
        return aDefault;

    return it->get<bool>();
}


std::string normalizedOperationKindId( std::string aKind )
{
    std::transform( aKind.begin(), aKind.end(), aKind.begin(),
                    []( unsigned char ch )
                    {
                        return static_cast<char>( std::tolower( ch ) );
                    } );

    if( aKind.find( '.' ) != std::string::npos )
        return aKind;

    if( aKind == "apply_surface_patch" )
        return "surface.apply_patch";

    if( aKind == "checkpoint" )
        return "session.checkpoint";

    if( aKind == "rollback_to" )
        return "session.rollback_to";

    if( aKind.rfind( "query_", 0 ) == 0 )
        return "query." + aKind.substr( 6 );

    if( aKind == "render_preview" )
        return "render.preview";

    if( aKind == "observe_step" )
        return "observe.step";

    return "pcb." + aKind;
}


AI_SESSION_OPERATION_KIND sessionOperationKindFromRecoveryId( const std::string& aKind )
{
    const std::string kind = normalizedOperationKindId( aKind );

    if( kind == "session.checkpoint" )
        return AI_SESSION_OPERATION_KIND::Checkpoint;

    if( kind == "session.rollback_to" )
        return AI_SESSION_OPERATION_KIND::RollbackTo;

    if( kind == "query.board_summary" )
        return AI_SESSION_OPERATION_KIND::QueryBoardSummary;

    if( kind == "query.items" )
        return AI_SESSION_OPERATION_KIND::QueryItems;

    if( kind == "query.item" )
        return AI_SESSION_OPERATION_KIND::QueryItem;

    if( kind == "query.selection" )
        return AI_SESSION_OPERATION_KIND::QuerySelection;

    if( kind == "query.nets" )
        return AI_SESSION_OPERATION_KIND::QueryNets;

    if( kind == "query.layers" )
        return AI_SESSION_OPERATION_KIND::QueryLayers;

    if( kind == "query.design_rules" )
        return AI_SESSION_OPERATION_KIND::QueryDesignRules;

    if( kind == "query.viewport" )
        return AI_SESSION_OPERATION_KIND::QueryViewport;

    if( kind == "query.activity_timeline" )
        return AI_SESSION_OPERATION_KIND::QueryActivityTimeline;

    if( kind == "render.preview" )
        return AI_SESSION_OPERATION_KIND::RenderPreview;

    if( kind == "observe.step" )
        return AI_SESSION_OPERATION_KIND::ObserveStep;

    if( kind == "pcb.create_via" )
        return AI_SESSION_OPERATION_KIND::CreateVia;

    if( kind == "pcb.create_track_segment" )
        return AI_SESSION_OPERATION_KIND::CreateTrackSegment;

    if( kind == "pcb.create_track_polyline" )
        return AI_SESSION_OPERATION_KIND::CreateTrackPolyline;

    if( kind == "pcb.create_zone" )
        return AI_SESSION_OPERATION_KIND::CreateZone;

    if( kind == "pcb.create_shape" )
        return AI_SESSION_OPERATION_KIND::CreateShape;

    if( kind == "pcb.move_items" )
        return AI_SESSION_OPERATION_KIND::MoveItems;

    if( kind == "pcb.delete_items" )
        return AI_SESSION_OPERATION_KIND::DeleteItems;

    if( kind == "pcb.update_item_geometry" )
        return AI_SESSION_OPERATION_KIND::UpdateItemGeometry;

    if( kind == "pcb.set_item_net" )
        return AI_SESSION_OPERATION_KIND::SetItemNet;

    if( kind == "pcb.set_item_layer" )
        return AI_SESSION_OPERATION_KIND::SetItemLayer;

    if( kind == "pcb.set_item_properties" )
        return AI_SESSION_OPERATION_KIND::SetItemProperties;

    if( kind == "pcb.set_metadata" )
        return AI_SESSION_OPERATION_KIND::SetMetadata;

    if( kind == "pcb.refill_zones" )
        return AI_SESSION_OPERATION_KIND::RefillZones;

    if( kind == "pcb.rebuild_connectivity" )
        return AI_SESSION_OPERATION_KIND::RebuildConnectivity;

    if( kind == "pcb.run_validation" )
        return AI_SESSION_OPERATION_KIND::RunValidation;

    if( kind == "surface.apply_patch" )
        return AI_SESSION_OPERATION_KIND::ApplySurfacePatch;

    return AI_SESSION_OPERATION_KIND::Unknown;
}


int64_t jsonInt64Value( const nlohmann::json& aJson, const char* aKey )
{
    const auto it = aJson.find( aKey );

    if( it == aJson.end() )
        return 0;

    if( it->is_number_integer() )
        return it->get<int64_t>();

    if( it->is_number_unsigned() )
        return static_cast<int64_t>( it->get<uint64_t>() );

    return 0;
}


nlohmann::json recordToJson( const AI_ARTIFACT_RECORD& aRecord )
{
    return {
        { "schema", { { "name", "kisurf.ai.artifact_record" }, { "version", 1 } } },
        { "uri", toUtf8String( aRecord.m_Uri ) },
        { "kind", toUtf8String( aRecord.m_Kind ) },
        { "project_id", toUtf8String( aRecord.m_ProjectId ) },
        { "document_id", toUtf8String( aRecord.m_DocumentId ) },
        { "agent_kind", toUtf8String( aRecord.m_AgentKind ) },
        { "source", toUtf8String( aRecord.m_Source ) },
        { "mime_type", toUtf8String( aRecord.m_MimeType ) },
        { "summary", toUtf8String( aRecord.m_Summary ) },
        { "metadata", toUtf8String( aRecord.m_MetadataJson ) },
        { "retention_class", toUtf8String( aRecord.m_RetentionClass ) },
        { "hash", toUtf8String( aRecord.m_Hash ) },
        { "blob_path", toUtf8String( aRecord.m_BlobPath ) },
        { "byte_size", aRecord.m_ByteSize },
        { "sequence", aRecord.m_Sequence },
        { "created_at_unix_seconds", aRecord.m_CreatedAtUnixSeconds },
        { "expires_at_unix_seconds", aRecord.m_ExpiresAtUnixSeconds }
    };
}


wxString jsonStringOrEmpty( const nlohmann::json& aJson, const char* aKey )
{
    if( !aJson.is_object() )
        return wxString();

    const auto it = aJson.find( aKey );

    if( it == aJson.end() || !it->is_string() )
        return wxString();

    return fromUtf8String( it->get<std::string>() );
}


nlohmann::json parseObjectOrEmpty( const wxString& aText )
{
    nlohmann::json parsed = nlohmann::json::parse(
            toUtf8String( aText ), nullptr, false );

    if( parsed.is_discarded() || !parsed.is_object() )
        return nlohmann::json::object();

    return parsed;
}


uint64_t boardStateDocumentRevision( const nlohmann::json& aRecoveryBasis )
{
    if( !aRecoveryBasis.is_object()
        || !aRecoveryBasis.contains( "board_state_version" )
        || !aRecoveryBasis["board_state_version"].is_object() )
    {
        return 0;
    }

    return jsonUint64Value( aRecoveryBasis["board_state_version"],
                            "document_revision" );
}


bool hasCheckpointOrJournalReplayBasis( const nlohmann::json& aCandidate )
{
    if( !aCandidate.is_object() )
        return false;

    if( aCandidate.contains( "checkpoint_id" )
        || aCandidate.contains( "rollback_checkpoint_id" ) )
    {
        return true;
    }

    if( aCandidate.contains( "session_journal" )
        && aCandidate["session_journal"].is_object() )
    {
        return true;
    }

    if( aCandidate.contains( "attempt_session_journal" )
        && aCandidate["attempt_session_journal"].is_object() )
    {
        return true;
    }

    return false;
}


size_t replayCandidateCount( const nlohmann::json& aPlanJson )
{
    if( !aPlanJson.is_object()
        || !aPlanJson.contains( "replay_candidates" )
        || !aPlanJson["replay_candidates"].is_array() )
    {
        return 0;
    }

    size_t count = 0;

    for( const nlohmann::json& candidate : aPlanJson["replay_candidates"] )
    {
        if( hasCheckpointOrJournalReplayBasis( candidate ) )
            ++count;
    }

    return count;
}


nlohmann::json firstReplayCandidateOrEmpty( const nlohmann::json& aPlanJson )
{
    if( !aPlanJson.is_object()
        || !aPlanJson.contains( "replay_candidates" )
        || !aPlanJson["replay_candidates"].is_array() )
    {
        return nlohmann::json::object();
    }

    for( const nlohmann::json& candidate : aPlanJson["replay_candidates"] )
    {
        if( hasCheckpointOrJournalReplayBasis( candidate ) )
            return candidate;
    }

    return nlohmann::json::object();
}


size_t journalOperationCount( const nlohmann::json& aJournal )
{
    if( !aJournal.is_object()
        || !aJournal.contains( "operations" )
        || !aJournal["operations"].is_array() )
    {
        return 0;
    }

    return aJournal["operations"].size();
}


nlohmann::json replayOperationArgumentsJson( const nlohmann::json& aOperation )
{
    const char* argumentKeys[] = {
        "arguments", "args", "parameters", "arguments_json", "argumentsJson"
    };

    for( const char* key : argumentKeys )
    {
        const auto it = aOperation.find( key );

        if( it == aOperation.end() )
            continue;

        if( it->is_string() )
        {
            nlohmann::json parsed = nlohmann::json::parse(
                    it->get<std::string>(), nullptr, false );

            if( !parsed.is_discarded() )
                return parsed;

            return *it;
        }

        return *it;
    }

    nlohmann::json derived = aOperation;

    for( const char* key : {
             "id", "operation_id", "kind", "result", "result_json", "resultJson",
             "warnings", "before_epoch", "after_epoch", "step_id" } )
    {
        derived.erase( key );
    }

    return derived.empty() ? nlohmann::json::object() : derived;
}


nlohmann::json replayOperationResultJson( const nlohmann::json& aOperation )
{
    const char* resultKeys[] = { "result", "result_json", "resultJson" };

    for( const char* key : resultKeys )
    {
        const auto it = aOperation.find( key );

        if( it == aOperation.end() )
            continue;

        if( it->is_string() )
        {
            nlohmann::json parsed = nlohmann::json::parse(
                    it->get<std::string>(), nullptr, false );

            if( !parsed.is_discarded() )
                return parsed;

            return *it;
        }

        return *it;
    }

    return nlohmann::json::object();
}


std::vector<wxString> replayOperationWarnings( const nlohmann::json& aOperation )
{
    std::vector<wxString> warnings;

    if( !aOperation.contains( "warnings" ) || !aOperation["warnings"].is_array() )
        return warnings;

    for( const nlohmann::json& warning : aOperation["warnings"] )
    {
        if( warning.is_string() )
            warnings.push_back( fromUtf8String( warning.get<std::string>() ) );
    }

    return warnings;
}


bool appendReplayJournalOperations(
        const AI_PROVIDER_RECOVERY_REPLAY_REQUEST& aRequest,
        AI_EXECUTION_SESSION& aSession,
        wxString& aError )
{
    const nlohmann::json journal = parseObjectOrEmpty( aRequest.m_JournalJson );

    if( !journal.contains( "operations" ) || !journal["operations"].is_array()
        || journal["operations"].empty() )
    {
        aError = wxS( "missing_session_journal" );
        return false;
    }

    for( const nlohmann::json& operationJson : journal["operations"] )
    {
        if( !operationJson.is_object() )
        {
            aError = wxS( "malformed_session_journal" );
            return false;
        }

        const wxString kindName = jsonStringOrEmpty( operationJson, "kind" );

        if( kindName.IsEmpty() )
        {
            aError = wxS( "missing_journal_operation_kind" );
            return false;
        }

        const AI_SESSION_OPERATION_KIND operationKind =
                sessionOperationKindFromRecoveryId( toUtf8String( kindName ) );

        if( operationKind == AI_SESSION_OPERATION_KIND::Unknown )
        {
            aError = wxString::Format( wxS( "unsupported_journal_operation:%s" ),
                                       kindName );
            return false;
        }

        AI_SESSION_OPERATION_RECORD operation;
        operation.m_Kind = operationKind;
        operation.m_ArgumentsJson =
                fromUtf8String( replayOperationArgumentsJson( operationJson ).dump() );
        operation.m_ResultJson =
                fromUtf8String( replayOperationResultJson( operationJson ).dump() );
        operation.m_Warnings = replayOperationWarnings( operationJson );

        aSession.AppendOperation( std::move( operation ) );
    }

    aError.clear();
    return true;
}


AI_PROVIDER_RECOVERY_REPLAY_EXECUTION_RESULT recoveryReplayExecutionResult(
        const AI_PROVIDER_RECOVERY_REPLAY_REQUEST& aRequest,
        const AI_PROVIDER_RECOVERY_REPLAY_EXECUTION_OPTIONS& aOptions,
        const wxString& aStatus,
        bool aOk,
        bool aBoardMutated,
        bool aUserReviewRequired,
        const wxString& aErrorCode,
        const wxString& aMessage,
        size_t aAppliedOperationCount )
{
    AI_PROVIDER_RECOVERY_REPLAY_EXECUTION_RESULT result;
    result.m_Ok = aOk;
    result.m_BoardMutated = aBoardMutated;
    result.m_UserReviewRequired = aUserReviewRequired;
    result.m_ErrorCode = aErrorCode;
    result.m_Message = aMessage;
    result.m_AppliedOperationCount = aAppliedOperationCount;

    nlohmann::json payload = {
        { "schema",
          { { "name", "kisurf.ai.provider_recovery_replay_execution" },
            { "version", 1 } } },
        { "status", toUtf8String( aStatus ) },
        { "ok", aOk },
        { "error_code", toUtf8String( aErrorCode ) },
        { "message", toUtf8String( aMessage ) },
        { "artifact_uri", toUtf8String( aRequest.m_ArtifactUri ) },
        { "agent_kind", toUtf8String( aRequest.m_AgentKind ) },
        { "source", toUtf8String( aRequest.m_Source ) },
        { "request_id", aRequest.m_RequestId },
        { "tool_call_id", toUtf8String( aRequest.m_ToolCallId ) },
        { "tool_name", toUtf8String( aRequest.m_ToolName ) },
        { "session_id", toUtf8String( aRequest.m_SessionId ) },
        { "checkpoint_id", aRequest.m_CheckpointId },
        { "rollback_checkpoint_id", aRequest.m_RollbackCheckpointId },
        { "replay_source", toUtf8String( aRequest.m_ReplaySource ) },
        { "journal_operation_count", aRequest.m_JournalOperationCount },
        { "applied_operation_count", aAppliedOperationCount },
        { "board_mutated", aBoardMutated },
        { "blind_tool_replay_allowed", false },
        { "user_review_required", aUserReviewRequired },
        { "user_reviewed", aOptions.m_UserReviewed },
        { "reviewer", toUtf8String( aOptions.m_Reviewer ) },
        { "review_note", toUtf8String( aOptions.m_ReviewNote ) },
        { "expected_document_revision", aRequest.m_ExpectedDocumentRevision },
        { "current_document_revision", aRequest.m_CurrentDocumentRevision } };

    result.m_ResultJson = fromUtf8String( payload.dump() );
    return result;
}


wxString artifactSummaryText( const AI_ARTIFACT_RECORD& aRecord )
{
    wxString text;
    text << wxS( "Artifact kind: " ) << aRecord.m_Kind << wxS( "\n" );

    if( !aRecord.m_Source.IsEmpty() )
        text << wxS( "Source: " ) << aRecord.m_Source << wxS( "\n" );

    if( !aRecord.m_Summary.IsEmpty() )
        text << wxS( "Summary: " ) << aRecord.m_Summary << wxS( "\n" );

    if( !aRecord.m_MetadataJson.IsEmpty() )
        text << wxS( "Metadata: " ) << aRecord.m_MetadataJson << wxS( "\n" );

    if( !aRecord.m_Uri.IsEmpty() )
        text << wxS( "Artifact URI: " ) << aRecord.m_Uri << wxS( "\n" );

    return text.Trim().Trim( false );
}


wxString artifactSummaryProvenanceJson( const AI_ARTIFACT_RECORD& aRecord )
{
    nlohmann::json provenance = {
        { "kind", "artifact_summary" },
        { "artifact_uri", toUtf8String( aRecord.m_Uri ) },
        { "artifact_kind", toUtf8String( aRecord.m_Kind ) },
        { "artifact_source", toUtf8String( aRecord.m_Source ) },
        { "retention_class", toUtf8String( aRecord.m_RetentionClass ) },
        { "hash", toUtf8String( aRecord.m_Hash ) },
        { "byte_size", aRecord.m_ByteSize }
    };

    if( !aRecord.m_MetadataJson.IsEmpty() )
    {
        nlohmann::json metadata =
                nlohmann::json::parse( toUtf8String( aRecord.m_MetadataJson ),
                                       nullptr, false );

        if( metadata.is_object() )
            provenance["artifact_metadata"] = metadata;
    }

    return fromUtf8String( provenance.dump() );
}


AI_ARTIFACT_RECORD recordFromJson( const nlohmann::json& aJson )
{
    AI_ARTIFACT_RECORD record;
    record.m_Uri = jsonStringValue( aJson, "uri" );
    record.m_Kind = jsonStringValue( aJson, "kind" );
    record.m_ProjectId = jsonStringValue( aJson, "project_id" );
    record.m_DocumentId = jsonStringValue( aJson, "document_id" );
    record.m_AgentKind = jsonStringValue( aJson, "agent_kind" );
    record.m_Source = jsonStringValue( aJson, "source" );
    record.m_MimeType = jsonStringValue( aJson, "mime_type" );
    record.m_Summary = jsonStringValue( aJson, "summary" );
    record.m_MetadataJson = jsonStringValue( aJson, "metadata" );
    record.m_RetentionClass = jsonStringValue( aJson, "retention_class" );
    record.m_Hash = jsonStringValue( aJson, "hash" );
    record.m_BlobPath = jsonStringValue( aJson, "blob_path" );
    record.m_ByteSize = static_cast<size_t>( jsonUint64Value( aJson, "byte_size" ) );
    record.m_Sequence = jsonUint64Value( aJson, "sequence" );
    record.m_CreatedAtUnixSeconds = jsonInt64Value( aJson, "created_at_unix_seconds" );
    record.m_ExpiresAtUnixSeconds = jsonInt64Value( aJson, "expires_at_unix_seconds" );
    return record;
}


bool writeRecords( const wxString& aPath, const std::vector<AI_ARTIFACT_RECORD>& aRecords,
                   wxString& aError )
{
    wxFileName fileName( aPath );

    if( !fileName.GetPath().IsEmpty() && !fileName.DirExists() )
    {
        if( !fileName.Mkdir( wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL ) )
        {
            aError = wxString::Format( wxS( "Unable to create artifact directory: %s" ),
                                       fileName.GetPath() );
            return false;
        }
    }

    wxFFile file( aPath, wxS( "wb" ) );

    if( !file.IsOpened() )
    {
        aError = wxString::Format( wxS( "Unable to open artifact manifest: %s" ),
                                   aPath );
        return false;
    }

    for( const AI_ARTIFACT_RECORD& record : aRecords )
    {
        file.Write( fromUtf8String( recordToJson( record ).dump() ), wxConvUTF8 );
        file.Write( wxS( "\n" ) );
    }

    aError.clear();
    return true;
}


bool recordMatchesQuery( const AI_ARTIFACT_RECORD& aRecord, const AI_ARTIFACT_QUERY& aQuery )
{
    if( !aQuery.m_Uri.IsEmpty() && aRecord.m_Uri != aQuery.m_Uri )
        return false;

    if( !aQuery.m_Kind.IsEmpty() && aRecord.m_Kind != aQuery.m_Kind )
        return false;

    if( !aQuery.m_ProjectId.IsEmpty() && aRecord.m_ProjectId != aQuery.m_ProjectId )
        return false;

    if( !aQuery.m_DocumentId.IsEmpty() && aRecord.m_DocumentId != aQuery.m_DocumentId )
        return false;

    if( !aQuery.m_AgentKind.IsEmpty() && aRecord.m_AgentKind != aQuery.m_AgentKind )
        return false;

    if( !aQuery.m_Source.IsEmpty() && aRecord.m_Source != aQuery.m_Source )
        return false;

    if( !aQuery.m_RetentionClass.IsEmpty()
        && aRecord.m_RetentionClass != aQuery.m_RetentionClass )
    {
        return false;
    }

    return true;
}
}


AI_ARTIFACT_STORE::AI_ARTIFACT_STORE() :
        AI_ARTIFACT_STORE( DefaultManifestPath() )
{
}


AI_ARTIFACT_STORE::AI_ARTIFACT_STORE(
        wxString aManifestPath, AI_ARTIFACT_RETENTION_POLICY aRetention ) :
        m_ManifestPath( std::move( aManifestPath ) ),
        m_Retention( aRetention )
{
}


wxString AI_ARTIFACT_STORE::DefaultManifestPath()
{
    wxString base = wxStandardPaths::Get().GetUserLocalDataDir();

    if( base.IsEmpty() )
        base = wxStandardPaths::Get().GetUserDataDir();

    if( base.IsEmpty() )
        base = wxStandardPaths::Get().GetTempDir();

    wxFileName path;
    path.AssignDir( base );
    path.AppendDir( wxS( "ai" ) );
    path.SetFullName( wxS( "artifacts.jsonl" ) );
    return path.GetFullPath();
}


wxString AI_ARTIFACT_STORE::BlobDirectory() const
{
    wxFileName manifest( m_ManifestPath );
    wxFileName blobDir;
    blobDir.AssignDir( manifest.GetPath() );
    blobDir.AppendDir( wxS( "artifacts" ) );
    return blobDir.GetPath();
}


bool AI_ARTIFACT_STORE::StorePayload( AI_ARTIFACT_RECORD& aRecord,
                                      const wxString& aPayload,
                                      wxString& aError )
{
    if( aRecord.m_Kind.IsEmpty() )
    {
        aError = wxS( "Artifact kind is required." );
        return false;
    }

    const std::string hash = stableHashText( aPayload );
    const std::string kindSlug = artifactKindSlug( aRecord.m_Kind );

    if( aRecord.m_Hash.IsEmpty() )
        aRecord.m_Hash = fromUtf8String( hash );

    if( aRecord.m_Uri.IsEmpty() )
        aRecord.m_Uri = wxS( "kisurf-artifact://" ) + fromUtf8String( kindSlug )
                        + wxS( "/" ) + fromUtf8String( hash );

    aRecord.m_ByteSize = toUtf8String( aPayload ).size();

    wxFileName blobDir;
    blobDir.AssignDir( BlobDirectory() );

    if( !blobDir.DirExists() )
    {
        if( !blobDir.Mkdir( wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL ) )
        {
            aError = wxString::Format( wxS( "Unable to create artifact blob directory: %s" ),
                                       blobDir.GetPath() );
            return false;
        }
    }

    wxFileName blobPath;
    blobPath.AssignDir( BlobDirectory() );
    blobPath.SetFullName( fromUtf8String( kindSlug + "-" + hash + ".blob" ) );
    aRecord.m_BlobPath = blobPath.GetFullPath();

    wxFFile blob( aRecord.m_BlobPath, wxS( "wb" ) );

    if( !blob.IsOpened() )
    {
        aError = wxString::Format( wxS( "Unable to open artifact payload: %s" ),
                                   aRecord.m_BlobPath );
        return false;
    }

    blob.Write( aPayload, wxConvUTF8 );
    blob.Close();

    wxFileName manifest( m_ManifestPath );

    if( !manifest.GetPath().IsEmpty() && !manifest.DirExists() )
    {
        if( !manifest.Mkdir( wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL ) )
        {
            aError = wxString::Format( wxS( "Unable to create artifact manifest directory: %s" ),
                                       manifest.GetPath() );
            return false;
        }
    }

    wxFFile file( m_ManifestPath, wxS( "ab" ) );

    if( !file.IsOpened() )
    {
        aError = wxString::Format( wxS( "Unable to open artifact manifest: %s" ),
                                   m_ManifestPath );
        return false;
    }

    file.Write( fromUtf8String( recordToJson( aRecord ).dump() + "\n" ), wxConvUTF8 );
    file.Close();

    if( !ApplyRetention( aError ) )
        return false;

    aError.clear();
    return true;
}


bool AI_ARTIFACT_STORE::ReadPayload( const wxString& aUri, wxString& aPayload,
                                     wxString& aError ) const
{
    AI_ARTIFACT_QUERY query;
    query.m_Uri = aUri;
    query.m_Limit = 1;

    std::vector<AI_ARTIFACT_RECORD> records = Query( query, aError );

    if( records.empty() )
    {
        aError = wxString::Format( wxS( "Artifact not found: %s" ), aUri );
        return false;
    }

    wxFFile file( records.front().m_BlobPath, wxS( "rb" ) );

    if( !file.IsOpened() )
    {
        aError = wxString::Format( wxS( "Unable to open artifact payload: %s" ),
                                   records.front().m_BlobPath );
        return false;
    }

    if( !file.ReadAll( &aPayload, wxConvUTF8 ) )
    {
        aError = wxString::Format( wxS( "Unable to read artifact payload: %s" ),
                                   records.front().m_BlobPath );
        return false;
    }

    aError.clear();
    return true;
}


std::vector<AI_ARTIFACT_RECORD> AI_ARTIFACT_STORE::LoadAll( wxString& aError ) const
{
    std::vector<AI_ARTIFACT_RECORD> records;
    wxTextFile                      file;

    if( !wxFileExists( m_ManifestPath ) )
    {
        aError.clear();
        return records;
    }

    if( !file.Open( m_ManifestPath ) )
    {
        aError = wxString::Format( wxS( "Unable to open artifact manifest: %s" ),
                                   m_ManifestPath );
        return records;
    }

    for( size_t lineIndex = 0; lineIndex < file.GetLineCount(); ++lineIndex )
    {
        const wxString line = file.GetLine( lineIndex ).Trim().Trim( false );

        if( line.IsEmpty() )
            continue;

        nlohmann::json recordJson = nlohmann::json::parse(
                toUtf8String( line ), nullptr, false );

        if( recordJson.is_discarded() || !recordJson.is_object() )
        {
            aError = wxString::Format( wxS( "Invalid artifact JSONL at line %zu" ),
                                       lineIndex + 1 );
            records.clear();
            return records;
        }

        records.push_back( recordFromJson( recordJson ) );
    }

    aError.clear();
    return records;
}


std::vector<AI_ARTIFACT_RECORD> AI_ARTIFACT_STORE::Query(
        const AI_ARTIFACT_QUERY& aQuery, wxString& aError ) const
{
    std::vector<AI_ARTIFACT_RECORD> all = LoadAll( aError );

    if( !aError.IsEmpty() )
        return {};

    std::vector<AI_ARTIFACT_RECORD> records;

    for( const AI_ARTIFACT_RECORD& record : all )
    {
        if( !recordMatchesQuery( record, aQuery ) )
            continue;

        records.push_back( record );

        if( aQuery.m_Limit > 0 && records.size() >= aQuery.m_Limit )
            break;
    }

    aError.clear();
    return records;
}


bool AI_ARTIFACT_STORE::ApplyRetention( wxString& aError ) const
{
    if( m_Retention.m_MaxRecords == 0 || !wxFileExists( m_ManifestPath ) )
    {
        aError.clear();
        return true;
    }

    std::vector<AI_ARTIFACT_RECORD> records = LoadAll( aError );

    if( !aError.IsEmpty() )
        return false;

    if( records.size() <= m_Retention.m_MaxRecords )
    {
        aError.clear();
        return true;
    }

    const size_t firstRetained = records.size() - m_Retention.m_MaxRecords;

    for( size_t i = 0; i < firstRetained; ++i )
    {
        if( !records.at( i ).m_BlobPath.IsEmpty()
            && wxFileExists( records.at( i ).m_BlobPath ) )
        {
            wxRemoveFile( records.at( i ).m_BlobPath );
        }
    }

    std::vector<AI_ARTIFACT_RECORD> retained( records.begin()
                                              + static_cast<std::ptrdiff_t>( firstRetained ),
                                              records.end() );

    return writeRecords( m_ManifestPath, retained, aError );
}


bool AiStoreToolResultArtifact( const wxString& aProjectId,
                                const wxString& aDocumentId,
                                const wxString& aAgentKind,
                                const wxString& aRetentionClass,
                                const wxString& aToolCallId,
                                const wxString& aToolName,
                                const wxString& aResultJson,
                                AI_ARTIFACT_STORE& aStore,
                                AI_ARTIFACT_RECORD& aRecord,
                                wxString& aError )
{
    aRecord = AI_ARTIFACT_RECORD();
    aRecord.m_Kind = wxS( "tool_result" );
    aRecord.m_ProjectId = aProjectId;
    aRecord.m_DocumentId = aDocumentId;
    aRecord.m_AgentKind = aAgentKind;
    aRecord.m_Source = aToolName;
    aRecord.m_MimeType = wxS( "application/json" );
    aRecord.m_RetentionClass =
            aRetentionClass.IsEmpty() ? wxString( wxS( "trace" ) ) : aRetentionClass;
    aRecord.m_Summary = wxString::Format( wxS( "Archived tool result for %s" ),
                                          aToolName );

    nlohmann::json metadata = {
        { "tool_call_id", toUtf8String( aToolCallId ) },
        { "tool_name", toUtf8String( aToolName ) }
    };

    aRecord.m_MetadataJson = fromUtf8String( metadata.dump() );

    return aStore.StorePayload( aRecord, aResultJson, aError );
}


bool AiStoreScriptOutputArtifact( const wxString& aProjectId,
                                  const wxString& aDocumentId,
                                  const wxString& aAgentKind,
                                  const wxString& aRetentionClass,
                                  const wxString& aToolCallId,
                                  const wxString& aToolName,
                                  const wxString& aArgumentsJson,
                                  const wxString& aResultJson,
                                  AI_ARTIFACT_STORE& aStore,
                                  AI_ARTIFACT_RECORD& aRecord,
                                  wxString& aError )
{
    const nlohmann::json arguments = parseObjectOrEmpty( aArgumentsJson );
    const nlohmann::json result = parseObjectOrEmpty( aResultJson );

    aRecord = AI_ARTIFACT_RECORD();
    aRecord.m_Kind = wxS( "script_output" );
    aRecord.m_ProjectId = aProjectId;
    aRecord.m_DocumentId = aDocumentId;
    aRecord.m_AgentKind = aAgentKind;
    aRecord.m_Source = aToolName;
    aRecord.m_MimeType = wxS( "application/json" );
    aRecord.m_RetentionClass =
            aRetentionClass.IsEmpty() ? wxString( wxS( "trace" ) ) : aRetentionClass;
    aRecord.m_Summary = wxString::Format( wxS( "Archived script output for %s" ),
                                          aToolName );

    nlohmann::json metadata = {
        { "tool_call_id", toUtf8String( aToolCallId ) },
        { "tool_name", toUtf8String( aToolName ) }
    };

    if( arguments.contains( "cell_id" ) && arguments["cell_id"].is_string() )
        metadata["cell_id"] = arguments["cell_id"];

    if( arguments.contains( "plan_id" ) && arguments["plan_id"].is_string() )
        metadata["plan_id"] = arguments["plan_id"];

    if( result.contains( "stdout" ) && result["stdout"].is_string() )
        metadata["stdout_chars"] = result["stdout"].get<std::string>().size();

    if( result.contains( "stderr" ) && result["stderr"].is_string() )
        metadata["stderr_chars"] = result["stderr"].get<std::string>().size();

    if( result.contains( "events" ) && result["events"].is_array() )
        metadata["event_count"] = result["events"].size();

    if( result.contains( "status" ) && result["status"].is_string() )
        metadata["status"] = result["status"];

    aRecord.m_MetadataJson = fromUtf8String( metadata.dump() );

    return aStore.StorePayload( aRecord, aResultJson, aError );
}


bool AiStoreFailedHiddenAttemptArtifact(
        const wxString& aProjectId,
        const wxString& aDocumentId,
        const wxString& aAgentKind,
        const wxString& aRetentionClass,
        uint64_t aRuntimeStepId,
        uint64_t aAttemptId,
        const wxString& aTerminalStatus,
        const wxString& aReasonCode,
        const wxString& aAuditPayloadJson,
        AI_ARTIFACT_STORE& aStore,
        AI_ARTIFACT_RECORD& aRecord,
        wxString& aError )
{
    aRecord = AI_ARTIFACT_RECORD();
    aRecord.m_Kind = wxS( "failed_hidden_attempt" );
    aRecord.m_ProjectId = aProjectId;
    aRecord.m_DocumentId = aDocumentId;
    aRecord.m_AgentKind = aAgentKind;
    aRecord.m_Source = wxS( "AI_NEXT_ACTION_RUNTIME" );
    aRecord.m_MimeType = wxS( "application/json" );
    aRecord.m_RetentionClass =
            aRetentionClass.IsEmpty() ? wxString( wxS( "trace" ) ) : aRetentionClass;
    aRecord.m_Summary = wxString::Format(
            wxS( "Archived failed hidden attempt %llu for runtime step %llu" ),
            static_cast<unsigned long long>( aAttemptId ),
            static_cast<unsigned long long>( aRuntimeStepId ) );

    nlohmann::json metadata = {
        { "runtime_step_id", aRuntimeStepId },
        { "attempt_id", aAttemptId },
        { "terminal_status", toUtf8String( aTerminalStatus ) },
        { "reason_code", toUtf8String( aReasonCode ) }
    };

    aRecord.m_MetadataJson = fromUtf8String( metadata.dump() );

    return aStore.StorePayload( aRecord, aAuditPayloadJson, aError );
}


bool AiStoreProviderRecoveryArtifact(
        const wxString& aProjectId,
        const wxString& aDocumentId,
        const wxString& aAgentKind,
        const wxString& aRetentionClass,
        const wxString& aSource,
        uint64_t aRequestId,
        const wxString& aProviderTraceJson,
        AI_ARTIFACT_STORE& aStore,
        AI_ARTIFACT_RECORD& aRecord,
        wxString& aError )
{
    const nlohmann::json trace = parseObjectOrEmpty( aProviderTraceJson );
    nlohmann::json       guard = nlohmann::json::object();

    if( trace.contains( "runtime_guard" ) && trace["runtime_guard"].is_object() )
        guard = trace["runtime_guard"];

    const wxString reason =
            jsonStringOrEmpty( guard, "reason" ).IsEmpty()
                    ? wxString( wxS( "provider_failure" ) )
                    : jsonStringOrEmpty( guard, "reason" );
    const wxString action = jsonStringOrEmpty( guard, "action" );

    nlohmann::json payload = {
        { "schema",
          { { "name", "kisurf.ai.provider_recovery" }, { "version", 1 } } },
        { "request_id", aRequestId },
        { "agent_kind", toUtf8String( aAgentKind ) },
        { "source", toUtf8String( aSource ) },
        { "reason", toUtf8String( reason ) },
        { "action", toUtf8String( action ) },
        { "provider_trace", trace }
    };

    if( guard.contains( "recovery_basis" ) )
        payload["recovery_basis"] = guard["recovery_basis"];

    aRecord = AI_ARTIFACT_RECORD();
    aRecord.m_Kind = wxS( "provider_recovery" );
    aRecord.m_ProjectId = aProjectId;
    aRecord.m_DocumentId = aDocumentId;
    aRecord.m_AgentKind = aAgentKind;
    aRecord.m_Source = aSource;
    aRecord.m_MimeType = wxS( "application/json" );
    aRecord.m_RetentionClass =
            aRetentionClass.IsEmpty() ? wxString( wxS( "trace" ) ) : aRetentionClass;
    aRecord.m_Summary = wxString::Format(
            wxS( "Archived provider recovery basis for request %llu" ),
            static_cast<unsigned long long>( aRequestId ) );

    nlohmann::json metadata = {
        { "request_id", aRequestId },
        { "reason", toUtf8String( reason ) },
        { "action", toUtf8String( action ) },
        { "source", toUtf8String( aSource ) }
    };

    if( guard.contains( "recovery_basis" )
        && guard["recovery_basis"].is_object()
        && guard["recovery_basis"].contains( "executed_tool_result_count" ) )
    {
        metadata["executed_tool_result_count"] =
                guard["recovery_basis"]["executed_tool_result_count"];
    }

    aRecord.m_MetadataJson = fromUtf8String( metadata.dump() );

    return aStore.StorePayload( aRecord, fromUtf8String( payload.dump() ), aError );
}


AI_PROVIDER_RECOVERY_POLICY AiEvaluateLatestProviderRecovery(
        const AI_ARTIFACT_STORE& aStore,
        const AI_PROVIDER_RECOVERY_QUERY& aQuery,
        wxString& aError )
{
    AI_PROVIDER_RECOVERY_POLICY policy;
    std::vector<AI_ARTIFACT_RECORD> records = aStore.LoadAll( aError );

    if( !aError.IsEmpty() )
        return policy;

    for( auto it = records.rbegin(); it != records.rend(); ++it )
    {
        const AI_ARTIFACT_RECORD& record = *it;

        if( record.m_Kind != wxS( "provider_recovery" ) )
            continue;

        if( !aQuery.m_ProjectId.IsEmpty()
            && record.m_ProjectId != aQuery.m_ProjectId )
        {
            continue;
        }

        if( !aQuery.m_DocumentId.IsEmpty()
            && record.m_DocumentId != aQuery.m_DocumentId )
        {
            continue;
        }

        if( !aQuery.m_AgentKind.IsEmpty()
            && record.m_AgentKind != aQuery.m_AgentKind )
        {
            continue;
        }

        wxString payloadText;

        if( !aStore.ReadPayload( record.m_Uri, payloadText, aError ) )
            return policy;

        const nlohmann::json payload = parseObjectOrEmpty( payloadText );
        nlohmann::json       providerTrace = nlohmann::json::object();
        nlohmann::json       guard = nlohmann::json::object();

        if( payload.contains( "provider_trace" )
            && payload["provider_trace"].is_object() )
        {
            providerTrace = payload["provider_trace"];
        }

        if( providerTrace.contains( "runtime_guard" )
            && providerTrace["runtime_guard"].is_object() )
        {
            guard = providerTrace["runtime_guard"];
        }

        policy.m_Available = true;
        policy.m_BlindToolReplayAllowed = false;
        policy.m_CheckpointOrJournalResumeRequired = true;
        policy.m_ArtifactUri = record.m_Uri;
        policy.m_AgentKind = record.m_AgentKind;
        policy.m_Source = record.m_Source;
        policy.m_RequestId = jsonUint64Value( payload, "request_id" );
        policy.m_Reason = jsonStringOrEmpty( payload, "reason" );
        policy.m_Action = jsonStringOrEmpty( payload, "action" );

        if( policy.m_Reason.IsEmpty() )
            policy.m_Reason = jsonStringOrEmpty( guard, "reason" );

        if( policy.m_Action.IsEmpty() )
            policy.m_Action = jsonStringOrEmpty( guard, "action" );

        if( guard.contains( "recovery_basis" )
            && guard["recovery_basis"].is_object() )
        {
            policy.m_RecoveryBasisJson =
                    fromUtf8String( guard["recovery_basis"].dump() );
        }
        else if( payload.contains( "recovery_basis" )
                 && payload["recovery_basis"].is_object() )
        {
            policy.m_RecoveryBasisJson =
                    fromUtf8String( payload["recovery_basis"].dump() );
        }

        aError.clear();
        return policy;
    }

    aError.clear();
    return policy;
}


AI_PROVIDER_RECOVERY_RESUME_PACKET AiBuildLatestProviderRecoveryResumePacket(
        const AI_ARTIFACT_STORE& aStore,
        const AI_PROVIDER_RECOVERY_QUERY& aQuery,
        wxString& aError )
{
    AI_PROVIDER_RECOVERY_RESUME_PACKET packet;
    AI_PROVIDER_RECOVERY_POLICY policy =
            AiEvaluateLatestProviderRecovery( aStore, aQuery, aError );

    if( !policy.m_Available || !aError.IsEmpty() )
        return packet;

    nlohmann::json recoveryBasis = parseObjectOrEmpty( policy.m_RecoveryBasisJson );

    if( recoveryBasis.empty() )
    {
        aError = wxS( "Provider recovery artifact has no recovery basis." );
        return packet;
    }

    packet.m_Available = true;
    packet.m_BlindToolReplayAllowed = false;
    packet.m_CheckpointOrJournalResumeRequired = true;
    packet.m_ArtifactUri = policy.m_ArtifactUri;
    packet.m_AgentKind = policy.m_AgentKind;
    packet.m_Source = policy.m_Source;
    packet.m_RequestId = policy.m_RequestId;
    packet.m_Reason = policy.m_Reason;
    packet.m_ProviderRecoveryAction = policy.m_Action;
    packet.m_ResumeAction = wxS( "resume_from_checkpoint_or_journal" );
    packet.m_RecoveryBasisJson = fromUtf8String( recoveryBasis.dump() );

    nlohmann::json resume = {
        { "schema",
          { { "name", "kisurf.ai.provider_recovery_resume" },
            { "version", 1 } } },
        { "artifact_uri", toUtf8String( packet.m_ArtifactUri ) },
        { "agent_kind", toUtf8String( packet.m_AgentKind ) },
        { "source", toUtf8String( packet.m_Source ) },
        { "request_id", packet.m_RequestId },
        { "reason", toUtf8String( packet.m_Reason ) },
        { "provider_recovery_action",
          toUtf8String( packet.m_ProviderRecoveryAction ) },
        { "resume_action", toUtf8String( packet.m_ResumeAction ) },
        { "replay_policy", "do_not_blindly_reexecute_tools" },
        { "blind_tool_replay_allowed", false },
        { "checkpoint_or_journal_resume_required", true },
        { "recovery_basis", std::move( recoveryBasis ) }
    };

    packet.m_ResumePacketJson = fromUtf8String( resume.dump() );
    aError.clear();
    return packet;
}


AI_PROVIDER_RECOVERY_RESUME_PLAN AiBuildLatestProviderRecoveryResumePlan(
        const AI_ARTIFACT_STORE& aStore,
        const AI_PROVIDER_RECOVERY_QUERY& aQuery,
        wxString& aError )
{
    AI_PROVIDER_RECOVERY_RESUME_PLAN plan;
    AI_PROVIDER_RECOVERY_RESUME_PACKET packet =
            AiBuildLatestProviderRecoveryResumePacket( aStore, aQuery, aError );

    if( !packet.m_Available || !aError.IsEmpty() )
        return plan;

    nlohmann::json recoveryBasis =
            parseObjectOrEmpty( packet.m_RecoveryBasisJson );
    nlohmann::json replayCandidates = nlohmann::json::array();

    if( recoveryBasis.contains( "tool_results" )
        && recoveryBasis["tool_results"].is_array() )
    {
        for( const nlohmann::json& tool : recoveryBasis["tool_results"] )
        {
            if( !tool.is_object() )
                continue;

            nlohmann::json candidate = nlohmann::json::object();

            for( const char* key : { "tool_call_id", "tool_name", "session_id" } )
            {
                if( tool.contains( key ) )
                    candidate[key] = tool[key];
            }

            for( const char* key : { "checkpoint_id", "rollback_checkpoint_id" } )
            {
                if( tool.contains( key ) )
                    candidate[key] = tool[key];
            }

            bool hasReplayBasis = candidate.contains( "checkpoint_id" )
                                  || candidate.contains( "rollback_checkpoint_id" );

            if( tool.contains( "session_journal" )
                && tool["session_journal"].is_object() )
            {
                candidate["session_journal"] = tool["session_journal"];
                hasReplayBasis = true;

                if( tool["session_journal"].contains( "operations" )
                    && tool["session_journal"]["operations"].is_array() )
                {
                    candidate["journal_operation_count"] =
                            tool["session_journal"]["operations"].size();
                }
            }

            if( tool.contains( "attempt_session_journal" )
                && tool["attempt_session_journal"].is_object() )
            {
                candidate["attempt_session_journal"] =
                        tool["attempt_session_journal"];
                hasReplayBasis = true;

                if( tool["attempt_session_journal"].contains( "operations" )
                    && tool["attempt_session_journal"]["operations"].is_array() )
                {
                    candidate["attempt_journal_operation_count"] =
                            tool["attempt_session_journal"]["operations"].size();
                }
            }

            if( hasReplayBasis )
            {
                candidate["candidate_action"] =
                        "review_then_replay_checkpoint_or_journal";
                candidate["blind_replay_allowed"] = false;
                replayCandidates.push_back( std::move( candidate ) );
            }
        }
    }

    plan.m_Available = true;
    plan.m_BlindToolReplayAllowed = false;
    plan.m_UserReviewRequired = true;
    plan.m_CheckpointOrJournalReplayRequired = true;
    plan.m_ArtifactUri = packet.m_ArtifactUri;
    plan.m_AgentKind = packet.m_AgentKind;
    plan.m_Source = packet.m_Source;
    plan.m_RequestId = packet.m_RequestId;
    plan.m_ResumeAction = packet.m_ResumeAction;
    plan.m_RecoveryBasisJson = packet.m_RecoveryBasisJson;

    nlohmann::json resumePacket = parseObjectOrEmpty( packet.m_ResumePacketJson );
    nlohmann::json planJson = {
        { "schema",
          { { "name", "kisurf.ai.provider_recovery_plan" },
            { "version", 1 } } },
        { "status", "ready_for_user_review" },
        { "artifact_uri", toUtf8String( plan.m_ArtifactUri ) },
        { "agent_kind", toUtf8String( plan.m_AgentKind ) },
        { "source", toUtf8String( plan.m_Source ) },
        { "request_id", plan.m_RequestId },
        { "resume_action", toUtf8String( plan.m_ResumeAction ) },
        { "blind_tool_replay_allowed", false },
        { "user_review_required", true },
        { "checkpoint_or_journal_replay_required", true },
        { "required_preflight_checks",
          nlohmann::json::array( {
                  "verify_live_document_version_matches_recovery_basis",
                  "verify_checkpoint_or_journal_is_available",
                  "review_effectful_tool_results_before_replay" } ) },
        { "resume_packet", std::move( resumePacket ) },
        { "recovery_basis", std::move( recoveryBasis ) },
        { "replay_candidates", std::move( replayCandidates ) } };

    plan.m_PlanJson = fromUtf8String( planJson.dump() );
    aError.clear();
    return plan;
}


AI_PROVIDER_RECOVERY_PREFLIGHT_RESULT AiPreflightProviderRecoveryResumePlan(
        const AI_PROVIDER_RECOVERY_RESUME_PLAN& aPlan,
        const AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT& aContext )
{
    AI_PROVIDER_RECOVERY_PREFLIGHT_RESULT result;
    result.m_BlindToolReplayAllowed = false;
    result.m_CheckpointOrJournalReplayRequired = true;
    result.m_CurrentDocumentRevision = aContext.m_DocumentRevision;

    const nlohmann::json planJson = parseObjectOrEmpty( aPlan.m_PlanJson );
    nlohmann::json recoveryBasis = parseObjectOrEmpty( aPlan.m_RecoveryBasisJson );

    if( recoveryBasis.empty()
        && planJson.contains( "recovery_basis" )
        && planJson["recovery_basis"].is_object() )
    {
        recoveryBasis = planJson["recovery_basis"];
    }

    result.m_ExpectedDocumentRevision =
            boardStateDocumentRevision( recoveryBasis );
    result.m_ReplayCandidateCount = replayCandidateCount( planJson );

    if( !aPlan.m_Available || planJson.empty() || recoveryBasis.empty() )
    {
        result.m_Reason = wxS( "invalid_recovery_plan" );
    }
    else if( jsonBoolValue( planJson, "blind_tool_replay_allowed", false ) )
    {
        result.m_Reason = wxS( "blind_replay_not_allowed" );
    }
    else if( result.m_ExpectedDocumentRevision != 0
             && result.m_CurrentDocumentRevision != 0
             && result.m_ExpectedDocumentRevision != result.m_CurrentDocumentRevision )
    {
        result.m_Reason = wxS( "document_revision_mismatch" );
    }
    else if( result.m_ReplayCandidateCount == 0 )
    {
        result.m_Reason = wxS( "missing_checkpoint_or_journal_candidate" );
    }
    else
    {
        result.m_Allowed = true;
        result.m_Reason = wxS( "ready_for_replay_review" );
    }

    nlohmann::json resultJson = {
        { "schema",
          { { "name", "kisurf.ai.provider_recovery_preflight" },
            { "version", 1 } } },
        { "allowed", result.m_Allowed },
        { "reason", toUtf8String( result.m_Reason ) },
        { "artifact_uri", toUtf8String( aPlan.m_ArtifactUri ) },
        { "agent_kind", toUtf8String( aPlan.m_AgentKind ) },
        { "source", toUtf8String( aPlan.m_Source ) },
        { "request_id", aPlan.m_RequestId },
        { "resume_action", toUtf8String( aPlan.m_ResumeAction ) },
        { "blind_tool_replay_allowed", false },
        { "checkpoint_or_journal_replay_required", true },
        { "expected_document_revision", result.m_ExpectedDocumentRevision },
        { "current_document_revision", result.m_CurrentDocumentRevision },
        { "replay_candidate_count", result.m_ReplayCandidateCount } };

    if( planJson.contains( "required_preflight_checks" )
        && planJson["required_preflight_checks"].is_array() )
    {
        resultJson["required_preflight_checks"] =
                planJson["required_preflight_checks"];
    }

    if( planJson.contains( "replay_candidates" )
        && planJson["replay_candidates"].is_array() )
    {
        resultJson["replay_candidates"] = planJson["replay_candidates"];
    }

    result.m_ResultJson = fromUtf8String( resultJson.dump() );
    return result;
}


AI_PROVIDER_RECOVERY_REPLAY_REQUEST AiBuildProviderRecoveryReplayRequest(
        const AI_PROVIDER_RECOVERY_RESUME_PLAN& aPlan,
        const AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT& aContext )
{
    AI_PROVIDER_RECOVERY_REPLAY_REQUEST request;
    request.m_BlindToolReplayAllowed = false;
    request.m_UserReviewRequired = true;
    request.m_CheckpointOrJournalReplayRequired = true;
    request.m_ArtifactUri = aPlan.m_ArtifactUri;
    request.m_AgentKind = aPlan.m_AgentKind;
    request.m_Source = aPlan.m_Source;
    request.m_RequestId = aPlan.m_RequestId;

    AI_PROVIDER_RECOVERY_PREFLIGHT_RESULT preflight =
            AiPreflightProviderRecoveryResumePlan( aPlan, aContext );

    request.m_Allowed = preflight.m_Allowed;
    request.m_ExpectedDocumentRevision = preflight.m_ExpectedDocumentRevision;
    request.m_CurrentDocumentRevision = preflight.m_CurrentDocumentRevision;

    const nlohmann::json planJson = parseObjectOrEmpty( aPlan.m_PlanJson );
    const nlohmann::json preflightJson =
            parseObjectOrEmpty( preflight.m_ResultJson );

    nlohmann::json requestJson = {
        { "schema",
          { { "name", "kisurf.ai.provider_recovery_replay_request" },
            { "version", 1 } } },
        { "status",
          preflight.m_Allowed ? "ready_for_user_review"
                              : "blocked_by_preflight" },
        { "allowed", preflight.m_Allowed },
        { "reason",
          preflight.m_Allowed ? "ready_for_user_review"
                              : toUtf8String( preflight.m_Reason ) },
        { "artifact_uri", toUtf8String( request.m_ArtifactUri ) },
        { "agent_kind", toUtf8String( request.m_AgentKind ) },
        { "source", toUtf8String( request.m_Source ) },
        { "request_id", request.m_RequestId },
        { "blind_tool_replay_allowed", false },
        { "user_review_required", true },
        { "checkpoint_or_journal_replay_required", true },
        { "expected_document_revision",
          request.m_ExpectedDocumentRevision },
        { "current_document_revision",
          request.m_CurrentDocumentRevision },
        { "preflight_result", preflightJson } };

    if( !preflight.m_Allowed )
    {
        request.m_Reason = preflight.m_Reason;
        request.m_RequestJson = fromUtf8String( requestJson.dump() );
        return request;
    }

    const nlohmann::json candidate = firstReplayCandidateOrEmpty( planJson );

    if( candidate.empty() )
    {
        request.m_Reason = wxS( "missing_checkpoint_or_journal_candidate" );
        requestJson["status"] = "blocked_by_missing_candidate";
        requestJson["allowed"] = false;
        requestJson["reason"] = "missing_checkpoint_or_journal_candidate";
        request.m_RequestJson = fromUtf8String( requestJson.dump() );
        return request;
    }

    request.m_Available = true;
    request.m_Allowed = true;
    request.m_Reason = wxS( "ready_for_user_review" );
    request.m_ToolCallId = jsonStringOrEmpty( candidate, "tool_call_id" );
    request.m_ToolName = jsonStringOrEmpty( candidate, "tool_name" );
    request.m_SessionId = jsonStringOrEmpty( candidate, "session_id" );
    request.m_CheckpointId = jsonUint64Value( candidate, "checkpoint_id" );
    request.m_RollbackCheckpointId =
            jsonUint64Value( candidate, "rollback_checkpoint_id" );
    request.m_CandidateJson = fromUtf8String( candidate.dump() );

    nlohmann::json journal = nlohmann::json::object();

    if( candidate.contains( "session_journal" )
        && candidate["session_journal"].is_object() )
    {
        journal = candidate["session_journal"];
        request.m_ReplaySource = wxS( "session_journal" );
    }
    else if( candidate.contains( "attempt_session_journal" )
             && candidate["attempt_session_journal"].is_object() )
    {
        journal = candidate["attempt_session_journal"];
        request.m_ReplaySource = wxS( "attempt_session_journal" );
    }
    else
    {
        request.m_ReplaySource = wxS( "checkpoint_only" );
    }

    request.m_JournalOperationCount = journalOperationCount( journal );

    if( !journal.empty() )
        request.m_JournalJson = fromUtf8String( journal.dump() );

    requestJson["selected_candidate"] = candidate;
    requestJson["candidate_action"] =
            "review_then_replay_checkpoint_or_journal";
    requestJson["replay_source"] = toUtf8String( request.m_ReplaySource );
    requestJson["tool_call_id"] = toUtf8String( request.m_ToolCallId );
    requestJson["tool_name"] = toUtf8String( request.m_ToolName );
    requestJson["session_id"] = toUtf8String( request.m_SessionId );
    requestJson["checkpoint_id"] = request.m_CheckpointId;
    requestJson["rollback_checkpoint_id"] = request.m_RollbackCheckpointId;
    requestJson["journal_operation_count"] =
            request.m_JournalOperationCount;

    if( !journal.empty() )
        requestJson["journal"] = journal;

    request.m_RequestJson = fromUtf8String( requestJson.dump() );
    return request;
}


AI_PROVIDER_RECOVERY_EPISODE AiBuildProviderRecoveryEpisode(
        const AI_PROVIDER_RECOVERY_RESUME_PLAN& aPlan,
        const AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT& aContext )
{
    AI_PROVIDER_RECOVERY_EPISODE episode;
    episode.m_Available = aPlan.m_Available;
    episode.m_UserReviewRequired = true;
    episode.m_AutomaticExecutionAllowed = false;
    episode.m_BlindToolReplayAllowed = false;
    episode.m_CheckpointOrJournalReplayRequired =
            aPlan.m_CheckpointOrJournalReplayRequired;
    episode.m_ArtifactUri = aPlan.m_ArtifactUri;
    episode.m_AgentKind = aPlan.m_AgentKind;
    episode.m_Source = aPlan.m_Source;
    episode.m_RequestId = aPlan.m_RequestId;

    AI_PROVIDER_RECOVERY_PREFLIGHT_RESULT preflight =
            AiPreflightProviderRecoveryResumePlan( aPlan, aContext );

    episode.m_PreflightAllowed = preflight.m_Allowed;

    AI_PROVIDER_RECOVERY_REPLAY_REQUEST replay;

    if( preflight.m_Allowed )
    {
        replay = AiBuildProviderRecoveryReplayRequest( aPlan, aContext );
        episode.m_ReplayRequestAvailable = replay.m_Available
                                           && replay.m_Allowed;
        episode.m_ReadyForUserReview = episode.m_ReplayRequestAvailable
                                       && replay.m_UserReviewRequired;
        episode.m_Status = episode.m_ReadyForUserReview
                                   ? wxString( wxS( "ready_for_user_review" ) )
                                   : wxString( wxS( "blocked_by_replay_request" ) );
        episode.m_Reason = replay.m_Reason;
    }
    else
    {
        episode.m_Status = wxS( "blocked_by_preflight" );
        episode.m_Reason = preflight.m_Reason;
    }

    if( !aPlan.m_Available )
    {
        episode.m_Status = wxS( "unavailable" );
        episode.m_Reason = wxS( "recovery_plan_unavailable" );
    }
    else if( episode.m_Reason.IsEmpty() )
    {
        episode.m_Reason = episode.m_Status;
    }

    nlohmann::json episodeJson = {
        { "schema",
          { { "name", "kisurf.ai.provider_recovery_episode" },
            { "version", 1 } } },
        { "status", toUtf8String( episode.m_Status ) },
        { "reason", toUtf8String( episode.m_Reason ) },
        { "available", episode.m_Available },
        { "artifact_uri", toUtf8String( episode.m_ArtifactUri ) },
        { "agent_kind", toUtf8String( episode.m_AgentKind ) },
        { "source", toUtf8String( episode.m_Source ) },
        { "request_id", episode.m_RequestId },
        { "preflight_allowed", episode.m_PreflightAllowed },
        { "replay_request_available", episode.m_ReplayRequestAvailable },
        { "ready_for_user_review", episode.m_ReadyForUserReview },
        { "user_review_required", true },
        { "automatic_execution_allowed", false },
        { "blind_tool_replay_allowed", false },
        { "checkpoint_or_journal_replay_required",
          episode.m_CheckpointOrJournalReplayRequired },
        { "preflight_result",
          parseObjectOrEmpty( preflight.m_ResultJson ) } };

    if( !replay.m_RequestJson.IsEmpty() )
        episodeJson["replay_request"] = parseObjectOrEmpty( replay.m_RequestJson );

    episode.m_EpisodeJson = fromUtf8String( episodeJson.dump() );
    return episode;
}


AI_PROVIDER_RECOVERY_REPLAY_EXECUTION_RESULT
AiExecuteProviderRecoveryReplayRequest(
        const AI_PROVIDER_RECOVERY_REPLAY_REQUEST& aRequest,
        const AI_PROVIDER_RECOVERY_REPLAY_EXECUTION_OPTIONS& aOptions,
        AI_ACCEPT_APPLY_ADAPTER& aAdapter )
{
    if( !aRequest.m_Available || !aRequest.m_Allowed )
    {
        const wxString message =
                aRequest.m_Reason.IsEmpty()
                        ? wxString( wxS( "Provider recovery replay request is not allowed." ) )
                        : aRequest.m_Reason;

        return recoveryReplayExecutionResult(
                aRequest, aOptions, wxS( "blocked" ), false, false, true,
                wxS( "replay_request_not_allowed" ), message, 0 );
    }

    if( aRequest.m_BlindToolReplayAllowed )
    {
        return recoveryReplayExecutionResult(
                aRequest, aOptions, wxS( "blocked" ), false, false, true,
                wxS( "blind_replay_not_allowed" ),
                wxS( "Provider recovery cannot blindly replay tool calls." ), 0 );
    }

    if( !aOptions.m_UserReviewed )
    {
        return recoveryReplayExecutionResult(
                aRequest, aOptions, wxS( "blocked" ), false, false, true,
                wxS( "user_review_required" ),
                wxS( "Provider recovery replay requires explicit user review." ),
                0 );
    }

    if( aRequest.m_JournalJson.IsEmpty()
        || aRequest.m_JournalOperationCount == 0 )
    {
        return recoveryReplayExecutionResult(
                aRequest, aOptions, wxS( "blocked" ), false, false, false,
                wxS( "missing_session_journal" ),
                wxS( "Provider recovery replay request has no session journal." ),
                0 );
    }

    AI_CONTEXT_VERSION contextVersion;
    contextVersion.m_DocumentRevision = aRequest.m_CurrentDocumentRevision;

    AI_EXECUTION_SESSION::OPEN_OPTIONS options;
    const wxString replayBaseHash = wxS( "provider-recovery-replay" );
    options.m_SessionId = 1;
    options.m_BoardId = aRequest.m_ArtifactUri.IsEmpty()
                                ? wxString( wxS( "provider-recovery" ) )
                                : aRequest.m_ArtifactUri;
    options.m_BaseHash = replayBaseHash;
    options.m_EditorKind = AI_EDITOR_KIND::Pcb;
    options.m_ContextVersion = contextVersion;

    AI_EXECUTION_SESSION session( std::move( options ) );

    wxString journalError;

    if( !appendReplayJournalOperations( aRequest, session, journalError ) )
    {
        const wxString errorCode =
                journalError.IsEmpty() ? wxString( wxS( "invalid_session_journal" ) )
                                       : journalError;

        return recoveryReplayExecutionResult(
                aRequest, aOptions, wxS( "blocked" ), false, false, false,
                errorCode,
                wxS( "Provider recovery session journal cannot be replayed." ),
                0 );
    }

    AI_ACCEPT_APPLY_RESULT applyResult = AI_ACCEPT_APPLIER::Apply(
            session, replayBaseHash, contextVersion, aAdapter );

    if( !applyResult.m_Ok )
    {
        const wxString errorCode =
                applyResult.m_ErrorCode.IsEmpty()
                        ? wxString( wxS( "replay_apply_failed" ) )
                        : applyResult.m_ErrorCode;
        const wxString message =
                applyResult.m_Message.IsEmpty()
                        ? wxString( wxS( "Provider recovery replay adapter failed." ) )
                        : applyResult.m_Message;

        return recoveryReplayExecutionResult(
                aRequest, aOptions, wxS( "blocked" ), false, false, false,
                errorCode, message, applyResult.m_AppliedOperationCount );
    }

    const wxString message =
            applyResult.m_Message.IsEmpty()
                    ? wxString( wxS( "Provider recovery journal replayed." ) )
                    : applyResult.m_Message;

    return recoveryReplayExecutionResult(
            aRequest, aOptions, wxS( "applied" ), true,
            applyResult.m_BoardMutated, false, wxString(),
            message, applyResult.m_AppliedOperationCount );
}


bool AiStoreVisualObservationArtifact(
        const wxString& aProjectId,
        const wxString& aDocumentId,
        const wxString& aAgentKind,
        const wxString& aRetentionClass,
        const AI_VISUAL_OBSERVATION_ARTIFACT& aVisual,
        AI_ARTIFACT_STORE& aStore,
        AI_ARTIFACT_RECORD& aRecord,
        wxString& aError )
{
    const AI_VISUAL_SNAPSHOT& snapshot = aVisual.m_Snapshot;
    const nlohmann::json sidecar = parseObjectOrEmpty( aVisual.m_SidecarJson );
    const wxString frameId = jsonStringOrEmpty( sidecar, "frame_id" );
    const wxString frameKind = jsonStringOrEmpty( sidecar, "frame_kind" );
    const wxString attemptId = jsonStringOrEmpty( sidecar, "attempt_id" );
    const wxString previewId = jsonStringOrEmpty( sidecar, "preview_id" );

    nlohmann::json payload = {
        { "schema",
          { { "name", "kisurf.ai.visual_observation_payload" },
            { "version", 1 } } },
        { "snapshot",
          { { "source", toUtf8String( snapshot.m_Source ) },
            { "mime_type", toUtf8String( snapshot.m_MimeType ) },
            { "data_uri", toUtf8String( snapshot.m_DataUri ) },
            { "frame_id", toUtf8String( snapshot.m_FrameId ) },
            { "frame_kind", toUtf8String( snapshot.m_FrameKind ) },
            { "width_px", snapshot.m_WidthPx },
            { "height_px", snapshot.m_HeightPx },
            { "byte_size", snapshot.m_ByteSize },
            { "unavailable_reason",
              toUtf8String( snapshot.m_UnavailableReason ) } } },
        { "visual_observation_artifact", sidecar }
    };

    aRecord = AI_ARTIFACT_RECORD();
    aRecord.m_Kind = wxS( "visual_observation" );
    aRecord.m_ProjectId = aProjectId;
    aRecord.m_DocumentId = aDocumentId;
    aRecord.m_AgentKind = aAgentKind;
    aRecord.m_Source = snapshot.m_Source;
    aRecord.m_MimeType = wxS( "application/json" );
    aRecord.m_RetentionClass =
            aRetentionClass.IsEmpty() ? wxString( wxS( "trace" ) ) : aRetentionClass;
    aRecord.m_Summary = frameId.IsEmpty()
                                ? wxString( wxS( "Archived visual observation" ) )
                                : wxString::Format(
                                          wxS( "Archived visual observation %s" ),
                                          frameId );

    nlohmann::json metadata = {
        { "frame_id", toUtf8String( frameId ) },
        { "frame_kind", toUtf8String( frameKind ) },
        { "attempt_id", toUtf8String( attemptId ) },
        { "preview_id", toUtf8String( previewId ) },
        { "source", toUtf8String( snapshot.m_Source ) },
        { "has_pixels", snapshot.HasPixels() },
        { "width_px", snapshot.m_WidthPx },
        { "height_px", snapshot.m_HeightPx },
        { "byte_size", snapshot.m_ByteSize }
    };

    aRecord.m_MetadataJson = fromUtf8String( metadata.dump() );

    return aStore.StorePayload( aRecord, fromUtf8String( payload.dump() ), aError );
}


bool AiStoreValidationReportArtifact(
        const wxString& aProjectId,
        const wxString& aDocumentId,
        const wxString& aAgentKind,
        const wxString& aRetentionClass,
        const wxString& aToolCallId,
        const wxString& aToolName,
        const wxString& aValidationJson,
        AI_ARTIFACT_STORE& aStore,
        AI_ARTIFACT_RECORD& aRecord,
        wxString& aError )
{
    nlohmann::json payload = parseObjectOrEmpty( aValidationJson );
    nlohmann::json validation = nlohmann::json::object();

    if( payload.contains( "validation" ) && payload["validation"].is_object() )
        validation = payload["validation"];

    aRecord = AI_ARTIFACT_RECORD();
    aRecord.m_Kind = wxS( "validation_report" );
    aRecord.m_ProjectId = aProjectId;
    aRecord.m_DocumentId = aDocumentId;
    aRecord.m_AgentKind = aAgentKind;
    aRecord.m_Source = aToolName;
    aRecord.m_MimeType = wxS( "application/json" );
    aRecord.m_RetentionClass =
            aRetentionClass.IsEmpty() ? wxString( wxS( "trace" ) ) : aRetentionClass;
    aRecord.m_Summary = wxString::Format( wxS( "Archived validation report for %s" ),
                                          aToolName );

    nlohmann::json metadata = {
        { "tool_call_id", toUtf8String( aToolCallId ) },
        { "tool_name", toUtf8String( aToolName ) }
    };

    if( payload.contains( "status" ) && payload["status"].is_string() )
        metadata["status"] = payload["status"];

    if( validation.contains( "backend" ) && validation["backend"].is_string() )
        metadata["backend"] = validation["backend"];

    if( validation.contains( "scope" ) && validation["scope"].is_string() )
        metadata["scope"] = validation["scope"];

    if( validation.contains( "grade" ) && validation["grade"].is_string() )
        metadata["grade"] = validation["grade"];

    if( validation.contains( "issue_count" )
        && validation["issue_count"].is_number_unsigned() )
    {
        metadata["issue_count"] = validation["issue_count"];
    }

    aRecord.m_MetadataJson = fromUtf8String( metadata.dump() );

    return aStore.StorePayload( aRecord, aValidationJson, aError );
}


bool AiExportArtifactSummariesToLocalTextIndex(
        const AI_ARTIFACT_STORE& aStore,
        const AI_ARTIFACT_QUERY& aQuery,
        AI_LOCAL_TEXT_MEMORY_INDEX& aIndex,
        wxString& aError )
{
    std::vector<AI_ARTIFACT_RECORD> artifacts = aStore.Query( aQuery, aError );

    if( !aError.IsEmpty() )
        return false;

    for( const AI_ARTIFACT_RECORD& artifact : artifacts )
    {
        AI_LOCAL_TEXT_MEMORY_RECORD record;
        record.m_Id = artifact.m_Uri;
        record.m_ProjectId = artifact.m_ProjectId;
        record.m_DocumentId = artifact.m_DocumentId;
        record.m_AgentKind = artifact.m_AgentKind;
        record.m_Type = artifact.m_Kind;
        record.m_Text = artifactSummaryText( artifact );
        record.m_Source = artifact.m_Source;
        record.m_ProvenanceJson = artifactSummaryProvenanceJson( artifact );
        record.m_AcceptanceState = artifact.m_RetentionClass;
        record.m_TrustLevel = 60;
        record.m_Sequence = artifact.m_Sequence;
        record.m_CreatedAtUnixSeconds = artifact.m_CreatedAtUnixSeconds;
        record.m_ExpiresAtUnixSeconds = artifact.m_ExpiresAtUnixSeconds;

        aIndex.AddRecord( std::move( record ) );
    }

    aError.clear();
    return true;
}
