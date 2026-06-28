#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_types.h>

#include <cstdint>
#include <vector>

#include <wx/string.h>


struct KICOMMON_API AI_CHAT_SESSION_MESSAGE_RECORD
{
    wxString m_Role;
    wxString m_Text;
};


struct KICOMMON_API AI_CHAT_SESSION_RECORD
{
    uint64_t m_ConversationId = 0;
    wxString m_ProjectId;
    wxString m_DocumentId;
    std::vector<AI_CHAT_SESSION_MESSAGE_RECORD> m_Messages;
    std::vector<AI_TOOL_CALL_RECORD>            m_ToolCalls;
};


class KICOMMON_API AI_CHAT_SESSION_STORE
{
public:
    AI_CHAT_SESSION_STORE();
    explicit AI_CHAT_SESSION_STORE( wxString aDirectory );

    bool WriteSession( const AI_CHAT_SESSION_RECORD& aRecord, wxString& aError ) const;
    AI_CHAT_SESSION_RECORD LoadSession( uint64_t aConversationId, wxString& aError ) const;

    wxString SessionPath( uint64_t aConversationId ) const;
    static wxString DefaultDirectory();

    const wxString& Directory() const { return m_Directory; }

private:
    wxString m_Directory;
};
