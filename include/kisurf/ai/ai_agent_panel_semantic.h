#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_semantic_ui.h>
#include <kisurf/ai/ai_types.h>

#include <cstddef>

struct KICOMMON_API AI_AGENT_PANEL_SEMANTIC_VIEW
{
    bool                 m_BackgroundAgentEnabled = false;
    bool                 m_InputHasText = false;
    bool                 m_HasActiveSuggestion = false;
    bool                 m_CanPreviewSuggestion = false;
    bool                 m_CanAcceptSuggestion = false;
    size_t               m_MessageCount = 0;
    size_t               m_SuggestionCount = 0;
    size_t               m_LogEntryCount = 0;
    wxString             m_ComposerStatusText;
    wxString             m_LogSummary;
};

KICOMMON_API AI_SEMANTIC_UI_TREE AiAgentPanelSemanticTree(
        const AI_AGENT_PANEL_SEMANTIC_VIEW& aView );

KICOMMON_API AI_PANEL_STATE_RECORD AiAgentPanelSemanticStateRecord(
        const AI_AGENT_PANEL_SEMANTIC_VIEW& aView );

KICOMMON_API AI_PANEL_STATE_RECORD AiAgentPanelSemanticStateRecord(
        const AI_SEMANTIC_UI_TREE& aTree );

KICOMMON_API void AiUpsertPanelStateRecord( AI_CONTEXT_SNAPSHOT& aSnapshot,
                                            AI_PANEL_STATE_RECORD aRecord );
