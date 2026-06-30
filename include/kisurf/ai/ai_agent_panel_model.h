#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_activity_log.h>
#include <kisurf/ai/ai_artifact_store.h>
#include <kisurf/ai/ai_chat_session_store.h>
#include <kisurf/ai/ai_local_text_memory.h>
#include <kisurf/ai/ai_memory_store.h>
#include <kisurf/ai/ai_next_action_runtime.h>
#include <kisurf/ai/ai_next_action_session_store.h>
#include <kisurf/ai/ai_observability_log.h>
#include <kisurf/ai/ai_prompt_trace_store.h>
#include <kisurf/ai/ai_runtime.h>
#include <kisurf/ai/ai_types.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>
#include <wx/string.h>

struct KICOMMON_API AI_AGENT_MESSAGE
{
    wxString m_Role;
    wxString m_Text;
};

struct KICOMMON_API AI_CHAT_REQUEST_STATE
{
    AI_PROVIDER_REQUEST            m_Request;
    AI_NEXT_ACTION_CONTEXT_VERSION m_OwnershipContext;
    bool                           m_DocumentWriteOwned = false;
    bool                           m_PreflightCompleted = false;
    AI_PROVIDER_RESPONSE           m_PreflightResponse;
};

class KICOMMON_API AI_AGENT_PANEL_MODEL
{
public:
    explicit AI_AGENT_PANEL_MODEL( std::unique_ptr<AI_PROVIDER> aProvider );

    bool CanSend( const wxString& aText ) const;
    AI_PROVIDER_RESPONSE SendUserText( const wxString& aText, AI_EDITOR_KIND aEditorKind );
    AI_PROVIDER_RESPONSE SendUserText( const wxString& aText, AI_EDITOR_KIND aEditorKind,
                                       AI_CONTEXT_SNAPSHOT aContextSnapshot );
    AI_CHAT_REQUEST_STATE PrepareUserTextRequest(
            const wxString& aText, AI_EDITOR_KIND aEditorKind,
            AI_CONTEXT_SNAPSHOT aContextSnapshot );
    AI_PROVIDER_RESPONSE ExecutePreparedChatRequest(
            const AI_CHAT_REQUEST_STATE& aState );
    AI_PROVIDER_RESPONSE ExecutePreparedChatRequest(
            const AI_CHAT_REQUEST_STATE& aState,
            AI_RUNTIME_STREAM_EVENT_SINK aEventSink );
    AI_PROVIDER_RESPONSE FinishPreparedChatRequest(
            AI_CHAT_REQUEST_STATE aState, AI_PROVIDER_RESPONSE aResponse );

    bool CancelLastRequest();
    bool CancelRequest( uint64_t aRequestId );
    uint64_t LastRequestId() const { return m_LastRequestId; }
    bool LastRequestCancelled() const;
    void StartNewChat();
    uint64_t ActiveChatSessionId() const { return m_ActiveChatSessionId; }
    void SetProvider( std::unique_ptr<AI_PROVIDER> aProvider );
    void SetNextActionProvider( std::unique_ptr<AI_PROVIDER> aProvider );
    void SetModelContextLengthChars( size_t aContextLengthChars );
    void SetPromptTraceStore( AI_PROMPT_TRACE_STORE* aStore );
    const wxString& PromptTraceStorePath() const;
    void SetMemoryStore( AI_MEMORY_STORE* aStore );
    const wxString& MemoryStorePath() const;
    void SetLocalTextMemoryIndex( AI_LOCAL_TEXT_MEMORY_INDEX* aIndex );
    void SetLocalTextResearchDirectory( const wxString& aDirectory );
    const wxString& LocalTextResearchDirectory() const
    {
        return m_LocalTextResearchDirectory;
    }
    bool LoadLocalTextResearchDirectory( const wxString& aDirectory,
                                         const wxString& aProjectId,
                                         const wxString& aDocumentId,
                                         wxString& aError );
    void SetArtifactStore( AI_ARTIFACT_STORE* aStore );
    const wxString& ArtifactStorePath() const;
    void SetChatSessionStore( AI_CHAT_SESSION_STORE* aStore );
    const wxString& ChatSessionStoreDirectory() const;
    void SetNextActionSessionStore( AI_NEXT_ACTION_SESSION_STORE* aStore );
    const wxString& NextActionSessionStoreDirectory() const;
    AI_PROVIDER_RECOVERY_POLICY LatestChatProviderRecoveryPolicy() const;
    AI_PROVIDER_RECOVERY_POLICY LatestNextActionProviderRecoveryPolicy() const;
    AI_PROVIDER_RECOVERY_RESUME_PACKET LatestChatProviderRecoveryResumePacket() const;
    AI_PROVIDER_RECOVERY_RESUME_PACKET LatestNextActionProviderRecoveryResumePacket() const;
    AI_PROVIDER_RECOVERY_RESUME_PLAN LatestChatProviderRecoveryResumePlan() const;
    AI_PROVIDER_RECOVERY_RESUME_PLAN LatestNextActionProviderRecoveryResumePlan() const;
    AI_PROVIDER_RECOVERY_PREFLIGHT_RESULT LatestChatProviderRecoveryPreflight(
            const AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT& aContext ) const;
    AI_PROVIDER_RECOVERY_PREFLIGHT_RESULT LatestNextActionProviderRecoveryPreflight(
            const AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT& aContext ) const;
    AI_PROVIDER_RECOVERY_REPLAY_REQUEST LatestChatProviderRecoveryReplayRequest(
            const AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT& aContext ) const;
    AI_PROVIDER_RECOVERY_REPLAY_REQUEST LatestNextActionProviderRecoveryReplayRequest(
            const AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT& aContext ) const;
    AI_PROVIDER_RECOVERY_EPISODE LatestChatProviderRecoveryEpisode(
            const AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT& aContext ) const;
    AI_PROVIDER_RECOVERY_EPISODE LatestNextActionProviderRecoveryEpisode(
            const AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT& aContext ) const;
    AI_PROVIDER_RECOVERY_REPLAY_EXECUTION_RESULT
    ExecuteChatProviderRecoveryReplayRequest(
            const AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT& aContext,
            const AI_PROVIDER_RECOVERY_REPLAY_EXECUTION_OPTIONS& aOptions,
            AI_ACCEPT_APPLY_ADAPTER& aAdapter ) const;
    AI_PROVIDER_RECOVERY_REPLAY_EXECUTION_RESULT
    ExecuteNextActionProviderRecoveryReplayRequest(
            const AI_PROVIDER_RECOVERY_PREFLIGHT_CONTEXT& aContext,
            const AI_PROVIDER_RECOVERY_REPLAY_EXECUTION_OPTIONS& aOptions,
            AI_ACCEPT_APPLY_ADAPTER& aAdapter ) const;
    void ConfigureNextActionServices( AI_SESSION_PREVIEW_SERVICE* aPreviewService,
                                      AI_SESSION_VALIDATION_SERVICE* aValidationService );
    void ConfigureNextActionCurrentContextSampler(
            std::function<AI_NEXT_ACTION_CONTEXT_VERSION()> aSampler );
    bool TryAcquireDocumentWriteOwnership(
            const wxString& aOwnerNamespace,
            const AI_NEXT_ACTION_CONTEXT_VERSION& aContextVersion );
    bool ReleaseDocumentWriteOwnership(
            const wxString& aOwnerNamespace,
            const AI_NEXT_ACTION_CONTEXT_VERSION& aContextVersion );
    std::optional<wxString> ActiveDocumentWriteOwnerNamespace() const;
    void ReloadDefaultProviders();
    void SetToolCallHandler( AI_TOOL_CALL_HANDLER* aHandler );

    const std::vector<AI_AGENT_MESSAGE>& Messages() const { return m_Messages; }
    AI_ACTIVITY_RECORD RecordActivity( AI_ACTIVITY_RECORD aRecord );
    std::vector<AI_ACTIVITY_RECORD> ActivityRecords() const;
    void SetBackgroundAgentEnabled( bool aEnabled );
    bool BackgroundAgentEnabled() const { return m_BackgroundAgentEnabled; }
    std::vector<AI_AGENT_OBSERVABILITY_ENTRY> ObservabilityEntries(
            size_t aLimit = 128 ) const;

    void SetActiveWorkspaceContext( AI_AGENT_WORKSPACE_CONTEXT_KIND aMode );
    AI_AGENT_WORKSPACE_CONTEXT_KIND ActiveWorkspaceContext() const
    {
        return m_ActiveWorkspaceContext;
    }
    void SaveWorkspaceContextState( AI_AGENT_WORKSPACE_CONTEXT_STATE aState );
    AI_AGENT_WORKSPACE_CONTEXT_STATE ActiveWorkspaceContextState() const;
    std::optional<AI_AGENT_WORKSPACE_CONTEXT_STATE> WorkspaceContextState(
            AI_AGENT_WORKSPACE_CONTEXT_KIND aMode ) const;
    std::vector<AI_AGENT_WORKSPACE_CONTEXT_STATE> WorkspaceContextStates() const;

    std::optional<AI_SUGGESTION_RECORD> UpdateSuggestionsIfBackgroundEnabled(
            AI_CONTEXT_SNAPSHOT aContextSnapshot, AI_ACTIVITY_RECORD aActivity,
            const wxString& aReason );
    std::optional<AI_SUGGESTION_RECORD> AddSuggestion( AI_SUGGESTION_RECORD aSuggestion );
    std::vector<AI_SUGGESTION_RECORD> Suggestions() const;
    std::optional<AI_SUGGESTION_RECORD> FindSuggestion( uint64_t aSuggestionId ) const;
    std::optional<uint64_t> LatestActiveSuggestionId() const;
    bool CanPreviewSuggestion( uint64_t aSuggestionId ) const;
    bool CanAcceptSuggestion( uint64_t aSuggestionId ) const;
    bool PreviewSuggestion( uint64_t aSuggestionId, AI_PREVIEW_MANAGER& aPreviewManager );
    bool AcceptSuggestion( uint64_t aSuggestionId, AI_EDIT_SESSION& aEditSession,
                           const AI_NEXT_ACTION_CONTEXT_VERSION& aCurrentContextVersion );
    bool RecordSuggestionGateResult( uint64_t aSuggestionId, const wxString& aKey,
                                     const AI_NEXT_ACTION_GATE_RESULT& aGate );
    bool MarkSuggestionAccepted( uint64_t aSuggestionId );
    bool RejectSuggestion( uint64_t aSuggestionId );
    bool ExpireSuggestion( uint64_t aSuggestionId );
    size_t ExpireSuggestions( const AI_CONTEXT_VERSION& aCurrentVersion );
    size_t ExpireSuggestions( const AI_NEXT_ACTION_CONTEXT_VERSION& aCurrentVersion );

private:
    void refreshLocalTextResearchDirectory( const AI_CONTEXT_SNAPSHOT& aContextSnapshot );
    void persistActiveChatSession();

    AI_ACTIVITY_LOG                         m_ActivityLog;
    std::unique_ptr<AI_PROMPT_TRACE_STORE>  m_DefaultPromptTraceStore;
    AI_PROMPT_TRACE_STORE*                  m_PromptTraceStore = nullptr;
    std::unique_ptr<AI_MEMORY_STORE>        m_DefaultMemoryStore;
    AI_MEMORY_STORE*                        m_MemoryStore = nullptr;
    std::unique_ptr<AI_LOCAL_TEXT_MEMORY_INDEX> m_DefaultLocalTextMemoryIndex;
    AI_LOCAL_TEXT_MEMORY_INDEX*             m_LocalTextMemoryIndex = nullptr;
    wxString                                m_LocalTextResearchDirectory;
    std::unique_ptr<AI_ARTIFACT_STORE>      m_DefaultArtifactStore;
    AI_ARTIFACT_STORE*                      m_ArtifactStore = nullptr;
    std::unique_ptr<AI_CHAT_SESSION_STORE>  m_DefaultChatSessionStore;
    AI_CHAT_SESSION_STORE*                  m_ChatSessionStore = nullptr;
    std::unique_ptr<AI_NEXT_ACTION_SESSION_STORE> m_DefaultNextActionSessionStore;
    AI_NEXT_ACTION_SESSION_STORE*           m_NextActionSessionStore = nullptr;
    AI_RUNTIME                              m_Runtime;
    AI_TOOL_CALL_HANDLER*                   m_ToolCallHandler = nullptr;
    std::unique_ptr<AI_NEXT_ACTION_RUNTIME> m_NextActionRuntime;
    std::vector<AI_AGENT_MESSAGE>           m_Messages;
    std::map<AI_AGENT_WORKSPACE_CONTEXT_KIND, AI_AGENT_WORKSPACE_CONTEXT_STATE>
            m_WorkspaceContextStates;
    AI_AGENT_WORKSPACE_CONTEXT_KIND m_ActiveWorkspaceContext =
            AI_AGENT_WORKSPACE_CONTEXT_KIND::General;
    bool                                    m_BackgroundAgentEnabled = false;
    uint64_t                                m_LastRequestId = 0;
    uint64_t                                m_ActiveChatSessionId = 1;
    uint64_t                                m_ChatActivityBoundarySequence = 0;
    size_t                                  m_ProviderInputBudgetChars = 160000;
    wxString                                m_LastChatProjectId;
    wxString                                m_LastChatDocumentId;
    wxString                                m_LastNextActionProjectId;
    wxString                                m_LastNextActionDocumentId;
    AI_SESSION_PREVIEW_SERVICE*             m_NextActionPreviewService = nullptr;
    AI_SESSION_VALIDATION_SERVICE*          m_NextActionValidationService = nullptr;
    std::function<AI_NEXT_ACTION_CONTEXT_VERSION()> m_NextActionContextSampler;

    struct DOCUMENT_WRITE_OWNERSHIP
    {
        wxString m_OwnerNamespace;
        wxString m_DocumentKey;
        uint64_t m_LeaseId = 0;
        uint64_t m_Depth = 1;
    };

    std::vector<DOCUMENT_WRITE_OWNERSHIP>   m_DocumentWriteOwnerships;
    uint64_t                                m_NextDocumentWriteLeaseId = 1;
};
