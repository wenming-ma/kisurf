#include <kisurf/ai/ai_agent_panel_model.h>
#include <kisurf/ai/ai_agent_suggestion_provider.h>
#include <kisurf/ai/ai_next_action_provider.h>

#include <utility>

namespace
{
std::unique_ptr<AI_SUGGESTION_PROVIDER> makeDefaultSuggestionProvider()
{
    auto controller = std::make_unique<AI_NEXT_ACTION_CONTROLLER>();
    controller->AddProvider( std::make_unique<AI_VIA_PATTERN_NEXT_ACTION_PROVIDER>() );
    controller->AddProvider( std::make_unique<AI_ROUTING_SEGMENT_NEXT_ACTION_PROVIDER>() );
    controller->AddProvider( std::make_unique<AI_PANEL_TABLE_NEXT_ACTION_PROVIDER>() );
    controller->AddProvider( std::make_unique<AI_AGENT_SUGGESTION_PROVIDER>(
            MakeDefaultAiProvider() ) );
    return controller;
}
} // namespace

AI_AGENT_PANEL_MODEL::AI_AGENT_PANEL_MODEL( std::unique_ptr<AI_PROVIDER> aProvider ) :
        AI_AGENT_PANEL_MODEL( std::move( aProvider ), makeDefaultSuggestionProvider() )
{
}


AI_AGENT_PANEL_MODEL::AI_AGENT_PANEL_MODEL(
        std::unique_ptr<AI_PROVIDER> aProvider,
        std::unique_ptr<AI_SUGGESTION_PROVIDER> aSuggestionProvider ) :
        m_ActivityLog( 256 ),
        m_Runtime( std::move( aProvider ), m_ActivityLog ),
        m_SuggestionProvider( std::move( aSuggestionProvider ) )
{
    if( m_SuggestionProvider )
    {
        m_SuggestionOrchestrator =
                std::make_unique<AI_SUGGESTION_ORCHESTRATOR>( *m_SuggestionProvider );
    }
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

    m_Messages.push_back( { wxS( "user" ), aText } );

    AI_PROVIDER_RESPONSE response = m_Runtime.Submit( request );
    m_LastRequestId = response.m_RequestId;

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


void AI_AGENT_PANEL_MODEL::SetSuggestionProvider(
        std::unique_ptr<AI_SUGGESTION_PROVIDER> aSuggestionProvider )
{
    m_SuggestionProvider = std::move( aSuggestionProvider );

    if( m_SuggestionProvider )
    {
        m_SuggestionOrchestrator =
                std::make_unique<AI_SUGGESTION_ORCHESTRATOR>( *m_SuggestionProvider );
    }
    else
    {
        m_SuggestionOrchestrator.reset();
    }
}


void AI_AGENT_PANEL_MODEL::ReloadDefaultProviders()
{
    SetProvider( MakeDefaultAiProvider() );
    SetSuggestionProvider( makeDefaultSuggestionProvider() );
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
    return formatter.Build( m_Runtime.TraceRecords(), ActivityRecords(), Suggestions(), aLimit );
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


std::optional<AI_SUGGESTION_RECORD> AI_AGENT_PANEL_MODEL::UpdateSuggestions(
        AI_CONTEXT_SNAPSHOT aContextSnapshot, AI_ACTIVITY_RECORD aActivity,
        const wxString& aReason )
{
    if( !m_SuggestionOrchestrator )
        return std::nullopt;

    AI_SUGGESTION_TRIGGER trigger;
    trigger.m_EditorKind = aContextSnapshot.m_EditorKind;
    trigger.m_ContextVersion = aContextSnapshot.m_Version;
    trigger.m_ContextSnapshot = std::move( aContextSnapshot );
    trigger.m_Activity = std::move( aActivity );
    trigger.m_Reason = aReason;
    return m_SuggestionOrchestrator->Update( std::move( trigger ) );
}


std::optional<AI_SUGGESTION_RECORD>
AI_AGENT_PANEL_MODEL::UpdateSuggestionsIfBackgroundEnabled(
        AI_CONTEXT_SNAPSHOT aContextSnapshot, AI_ACTIVITY_RECORD aActivity,
        const wxString& aReason )
{
    if( !m_BackgroundAgentEnabled )
        return std::nullopt;

    if( !m_SuggestionOrchestrator )
        return std::nullopt;

    AI_SUGGESTION_TRIGGER trigger;
    trigger.m_EditorKind = aContextSnapshot.m_EditorKind;
    trigger.m_ContextVersion = aContextSnapshot.m_Version;
    trigger.m_ContextSnapshot = std::move( aContextSnapshot );
    trigger.m_Activity = std::move( aActivity );
    trigger.m_Reason = aReason;
    trigger.m_PreviewOnly = true;
    return m_SuggestionOrchestrator->Update( std::move( trigger ) );
}


std::optional<AI_SUGGESTION_RECORD> AI_AGENT_PANEL_MODEL::AddSuggestion(
        AI_SUGGESTION_RECORD aSuggestion )
{
    return m_SuggestionOrchestrator
           ? m_SuggestionOrchestrator->AddSuggestion( std::move( aSuggestion ) )
           : std::nullopt;
}


std::vector<AI_SUGGESTION_RECORD> AI_AGENT_PANEL_MODEL::Suggestions() const
{
    return m_SuggestionOrchestrator ? m_SuggestionOrchestrator->Records()
                                    : std::vector<AI_SUGGESTION_RECORD>();
}


std::optional<AI_SUGGESTION_RECORD> AI_AGENT_PANEL_MODEL::FindSuggestion(
        uint64_t aSuggestionId ) const
{
    return m_SuggestionOrchestrator ? m_SuggestionOrchestrator->Find( aSuggestionId )
                                    : std::nullopt;
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
    return m_SuggestionOrchestrator
           && m_SuggestionOrchestrator->CanPreview( aSuggestionId );
}


bool AI_AGENT_PANEL_MODEL::CanAcceptSuggestion( uint64_t aSuggestionId ) const
{
    return m_SuggestionOrchestrator && m_SuggestionOrchestrator->CanAccept( aSuggestionId );
}


bool AI_AGENT_PANEL_MODEL::PreviewSuggestion( uint64_t aSuggestionId,
                                              AI_PREVIEW_MANAGER& aPreviewManager )
{
    return m_SuggestionOrchestrator
           && m_SuggestionOrchestrator->BeginPreview( aSuggestionId, aPreviewManager );
}


bool AI_AGENT_PANEL_MODEL::AcceptSuggestion( uint64_t aSuggestionId,
                                             AI_EDIT_SESSION& aEditSession )
{
    return m_SuggestionOrchestrator
           && m_SuggestionOrchestrator->Accept( aSuggestionId, aEditSession );
}


bool AI_AGENT_PANEL_MODEL::MarkSuggestionAccepted( uint64_t aSuggestionId )
{
    return m_SuggestionOrchestrator
           && m_SuggestionOrchestrator->MarkAccepted( aSuggestionId );
}


bool AI_AGENT_PANEL_MODEL::RejectSuggestion( uint64_t aSuggestionId )
{
    return m_SuggestionOrchestrator && m_SuggestionOrchestrator->Reject( aSuggestionId );
}


size_t AI_AGENT_PANEL_MODEL::ExpireSuggestions( const AI_CONTEXT_VERSION& aCurrentVersion )
{
    return m_SuggestionOrchestrator ? m_SuggestionOrchestrator->ExpireStale( aCurrentVersion ) : 0;
}
