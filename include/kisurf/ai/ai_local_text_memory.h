#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_types.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include <wx/string.h>


struct KICOMMON_API AI_LOCAL_TEXT_MEMORY_RECORD
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


struct KICOMMON_API AI_LOCAL_TEXT_MEMORY_QUERY
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
};


struct KICOMMON_API AI_LOCAL_TEXT_MEMORY_RESULT
{
    AI_LOCAL_TEXT_MEMORY_RECORD m_Record;
    int                         m_Score = 0;
};


struct KICOMMON_API AI_LOCAL_TEXT_FILE_RECORD_OPTIONS
{
    wxString              m_ProjectId;
    wxString              m_DocumentId;
    wxString              m_AgentKind;
    wxString              m_Type;
    wxString              m_Source;
    wxString              m_BoardStateVersion;
    wxString              m_AcceptanceState;
    std::vector<wxString> m_ObjectIds;
    int                   m_TrustLevel = 0;
    uint64_t              m_Sequence = 0;
    int64_t               m_CreatedAtUnixSeconds = 0;
    int64_t               m_ExpiresAtUnixSeconds = 0;
};


class KICOMMON_API AI_LOCAL_TEXT_MEMORY_INDEX
{
public:
    void AddRecord( AI_LOCAL_TEXT_MEMORY_RECORD aRecord );

    std::vector<AI_LOCAL_TEXT_MEMORY_RESULT> Search(
            const AI_LOCAL_TEXT_MEMORY_QUERY& aQuery, size_t aLimit ) const;

    bool LoadJsonlFile( const wxString& aPath, wxString& aError );
    bool LoadTextFile( const wxString& aPath,
                       const AI_LOCAL_TEXT_FILE_RECORD_OPTIONS& aOptions,
                       wxString& aError );
    bool LoadTextDirectory( const wxString& aDirectory,
                            const AI_LOCAL_TEXT_FILE_RECORD_OPTIONS& aOptions,
                            wxString& aError,
                            bool aRecursive = true );

    const std::vector<AI_LOCAL_TEXT_MEMORY_RECORD>& Records() const { return m_Records; }

private:
    std::vector<AI_LOCAL_TEXT_MEMORY_RECORD> m_Records;
};

KICOMMON_API AI_PROVIDER_INPUT_BLOCK AiLocalTextMemoryResultToProviderInputBlock(
        const AI_LOCAL_TEXT_MEMORY_RESULT& aResult );
