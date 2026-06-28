#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_types.h>

#include <cstddef>
#include <vector>

#include <wx/string.h>


struct KICOMMON_API AI_PROMPT_TRACE_ENTRY
{
    uint64_t                 m_RequestId = 0;
    uint64_t                 m_ConversationId = 0;
    AI_PROVIDER_REQUEST_KIND m_RequestKind = AI_PROVIDER_REQUEST_KIND::Chat;
    size_t                   m_ContextEstimatedChars = 0;
    wxString                 m_ProviderStatus;
    wxString                 m_PromptTraceJson;
    wxString                 m_ProviderTraceJson;
};


struct KICOMMON_API AI_PROMPT_TRACE_RETENTION_POLICY
{
    // Zero disables count-based retention.
    size_t m_MaxEntries = 1000;
};


class KICOMMON_API AI_PROMPT_TRACE_STORE
{
public:
    AI_PROMPT_TRACE_STORE();
    explicit AI_PROMPT_TRACE_STORE(
            wxString aPath,
            AI_PROMPT_TRACE_RETENTION_POLICY aRetention = AI_PROMPT_TRACE_RETENTION_POLICY() );

    bool Append( const AI_PROVIDER_REQUEST& aRequest, const wxString& aProviderStatus,
                 wxString& aError );
    bool Append( const AI_PROVIDER_REQUEST& aRequest, const wxString& aProviderStatus,
                 const wxString& aProviderTraceJson, wxString& aError );

    std::vector<AI_PROMPT_TRACE_ENTRY> LoadAll( wxString& aError ) const;

    static wxString DefaultPath();

    const wxString& Path() const { return m_Path; }
    const AI_PROMPT_TRACE_RETENTION_POLICY& RetentionPolicy() const { return m_Retention; }

private:
    bool ApplyRetention( wxString& aError ) const;

private:
    wxString                          m_Path;
    AI_PROMPT_TRACE_RETENTION_POLICY  m_Retention;
};
