#include <kisurf/ai/ai_runtime.h>

#include <kisurf/ai/ai_artifact_store.h>
#include <kisurf/ai/ai_provider_input_compiler.h>
#include <kisurf/ai/ai_prompt_trace_store.h>
#include <kisurf/ai/ai_visual_snapshot.h>

#include <nlohmann/json.hpp>
#include <iterator>
#include <string>
#include <utility>

namespace
{
std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


wxString deniedToolResultJson( const AI_TOOL_INVOCATION_RESULT& aResult )
{
    nlohmann::json payload = { { "action", toUtf8String( aResult.m_ActionName ) },
                               { "allowed", aResult.m_Allowed },
                               { "executed", aResult.m_Executed },
                               { "dry_run", false },
                               { "status", "denied" },
                               { "error_code", toUtf8String( aResult.m_ErrorCode ) },
                               { "message", toUtf8String( aResult.m_Message ) } };

    return wxString::FromUTF8( payload.dump().c_str() );
}


void recordModelToolCall( AI_ACTIVITY_LOG& aActivityLog, const AI_PROVIDER_REQUEST& aRequest,
                          const AI_TOOL_CALL_RECORD& aToolCall )
{
    AI_ACTIVITY_RECORD record;
    record.m_RequestId = aRequest.m_RequestId;
    record.m_ToolCallId = aToolCall.m_ToolCallId;
    record.m_Kind = AI_ACTIVITY_KIND::ModelToolRequest;
    record.m_EditorKind = aRequest.m_EditorKind;
    record.m_ActionName = aToolCall.m_ToolName;
    record.m_ArgumentsJson = aToolCall.m_ArgumentsJson;
    record.m_Message = wxS( "Provider tool call requested." );
    aActivityLog.Append( record );
}


void copyToolResult( AI_TOOL_CALL_RECORD& aToolCall,
                     const AI_TOOL_INVOCATION_RESULT& aResult )
{
    aToolCall.m_Allowed = aResult.m_Allowed;
    aToolCall.m_Executed = aResult.m_Executed;
    aToolCall.m_ErrorCode = aResult.m_ErrorCode;
    aToolCall.m_Message = aResult.m_Message;
    aToolCall.m_ResultJson = aResult.m_ResultJson;
}


void recordToolResult( AI_ACTIVITY_LOG& aActivityLog,
                       const AI_TOOL_INVOCATION_RESULT& aResult )
{
    AI_ACTIVITY_RECORD record;
    record.m_RequestId = aResult.m_RequestId;
    record.m_ToolCallId = aResult.m_ToolCallId;
    record.m_Kind = AI_ACTIVITY_KIND::ToolResult;
    record.m_ActionName = aResult.m_ActionName;
    record.m_ResultJson = aResult.m_ResultJson;
    record.m_ErrorCode = aResult.m_ErrorCode;
    record.m_Allowed = aResult.m_Allowed;
    record.m_Executed = aResult.m_Executed;
    record.m_Message = aResult.m_Message;
    aActivityLog.Append( record );
}


wxString fromUtf8String( const std::string& aText )
{
    return wxString::FromUTF8( aText.c_str() );
}


std::string jsonStringValue( const nlohmann::json& aJson, const char* aKey )
{
    if( !aJson.is_object() || !aJson.contains( aKey ) || !aJson[aKey].is_string() )
        return {};

    return aJson[aKey].get<std::string>();
}


void recordPythonWorkerEvents( AI_ACTIVITY_LOG& aActivityLog,
                               const AI_TOOL_INVOCATION_RESULT& aResult )
{
    if( aResult.m_ResultJson.IsEmpty() )
        return;

    nlohmann::json payload = nlohmann::json::parse( toUtf8String( aResult.m_ResultJson ),
                                                    nullptr, false );

    if( payload.is_discarded() || !payload.is_object()
        || !payload.contains( "recorded_events" )
        || !payload["recorded_events"].is_array() )
    {
        return;
    }

    for( const nlohmann::json& event : payload["recorded_events"] )
    {
        if( !event.is_object() )
            continue;

        const std::string eventKind = jsonStringValue( event, "kind" );
        const std::string actionSuffix = eventKind.empty() ? "event" : eventKind;

        AI_ACTIVITY_RECORD record;
        record.m_RequestId = aResult.m_RequestId;
        record.m_ToolCallId = aResult.m_ToolCallId;
        record.m_Kind = AI_ACTIVITY_KIND::ToolResult;
        record.m_ActionName = aResult.m_ActionName;

        if( record.m_ActionName.IsEmpty() )
            record.m_ActionName = wxS( "python" );

        record.m_ActionName << wxS( "." ) << fromUtf8String( actionSuffix );
        record.m_ResultJson = fromUtf8String( event.dump() );
        record.m_ErrorCode = aResult.m_ErrorCode;
        record.m_Allowed = aResult.m_Allowed;
        record.m_Executed = aResult.m_Executed;
        record.m_Message = fromUtf8String( jsonStringValue( event, "message" ) );
        aActivityLog.Append( record );
    }
}


bool sameToolCallIdentity( const AI_TOOL_CALL_RECORD& aFirst,
                           const AI_TOOL_CALL_RECORD& aSecond )
{
    return !aFirst.m_ToolCallId.IsEmpty()
           && aFirst.m_ToolCallId == aSecond.m_ToolCallId
           && aFirst.m_ToolName == aSecond.m_ToolName
           && aFirst.m_ArgumentsJson == aSecond.m_ArgumentsJson;
}


bool repeatsHandledToolCall( const std::vector<AI_TOOL_CALL_RECORD>& aRoundToolCalls,
                             const std::vector<AI_TOOL_CALL_RECORD>& aHandledToolCalls )
{
    for( const AI_TOOL_CALL_RECORD& roundCall : aRoundToolCalls )
    {
        for( const AI_TOOL_CALL_RECORD& handledCall : aHandledToolCalls )
        {
            if( sameToolCallIdentity( roundCall, handledCall ) )
                return true;
        }
    }

    return false;
}


wxString promptTraceStatusForResponse( const AI_PROVIDER_RESPONSE& aResponse )
{
    if( aResponse.m_Title.CmpNoCase( wxS( "AI Provider Error" ) ) == 0 )
        return wxS( "provider_error" );

    return wxS( "provider_response" );
}


bool isScriptOutputTool( const wxString& aToolName )
{
    return aToolName == wxS( "kisurf_run_cell" )
           || aToolName == wxS( "script_run_bounded_plan" );
}


bool hasExecutedToolCall( const std::vector<AI_TOOL_CALL_RECORD>& aToolCalls )
{
    for( const AI_TOOL_CALL_RECORD& toolCall : aToolCalls )
    {
        if( toolCall.m_Executed )
            return true;
    }

    return false;
}


nlohmann::json executedToolCallRefsJson( const std::vector<AI_TOOL_CALL_RECORD>& aToolCalls )
{
    nlohmann::json refs = nlohmann::json::array();

    for( const AI_TOOL_CALL_RECORD& toolCall : aToolCalls )
    {
        if( !toolCall.m_Executed )
            continue;

        refs.push_back( {
                { "tool_call_id", toUtf8String( toolCall.m_ToolCallId ) },
                { "tool_name", toUtf8String( toolCall.m_ToolName ) },
                { "allowed", toolCall.m_Allowed },
                { "executed", toolCall.m_Executed },
                { "error_code", toUtf8String( toolCall.m_ErrorCode ) }
        } );
    }

    return refs;
}


void copyIfPresent( nlohmann::json& aTarget, const nlohmann::json& aSource,
                    const char* aKey )
{
    if( aSource.contains( aKey ) )
        aTarget[aKey] = aSource[aKey];
}


nlohmann::json contextVersionJson( const AI_CONTEXT_VERSION& aVersion )
{
    return {
        { "document_revision", aVersion.m_DocumentRevision },
        { "selection_revision", aVersion.m_SelectionRevision },
        { "view_revision", aVersion.m_ViewRevision }
    };
}


nlohmann::json recoveryBasisJson( const AI_PROVIDER_REQUEST& aRequest,
                                  const std::vector<AI_TOOL_CALL_RECORD>& aToolCalls )
{
    nlohmann::json basis = {
        { "requires_checkpoint_resume", true },
        { "executed_tool_result_count", 0 },
        { "board_state_version", contextVersionJson( aRequest.m_ContextVersion ) },
        { "tool_results", nlohmann::json::array() }
    };

    size_t executedCount = 0;

    for( const AI_TOOL_CALL_RECORD& toolCall : aToolCalls )
    {
        if( !toolCall.m_Executed )
            continue;

        ++executedCount;

        nlohmann::json tool = {
            { "tool_call_id", toUtf8String( toolCall.m_ToolCallId ) },
            { "tool_name", toUtf8String( toolCall.m_ToolName ) },
            { "allowed", toolCall.m_Allowed },
            { "executed", toolCall.m_Executed }
        };

        nlohmann::json result = nlohmann::json::parse(
                toUtf8String( toolCall.m_ResultJson ), nullptr, false );

        if( result.is_object() )
        {
            copyIfPresent( tool, result, "session_id" );
            copyIfPresent( tool, result, "hidden_session_id" );
            copyIfPresent( tool, result, "checkpoint_id" );
            copyIfPresent( tool, result, "rollback_checkpoint_id" );
            copyIfPresent( tool, result, "preview_id" );
            copyIfPresent( tool, result, "attempt_id" );

            if( result.contains( "session_journal" )
                && result["session_journal"].is_object()
                && result["session_journal"].contains( "operations" )
                && result["session_journal"]["operations"].is_array() )
            {
                tool["session_journal"] = result["session_journal"];
                tool["journal_operation_count"] =
                        result["session_journal"]["operations"].size();
            }

            if( result.contains( "attempt_session_journal" )
                && result["attempt_session_journal"].is_object()
                && result["attempt_session_journal"].contains( "operations" )
                && result["attempt_session_journal"]["operations"].is_array() )
            {
                tool["attempt_session_journal"] = result["attempt_session_journal"];
                tool["attempt_journal_operation_count"] =
                        result["attempt_session_journal"]["operations"].size();
            }
        }

        basis["tool_results"].push_back( std::move( tool ) );
    }

    basis["executed_tool_result_count"] = executedCount;
    return basis;
}


void markPostSideEffectProviderFailure(
        AI_PROVIDER_RESPONSE& aResponse,
        const AI_PROVIDER_REQUEST& aRequest,
        const std::vector<AI_TOOL_CALL_RECORD>& aHandledToolCalls )
{
    if( promptTraceStatusForResponse( aResponse ) != wxS( "provider_error" )
        || !hasExecutedToolCall( aHandledToolCalls ) )
    {
        return;
    }

    nlohmann::json trace = nlohmann::json::parse(
            toUtf8String( aResponse.m_ProviderTraceJson ), nullptr, false );

    if( trace.is_discarded() || !trace.is_object() )
    {
        trace = nlohmann::json::object();
        trace["schema"] = { { "name", "kisurf.ai.provider_trace" },
                            { "version", 1 } };
    }

    trace["runtime_guard"] = {
        { "reason", "post_side_effect_ambiguity" },
        { "action", "checkpoint_or_journal_recovery_required" },
        { "replay_policy", "do_not_blindly_reexecute_tools" },
        { "executed_tool_calls", executedToolCallRefsJson( aHandledToolCalls ) },
        { "recovery_basis", recoveryBasisJson( aRequest, aHandledToolCalls ) }
    };

    aResponse.m_ProviderTraceJson = fromUtf8String( trace.dump() );
}


void appendPromptTrace( AI_PROMPT_TRACE_STORE* aStore,
                        const AI_PROVIDER_REQUEST& aRequest,
                        const AI_PROVIDER_RESPONSE& aResponse )
{
    if( !aStore )
        return;

    wxString error;
    aStore->Append( aRequest, promptTraceStatusForResponse( aResponse ),
                    aResponse.m_ProviderTraceJson, error );
}


void archiveLargeToolResult( AI_ARTIFACT_STORE* aStore,
                             const AI_PROVIDER_REQUEST& aRequest,
                             const AI_TOOL_CALL_RECORD& aToolCall )
{
    if( !aStore || aToolCall.m_ResultJson.IsEmpty()
        || aToolCall.m_ResultJson.length() <= aRequest.m_MaxToolResultChars )
    {
        return;
    }

    AI_ARTIFACT_RECORD artifact;
    wxString           error;

    if( isScriptOutputTool( aToolCall.m_ToolName ) )
    {
        AiStoreScriptOutputArtifact( aRequest.m_ContextSnapshot.m_ProjectId,
                                     aRequest.m_ContextSnapshot.m_DocumentId,
                                     wxS( "chat" ), wxS( "trace" ),
                                     aToolCall.m_ToolCallId,
                                     aToolCall.m_ToolName,
                                     aToolCall.m_ArgumentsJson,
                                     aToolCall.m_ResultJson,
                                     *aStore, artifact, error );
        return;
    }

    AiStoreToolResultArtifact( aRequest.m_ContextSnapshot.m_ProjectId,
                               aRequest.m_ContextSnapshot.m_DocumentId,
                               wxS( "chat" ), wxS( "trace" ),
                               aToolCall.m_ToolCallId,
                               aToolCall.m_ToolName,
                               aToolCall.m_ResultJson,
                               *aStore, artifact, error );
}


void archiveProviderRecoveryArtifact( AI_ARTIFACT_STORE* aStore,
                                      const AI_PROVIDER_REQUEST& aRequest,
                                      AI_PROVIDER_RESPONSE& aResponse )
{
    if( !aStore || aResponse.m_ProviderTraceJson.IsEmpty() )
        return;

    nlohmann::json trace = nlohmann::json::parse(
            toUtf8String( aResponse.m_ProviderTraceJson ), nullptr, false );

    if( trace.is_discarded() || !trace.is_object()
        || !trace.contains( "runtime_guard" )
        || !trace["runtime_guard"].is_object()
        || !trace["runtime_guard"].contains( "recovery_basis" ) )
    {
        return;
    }

    AI_ARTIFACT_RECORD artifact;
    wxString           error;

    if( !AiStoreProviderRecoveryArtifact(
                aRequest.m_ContextSnapshot.m_ProjectId,
                aRequest.m_ContextSnapshot.m_DocumentId,
                wxS( "chat" ), wxS( "trace" ), wxS( "AI_RUNTIME" ),
                aRequest.m_RequestId, aResponse.m_ProviderTraceJson,
                *aStore, artifact, error ) )
    {
        return;
    }

    trace["runtime_guard"]["recovery_artifact_ref"] = {
        { "uri", toUtf8String( artifact.m_Uri ) },
        { "kind", "provider_recovery" },
        { "retention", toUtf8String( artifact.m_RetentionClass ) },
        { "request_id", aRequest.m_RequestId }
    };

    aResponse.m_ProviderTraceJson = fromUtf8String( trace.dump() );
}


nlohmann::json parseObjectOrEmpty( const wxString& aText )
{
    nlohmann::json parsed = nlohmann::json::parse( toUtf8String( aText ),
                                                   nullptr, false );

    if( parsed.is_discarded() || !parsed.is_object() )
        return nlohmann::json::object();

    return parsed;
}


void archiveVisualObservationForProviderInput( AI_ARTIFACT_STORE* aStore,
                                               AI_PROVIDER_REQUEST& aRequest,
                                               const wxString& aAgentKind )
{
    AI_VISUAL_SNAPSHOT& visual = aRequest.m_ContextSnapshot.m_Visual;

    if( !aStore || !visual.HasPixels() )
        return;

    AI_VISUAL_OBSERVATION_ARTIFACT visualArtifact;
    visualArtifact.m_Snapshot = visual;
    visualArtifact.m_SidecarJson = visual.m_SidecarJson;

    AI_ARTIFACT_RECORD artifact;
    wxString           error;

    if( !AiStoreVisualObservationArtifact(
                aRequest.m_ContextSnapshot.m_ProjectId,
                aRequest.m_ContextSnapshot.m_DocumentId,
                aAgentKind, wxS( "trace" ), visualArtifact,
                *aStore, artifact, error ) )
    {
        return;
    }

    nlohmann::json sidecar = parseObjectOrEmpty( visual.m_SidecarJson );

    sidecar["artifact_ref"] = {
        { "uri", toUtf8String( artifact.m_Uri ) },
        { "kind", "visual_observation" },
        { "retention", toUtf8String( artifact.m_RetentionClass ) },
        { "frame_id", toUtf8String( visual.m_FrameId ) },
        { "frame_kind", toUtf8String( visual.m_FrameKind ) }
    };

    visual.m_SidecarJson = fromUtf8String( sidecar.dump() );
}
} // namespace


AI_RUNTIME::AI_RUNTIME( std::unique_ptr<AI_PROVIDER> aProvider ) :
        m_Provider( std::move( aProvider ) ),
        m_NextRequestId( 1 ),
        m_OwnedActivityLog( 256 ),
        m_ActivityLog( &m_OwnedActivityLog )
{
}


AI_RUNTIME::AI_RUNTIME( std::unique_ptr<AI_PROVIDER> aProvider,
                        AI_ACTIVITY_LOG& aActivityLog ) :
        m_Provider( std::move( aProvider ) ),
        m_NextRequestId( 1 ),
        m_OwnedActivityLog( 0 ),
        m_ActivityLog( &aActivityLog )
{
}


AI_PROVIDER_RESPONSE AI_RUNTIME::Submit( AI_PROVIDER_REQUEST aRequest )
{
    aRequest.m_RequestId = m_NextRequestId.fetch_add( 1 );
    AI_TOOL_CALL_HANDLER* handler = nullptr;
    AI_PROMPT_TRACE_STORE* traceStore = nullptr;
    AI_ARTIFACT_STORE* artifactStore = nullptr;

    {
        std::lock_guard<std::mutex> lock( m_Mutex );
        handler = m_ToolCallHandler;
        traceStore = m_PromptTraceStore;
        artifactStore = m_ArtifactStore;
    }

    archiveVisualObservationForProviderInput( artifactStore, aRequest, wxS( "chat" ) );

    AI_PROVIDER_REQUEST providerRequest = AiCompileProviderInputWithBudget( aRequest );

    AI_PROVIDER_RESPONSE response = m_Provider->Generate( providerRequest );

    appendPromptTrace( traceStore, providerRequest, response );

    std::vector<AI_TOOL_CALL_RECORD> handledToolCalls;
    size_t                           toolRounds = 0;

    while( !response.m_ToolCalls.empty() && toolRounds < aRequest.m_MaxToolRounds )
    {
        std::vector<AI_TOOL_CALL_RECORD> roundToolCalls = std::move( response.m_ToolCalls );

        if( repeatsHandledToolCall( roundToolCalls, handledToolCalls ) )
            break;

        for( AI_TOOL_CALL_RECORD& toolCall : roundToolCalls )
        {
            toolCall.m_RequestId = aRequest.m_RequestId;
            recordModelToolCall( *m_ActivityLog, providerRequest, toolCall );

            if( handler )
            {
                AI_TOOL_INVOCATION_RESULT result =
                        handler->HandleToolCall( providerRequest, toolCall );
                copyToolResult( toolCall, result );
                recordToolResult( *m_ActivityLog, result );
                recordPythonWorkerEvents( *m_ActivityLog, result );
                archiveLargeToolResult( artifactStore, aRequest, toolCall );
            }
            else
            {
                AI_TOOL_INVOCATION_RESULT result;
                result.m_RequestId = aRequest.m_RequestId;
                result.m_ToolCallId = toolCall.m_ToolCallId;
                result.m_ActionName = toolCall.m_ToolName;
                result.m_Allowed = false;
                result.m_Executed = false;
                result.m_ErrorCode = wxS( "no_tool_handler" );
                result.m_Message = wxS( "No tool handler installed." );
                result.m_ResultJson = deniedToolResultJson( result );
                copyToolResult( toolCall, result );
                recordToolResult( *m_ActivityLog, result );
                recordPythonWorkerEvents( *m_ActivityLog, result );
                archiveLargeToolResult( artifactStore, aRequest, toolCall );
            }
        }

        handledToolCalls.insert( handledToolCalls.end(),
                                 std::make_move_iterator( roundToolCalls.begin() ),
                                 std::make_move_iterator( roundToolCalls.end() ) );
        ++toolRounds;

        AI_PROVIDER_REQUEST continuationRequest = aRequest;
        continuationRequest.m_ToolResults = handledToolCalls;
        continuationRequest.m_ContextCompiled = false;
        continuationRequest.m_CompiledUserMessageText.Clear();
        continuationRequest.m_PromptTraceJson.Clear();
        continuationRequest.m_ProviderInputBlocks.clear();

        providerRequest = AiCompileProviderInputWithBudget( continuationRequest );

        AI_PROVIDER_RESPONSE continuationResponse = m_Provider->Generate( providerRequest );
        markPostSideEffectProviderFailure( continuationResponse, providerRequest,
                                           handledToolCalls );
        archiveProviderRecoveryArtifact( artifactStore, providerRequest,
                                         continuationResponse );
        appendPromptTrace( traceStore, providerRequest, continuationResponse );
        continuationResponse.m_RequestId = aRequest.m_RequestId;
        response = std::move( continuationResponse );
    }

    if( !handledToolCalls.empty() )
        response.m_ToolCalls = std::move( handledToolCalls );

    AI_TRACE_RECORD record;
    record.m_RequestId = aRequest.m_RequestId;
    record.m_Request = providerRequest;
    record.m_Response = response;

    {
        std::lock_guard<std::mutex> lock( m_Mutex );
        m_TraceRecords.push_back( record );
    }

    return response;
}


bool AI_RUNTIME::Cancel( uint64_t aRequestId )
{
    std::lock_guard<std::mutex> lock( m_Mutex );

    for( AI_TRACE_RECORD& record : m_TraceRecords )
    {
        if( record.m_RequestId == aRequestId )
        {
            record.m_Cancelled = true;
            return true;
        }
    }

    return false;
}


void AI_RUNTIME::SetProvider( std::unique_ptr<AI_PROVIDER> aProvider )
{
    if( !aProvider )
        return;

    std::lock_guard<std::mutex> lock( m_Mutex );
    m_Provider = std::move( aProvider );
}


void AI_RUNTIME::SetToolCallHandler( AI_TOOL_CALL_HANDLER* aHandler )
{
    std::lock_guard<std::mutex> lock( m_Mutex );
    m_ToolCallHandler = aHandler;
}


void AI_RUNTIME::SetPromptTraceStore( AI_PROMPT_TRACE_STORE* aStore )
{
    std::lock_guard<std::mutex> lock( m_Mutex );
    m_PromptTraceStore = aStore;
}


void AI_RUNTIME::SetArtifactStore( AI_ARTIFACT_STORE* aStore )
{
    std::lock_guard<std::mutex> lock( m_Mutex );
    m_ArtifactStore = aStore;
}


std::vector<AI_TRACE_RECORD> AI_RUNTIME::TraceRecords() const
{
    std::lock_guard<std::mutex> lock( m_Mutex );
    return m_TraceRecords;
}


std::vector<AI_ACTIVITY_RECORD> AI_RUNTIME::ActivityRecords() const
{
    return m_ActivityLog->Records();
}
