#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_activity_log.h>
#include <kisurf/ai/ai_agent_panel_base.h>
#include <kisurf/ai/ai_agent_panel_model.h>
#include <kisurf/ai/ai_model_config.h>
#include <kisurf/ai/ai_runtime.h>
#include <kisurf/ai/ai_semantic_ui.h>
#include <kisurf/ai/ai_tool_execution.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>
#include <wx/timer.h>

class wxCommandEvent;
class AI_ACCEPT_APPLY_ADAPTER;
class AI_SESSION_TOOL_CALL_HANDLER;
class AI_SESSION_PREVIEW_SERVICE;
class AI_SESSION_SHADOW_BOARD_SEEDER;
class AI_SESSION_VALIDATION_SERVICE;

struct KICOMMON_API AI_AGENT_COMPOSER_STATUS_VIEW
{
    bool                 m_BackgroundAgentEnabled = false;
    bool                 m_BackgroundAgentBusy = false;
    bool                 m_ChatAgentBusy = false;
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
    bool m_TargetsAutomaticPreview = false;
};

enum class AI_AGENT_BACKGROUND_UPDATE_ACTION
{
    Ignore,
    QueueAsync,
    DropWhileBusy
};

struct KICOMMON_API AI_AGENT_BACKGROUND_UPDATE_VIEW
{
    bool m_BackgroundAgentEnabled = false;
    bool m_HasContextProvider = false;
    bool m_UpdateInFlight = false;
};

class KICOMMON_API AI_AGENT_PANEL : public AI_AGENT_PANEL_BASE
{
public:
    using CONTEXT_PROVIDER = std::function<AI_CONTEXT_SNAPSHOT()>;
    using SUGGESTION_PREVIEW_HANDLER =
            std::function<bool( AI_AGENT_PANEL_MODEL& aModel, uint64_t aSuggestionId )>;
    using SUGGESTION_ACCEPT_HANDLER =
            std::function<bool( AI_AGENT_PANEL_MODEL& aModel, uint64_t aSuggestionId,
                                const AI_NEXT_ACTION_CONTEXT_VERSION& aCurrentContextVersion )>;
    using SUGGESTION_REJECT_HANDLER =
            std::function<bool( AI_AGENT_PANEL_MODEL& aModel, uint64_t aSuggestionId )>;

    AI_AGENT_PANEL( wxWindow* aParent, AI_EDITOR_KIND aEditorKind,
                    CONTEXT_PROVIDER aContextProvider = CONTEXT_PROVIDER() );
    ~AI_AGENT_PANEL() override;

    void SendCurrentText();
    void StartNewChat();
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
    bool RecoverLatestProviderFailure();
    bool HandlePreviewShortcut( int aKeyCode, bool aHasModifier = false,
                                bool aFocusInsideAgentPanel = false );
    bool HandlePreviewPointer( bool aFocusInsideAgentPanel = false );
    bool PulseBackgroundAgent( const wxString& aReason = wxS( "semantic_tick" ) );
    bool ShouldContinueBackgroundIdlePulse() const;

private:
    struct CHAT_SEND_STATE
    {
        std::atomic_bool      m_Alive{ true };
        std::atomic<uint64_t> m_Generation{ 0 };
    };

    void OnModelSettings( wxCommandEvent& aEvent ) override;
    void OnNewChat( wxCommandEvent& aEvent ) override;
    void OnBackgroundAgentToggled( wxCommandEvent& aEvent ) override;
    void OnPromptTextChanged( wxCommandEvent& aEvent ) override;
    void OnPromptEnter( wxCommandEvent& aEvent ) override;
    void OnSend( wxCommandEvent& aEvent ) override;
    void OnStop( wxCommandEvent& aEvent ) override;
    void OnChatStartTimer( wxTimerEvent& aEvent );
    void OnBackgroundPulseTimer( wxTimerEvent& aEvent );

    bool acceptActionPreviewSuggestion( uint64_t aSuggestionId );
    void loadConfiguredResearchFolder( const AI_MODEL_CONFIG& aConfig,
                                       bool aReportErrors );
    AI_CONTEXT_SNAPSHOT contextSnapshotWithPanelState() const;
    void updateModeControls();
    void updateComposerStatus();
    void saveWorkspaceContextStateFromContext( const AI_CONTEXT_SNAPSHOT& aSnapshot,
                                               const AI_ACTIVITY_RECORD& aActivity );
    void queueBackgroundSuggestionUpdate( AI_CONTEXT_SNAPSHOT aSnapshot,
                                          AI_ACTIVITY_RECORD aActivity,
                                          wxString aReason );
    void scheduleBackgroundSemanticTick(
            const wxString& aReason = wxS( "semantic_event" ) );
    void scheduleBackgroundRetryAfterEmptyResult(
            const wxString& aReason = wxS( "agent.background.empty_retry" ) );
    void finishBackgroundSuggestionUpdate(
            uint64_t aGeneration,
            std::optional<AI_SUGGESTION_RECORD> aSuggestion );
    bool backgroundSuggestionUpdateInFlight() const;
    void renderPendingChatRequest();
    void handlePreparedChatStreamEvent( const AI_RUNTIME_STREAM_EVENT& aEvent,
                                        uint64_t aGeneration );
    void prepareAndRunPendingChatRequest( wxString aText, uint64_t aGeneration );
    void runPreparedChatRequest( std::shared_ptr<AI_AGENT_PANEL_MODEL> aModel,
                                 std::shared_ptr<CHAT_SEND_STATE> aChatState,
                                 AI_CHAT_REQUEST_STATE aState,
                                 uint64_t aGeneration );

    AI_EDITOR_KIND                            m_EditorKind;
    CONTEXT_PROVIDER                          m_ContextProvider;
    std::shared_ptr<AI_AGENT_PANEL_MODEL>     m_Model;
    AI_TOOL_EXECUTION_POLICY                  m_ToolExecutionPolicy;
    AI_ACTIVITY_LOG                           m_ToolActivityLog;
    std::unique_ptr<AI_ACTION_RUNNER>         m_ActionRunner;
    std::unique_ptr<AI_TOOL_CALL_HANDLER>     m_ToolCallHandler;
    std::unique_ptr<AI_TOOL_CALL_HANDLER>     m_RuntimeToolCallHandler;
    std::unique_ptr<AI_SESSION_PREVIEW_SERVICE>
            m_MarshalledNextActionPreviewService;
    std::unique_ptr<AI_SESSION_VALIDATION_SERVICE>
            m_MarshalledNextActionValidationService;
    AI_SESSION_TOOL_CALL_HANDLER*             m_SessionToolCallHandler = nullptr;
    bool                                      m_HasSessionPreviewService = false;
    bool                                      m_HasSessionAcceptAdapter = false;
    AI_ACCEPT_APPLY_ADAPTER*                  m_AcceptAdapter = nullptr;
    SUGGESTION_PREVIEW_HANDLER                m_PreviewSuggestionHandler;
    SUGGESTION_ACCEPT_HANDLER                 m_AcceptSuggestionHandler;
    SUGGESTION_REJECT_HANDLER                 m_RejectSuggestionHandler;

    struct BACKGROUND_UPDATE_STATE
    {
        std::atomic_bool     m_Alive{ true };
        std::atomic_bool     m_InFlight{ false };
        std::atomic<uint64_t> m_Generation{ 0 };
    };

    std::shared_ptr<BACKGROUND_UPDATE_STATE> m_BackgroundUpdateState;
    std::shared_ptr<CHAT_SEND_STATE>         m_ChatSendState;
    wxTimer                                  m_ChatStartTimer;
    wxTimer                                  m_BackgroundPulseTimer;
    wxString                                 m_LastBackgroundTickFingerprint;
    wxString                                 m_LastBackgroundEmptyRetryFingerprint;
    wxString                                 m_DeferredBackgroundReason;
    bool                                     m_ChatSendInFlight = false;
    wxString                                 m_PendingUserText;
    wxString                                 m_StreamingAssistantText;
    wxString                                 m_DeferredChatText;
    uint64_t                                 m_DeferredChatGeneration = 0;
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
KICOMMON_API wxString AiAgentStreamingAssistantTextAfterEvent(
        const wxString& aCurrentText,
        const AI_RUNTIME_STREAM_EVENT& aEvent );
KICOMMON_API AI_AGENT_PREVIEW_SHORTCUT_ACTION AiAgentPreviewShortcutAction(
        const AI_AGENT_PREVIEW_SHORTCUT_VIEW& aView, int aKeyCode );
KICOMMON_API AI_AGENT_PREVIEW_SHORTCUT_ACTION AiAgentPreviewPointerAction(
        const AI_AGENT_PREVIEW_SHORTCUT_VIEW& aView );
KICOMMON_API bool AiAgentSuggestionTargetsWorkspacePreview(
        const AI_SUGGESTION_RECORD& aSuggestion );
KICOMMON_API bool AiAgentSuggestionTargetsAutomaticPreview(
        const AI_SUGGESTION_RECORD& aSuggestion );
KICOMMON_API bool AiAgentShouldAutoPreviewBackgroundSuggestion(
        const AI_AGENT_BACKGROUND_PREVIEW_VIEW& aView );
KICOMMON_API AI_AGENT_BACKGROUND_UPDATE_ACTION AiAgentBackgroundUpdateAction(
        const AI_AGENT_BACKGROUND_UPDATE_VIEW& aView );
KICOMMON_API bool AiAgentSnapshotNeedsBackgroundTick(
        const AI_CONTEXT_SNAPSHOT& aSnapshot );
KICOMMON_API wxString AiAgentBackgroundTickFingerprint(
        const AI_CONTEXT_SNAPSHOT& aSnapshot );
KICOMMON_API bool AiAgentShouldQueueBackgroundTick(
        const AI_CONTEXT_SNAPSHOT& aSnapshot,
        const wxString& aLastBackgroundTickFingerprint );
