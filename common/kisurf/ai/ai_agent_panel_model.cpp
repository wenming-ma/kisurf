#include <kisurf/ai/ai_agent_panel_model.h>
#include <kisurf/ai/ai_provider.h>

#include <utility>

namespace
{
wxString documentWriteKey( const AI_NEXT_ACTION_CONTEXT_VERSION& aContextVersion )
{
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
} // namespace


AI_AGENT_PANEL_MODEL::AI_AGENT_PANEL_MODEL( std::unique_ptr<AI_PROVIDER> aProvider ) :
        m_ActivityLog( 256 ),
        m_Runtime( std::move( aProvider ), m_ActivityLog ),
        m_NextActionRuntime( std::make_unique<AI_NEXT_ACTION_RUNTIME>(
                MakeDefaultAiProvider(), m_NextActionValidationService,
                m_NextActionPreviewService ) )
{
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
    AI_PROVIDER_REQUEST request;
    request.m_EditorKind = aEditorKind;
    request.m_ContextSnapshot = std::move( aContextSnapshot );

    if( request.m_ContextSnapshot.m_EditorKind == AI_EDITOR_KIND::Unknown )
        request.m_ContextSnapshot.m_EditorKind = aEditorKind;

    std::vector<AI_ACTIVITY_RECORD> activity = ActivityRecords();
    request.m_ContextSnapshot.m_RecentActivity.insert(
            request.m_ContextSnapshot.m_RecentActivity.end(), activity.begin(), activity.end() );

    request.m_ContextVersion = request.m_ContextSnapshot.m_Version;
    request.m_UserText = aText;
    request.m_MaxToolRounds = 6;

    m_Messages.push_back( { wxS( "user" ), aText } );

    AI_NEXT_ACTION_CONTEXT_VERSION ownershipContext;
    ownershipContext.m_ContextVersion = request.m_ContextVersion;
    const bool chatOwnsDocument =
            TryAcquireDocumentWriteOwnership( wxS( "chat" ), ownershipContext );

    if( !chatOwnsDocument )
    {
        AI_PROVIDER_RESPONSE response;
        response.m_Title = wxS( "Document write ownership unavailable" );
        response.m_Body =
                wxS( "Cannot run chat tool calls because document write ownership "
                     "is currently held by another AI runtime." );
        m_Messages.push_back( { wxS( "assistant" ), response.m_Body } );
        return response;
    }

    AI_PROVIDER_RESPONSE response = m_Runtime.Submit( request );
    m_LastRequestId = response.m_RequestId;

    ReleaseDocumentWriteOwnership( wxS( "chat" ), ownershipContext );

    m_Messages.push_back( { wxS( "assistant" ), response.m_Body } );

    return response;
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


void AI_AGENT_PANEL_MODEL::SetProvider( std::unique_ptr<AI_PROVIDER> aProvider )
{
    m_Runtime.SetProvider( std::move( aProvider ) );
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
    }
    else
    {
        m_NextActionRuntime.reset();
    }
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

    if( m_DocumentWriteOwnership
        && documentWriteKeysConflict( m_DocumentWriteOwnership->m_DocumentKey,
                                      documentKey ) )
    {
        if( m_DocumentWriteOwnership->m_OwnerNamespace != aOwnerNamespace )
            return false;

        ++m_DocumentWriteOwnership->m_Depth;
        return true;
    }

    m_DocumentWriteOwnership = DOCUMENT_WRITE_OWNERSHIP{
        aOwnerNamespace,
        documentKey,
        m_NextDocumentWriteLeaseId++
    };

    return true;
}


bool AI_AGENT_PANEL_MODEL::ReleaseDocumentWriteOwnership(
        const wxString& aOwnerNamespace,
        const AI_NEXT_ACTION_CONTEXT_VERSION& aContextVersion )
{
    if( !m_DocumentWriteOwnership )
        return false;

    if( m_DocumentWriteOwnership->m_OwnerNamespace != aOwnerNamespace )
        return false;

    if( m_DocumentWriteOwnership->m_DocumentKey != documentWriteKey( aContextVersion ) )
        return false;

    if( m_DocumentWriteOwnership->m_Depth > 1 )
    {
        --m_DocumentWriteOwnership->m_Depth;
        return true;
    }

    m_DocumentWriteOwnership.reset();
    return true;
}


std::optional<wxString> AI_AGENT_PANEL_MODEL::ActiveDocumentWriteOwnerNamespace() const
{
    if( !m_DocumentWriteOwnership )
        return std::nullopt;

    return m_DocumentWriteOwnership->m_OwnerNamespace;
}


void AI_AGENT_PANEL_MODEL::ReloadDefaultProviders()
{
    SetProvider( MakeDefaultAiProvider() );
    SetNextActionProvider( MakeDefaultAiProvider() );
}


void AI_AGENT_PANEL_MODEL::SetToolCallHandler( AI_TOOL_CALL_HANDLER* aHandler )
{
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

    return formatter.Build( m_Runtime.TraceRecords(), ActivityRecords(), Suggestions(),
                            nextActionReplayTraces, aLimit );
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

    AI_NEXT_ACTION_CONTEXT_VERSION ownershipContext =
            AiNextActionContextVersionFromSnapshot( aContextSnapshot,
                                                    aActivity.m_Sequence );

    if( !TryAcquireDocumentWriteOwnership( wxS( "nextaction" ),
                                           ownershipContext ) )
    {
        return std::nullopt;
    }

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
