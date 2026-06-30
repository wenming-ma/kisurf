#include <kisurf/ai/ai_agent_panel_model.h>
#include <kisurf/ai/ai_provider.h>
#include <kisurf/ai/ai_session_tool_call_handler.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <exception>
#include <utility>

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


wxString documentWriteKey( const AI_NEXT_ACTION_CONTEXT_VERSION& aContextVersion )
{
    if( !aContextVersion.m_DocumentId.IsEmpty() )
    {
        wxString key = wxS( "identity:" );

        if( !aContextVersion.m_ProjectId.IsEmpty() )
            key << aContextVersion.m_ProjectId;
        else
            key << wxS( "*" );

        key << wxS( "/" ) << aContextVersion.m_DocumentId;
        return key;
    }

    if( !aContextVersion.m_BoardBaseHash.IsEmpty() )
        return wxS( "board:" ) + aContextVersion.m_BoardBaseHash;

    if( aContextVersion.m_ContextVersion.m_DocumentRevision != 0 )
    {
        return wxString::Format(
                wxS( "doc:%llu" ),
                static_cast<unsigned long long>(
                        aContextVersion.m_ContextVersion.m_DocumentRevision ) );
    }

    return wxS( "unknown" );
}


bool documentWriteKeysConflict( const wxString& aActiveKey,
                                const wxString& aRequestedKey )
{
    return aActiveKey == aRequestedKey || aActiveKey == wxS( "unknown" )
           || aRequestedKey == wxS( "unknown" );
}


size_t chatRecentTurnsTextBudget( size_t aMaxProviderInputChars )
{
    constexpr size_t MIN_RECENT_TURNS_TEXT_BUDGET = 16000;
    constexpr size_t FIXED_CONTEXT_RESERVE_CHARS = 8000;

    if( aMaxProviderInputChars <= FIXED_CONTEXT_RESERVE_CHARS )
        return MIN_RECENT_TURNS_TEXT_BUDGET;

    return std::max( MIN_RECENT_TURNS_TEXT_BUDGET,
                     aMaxProviderInputChars - FIXED_CONTEXT_RESERVE_CHARS );
}


AI_PROVIDER_INPUT_BLOCK recentChatTurnsBlock(
        const std::vector<AI_AGENT_MESSAGE>& aMessages,
        size_t aMaxProviderInputChars )
{
    const size_t recentTurnsTextBudget =
            chatRecentTurnsTextBudget( aMaxProviderInputChars );

    AI_PROVIDER_INPUT_BLOCK block;

    if( aMessages.empty() )
        return block;

    const wxString header = wxS( "Previous chat turns (current chat only):" );
    size_t         originalChars = 0;
    size_t         sentMessageChars = 0;
    size_t         projectedChars = header.length();
    size_t         omittedMessageCount = 0;
    std::vector<size_t> includedMessageIndexes;

    for( const AI_AGENT_MESSAGE& message : aMessages )
        originalChars += message.m_Text.length();

    for( size_t offset = 0; offset < aMessages.size(); ++offset )
    {
        const size_t            index = aMessages.size() - 1 - offset;
        const AI_AGENT_MESSAGE& message = aMessages[index];
        const size_t            entryChars =
                1 + message.m_Role.length() + 2 + message.m_Text.length();

        if( projectedChars + entryChars > recentTurnsTextBudget )
        {
            ++omittedMessageCount;
            continue;
        }

        includedMessageIndexes.push_back( index );
        projectedChars += entryChars;
        sentMessageChars += message.m_Text.length();
    }

    wxString text;
    text << header;

    if( omittedMessageCount > 0 )
    {
        text << wxS( "\n[omitted chat messages due to context budget: " )
             << static_cast<unsigned long long>( omittedMessageCount )
             << wxS( "]" );
    }

    for( auto it = includedMessageIndexes.rbegin();
         it != includedMessageIndexes.rend(); ++it )
    {
        const AI_AGENT_MESSAGE& message = aMessages[*it];
        text << wxS( "\n" ) << message.m_Role << wxS( ": " )
             << message.m_Text;
    }

    nlohmann::json metadata = {
        { "message_count", aMessages.size() },
        { "sent_message_count", includedMessageIndexes.size() },
        { "older_message_count", omittedMessageCount },
        { "omitted_message_count", omittedMessageCount },
        { "truncated_chat_turn_count", 0 },
        { "original_message_chars", originalChars },
        { "sent_message_chars", sentMessageChars },
        { "max_projected_chars", recentTurnsTextBudget },
        { "max_provider_input_chars", aMaxProviderInputChars },
        { "compression_policy",
          "recent_complete_messages_at_model_context_threshold" }
    };

    block.m_Id = wxS( "chat.recent_turns" );
    block.m_Kind = wxS( "chat_recent_turns" );
    block.m_Source = wxS( "chat_session" );
    block.m_Text = text;
    block.m_OriginalChars = originalChars;
    block.m_SentChars = text.length();
    block.m_MetadataJson = fromUtf8String( metadata.dump() );
    return block;
}


AI_PROVIDER_INPUT_BLOCK recentChatToolCallsBlock(
        const std::vector<AI_TRACE_RECORD>& aTraceRecords,
        uint64_t aConversationId )
{
    constexpr size_t CHAT_RECENT_TOOL_CALL_LIMIT = 12;

    std::vector<const AI_TOOL_CALL_RECORD*> toolCalls;

    for( const AI_TRACE_RECORD& trace : aTraceRecords )
    {
        if( trace.m_Request.m_ConversationId != aConversationId )
            continue;

        for( const AI_TOOL_CALL_RECORD& toolCall : trace.m_Response.m_ToolCalls )
            toolCalls.push_back( &toolCall );
    }

    AI_PROVIDER_INPUT_BLOCK block;

    if( toolCalls.empty() )
        return block;

    const size_t start = toolCalls.size() > CHAT_RECENT_TOOL_CALL_LIMIT
                         ? toolCalls.size() - CHAT_RECENT_TOOL_CALL_LIMIT : 0;
    nlohmann::json calls = nlohmann::json::array();

    for( size_t ii = start; ii < toolCalls.size(); ++ii )
    {
        const AI_TOOL_CALL_RECORD& toolCall = *toolCalls[ii];

        calls.push_back( {
                { "request_id", toolCall.m_RequestId },
                { "tool_call_id", toUtf8String( toolCall.m_ToolCallId ) },
                { "tool_name", toUtf8String( toolCall.m_ToolName ) },
                { "allowed", toolCall.m_Allowed },
                { "executed", toolCall.m_Executed },
                { "error_code", toUtf8String( toolCall.m_ErrorCode ) },
                { "message", toUtf8String( toolCall.m_Message ) },
                { "arguments_chars", toolCall.m_ArgumentsJson.length() },
                { "result_chars", toolCall.m_ResultJson.length() } } );
    }

    nlohmann::json payload = {
        { "schema",
          { { "name", "kisurf.ai.chat_recent_tool_calls" },
            { "version", 1 } } },
        { "conversation_id", aConversationId },
        { "original_tool_call_count", toolCalls.size() },
        { "sent_tool_call_count", toolCalls.size() - start },
        { "tool_calls", std::move( calls ) } };

    wxString text;
    text << wxS( "Previous chat tool calls (current chat only, compact):\n" )
         << fromUtf8String( payload.dump() );

    block.m_Id = wxS( "chat.recent_tool_calls" );
    block.m_Kind = wxS( "chat_recent_tool_calls" );
    block.m_Source = wxS( "chat_session" );
    block.m_Text = text;
    block.m_OriginalChars = text.length();
    block.m_MetadataJson = wxString::Format(
            wxS( "{\"conversation_id\":%llu,\"original_tool_call_count\":%llu,"
                 "\"sent_tool_call_count\":%llu}" ),
            static_cast<unsigned long long>( aConversationId ),
            static_cast<unsigned long long>( toolCalls.size() ),
            static_cast<unsigned long long>( toolCalls.size() - start ) );
    return block;
}


wxString chatTranscriptSummary( const std::vector<AI_AGENT_MESSAGE>& aMessages )
{
    for( const AI_AGENT_MESSAGE& message : aMessages )
    {
        if( message.m_Role == wxS( "user" ) && !message.m_Text.IsEmpty() )
            return message.m_Text.Left( 160 );
    }

    return wxS( "Archived chat transcript." );
}


nlohmann::json parseJsonObjectOrString( const wxString& aText )
{
    if( aText.IsEmpty() )
        return nlohmann::json::object();

    try
    {
        nlohmann::json parsed = nlohmann::json::parse( toUtf8String( aText ) );

        if( parsed.is_object() )
            return parsed;
    }
    catch( const nlohmann::json::exception& )
    {
    }

    return nlohmann::json{ { "raw", toUtf8String( aText ) } };
}


AI_ACTIVITY_RECORD nextActionRuntimeFailureRecord(
        const AI_CONTEXT_SNAPSHOT& aContextSnapshot,
        const AI_ACTIVITY_RECORD& aActivity,
        const wxString& aReason,
        const wxString& aErrorCode,
        const wxString& aMessage )
{
    nlohmann::json args = {
        { "reason", toUtf8String( aReason ) },
        { "activity_sequence", aActivity.m_Sequence },
        { "context_version", toUtf8String( aContextSnapshot.m_Version.AsString() ) },
        { "tool_state", toUtf8String( aContextSnapshot.m_ToolState.KindAsString() ) },
        { "dynamic_context", toUtf8String( AiDynamicContextKind( aContextSnapshot ) ) }
    };

    nlohmann::json result = {
        { "ok", false },
        { "status", "failed" },
        { "error_code", toUtf8String( aErrorCode ) },
        { "message", toUtf8String( aMessage ) }
    };

    AI_ACTIVITY_RECORD record;
    record.m_RequestId = aActivity.m_RequestId;
    record.m_Kind = AI_ACTIVITY_KIND::PolicyDecision;
    record.m_EditorKind = aContextSnapshot.m_EditorKind;
    record.m_ActionName = wxS( "agent.background.next_action_failed" );
    record.m_ArgumentsJson = fromUtf8String( args.dump() );
    record.m_ResultJson = fromUtf8String( result.dump() );
    record.m_ErrorCode = aErrorCode;
    record.m_Allowed = false;
    record.m_Executed = true;
    record.m_Message = aMessage;
    return record;
}


void appendProviderRecoveryEntry(
        const AI_PROVIDER_RECOVERY_POLICY& aPolicy,
        const AI_PROVIDER_RECOVERY_RESUME_PLAN& aPlan,
        std::vector<AI_AGENT_OBSERVABILITY_ENTRY>& aEntries )
{
    if( !aPolicy.m_Available )
        return;

    nlohmann::json recoveryBasis =
            parseJsonObjectOrString( aPolicy.m_RecoveryBasisJson );
    nlohmann::json resumePacket = {
        { "schema",
          { { "name", "kisurf.ai.provider_recovery_resume" },
            { "version", 1 } } },
        { "artifact_uri", toUtf8String( aPolicy.m_ArtifactUri ) },
        { "agent_kind", toUtf8String( aPolicy.m_AgentKind ) },
        { "source", toUtf8String( aPolicy.m_Source ) },
        { "request_id", aPolicy.m_RequestId },
        { "reason", toUtf8String( aPolicy.m_Reason ) },
        { "provider_recovery_action", toUtf8String( aPolicy.m_Action ) },
        { "resume_action", "resume_from_checkpoint_or_journal" },
        { "replay_policy", "do_not_blindly_reexecute_tools" },
        { "blind_tool_replay_allowed", false },
        { "checkpoint_or_journal_resume_required", true },
        { "recovery_basis", recoveryBasis } };

    nlohmann::json details = {
        { "agent_kind", toUtf8String( aPolicy.m_AgentKind ) },
        { "artifact_kind", "provider_recovery" },
        { "artifact_uri", toUtf8String( aPolicy.m_ArtifactUri ) },
        { "request_id", aPolicy.m_RequestId },
        { "reason", toUtf8String( aPolicy.m_Reason ) },
        { "action", toUtf8String( aPolicy.m_Action ) },
        { "replay_policy", "do_not_blindly_reexecute_tools" },
        { "blind_tool_replay_allowed", aPolicy.m_BlindToolReplayAllowed },
        { "checkpoint_or_journal_resume_required",
          aPolicy.m_CheckpointOrJournalResumeRequired },
        { "resume_action", "resume_from_checkpoint_or_journal" },
        { "recovery_basis", recoveryBasis },
        { "resume_packet", resumePacket } };

    if( aPlan.m_Available )
        details["resume_plan"] = parseJsonObjectOrString( aPlan.m_PlanJson );

    AI_AGENT_OBSERVABILITY_ENTRY entry;
    entry.m_Sequence = 900000000 + aPolicy.m_RequestId;
    entry.m_RequestId = aPolicy.m_RequestId;
    entry.m_Kind = AI_AGENT_OBSERVABILITY_KIND::System;
    entry.m_Title = wxS( "Provider recovery required" );
    entry.m_Summary =
            wxS( "[" ) + aPolicy.m_AgentKind
            + wxS( "] provider failed after side-effectful tool execution; "
                   "checkpoint/journal resume required; blind replay disabled." );
    entry.m_DetailsJson = fromUtf8String( details.dump() );
    aEntries.push_back( std::move( entry ) );
}
} // namespace


AI_AGENT_PANEL_MODEL::AI_AGENT_PANEL_MODEL( std::unique_ptr<AI_PROVIDER> aProvider ) :
        m_ActivityLog( 256 ),
        m_DefaultPromptTraceStore( std::make_unique<AI_PROMPT_TRACE_STORE>() ),
        m_DefaultMemoryStore( std::make_unique<AI_MEMORY_STORE>() ),
        m_DefaultLocalTextMemoryIndex( std::make_unique<AI_LOCAL_TEXT_MEMORY_INDEX>() ),
        m_DefaultArtifactStore( std::make_unique<AI_ARTIFACT_STORE>() ),
        m_DefaultChatSessionStore( std::make_unique<AI_CHAT_SESSION_STORE>() ),
        m_DefaultNextActionSessionStore( std::make_unique<AI_NEXT_ACTION_SESSION_STORE>() ),
        m_Runtime( std::move( aProvider ), m_ActivityLog ),
        m_NextActionRuntime( std::make_unique<AI_NEXT_ACTION_RUNTIME>(
                MakeDefaultAiProvider(), m_NextActionValidationService,
                m_NextActionPreviewService ) )
{
    SetPromptTraceStore( nullptr );
    SetMemoryStore( nullptr );
    SetLocalTextMemoryIndex( nullptr );
    SetArtifactStore( nullptr );
    SetChatSessionStore( nullptr );
    SetNextActionSessionStore( nullptr );
}


bool AI_AGENT_PANEL_MODEL::CanSend( const wxString& aText ) const
{
    wxString trimmed = aText;
    trimmed.Trim( true ).Trim( false );
    return !trimmed.IsEmpty();
}


AI_PROVIDER_RESPONSE AI_AGENT_PANEL_MODEL::SendUserText( const wxString& aText,
                                                         AI_EDITOR_KIND aEditorKind )
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = aEditorKind;
    return SendUserText( aText, aEditorKind, snapshot );
}


AI_PROVIDER_RESPONSE AI_AGENT_PANEL_MODEL::SendUserText( const wxString& aText,
                                                         AI_EDITOR_KIND aEditorKind,
                                                         AI_CONTEXT_SNAPSHOT aContextSnapshot )
{
    AI_CHAT_REQUEST_STATE state =
            PrepareUserTextRequest( aText, aEditorKind, std::move( aContextSnapshot ) );
    AI_PROVIDER_RESPONSE response = ExecutePreparedChatRequest( state );
    return FinishPreparedChatRequest( std::move( state ), std::move( response ) );
}


AI_CHAT_REQUEST_STATE AI_AGENT_PANEL_MODEL::PrepareUserTextRequest(
        const wxString& aText, AI_EDITOR_KIND aEditorKind,
        AI_CONTEXT_SNAPSHOT aContextSnapshot )
{
    AI_CHAT_REQUEST_STATE state;
    const std::vector<AI_AGENT_MESSAGE> priorMessages = m_Messages;

    AI_PROVIDER_REQUEST request;
    request.m_ConversationId = m_ActiveChatSessionId;
    request.m_EditorKind = aEditorKind;
    request.m_ContextSnapshot = std::move( aContextSnapshot );
    request.m_MaxProviderInputChars = m_ProviderInputBudgetChars;

    if( request.m_ContextSnapshot.m_EditorKind == AI_EDITOR_KIND::Unknown )
        request.m_ContextSnapshot.m_EditorKind = aEditorKind;

    m_LastChatProjectId = request.m_ContextSnapshot.m_ProjectId;
    m_LastChatDocumentId = request.m_ContextSnapshot.m_DocumentId;
    m_Messages.push_back( { wxS( "user" ), aText } );
    persistActiveChatSession();

    refreshLocalTextResearchDirectory( request.m_ContextSnapshot );

    std::vector<AI_ACTIVITY_RECORD> activity = ActivityRecords();

    for( const AI_ACTIVITY_RECORD& record : activity )
    {
        if( record.m_Sequence > m_ChatActivityBoundarySequence )
            request.m_ContextSnapshot.m_RecentActivity.push_back( record );
    }

    request.m_ContextVersion = request.m_ContextSnapshot.m_Version;
    request.m_UserText = aText;
    request.m_MaxToolRounds = 12;

    AI_PROVIDER_INPUT_BLOCK recentTurns =
            recentChatTurnsBlock( priorMessages,
                                  request.m_MaxProviderInputChars );

    if( !recentTurns.m_Id.IsEmpty() )
        request.m_ProviderInputBlocks.push_back( std::move( recentTurns ) );

    AI_PROVIDER_INPUT_BLOCK recentToolCalls =
            recentChatToolCallsBlock( m_Runtime.TraceRecords(),
                                      m_ActiveChatSessionId );

    if( !recentToolCalls.m_Id.IsEmpty() )
        request.m_ProviderInputBlocks.push_back( std::move( recentToolCalls ) );

    if( m_MemoryStore && !request.m_ContextSnapshot.m_ProjectId.IsEmpty()
        && !request.m_ContextSnapshot.m_DocumentId.IsEmpty() )
    {
        AI_MEMORY_QUERY query;
        query.m_ProjectId = request.m_ContextSnapshot.m_ProjectId;
        query.m_DocumentId = request.m_ContextSnapshot.m_DocumentId;
        query.m_AcceptanceState = wxS( "accepted" );
        query.m_MinTrustLevel = 70;
        query.m_Limit = request.m_MaxRetrievedMemoryRecords;

        wxString error;
        std::vector<AI_MEMORY_RECORD> records = m_MemoryStore->Query( query, error );

        if( error.IsEmpty() )
        {
            for( const AI_MEMORY_RECORD& record : records )
                request.m_RetrievedMemoryBlocks.push_back(
                        AiMemoryRecordToProviderInputBlock( record ) );
        }
    }

    if( m_LocalTextMemoryIndex && !request.m_ContextSnapshot.m_ProjectId.IsEmpty()
        && !request.m_ContextSnapshot.m_DocumentId.IsEmpty()
        && request.m_RetrievedMemoryBlocks.size() < request.m_MaxRetrievedMemoryRecords )
    {
        AI_LOCAL_TEXT_MEMORY_QUERY query;
        query.m_Text = aText;
        query.m_ProjectId = request.m_ContextSnapshot.m_ProjectId;
        query.m_DocumentId = request.m_ContextSnapshot.m_DocumentId;

        const size_t remaining =
                request.m_MaxRetrievedMemoryRecords - request.m_RetrievedMemoryBlocks.size();

        for( const AI_LOCAL_TEXT_MEMORY_RESULT& result :
             m_LocalTextMemoryIndex->Search( query, remaining ) )
        {
            request.m_RetrievedMemoryBlocks.push_back(
                    AiLocalTextMemoryResultToProviderInputBlock( result ) );
        }
    }

    AI_NEXT_ACTION_CONTEXT_VERSION ownershipContext;
    ownershipContext.m_ContextVersion = request.m_ContextVersion;
    ownershipContext.m_ProjectId = request.m_ContextSnapshot.m_ProjectId;
    ownershipContext.m_DocumentId = request.m_ContextSnapshot.m_DocumentId;
    state.m_OwnershipContext = ownershipContext;
    state.m_DocumentWriteOwned =
            TryAcquireDocumentWriteOwnership( wxS( "chat" ), ownershipContext );

    if( !state.m_DocumentWriteOwned )
    {
        AI_PROVIDER_RESPONSE response;
        response.m_Title = wxS( "Document write ownership unavailable" );
        response.m_Body =
                wxS( "Cannot run chat tool calls because document write ownership "
                     "is currently held by another AI runtime." );
        state.m_PreflightCompleted = true;
        state.m_PreflightResponse = std::move( response );
    }

    state.m_Request = std::move( request );
    return state;
}


AI_PROVIDER_RESPONSE AI_AGENT_PANEL_MODEL::ExecutePreparedChatRequest(
        const AI_CHAT_REQUEST_STATE& aState )
{
    return ExecutePreparedChatRequest( aState, AI_RUNTIME_STREAM_EVENT_SINK() );
}


AI_PROVIDER_RESPONSE AI_AGENT_PANEL_MODEL::ExecutePreparedChatRequest(
        const AI_CHAT_REQUEST_STATE& aState,
        AI_RUNTIME_STREAM_EVENT_SINK aEventSink )
{
    if( aState.m_PreflightCompleted )
        return aState.m_PreflightResponse;

    return m_Runtime.Submit( aState.m_Request, std::move( aEventSink ) );
}


AI_PROVIDER_RESPONSE AI_AGENT_PANEL_MODEL::FinishPreparedChatRequest(
        AI_CHAT_REQUEST_STATE aState, AI_PROVIDER_RESPONSE aResponse )
{
    if( aResponse.m_RequestId != 0 )
        m_LastRequestId = aResponse.m_RequestId;

    if( aState.m_DocumentWriteOwned )
        ReleaseDocumentWriteOwnership( wxS( "chat" ), aState.m_OwnershipContext );

    m_Messages.push_back( { wxS( "assistant" ), aResponse.m_Body } );
    persistActiveChatSession();

    return aResponse;
}


bool AI_AGENT_PANEL_MODEL::CancelLastRequest()
{
    return m_LastRequestId != 0 && CancelRequest( m_LastRequestId );
}


bool AI_AGENT_PANEL_MODEL::CancelRequest( uint64_t aRequestId )
{
    return m_Runtime.Cancel( aRequestId );
}


bool AI_AGENT_PANEL_MODEL::LastRequestCancelled() const
{
    if( m_LastRequestId == 0 )
        return false;

    for( const AI_TRACE_RECORD& record : m_Runtime.TraceRecords() )
    {
        if( record.m_RequestId == m_LastRequestId )
            return record.m_Cancelled;
    }

    return false;
}


void AI_AGENT_PANEL_MODEL::StartNewChat()
{
    if( m_ArtifactStore && !m_Messages.empty() )
    {
        nlohmann::json messages = nlohmann::json::array();

        for( const AI_AGENT_MESSAGE& message : m_Messages )
        {
            messages.push_back( {
                    { "role", toUtf8String( message.m_Role ) },
                    { "text", toUtf8String( message.m_Text ) } } );
        }

        const wxString summaryText = chatTranscriptSummary( m_Messages );

        nlohmann::json payload = {
            { "schema", { { "name", "kisurf.ai.chat_transcript" },
                          { "version", 1 } } },
            { "conversation_id", m_ActiveChatSessionId },
            { "message_count", m_Messages.size() },
            { "summary", toUtf8String( summaryText ) },
            { "messages", std::move( messages ) } };

        nlohmann::json metadata = {
            { "conversation_id", m_ActiveChatSessionId },
            { "message_count", m_Messages.size() },
            { "summary_chars", summaryText.length() } };

        AI_ARTIFACT_RECORD record;
        record.m_Kind = wxS( "chat_transcript" );
        record.m_ProjectId = m_LastChatProjectId;
        record.m_DocumentId = m_LastChatDocumentId;
        record.m_AgentKind = wxS( "chat" );
        record.m_Source = wxS( "chat.new" );
        record.m_MimeType = wxS( "application/json" );
        record.m_RetentionClass = wxS( "session_archive" );
        record.m_Summary = wxString::Format(
                wxS( "Archived chat session %llu with %llu messages" ),
                static_cast<unsigned long long>( m_ActiveChatSessionId ),
                static_cast<unsigned long long>( m_Messages.size() ) );
        record.m_MetadataJson = fromUtf8String( metadata.dump() );

        wxString error;
        m_ArtifactStore->StorePayload( record, fromUtf8String( payload.dump() ),
                                       error );
    }

    if( auto* sessionHandler = dynamic_cast<AI_SESSION_TOOL_CALL_HANDLER*>( m_ToolCallHandler ) )
        sessionHandler->CancelActiveSession( wxS( "chat.new" ) );

    if( m_NextActionRuntime )
        m_NextActionRuntime->ExpireActive();

    m_Messages.clear();
    m_ActiveChatSessionId =
            m_ChatSessionStore
                    ? m_ChatSessionStore->NextConversationId( m_ActiveChatSessionId + 1 )
                    : m_ActiveChatSessionId + 1;
    m_LastChatProjectId.Clear();
    m_LastChatDocumentId.Clear();

    AI_ACTIVITY_RECORD record;
    record.m_Kind = AI_ACTIVITY_KIND::UserAction;
    record.m_ActionName = wxS( "chat.new" );
    record.m_Message = wxS( "Started a new chat session." );
    AI_ACTIVITY_RECORD boundary = m_ActivityLog.Append( std::move( record ) );
    m_ChatActivityBoundarySequence = boundary.m_Sequence;
}


void AI_AGENT_PANEL_MODEL::SetProvider( std::unique_ptr<AI_PROVIDER> aProvider )
{
    m_Runtime.SetProvider( std::move( aProvider ) );
}


void AI_AGENT_PANEL_MODEL::SetPromptTraceStore( AI_PROMPT_TRACE_STORE* aStore )
{
    m_PromptTraceStore = aStore ? aStore : m_DefaultPromptTraceStore.get();

    m_Runtime.SetPromptTraceStore( m_PromptTraceStore );

    if( m_NextActionRuntime )
        m_NextActionRuntime->SetPromptTraceStore( m_PromptTraceStore );
}


const wxString& AI_AGENT_PANEL_MODEL::PromptTraceStorePath() const
{
    return m_PromptTraceStore ? m_PromptTraceStore->Path()
                              : m_DefaultPromptTraceStore->Path();
}


void AI_AGENT_PANEL_MODEL::SetMemoryStore( AI_MEMORY_STORE* aStore )
{
    m_MemoryStore = aStore ? aStore : m_DefaultMemoryStore.get();

    if( m_NextActionRuntime )
        m_NextActionRuntime->SetMemoryStore( m_MemoryStore );
}


void AI_AGENT_PANEL_MODEL::SetLocalTextMemoryIndex( AI_LOCAL_TEXT_MEMORY_INDEX* aIndex )
{
    m_LocalTextMemoryIndex =
            aIndex ? aIndex : m_DefaultLocalTextMemoryIndex.get();

    if( m_NextActionRuntime )
        m_NextActionRuntime->SetLocalTextMemoryIndex( m_LocalTextMemoryIndex );
}


void AI_AGENT_PANEL_MODEL::SetLocalTextResearchDirectory( const wxString& aDirectory )
{
    m_LocalTextResearchDirectory = aDirectory;
    m_LocalTextResearchDirectory.Trim( true ).Trim( false );
}


bool AI_AGENT_PANEL_MODEL::LoadLocalTextResearchDirectory(
        const wxString& aDirectory,
        const wxString& aProjectId,
        const wxString& aDocumentId,
        wxString& aError )
{
    if( !m_LocalTextMemoryIndex )
        SetLocalTextMemoryIndex( nullptr );

    AI_LOCAL_TEXT_FILE_RECORD_OPTIONS options;
    options.m_ProjectId = aProjectId;
    options.m_DocumentId = aDocumentId;
    options.m_AgentKind = wxS( "shared" );
    options.m_Type = wxS( "project_research" );
    options.m_Source = wxS( "research_folder" );
    options.m_AcceptanceState = wxS( "accepted" );
    options.m_TrustLevel = 75;
    options.m_Sequence = static_cast<uint64_t>(
            m_LocalTextMemoryIndex->Records().size() + 1 );

    return m_LocalTextMemoryIndex->LoadTextDirectory( aDirectory, options, aError );
}


void AI_AGENT_PANEL_MODEL::refreshLocalTextResearchDirectory(
        const AI_CONTEXT_SNAPSHOT& aContextSnapshot )
{
    if( m_LocalTextResearchDirectory.IsEmpty()
        || aContextSnapshot.m_ProjectId.IsEmpty()
        || aContextSnapshot.m_DocumentId.IsEmpty() )
    {
        return;
    }

    wxString error;
    LoadLocalTextResearchDirectory( m_LocalTextResearchDirectory,
                                    aContextSnapshot.m_ProjectId,
                                    aContextSnapshot.m_DocumentId,
                                    error );
}


const wxString& AI_AGENT_PANEL_MODEL::MemoryStorePath() const
{
    return m_MemoryStore ? m_MemoryStore->Path() : m_DefaultMemoryStore->Path();
}


void AI_AGENT_PANEL_MODEL::SetArtifactStore( AI_ARTIFACT_STORE* aStore )
{
    m_ArtifactStore = aStore ? aStore : m_DefaultArtifactStore.get();

    m_Runtime.SetArtifactStore( m_ArtifactStore );

    if( m_NextActionRuntime )
        m_NextActionRuntime->SetArtifactStore( m_ArtifactStore );
}


const wxString& AI_AGENT_PANEL_MODEL::ArtifactStorePath() const
{
    return m_ArtifactStore ? m_ArtifactStore->ManifestPath()
                           : m_DefaultArtifactStore->ManifestPath();
}


void AI_AGENT_PANEL_MODEL::SetChatSessionStore( AI_CHAT_SESSION_STORE* aStore )
{
    m_ChatSessionStore = aStore ? aStore : m_DefaultChatSessionStore.get();

    if( m_ChatSessionStore && m_Messages.empty() )
        m_ActiveChatSessionId =
                m_ChatSessionStore->NextConversationId();
}


const wxString& AI_AGENT_PANEL_MODEL::ChatSessionStoreDirectory() const
{
    return m_ChatSessionStore ? m_ChatSessionStore->Directory()
                              : m_DefaultChatSessionStore->Directory();
}


void AI_AGENT_PANEL_MODEL::SetNextActionSessionStore(
        AI_NEXT_ACTION_SESSION_STORE* aStore )
{
    m_NextActionSessionStore =
            aStore ? aStore : m_DefaultNextActionSessionStore.get();

    if( m_NextActionRuntime )
        m_NextActionRuntime->SetSessionStore( m_NextActionSessionStore );
}


const wxString& AI_AGENT_PANEL_MODEL::NextActionSessionStoreDirectory() const
{
    return m_NextActionSessionStore ? m_NextActionSessionStore->Directory()
                                    : m_DefaultNextActionSessionStore->Directory();
}


void AI_AGENT_PANEL_MODEL::persistActiveChatSession()
{
    if( !m_ChatSessionStore || m_Messages.empty() )
        return;

    AI_CHAT_SESSION_RECORD record;
    record.m_ConversationId = m_ActiveChatSessionId;
    record.m_ProjectId = m_LastChatProjectId;
    record.m_DocumentId = m_LastChatDocumentId;
    record.m_Messages.reserve( m_Messages.size() );

    for( const AI_AGENT_MESSAGE& message : m_Messages )
    {
        AI_CHAT_SESSION_MESSAGE_RECORD stored;
        stored.m_Role = message.m_Role;
        stored.m_Text = message.m_Text;
        record.m_Messages.push_back( std::move( stored ) );
    }

    for( const AI_TRACE_RECORD& trace : m_Runtime.TraceRecords() )
    {
        if( trace.m_Request.m_ConversationId != m_ActiveChatSessionId )
            continue;

        for( const AI_TOOL_CALL_RECORD& toolCall : trace.m_Response.m_ToolCalls )
            record.m_ToolCalls.push_back( toolCall );
    }

    wxString error;
    m_ChatSessionStore->WriteSession( record, error );
}


AI_PROVIDER_RECOVERY_POLICY AI_AGENT_PANEL_MODEL::LatestChatProviderRecoveryPolicy() const
{
    AI_PROVIDER_RECOVERY_POLICY policy;

    if( !m_ArtifactStore )
        return policy;

    AI_PROVIDER_RECOVERY_QUERY query;
    query.m_ProjectId = m_LastChatProjectId;
    query.m_DocumentId = m_LastChatDocumentId;
    query.m_AgentKind = wxS( "chat" );

    wxString error;
    return AiEvaluateLatestProviderRecovery( *m_ArtifactStore, query, error );
}


AI_PROVIDER_RECOVERY_POLICY
AI_AGENT_PANEL_MODEL::LatestNextActionProviderRecoveryPolicy() const
{
    AI_PROVIDER_RECOVERY_POLICY policy;

    if( !m_ArtifactStore )
        return policy;

    AI_PROVIDER_RECOVERY_QUERY query;
    query.m_ProjectId = m_LastNextActionProjectId;
    query.m_DocumentId = m_LastNextActionDocumentId;
    query.m_AgentKind = wxS( "next_action" );

    wxString error;
    return AiEvaluateLatestProviderRecovery( *m_ArtifactStore, query, error );
}


AI_PROVIDER_RECOVERY_RESUME_PACKET
AI_AGENT_PANEL_MODEL::LatestChatProviderRecoveryResumePacket() const
{
    AI_PROVIDER_RECOVERY_RESUME_PACKET packet;

    if( !m_ArtifactStore )
        return packet;

    AI_PROVIDER_RECOVERY_QUERY query;
    query.m_ProjectId = m_LastChatProjectId;
    query.m_DocumentId = m_LastChatDocumentId;
    query.m_AgentKind = wxS( "chat" );

    wxString error;
    return AiBuildLatestProviderRecoveryResumePacket( *m_ArtifactStore, query,
                                                      error );
}


AI_PROVIDER_RECOVERY_RESUME_PACKET
AI_AGENT_PANEL_MODEL::LatestNextActionProviderRecoveryResumePacket() const
{
    AI_PROVIDER_RECOVERY_RESUME_PACKET packet;

    if( !m_ArtifactStore )
        return packet;

    AI_PROVIDER_RECOVERY_QUERY query;
    query.m_ProjectId = m_LastNextActionProjectId;
    query.m_DocumentId = m_LastNextActionDocumentId;
    query.m_AgentKind = wxS( "next_action" );

    wxString error;
    return AiBuildLatestProviderRecoveryResumePacket( *m_ArtifactStore, query,
                                                      error );
}


AI_PROVIDER_RECOVERY_RESUME_PLAN
AI_AGENT_PANEL_MODEL::LatestChatProviderRecoveryResumePlan() const
{
    AI_PROVIDER_RECOVERY_RESUME_PLAN plan;

    if( !m_ArtifactStore )
        return plan;

    AI_PROVIDER_RECOVERY_QUERY query;
    query.m_ProjectId = m_LastChatProjectId;
    query.m_DocumentId = m_LastChatDocumentId;
    query.m_AgentKind = wxS( "chat" );

    wxString error;
    return AiBuildLatestProviderRecoveryResumePlan( *m_ArtifactStore, query,
                                                    error );
}


AI_PROVIDER_RECOVERY_RESUME_PLAN
AI_AGENT_PANEL_MODEL::LatestNextActionProviderRecoveryResumePlan() const
{
    AI_PROVIDER_RECOVERY_RESUME_PLAN plan;

    if( !m_ArtifactStore )
        return plan;

    AI_PROVIDER_RECOVERY_QUERY query;
    query.m_ProjectId = m_LastNextActionProjectId;
    query.m_DocumentId = m_LastNextActionDocumentId;
    query.m_AgentKind = wxS( "next_action" );

    wxString error;
    return AiBuildLatestProviderRecoveryResumePlan( *m_ArtifactStore, query,
                                                    error );
}


AI_PROVIDER_RECOVERY_PREFLIGHT_RESULT
AI_AGENT_PANEL_MODEL::LatestChatProviderRecoveryPreflight(
        const AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT& aContext ) const
{
    AI_PROVIDER_RECOVERY_RESUME_PLAN plan =
            LatestChatProviderRecoveryResumePlan();

    return AiPreflightProviderRecoveryResumePlan( plan, aContext );
}


AI_PROVIDER_RECOVERY_PREFLIGHT_RESULT
AI_AGENT_PANEL_MODEL::LatestNextActionProviderRecoveryPreflight(
        const AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT& aContext ) const
{
    AI_PROVIDER_RECOVERY_RESUME_PLAN plan =
            LatestNextActionProviderRecoveryResumePlan();

    return AiPreflightProviderRecoveryResumePlan( plan, aContext );
}


AI_PROVIDER_RECOVERY_REPLAY_REQUEST
AI_AGENT_PANEL_MODEL::LatestChatProviderRecoveryReplayRequest(
        const AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT& aContext ) const
{
    AI_PROVIDER_RECOVERY_RESUME_PLAN plan =
            LatestChatProviderRecoveryResumePlan();

    return AiBuildProviderRecoveryReplayRequest( plan, aContext );
}


AI_PROVIDER_RECOVERY_REPLAY_REQUEST
AI_AGENT_PANEL_MODEL::LatestNextActionProviderRecoveryReplayRequest(
        const AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT& aContext ) const
{
    AI_PROVIDER_RECOVERY_RESUME_PLAN plan =
            LatestNextActionProviderRecoveryResumePlan();

    return AiBuildProviderRecoveryReplayRequest( plan, aContext );
}


AI_PROVIDER_RECOVERY_EPISODE
AI_AGENT_PANEL_MODEL::LatestChatProviderRecoveryEpisode(
        const AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT& aContext ) const
{
    AI_PROVIDER_RECOVERY_RESUME_PLAN plan =
            LatestChatProviderRecoveryResumePlan();

    return AiBuildProviderRecoveryEpisode( plan, aContext );
}


AI_PROVIDER_RECOVERY_EPISODE
AI_AGENT_PANEL_MODEL::LatestNextActionProviderRecoveryEpisode(
        const AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT& aContext ) const
{
    AI_PROVIDER_RECOVERY_RESUME_PLAN plan =
            LatestNextActionProviderRecoveryResumePlan();

    return AiBuildProviderRecoveryEpisode( plan, aContext );
}


AI_PROVIDER_RECOVERY_REPLAY_EXECUTION_RESULT
AI_AGENT_PANEL_MODEL::ExecuteChatProviderRecoveryReplayRequest(
        const AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT& aContext,
        const AI_PROVIDER_RECOVERY_REPLAY_EXECUTION_OPTIONS& aOptions,
        AI_ACCEPT_APPLY_ADAPTER& aAdapter ) const
{
    AI_PROVIDER_RECOVERY_REPLAY_REQUEST request =
            LatestChatProviderRecoveryReplayRequest( aContext );

    return AiExecuteProviderRecoveryReplayRequest( request, aOptions, aAdapter );
}


AI_PROVIDER_RECOVERY_REPLAY_EXECUTION_RESULT
AI_AGENT_PANEL_MODEL::ExecuteNextActionProviderRecoveryReplayRequest(
        const AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT& aContext,
        const AI_PROVIDER_RECOVERY_REPLAY_EXECUTION_OPTIONS& aOptions,
        AI_ACCEPT_APPLY_ADAPTER& aAdapter ) const
{
    AI_PROVIDER_RECOVERY_REPLAY_REQUEST request =
            LatestNextActionProviderRecoveryReplayRequest( aContext );

    return AiExecuteProviderRecoveryReplayRequest( request, aOptions, aAdapter );
}


void AI_AGENT_PANEL_MODEL::SetNextActionProvider( std::unique_ptr<AI_PROVIDER> aProvider )
{
    if( aProvider )
    {
        m_NextActionRuntime = std::make_unique<AI_NEXT_ACTION_RUNTIME>(
                std::move( aProvider ), m_NextActionValidationService,
                m_NextActionPreviewService );

        if( m_NextActionContextSampler )
            m_NextActionRuntime->SetCurrentContextSampler( m_NextActionContextSampler );

        if( m_PromptTraceStore )
            m_NextActionRuntime->SetPromptTraceStore( m_PromptTraceStore );

        if( m_MemoryStore )
            m_NextActionRuntime->SetMemoryStore( m_MemoryStore );

        if( m_LocalTextMemoryIndex )
            m_NextActionRuntime->SetLocalTextMemoryIndex( m_LocalTextMemoryIndex );

        if( m_ArtifactStore )
            m_NextActionRuntime->SetArtifactStore( m_ArtifactStore );

        if( m_NextActionSessionStore )
            m_NextActionRuntime->SetSessionStore( m_NextActionSessionStore );
    }
    else
    {
        m_NextActionRuntime.reset();
    }
}


void AI_AGENT_PANEL_MODEL::SetModelContextLengthChars( size_t aContextLengthChars )
{
    m_ProviderInputBudgetChars =
            AI_PROVIDER_SETTINGS::InputBudgetCharsForContextLength( aContextLengthChars );
}


void AI_AGENT_PANEL_MODEL::ConfigureNextActionServices(
        AI_SESSION_PREVIEW_SERVICE* aPreviewService,
        AI_SESSION_VALIDATION_SERVICE* aValidationService )
{
    m_NextActionPreviewService = aPreviewService;
    m_NextActionValidationService = aValidationService;

    if( m_NextActionRuntime )
        m_NextActionRuntime->SetServices( aValidationService, aPreviewService );
}


void AI_AGENT_PANEL_MODEL::ConfigureNextActionCurrentContextSampler(
        std::function<AI_NEXT_ACTION_CONTEXT_VERSION()> aSampler )
{
    m_NextActionContextSampler = std::move( aSampler );

    if( m_NextActionRuntime )
        m_NextActionRuntime->SetCurrentContextSampler( m_NextActionContextSampler );
}


bool AI_AGENT_PANEL_MODEL::TryAcquireDocumentWriteOwnership(
        const wxString& aOwnerNamespace,
        const AI_NEXT_ACTION_CONTEXT_VERSION& aContextVersion )
{
    if( aOwnerNamespace.IsEmpty() )
        return false;

    const wxString documentKey = documentWriteKey( aContextVersion );

    for( DOCUMENT_WRITE_OWNERSHIP& ownership : m_DocumentWriteOwnerships )
    {
        if( !documentWriteKeysConflict( ownership.m_DocumentKey, documentKey ) )
            continue;

        if( ownership.m_OwnerNamespace != aOwnerNamespace )
            return false;

        if( ownership.m_DocumentKey == documentKey )
        {
            ++ownership.m_Depth;
            return true;
        }
    }

    m_DocumentWriteOwnerships.push_back( DOCUMENT_WRITE_OWNERSHIP{
        aOwnerNamespace,
        documentKey,
        m_NextDocumentWriteLeaseId++
    } );

    return true;
}


bool AI_AGENT_PANEL_MODEL::ReleaseDocumentWriteOwnership(
        const wxString& aOwnerNamespace,
        const AI_NEXT_ACTION_CONTEXT_VERSION& aContextVersion )
{
    const wxString documentKey = documentWriteKey( aContextVersion );

    for( auto it = m_DocumentWriteOwnerships.begin();
         it != m_DocumentWriteOwnerships.end(); ++it )
    {
        if( it->m_OwnerNamespace != aOwnerNamespace
            || it->m_DocumentKey != documentKey )
        {
            continue;
        }

        if( it->m_Depth > 1 )
        {
            --it->m_Depth;
            return true;
        }

        m_DocumentWriteOwnerships.erase( it );
        return true;
    }

    return false;
}


std::optional<wxString> AI_AGENT_PANEL_MODEL::ActiveDocumentWriteOwnerNamespace() const
{
    if( m_DocumentWriteOwnerships.empty() )
        return std::nullopt;

    return m_DocumentWriteOwnerships.front().m_OwnerNamespace;
}


void AI_AGENT_PANEL_MODEL::ReloadDefaultProviders()
{
    AI_MODEL_CONFIG config = AI_MODEL_CONFIG_STORE::LoadUserConfig();
    SetModelContextLengthChars( config.m_ContextLengthChars );
    SetProvider( MakeAiProviderFromModelConfig( config ) );
    SetNextActionProvider( MakeAiProviderFromModelConfig( config ) );
}


void AI_AGENT_PANEL_MODEL::SetToolCallHandler( AI_TOOL_CALL_HANDLER* aHandler )
{
    m_ToolCallHandler = aHandler;
    m_Runtime.SetToolCallHandler( aHandler );
}


AI_ACTIVITY_RECORD AI_AGENT_PANEL_MODEL::RecordActivity( AI_ACTIVITY_RECORD aRecord )
{
    return m_ActivityLog.Append( std::move( aRecord ) );
}


void AI_AGENT_PANEL_MODEL::SetBackgroundAgentEnabled( bool aEnabled )
{
    m_BackgroundAgentEnabled = aEnabled;
}


std::vector<AI_ACTIVITY_RECORD> AI_AGENT_PANEL_MODEL::ActivityRecords() const
{
    return m_ActivityLog.Records();
}


std::vector<AI_AGENT_OBSERVABILITY_ENTRY> AI_AGENT_PANEL_MODEL::ObservabilityEntries(
        size_t aLimit ) const
{
    AI_AGENT_OBSERVABILITY_LOG formatter;
    std::vector<AI_NEXT_ACTION_REPLAY_TRACE_RECORD> nextActionReplayTraces;

    if( m_NextActionRuntime )
        nextActionReplayTraces = m_NextActionRuntime->ReplayTraceRecords();

    std::vector<AI_AGENT_OBSERVABILITY_ENTRY> entries =
            formatter.Build( m_Runtime.TraceRecords(), ActivityRecords(),
                             Suggestions(), nextActionReplayTraces, 0 );

    appendProviderRecoveryEntry( LatestChatProviderRecoveryPolicy(),
                                 LatestChatProviderRecoveryResumePlan(), entries );
    appendProviderRecoveryEntry( LatestNextActionProviderRecoveryPolicy(),
                                 LatestNextActionProviderRecoveryResumePlan(),
                                 entries );

    std::stable_sort( entries.begin(), entries.end(),
                      []( const AI_AGENT_OBSERVABILITY_ENTRY& aFirst,
                          const AI_AGENT_OBSERVABILITY_ENTRY& aSecond )
                      {
                          return aFirst.m_Sequence < aSecond.m_Sequence;
                      } );

    if( aLimit > 0 && entries.size() > aLimit )
        entries.erase( entries.begin(),
                       entries.end() - static_cast<std::ptrdiff_t>( aLimit ) );

    for( size_t ii = 0; ii < entries.size(); ++ii )
        entries[ii].m_Sequence = ii + 1;

    return entries;
}


void AI_AGENT_PANEL_MODEL::SetActiveWorkspaceContext( AI_AGENT_WORKSPACE_CONTEXT_KIND aMode )
{
    m_ActiveWorkspaceContext = aMode;

    if( !m_WorkspaceContextStates.contains( aMode ) )
    {
        AI_AGENT_WORKSPACE_CONTEXT_STATE state;
        state.m_ContextKind = aMode;
        m_WorkspaceContextStates.emplace( aMode, state );
    }
}


void AI_AGENT_PANEL_MODEL::SaveWorkspaceContextState( AI_AGENT_WORKSPACE_CONTEXT_STATE aState )
{
    const AI_AGENT_WORKSPACE_CONTEXT_KIND mode = aState.m_ContextKind;
    m_WorkspaceContextStates[mode] = std::move( aState );
}


AI_AGENT_WORKSPACE_CONTEXT_STATE AI_AGENT_PANEL_MODEL::ActiveWorkspaceContextState() const
{
    auto it = m_WorkspaceContextStates.find( m_ActiveWorkspaceContext );

    if( it != m_WorkspaceContextStates.end() )
        return it->second;

    AI_AGENT_WORKSPACE_CONTEXT_STATE state;
    state.m_ContextKind = m_ActiveWorkspaceContext;
    return state;
}


std::optional<AI_AGENT_WORKSPACE_CONTEXT_STATE> AI_AGENT_PANEL_MODEL::WorkspaceContextState(
        AI_AGENT_WORKSPACE_CONTEXT_KIND aMode ) const
{
    auto it = m_WorkspaceContextStates.find( aMode );

    if( it == m_WorkspaceContextStates.end() )
        return std::nullopt;

    return it->second;
}


std::vector<AI_AGENT_WORKSPACE_CONTEXT_STATE> AI_AGENT_PANEL_MODEL::WorkspaceContextStates() const
{
    std::vector<AI_AGENT_WORKSPACE_CONTEXT_STATE> states;
    states.reserve( m_WorkspaceContextStates.size() );

    for( const auto& [mode, state] : m_WorkspaceContextStates )
        states.push_back( state );

    return states;
}


std::optional<AI_SUGGESTION_RECORD>
AI_AGENT_PANEL_MODEL::UpdateSuggestionsIfBackgroundEnabled(
        AI_CONTEXT_SNAPSHOT aContextSnapshot, AI_ACTIVITY_RECORD aActivity,
        const wxString& aReason )
{
    if( !m_BackgroundAgentEnabled )
        return std::nullopt;

    if( !m_NextActionRuntime )
        return std::nullopt;

    refreshLocalTextResearchDirectory( aContextSnapshot );

    m_LastNextActionProjectId = aContextSnapshot.m_ProjectId;
    m_LastNextActionDocumentId = aContextSnapshot.m_DocumentId;

    AI_NEXT_ACTION_CONTEXT_VERSION ownershipContext =
            AiNextActionContextVersionFromSnapshot( aContextSnapshot,
                                                    aActivity.m_Sequence );

    if( !TryAcquireDocumentWriteOwnership( wxS( "nextaction" ),
                                           ownershipContext ) )
    {
        return std::nullopt;
    }

    const AI_CONTEXT_SNAPSHOT failureContext = aContextSnapshot;
    const AI_ACTIVITY_RECORD   failureActivity = aActivity;

    try
    {
        AI_SUGGESTION_TRIGGER trigger;
        trigger.m_EditorKind = aContextSnapshot.m_EditorKind;
        trigger.m_ContextVersion = aContextSnapshot.m_Version;
        trigger.m_ContextSnapshot = std::move( aContextSnapshot );
        trigger.m_Activity = std::move( aActivity );
        trigger.m_Reason = aReason;
        trigger.m_PreviewOnly = true;
        std::optional<AI_SUGGESTION_RECORD> suggestion =
                m_NextActionRuntime->Update( std::move( trigger ) );

        ReleaseDocumentWriteOwnership( wxS( "nextaction" ), ownershipContext );
        return suggestion;
    }
    catch( const std::exception& e )
    {
        ReleaseDocumentWriteOwnership( wxS( "nextaction" ), ownershipContext );
        RecordActivity( nextActionRuntimeFailureRecord(
                failureContext, failureActivity, aReason,
                wxS( "next_action_runtime_exception" ),
                wxString::FromUTF8( e.what() ) ) );
        return std::nullopt;
    }
    catch( ... )
    {
        ReleaseDocumentWriteOwnership( wxS( "nextaction" ), ownershipContext );
        RecordActivity( nextActionRuntimeFailureRecord(
                failureContext, failureActivity, aReason,
                wxS( "next_action_runtime_exception" ),
                wxS( "Next Action runtime failed with an unknown exception." ) ) );
        return std::nullopt;
    }
}


std::optional<AI_SUGGESTION_RECORD> AI_AGENT_PANEL_MODEL::AddSuggestion(
        AI_SUGGESTION_RECORD aSuggestion )
{
    return m_NextActionRuntime
           ? m_NextActionRuntime->AddPublishedSuggestion( std::move( aSuggestion ) )
           : std::nullopt;
}


std::vector<AI_SUGGESTION_RECORD> AI_AGENT_PANEL_MODEL::Suggestions() const
{
    return m_NextActionRuntime ? m_NextActionRuntime->Suggestions()
                               : std::vector<AI_SUGGESTION_RECORD>();
}


std::optional<AI_SUGGESTION_RECORD> AI_AGENT_PANEL_MODEL::FindSuggestion(
        uint64_t aSuggestionId ) const
{
    if( m_NextActionRuntime )
    {
        if( std::optional<AI_SUGGESTION_RECORD> suggestion =
                    m_NextActionRuntime->FindSuggestion( aSuggestionId ) )
        {
            return suggestion;
        }
    }

    return std::nullopt;
}


std::optional<uint64_t> AI_AGENT_PANEL_MODEL::LatestActiveSuggestionId() const
{
    std::vector<AI_SUGGESTION_RECORD> records = Suggestions();

    for( auto it = records.rbegin(); it != records.rend(); ++it )
    {
        if( it->m_Status == AI_SUGGESTION_STATUS::Pending
            || it->m_Status == AI_SUGGESTION_STATUS::Previewing )
        {
            return it->m_Id;
        }
    }

    return std::nullopt;
}


bool AI_AGENT_PANEL_MODEL::CanPreviewSuggestion( uint64_t aSuggestionId ) const
{
    if( m_NextActionRuntime && m_NextActionRuntime->CanPreview( aSuggestionId ) )
        return true;

    return false;
}


bool AI_AGENT_PANEL_MODEL::CanAcceptSuggestion( uint64_t aSuggestionId ) const
{
    if( m_NextActionRuntime && m_NextActionRuntime->CanAccept( aSuggestionId ) )
        return true;

    return false;
}


bool AI_AGENT_PANEL_MODEL::PreviewSuggestion( uint64_t aSuggestionId,
                                              AI_PREVIEW_MANAGER& aPreviewManager )
{
    if( m_NextActionRuntime && m_NextActionRuntime->BeginPreview( aSuggestionId,
                                                                  aPreviewManager ) )
    {
        return true;
    }

    return false;
}


bool AI_AGENT_PANEL_MODEL::AcceptSuggestion( uint64_t aSuggestionId,
                                             AI_EDIT_SESSION& aEditSession,
                                             const AI_NEXT_ACTION_CONTEXT_VERSION& aCurrentContextVersion )
{
    if( m_NextActionRuntime && m_NextActionRuntime->FindSuggestion( aSuggestionId ) )
    {
        if( !TryAcquireDocumentWriteOwnership( wxS( "nextaction" ),
                                               aCurrentContextVersion ) )
        {
            return false;
        }

        const bool accepted = m_NextActionRuntime->Accept(
                aSuggestionId, aEditSession, aCurrentContextVersion );

        ReleaseDocumentWriteOwnership( wxS( "nextaction" ),
                                       aCurrentContextVersion );
        return accepted;
    }

    wxUnusedVar( aEditSession );
    wxUnusedVar( aCurrentContextVersion );
    return false;
}


bool AI_AGENT_PANEL_MODEL::RecordSuggestionGateResult(
        uint64_t aSuggestionId, const wxString& aKey,
        const AI_NEXT_ACTION_GATE_RESULT& aGate )
{
    return m_NextActionRuntime
           && m_NextActionRuntime->RecordSuggestionGateResult(
                   aSuggestionId, aKey, aGate );
}


bool AI_AGENT_PANEL_MODEL::MarkSuggestionAccepted( uint64_t aSuggestionId )
{
    if( m_NextActionRuntime && m_NextActionRuntime->MarkAccepted( aSuggestionId ) )
        return true;

    return false;
}


bool AI_AGENT_PANEL_MODEL::RejectSuggestion( uint64_t aSuggestionId )
{
    if( m_NextActionRuntime && m_NextActionRuntime->Reject( aSuggestionId ) )
        return true;

    return false;
}


bool AI_AGENT_PANEL_MODEL::ExpireSuggestion( uint64_t aSuggestionId )
{
    return m_NextActionRuntime && m_NextActionRuntime->Expire( aSuggestionId );
}


size_t AI_AGENT_PANEL_MODEL::ExpireSuggestions( const AI_CONTEXT_VERSION& aCurrentVersion )
{
    return m_NextActionRuntime ? m_NextActionRuntime->ExpireStale( aCurrentVersion )
                               : 0;
}


size_t AI_AGENT_PANEL_MODEL::ExpireSuggestions(
        const AI_NEXT_ACTION_CONTEXT_VERSION& aCurrentVersion )
{
    return m_NextActionRuntime ? m_NextActionRuntime->ExpireStale( aCurrentVersion )
                               : 0;
}
