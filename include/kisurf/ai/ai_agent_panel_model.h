#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_activity_log.h>
#include <kisurf/ai/ai_next_action_runtime.h>
#include <kisurf/ai/ai_observability_log.h>
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

class KICOMMON_API AI_AGENT_PANEL_MODEL
{
public:
    explicit AI_AGENT_PANEL_MODEL( std::unique_ptr<AI_PROVIDER> aProvider );

    bool CanSend( const wxString& aText ) const;
    AI_PROVIDER_RESPONSE SendUserText( const wxString& aText, AI_EDITOR_KIND aEditorKind );
    AI_PROVIDER_RESPONSE SendUserText( const wxString& aText, AI_EDITOR_KIND aEditorKind,
                                       AI_CONTEXT_SNAPSHOT aContextSnapshot );

    bool CancelLastRequest();
    bool CancelRequest( uint64_t aRequestId );
    uint64_t LastRequestId() const { return m_LastRequestId; }
    bool LastRequestCancelled() const;
    void SetProvider( std::unique_ptr<AI_PROVIDER> aProvider );
    void SetNextActionProvider( std::unique_ptr<AI_PROVIDER> aProvider );
    void ConfigureNextActionServices( AI_SESSION_PREVIEW_SERVICE* aPreviewService,
                                      AI_SESSION_VALIDATION_SERVICE* aValidationService );
    void ConfigureNextActionCurrentContextSampler(
            std::function<AI_NEXT_ACTION_CONTEXT_VERSION()> aSampler );
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
    bool MarkSuggestionAccepted( uint64_t aSuggestionId );
    bool RejectSuggestion( uint64_t aSuggestionId );
    bool ExpireSuggestion( uint64_t aSuggestionId );
    size_t ExpireSuggestions( const AI_CONTEXT_VERSION& aCurrentVersion );
    size_t ExpireSuggestions( const AI_NEXT_ACTION_CONTEXT_VERSION& aCurrentVersion );

private:
    AI_ACTIVITY_LOG                         m_ActivityLog;
    AI_RUNTIME                              m_Runtime;
    std::unique_ptr<AI_NEXT_ACTION_RUNTIME> m_NextActionRuntime;
    std::vector<AI_AGENT_MESSAGE>           m_Messages;
    std::map<AI_AGENT_WORKSPACE_CONTEXT_KIND, AI_AGENT_WORKSPACE_CONTEXT_STATE>
            m_WorkspaceContextStates;
    AI_AGENT_WORKSPACE_CONTEXT_KIND m_ActiveWorkspaceContext =
            AI_AGENT_WORKSPACE_CONTEXT_KIND::General;
    bool                                    m_BackgroundAgentEnabled = false;
    uint64_t                                m_LastRequestId = 0;
    AI_SESSION_PREVIEW_SERVICE*             m_NextActionPreviewService = nullptr;
    AI_SESSION_VALIDATION_SERVICE*          m_NextActionValidationService = nullptr;
    std::function<AI_NEXT_ACTION_CONTEXT_VERSION()> m_NextActionContextSampler;
};
