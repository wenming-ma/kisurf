#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_local_text_memory.h>
#include <kisurf/ai/ai_types.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include <wx/string.h>


struct KICOMMON_API AI_MEMORY_RECORD
{
    wxString              m_Id;
    wxString              m_ProjectId;
    wxString              m_DocumentId;
    wxString              m_AgentKind;
    wxString              m_Type;
    wxString              m_Text;
    wxString              m_Source;
    wxString              m_ProvenanceJson;
    wxString              m_BoardStateVersion;
    wxString              m_AcceptanceState;
    std::vector<wxString> m_ObjectIds;
    int                   m_TrustLevel = 0;
    uint64_t              m_Sequence = 0;
    int64_t               m_CreatedAtUnixSeconds = 0;
    int64_t               m_ExpiresAtUnixSeconds = 0;
};


struct KICOMMON_API AI_MEMORY_QUERY
{
    wxString              m_Text;
    wxString              m_ProjectId;
    wxString              m_DocumentId;
    wxString              m_AgentKind;
    wxString              m_Type;
    wxString              m_BoardStateVersion;
    wxString              m_AcceptanceState;
    std::vector<wxString> m_Types;
    std::vector<wxString> m_ObjectIds;
    int                   m_MinTrustLevel = 0;
    int64_t               m_NowUnixSeconds = 0;
    bool                  m_IncludeExpired = false;
    size_t                m_Limit = 0;
};


struct KICOMMON_API AI_MEMORY_RETENTION_POLICY
{
    // Zero disables count-based retention.
    size_t m_MaxRecords = 0;
};


class KICOMMON_API AI_MEMORY_STORE
{
public:
    AI_MEMORY_STORE();
    explicit AI_MEMORY_STORE(
            wxString aPath,
            AI_MEMORY_RETENTION_POLICY aRetention = AI_MEMORY_RETENTION_POLICY() );

    bool Append( const AI_MEMORY_RECORD& aRecord, wxString& aError );
    std::vector<AI_MEMORY_RECORD> Query( const AI_MEMORY_QUERY& aQuery, wxString& aError ) const;
    std::vector<AI_MEMORY_RECORD> LoadAll( wxString& aError ) const;

    bool ExportToLocalTextIndex( AI_LOCAL_TEXT_MEMORY_INDEX& aIndex,
                                 const AI_MEMORY_QUERY& aQuery,
                                 wxString& aError ) const;

    static wxString DefaultPath();

    const wxString& Path() const { return m_Path; }
    const AI_MEMORY_RETENTION_POLICY& RetentionPolicy() const { return m_Retention; }

private:
    bool ApplyRetention( wxString& aError ) const;

private:
    wxString                   m_Path;
    AI_MEMORY_RETENTION_POLICY m_Retention;
};

KICOMMON_API AI_PROVIDER_INPUT_BLOCK AiMemoryRecordToProviderInputBlock(
        const AI_MEMORY_RECORD& aRecord );
