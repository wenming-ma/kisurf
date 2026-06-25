#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_next_action_runtime.h>
#include <kisurf/ai/ai_types.h>

#include <cstddef>
#include <vector>
#include <wx/string.h>

enum class AI_AGENT_OBSERVABILITY_KIND
{
    UserInput,
    ModelInput,
    ModelToolCall,
    ToolResult,
    ModelOutput,
    Suggestion,
    NextActionReplay,
    System
};

struct KICOMMON_API AI_AGENT_OBSERVABILITY_ENTRY
{
    uint64_t                    m_Sequence = 0;
    uint64_t                    m_RequestId = 0;
    wxString                    m_ToolCallId;
    AI_AGENT_OBSERVABILITY_KIND m_Kind = AI_AGENT_OBSERVABILITY_KIND::System;
    AI_EDITOR_KIND              m_EditorKind = AI_EDITOR_KIND::Unknown;
    wxString                    m_Title;
    wxString                    m_Summary;
    wxString                    m_DetailsJson;
    bool                        m_Allowed = false;
    bool                        m_Executed = false;
    wxString                    m_ErrorCode;
};

class KICOMMON_API AI_AGENT_OBSERVABILITY_LOG
{
public:
    std::vector<AI_AGENT_OBSERVABILITY_ENTRY> Build(
            const std::vector<AI_TRACE_RECORD>& aTraces,
            const std::vector<AI_ACTIVITY_RECORD>& aActivity,
            const std::vector<AI_SUGGESTION_RECORD>& aSuggestions,
            size_t aLimit = 128 ) const;
    std::vector<AI_AGENT_OBSERVABILITY_ENTRY> Build(
            const std::vector<AI_TRACE_RECORD>& aTraces,
            const std::vector<AI_ACTIVITY_RECORD>& aActivity,
            const std::vector<AI_SUGGESTION_RECORD>& aSuggestions,
            const std::vector<AI_NEXT_ACTION_REPLAY_TRACE_RECORD>& aNextActionReplayTraces,
            size_t aLimit = 128 ) const;
};
