#pragma once

#include <kicommon.h>

#include <cstdint>
#include <vector>

#include <wx/string.h>


struct KICOMMON_API AI_NEXT_ACTION_SESSION_STEP_RECORD
{
    uint64_t              m_StepId = 0;
    uint64_t              m_ObservationPacketId = 0;
    uint64_t              m_PublishedSuggestionId = 0;
    wxString              m_Status;
    wxString              m_SuggestionStreamId;
    wxString              m_SemanticEventJson;
    wxString              m_ObservationPacketJson;
    wxString              m_LlmDecisionJson;
    wxString              m_LlmDecisionToolResultsJson;
    wxString              m_ReviewDecisionJson;
    wxString              m_ReviewToolResultsJson;
    std::vector<uint64_t> m_AttemptIds;
};


struct KICOMMON_API AI_NEXT_ACTION_SESSION_RECORD
{
    uint64_t m_ConversationId = 0;
    wxString m_SessionType;
    wxString m_ProjectId;
    wxString m_DocumentId;
    wxString m_ContextKey;
    std::vector<AI_NEXT_ACTION_SESSION_STEP_RECORD> m_Steps;
};


class KICOMMON_API AI_NEXT_ACTION_SESSION_STORE
{
public:
    AI_NEXT_ACTION_SESSION_STORE();
    explicit AI_NEXT_ACTION_SESSION_STORE( wxString aDirectory );

    bool WriteSession( const AI_NEXT_ACTION_SESSION_RECORD& aRecord,
                       wxString& aError ) const;
    AI_NEXT_ACTION_SESSION_RECORD LoadSession( uint64_t aConversationId,
                                               wxString& aError ) const;

    wxString SessionPath( uint64_t aConversationId ) const;
    static wxString DefaultDirectory();

    const wxString& Directory() const { return m_Directory; }

private:
    wxString m_Directory;
};
