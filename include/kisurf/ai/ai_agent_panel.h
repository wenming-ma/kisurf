#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_activity_log.h>
#include <kisurf/ai/ai_agent_panel_base.h>
#include <kisurf/ai/ai_agent_panel_model.h>
#include <kisurf/ai/ai_runtime.h>
#include <kisurf/ai/ai_semantic_ui.h>
#include <kisurf/ai/ai_tool_execution.h>

#include <functional>
#include <memory>
#include <vector>

class wxCommandEvent;
class AI_ACCEPT_APPLY_ADAPTER;
class AI_SESSION_PREVIEW_SERVICE;
class AI_SESSION_SHADOW_BOARD_SEEDER;
class AI_SESSION_VALIDATION_SERVICE;

struct KICOMMON_API AI_AGENT_COMPOSER_STATUS_VIEW
{
    bool                 m_BackgroundAgentEnabled = false;
    bool                 m_InputHasText = false;
    bool                 m_HasActiveSuggestion = false;
    uint64_t             m_LatestRequestId = 0;
    bool                 m_LastRequestCancelled = false;
};

enum class AI_AGENT_PREVIEW_SHORTCUT_ACTION
{
    Ignore,
    Accept,
    Reject
};

struct KICOMMON_API AI_AGENT_PREVIEW_SHORTCUT_VIEW
{
    bool m_BackgroundAgentEnabled = false;
    bool m_HasActiveSuggestion = false;
    bool m_FocusInsideAgentPanel = false;
    bool m_HasModifier = false;
};

struct KICOMMON_API AI_AGENT_BACKGROUND_PREVIEW_VIEW
{
    bool m_BackgroundAgentEnabled = false;
    bool m_HasNewSuggestion = false;
    bool m_HasPreviewHandler = false;
    bool m_CanPreviewSuggestion = false;
    bool m_TargetsWorkspacePreview = false;
};

class KICOMMON_API AI_AGENT_PANEL : public AI_AGENT_PANEL_BASE
{
public:
    using CONTEXT_PROVIDER = std::function<AI_CONTEXT_SNAPSHOT()>;
    using SUGGESTION_PREVIEW_HANDLER =
            std::function<bool( AI_AGENT_PANEL_MODEL& aModel, uint64_t aSuggestionId )>;
    using SUGGESTION_ACCEPT_HANDLER =
            std::function<bool( AI_AGENT_PANEL_MODEL& aModel, uint64_t aSuggestionId )>;
    using SUGGESTION_REJECT_HANDLER =
            std::function<bool( AI_AGENT_PANEL_MODEL& aModel, uint64_t aSuggestionId )>;

    AI_AGENT_PANEL( wxWindow* aParent, AI_EDITOR_KIND aEditorKind,
                    CONTEXT_PROVIDER aContextProvider = CONTEXT_PROVIDER() );

    void SendCurrentText();
    void RefreshTranscript();
    void RefreshSuggestions();
    void RefreshLog();
    void RecordActivity( AI_ACTIVITY_RECORD aRecord );
    void SetBackgroundAgentEnabled( bool aEnabled );
    bool BackgroundAgentEnabled() const;
    bool ShowModelSettingsDialog();
    AI_SEMANTIC_UI_TREE SemanticUiTree() const;
    AI_PANEL_STATE_RECORD SemanticPanelStateRecord() const;
    AI_SEMANTIC_UI_ACTION_RESULT InvokeSemanticUiAction(
            const AI_SEMANTIC_UI_ACTION_REQUEST& aRequest );
    void ConfigureActionToolCalls(
            std::unique_ptr<AI_ACTION_RUNNER> aRunner,
            const std::vector<wxString>& aAllowlistedActions,
            std::vector<AI_ACTION_DESCRIPTOR> aFallbackActions = {},
            AI_ACCEPT_APPLY_ADAPTER* aAcceptAdapter = nullptr,
            AI_SESSION_PREVIEW_SERVICE* aPreviewService = nullptr,
            AI_SESSION_SHADOW_BOARD_SEEDER* aShadowBoardSeeder = nullptr,
            AI_SESSION_VALIDATION_SERVICE* aValidationService = nullptr );
    void ConfigureSuggestionReview( SUGGESTION_PREVIEW_HANDLER aPreviewHandler,
                                    SUGGESTION_ACCEPT_HANDLER aAcceptHandler,
                                    SUGGESTION_REJECT_HANDLER aRejectHandler =
                                            SUGGESTION_REJECT_HANDLER() );
    bool PreviewLatestSuggestion();
    bool AcceptLatestSuggestion();
    bool RejectLatestSuggestion();
    bool HandlePreviewShortcut( int aKeyCode, bool aHasModifier = false,
                                bool aFocusInsideAgentPanel = false );
    bool HandlePreviewPointer( bool aFocusInsideAgentPanel = false );

private:
    void OnModelSettings( wxCommandEvent& aEvent ) override;
    void OnBackgroundAgentToggled( wxCommandEvent& aEvent ) override;
    void OnPromptTextChanged( wxCommandEvent& aEvent ) override;
    void OnPromptEnter( wxCommandEvent& aEvent ) override;
    void OnSend( wxCommandEvent& aEvent ) override;
    void OnStop( wxCommandEvent& aEvent ) override;
    void OnPreviewSuggestion( wxCommandEvent& aEvent ) override;
    void OnAcceptSuggestion( wxCommandEvent& aEvent ) override;
    void OnRejectSuggestion( wxCommandEvent& aEvent ) override;

    bool acceptActionPreviewSuggestion( uint64_t aSuggestionId );
    AI_CONTEXT_SNAPSHOT contextSnapshotWithPanelState() const;
    void updateModeControls();
    void updateComposerStatus();
    void saveWorkspaceContextStateFromContext( const AI_CONTEXT_SNAPSHOT& aSnapshot,
                                               const AI_ACTIVITY_RECORD& aActivity );

    AI_EDITOR_KIND                            m_EditorKind;
    CONTEXT_PROVIDER                          m_ContextProvider;
    std::unique_ptr<AI_AGENT_PANEL_MODEL>     m_Model;
    AI_TOOL_EXECUTION_POLICY                  m_ToolExecutionPolicy;
    AI_ACTIVITY_LOG                           m_ToolActivityLog;
    std::unique_ptr<AI_ACTION_RUNNER>         m_ActionRunner;
    std::unique_ptr<AI_TOOL_CALL_HANDLER>        m_ToolCallHandler;
    SUGGESTION_PREVIEW_HANDLER                m_PreviewSuggestionHandler;
    SUGGESTION_ACCEPT_HANDLER                 m_AcceptSuggestionHandler;
    SUGGESTION_REJECT_HANDLER                 m_RejectSuggestionHandler;
};

KICOMMON_API wxString AiAgentWorkspaceContextTitle( AI_AGENT_WORKSPACE_CONTEXT_KIND aMode );
KICOMMON_API AI_AGENT_WORKSPACE_CONTEXT_KIND AiAgentWorkspaceContextForToolState(
        AI_TOOL_STATE_KIND aKind );
KICOMMON_API wxString AiAgentTranscriptHtml(
        const std::vector<AI_AGENT_MESSAGE>& aMessages );
KICOMMON_API wxString AiAgentSuggestionSummary( const AI_SUGGESTION_RECORD& aSuggestion );
KICOMMON_API wxString AiAgentObservabilityEntryText(
        const AI_AGENT_OBSERVABILITY_ENTRY& aEntry );
KICOMMON_API wxString AiAgentComposerStatusText(
        const AI_AGENT_COMPOSER_STATUS_VIEW& aView );
KICOMMON_API AI_AGENT_PREVIEW_SHORTCUT_ACTION AiAgentPreviewShortcutAction(
        const AI_AGENT_PREVIEW_SHORTCUT_VIEW& aView, int aKeyCode );
KICOMMON_API AI_AGENT_PREVIEW_SHORTCUT_ACTION AiAgentPreviewPointerAction(
        const AI_AGENT_PREVIEW_SHORTCUT_VIEW& aView );
KICOMMON_API bool AiAgentSuggestionTargetsWorkspacePreview(
        const AI_SUGGESTION_RECORD& aSuggestion );
KICOMMON_API bool AiAgentShouldAutoPreviewBackgroundSuggestion(
        const AI_AGENT_BACKGROUND_PREVIEW_VIEW& aView );
