#include <kisurf/ai/ai_memory_store.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <utility>

#include <wx/ffile.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/textfile.h>

namespace
{
std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


wxString fromUtf8String( const std::string& aText )
{
    return wxString::FromUTF8( aText.c_str() );
}


std::string lowerAscii( const wxString& aText )
{
    std::string text = toUtf8String( aText );

    std::transform( text.begin(), text.end(), text.begin(), []( unsigned char ch )
    {
        return static_cast<char>( std::tolower( ch ) );
    } );

    return text;
}


std::vector<std::string> tokenize( const wxString& aText )
{
    std::vector<std::string> tokens;
    std::string              text = lowerAscii( aText );
    std::string              current;

    for( unsigned char ch : text )
    {
        if( std::isalnum( ch ) || ch == '_' )
        {
            current.push_back( static_cast<char>( ch ) );
            continue;
        }

        if( !current.empty() )
        {
            tokens.push_back( current );
            current.clear();
        }
    }

    if( !current.empty() )
        tokens.push_back( current );

    std::sort( tokens.begin(), tokens.end() );
    tokens.erase( std::unique( tokens.begin(), tokens.end() ), tokens.end() );

    return tokens;
}


bool containsString( const std::vector<wxString>& aValues, const wxString& aNeedle )
{
    return std::find( aValues.begin(), aValues.end(), aNeedle ) != aValues.end();
}


wxString jsonStringValue( const nlohmann::json& aJson, const char* aKey )
{
    const auto it = aJson.find( aKey );

    if( it == aJson.end() || !it->is_string() )
        return wxString();

    return fromUtf8String( it->get<std::string>() );
}


uint64_t jsonUint64Value( const nlohmann::json& aJson, const char* aKey )
{
    const auto it = aJson.find( aKey );

    if( it == aJson.end() )
        return 0;

    if( it->is_number_unsigned() )
        return it->get<uint64_t>();

    if( it->is_number_integer() )
    {
        const int64_t value = it->get<int64_t>();
        return value > 0 ? static_cast<uint64_t>( value ) : 0;
    }

    return 0;
}


int64_t jsonInt64Value( const nlohmann::json& aJson, const char* aKey )
{
    const auto it = aJson.find( aKey );

    if( it == aJson.end() )
        return 0;

    if( it->is_number_integer() )
        return it->get<int64_t>();

    if( it->is_number_unsigned() )
        return static_cast<int64_t>( it->get<uint64_t>() );

    return 0;
}


int jsonIntValue( const nlohmann::json& aJson, const char* aKey )
{
    const auto it = aJson.find( aKey );

    if( it == aJson.end() || !it->is_number_integer() )
        return 0;

    return it->get<int>();
}


std::vector<wxString> jsonStringArrayValue( const nlohmann::json& aJson, const char* aKey )
{
    std::vector<wxString> values;
    const auto            it = aJson.find( aKey );

    if( it == aJson.end() || !it->is_array() )
        return values;

    for( const nlohmann::json& value : *it )
    {
        if( value.is_string() )
            values.push_back( fromUtf8String( value.get<std::string>() ) );
    }

    return values;
}


nlohmann::json stringArrayJson( const std::vector<wxString>& aValues )
{
    nlohmann::json values = nlohmann::json::array();

    for( const wxString& value : aValues )
        values.push_back( toUtf8String( value ) );

    return values;
}


nlohmann::json recordToJson( const AI_MEMORY_RECORD& aRecord )
{
    return {
        { "schema", { { "name", "kisurf.ai.memory_record" }, { "version", 1 } } },
        { "id", toUtf8String( aRecord.m_Id ) },
        { "project_id", toUtf8String( aRecord.m_ProjectId ) },
        { "document_id", toUtf8String( aRecord.m_DocumentId ) },
        { "agent_kind", toUtf8String( aRecord.m_AgentKind ) },
        { "type", toUtf8String( aRecord.m_Type ) },
        { "text", toUtf8String( aRecord.m_Text ) },
        { "source", toUtf8String( aRecord.m_Source ) },
        { "provenance", toUtf8String( aRecord.m_ProvenanceJson ) },
        { "board_state_version", toUtf8String( aRecord.m_BoardStateVersion ) },
        { "acceptance_state", toUtf8String( aRecord.m_AcceptanceState ) },
        { "object_ids", stringArrayJson( aRecord.m_ObjectIds ) },
        { "trust_level", aRecord.m_TrustLevel },
        { "sequence", aRecord.m_Sequence },
        { "created_at_unix_seconds", aRecord.m_CreatedAtUnixSeconds },
        { "expires_at_unix_seconds", aRecord.m_ExpiresAtUnixSeconds }
    };
}


AI_MEMORY_RECORD recordFromJson( const nlohmann::json& aJson )
{
    AI_MEMORY_RECORD record;
    record.m_Id = jsonStringValue( aJson, "id" );
    record.m_ProjectId = jsonStringValue( aJson, "project_id" );
    record.m_DocumentId = jsonStringValue( aJson, "document_id" );
    record.m_AgentKind = jsonStringValue( aJson, "agent_kind" );
    record.m_Type = jsonStringValue( aJson, "type" );
    record.m_Text = jsonStringValue( aJson, "text" );
    record.m_Source = jsonStringValue( aJson, "source" );
    record.m_ProvenanceJson = jsonStringValue( aJson, "provenance" );
    record.m_BoardStateVersion = jsonStringValue( aJson, "board_state_version" );
    record.m_AcceptanceState = jsonStringValue( aJson, "acceptance_state" );
    record.m_ObjectIds = jsonStringArrayValue( aJson, "object_ids" );
    record.m_TrustLevel = jsonIntValue( aJson, "trust_level" );
    record.m_Sequence = jsonUint64Value( aJson, "sequence" );
    record.m_CreatedAtUnixSeconds = jsonInt64Value( aJson, "created_at_unix_seconds" );
    record.m_ExpiresAtUnixSeconds = jsonInt64Value( aJson, "expires_at_unix_seconds" );
    return record;
}


AI_PROVIDER_INPUT_BLOCK memoryRecordToProviderInputBlock( const AI_MEMORY_RECORD& aRecord )
{
    AI_PROVIDER_INPUT_BLOCK block;
    block.m_Id = wxS( "memory." ) + aRecord.m_Id;
    block.m_Kind = wxS( "retrieved_memory" );
    block.m_Source = aRecord.m_Source.IsEmpty() ? wxString( wxS( "memory_store" ) )
                                                : aRecord.m_Source;
    block.m_Text = aRecord.m_Text;

    nlohmann::json metadata = {
        { "id", toUtf8String( aRecord.m_Id ) },
        { "project_id", toUtf8String( aRecord.m_ProjectId ) },
        { "document_id", toUtf8String( aRecord.m_DocumentId ) },
        { "agent_kind", toUtf8String( aRecord.m_AgentKind ) },
        { "type", toUtf8String( aRecord.m_Type ) },
        { "source", toUtf8String( aRecord.m_Source ) },
        { "provenance", toUtf8String( aRecord.m_ProvenanceJson ) },
        { "board_state_version", toUtf8String( aRecord.m_BoardStateVersion ) },
        { "acceptance_state", toUtf8String( aRecord.m_AcceptanceState ) },
        { "object_ids", stringArrayJson( aRecord.m_ObjectIds ) },
        { "trust_level", aRecord.m_TrustLevel },
        { "sequence", aRecord.m_Sequence }
    };

    block.m_MetadataJson = fromUtf8String( metadata.dump() );
    return block;
}


bool recordMatchesFilter( const AI_MEMORY_RECORD& aRecord, const AI_MEMORY_QUERY& aQuery )
{
    if( !aQuery.m_ProjectId.IsEmpty() && aRecord.m_ProjectId != aQuery.m_ProjectId )
        return false;

    if( !aQuery.m_DocumentId.IsEmpty() && aRecord.m_DocumentId != aQuery.m_DocumentId )
        return false;

    if( !aQuery.m_AgentKind.IsEmpty() && aRecord.m_AgentKind != aQuery.m_AgentKind )
        return false;

    if( !aQuery.m_Type.IsEmpty() && aRecord.m_Type != aQuery.m_Type )
        return false;

    if( !aQuery.m_BoardStateVersion.IsEmpty()
        && aRecord.m_BoardStateVersion != aQuery.m_BoardStateVersion )
    {
        return false;
    }

    if( !aQuery.m_AcceptanceState.IsEmpty()
        && aRecord.m_AcceptanceState != aQuery.m_AcceptanceState )
    {
        return false;
    }

    if( !aQuery.m_Types.empty() && !containsString( aQuery.m_Types, aRecord.m_Type ) )
        return false;

    for( const wxString& objectId : aQuery.m_ObjectIds )
    {
        if( !containsString( aRecord.m_ObjectIds, objectId ) )
            return false;
    }

    if( aRecord.m_TrustLevel < aQuery.m_MinTrustLevel )
        return false;

    if( !aQuery.m_IncludeExpired && aQuery.m_NowUnixSeconds > 0
        && aRecord.m_ExpiresAtUnixSeconds > 0
        && aRecord.m_ExpiresAtUnixSeconds < aQuery.m_NowUnixSeconds )
    {
        return false;
    }

    return true;
}


int lexicalScore( const AI_MEMORY_RECORD& aRecord, const AI_MEMORY_QUERY& aQuery )
{
    if( aQuery.m_Text.IsEmpty() )
        return 1;

    const std::vector<std::string> queryTokens = tokenize( aQuery.m_Text );
    const std::vector<std::string> recordTokens = tokenize( aRecord.m_Text );
    const std::set<std::string>    recordTokenSet( recordTokens.begin(), recordTokens.end() );
    const std::string              recordText = lowerAscii( aRecord.m_Text );
    const std::string              queryText = lowerAscii( aQuery.m_Text );

    int score = 0;

    if( !queryText.empty() && recordText.find( queryText ) != std::string::npos )
        score += 20;

    for( const std::string& token : queryTokens )
    {
        if( recordTokenSet.find( token ) != recordTokenSet.end() )
        {
            score += 10;
            continue;
        }

        if( recordText.find( token ) != std::string::npos )
            score += 4;
    }

    return score;
}


bool writeRecords( const wxString& aPath, const std::vector<AI_MEMORY_RECORD>& aRecords,
                   wxString& aError )
{
    wxFileName fileName( aPath );

    if( !fileName.GetPath().IsEmpty() && !fileName.DirExists() )
    {
        if( !fileName.Mkdir( wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL ) )
        {
            aError = wxString::Format( wxS( "Unable to create memory store directory: %s" ),
                                       fileName.GetPath() );
            return false;
        }
    }

    wxFFile file( aPath, wxS( "wb" ) );

    if( !file.IsOpened() )
    {
        aError = wxString::Format( wxS( "Unable to open memory store: %s" ), aPath );
        return false;
    }

    for( const AI_MEMORY_RECORD& record : aRecords )
    {
        file.Write( fromUtf8String( recordToJson( record ).dump() ) );
        file.Write( wxS( "\n" ) );
    }

    return true;
}
}


AI_MEMORY_STORE::AI_MEMORY_STORE() :
        AI_MEMORY_STORE( DefaultPath() )
{
}


AI_MEMORY_STORE::AI_MEMORY_STORE( wxString aPath, AI_MEMORY_RETENTION_POLICY aRetention ) :
        m_Path( std::move( aPath ) ),
        m_Retention( aRetention )
{
}


wxString AI_MEMORY_STORE::DefaultPath()
{
    wxString base = wxStandardPaths::Get().GetUserLocalDataDir();

    if( base.IsEmpty() )
        base = wxStandardPaths::Get().GetUserDataDir();

    if( base.IsEmpty() )
        base = wxStandardPaths::Get().GetTempDir();

    wxFileName path;
    path.AssignDir( base );
    path.AppendDir( wxS( "ai" ) );
    path.SetFullName( wxS( "memory.jsonl" ) );
    return path.GetFullPath();
}


bool AI_MEMORY_STORE::Append( const AI_MEMORY_RECORD& aRecord, wxString& aError )
{
    if( aRecord.m_Id.IsEmpty() )
    {
        aError = wxS( "Memory record id is required." );
        return false;
    }

    if( aRecord.m_Text.IsEmpty() )
    {
        aError = wxS( "Memory record text is required." );
        return false;
    }

    wxFileName fileName( m_Path );

    if( !fileName.GetPath().IsEmpty() && !fileName.DirExists() )
    {
        if( !fileName.Mkdir( wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL ) )
        {
            aError = wxString::Format( wxS( "Unable to create memory store directory: %s" ),
                                       fileName.GetPath() );
            return false;
        }
    }

    wxFFile file( m_Path, wxS( "ab" ) );

    if( !file.IsOpened() )
    {
        aError = wxString::Format( wxS( "Unable to open memory store: %s" ), m_Path );
        return false;
    }

    file.Write( fromUtf8String( recordToJson( aRecord ).dump() ) );
    file.Write( wxS( "\n" ) );
    file.Close();

    return ApplyRetention( aError );
}


std::vector<AI_MEMORY_RECORD> AI_MEMORY_STORE::LoadAll( wxString& aError ) const
{
    std::vector<AI_MEMORY_RECORD> records;

    if( m_Path.IsEmpty() || !wxFileExists( m_Path ) )
    {
        aError.clear();
        return records;
    }

    wxTextFile file;

    if( !file.Open( m_Path ) )
    {
        aError = wxString::Format( wxS( "Unable to open memory store: %s" ), m_Path );
        return records;
    }

    for( size_t lineIndex = 0; lineIndex < file.GetLineCount(); ++lineIndex )
    {
        const wxString line = file.GetLine( lineIndex ).Trim().Trim( false );

        if( line.IsEmpty() )
            continue;

        nlohmann::json json = nlohmann::json::parse( toUtf8String( line ), nullptr, false );

        if( json.is_discarded() || !json.is_object() )
        {
            aError = wxString::Format( wxS( "Invalid memory store JSONL at line %zu" ),
                                       lineIndex + 1 );
            return std::vector<AI_MEMORY_RECORD>();
        }

        records.push_back( recordFromJson( json ) );
    }

    aError.clear();
    return records;
}


std::vector<AI_MEMORY_RECORD> AI_MEMORY_STORE::Query( const AI_MEMORY_QUERY& aQuery,
                                                      wxString& aError ) const
{
    std::vector<AI_MEMORY_RECORD> records = LoadAll( aError );

    if( !aError.IsEmpty() )
        return std::vector<AI_MEMORY_RECORD>();

    struct SCORED_RECORD
    {
        AI_MEMORY_RECORD m_Record;
        int              m_Score = 0;
    };

    std::vector<SCORED_RECORD> scoredRecords;

    for( const AI_MEMORY_RECORD& record : records )
    {
        if( !recordMatchesFilter( record, aQuery ) )
            continue;

        const int score = lexicalScore( record, aQuery );

        if( score <= 0 )
            continue;

        scoredRecords.push_back( SCORED_RECORD{ record, score } );
    }

    std::sort( scoredRecords.begin(), scoredRecords.end(),
               []( const SCORED_RECORD& aLeft, const SCORED_RECORD& aRight )
               {
                   if( aLeft.m_Score != aRight.m_Score )
                       return aLeft.m_Score > aRight.m_Score;

                   if( aLeft.m_Record.m_Sequence != aRight.m_Record.m_Sequence )
                       return aLeft.m_Record.m_Sequence > aRight.m_Record.m_Sequence;

                   return aLeft.m_Record.m_Id < aRight.m_Record.m_Id;
               } );

    if( aQuery.m_Limit > 0 && scoredRecords.size() > aQuery.m_Limit )
        scoredRecords.resize( aQuery.m_Limit );

    std::vector<AI_MEMORY_RECORD> results;
    results.reserve( scoredRecords.size() );

    for( const SCORED_RECORD& scoredRecord : scoredRecords )
        results.push_back( scoredRecord.m_Record );

    return results;
}


bool AI_MEMORY_STORE::ExportToLocalTextIndex( AI_LOCAL_TEXT_MEMORY_INDEX& aIndex,
                                              const AI_MEMORY_QUERY& aQuery,
                                              wxString& aError ) const
{
    std::vector<AI_MEMORY_RECORD> records = Query( aQuery, aError );

    if( !aError.IsEmpty() )
        return false;

    for( const AI_MEMORY_RECORD& record : records )
    {
        AI_LOCAL_TEXT_MEMORY_RECORD localRecord;
        localRecord.m_Id = record.m_Id;
        localRecord.m_ProjectId = record.m_ProjectId;
        localRecord.m_DocumentId = record.m_DocumentId;
        localRecord.m_AgentKind = record.m_AgentKind;
        localRecord.m_Type = record.m_Type;
        localRecord.m_Text = record.m_Text;
        localRecord.m_Source = record.m_Source;
        localRecord.m_ProvenanceJson = record.m_ProvenanceJson;
        localRecord.m_BoardStateVersion = record.m_BoardStateVersion;
        localRecord.m_AcceptanceState = record.m_AcceptanceState;
        localRecord.m_ObjectIds = record.m_ObjectIds;
        localRecord.m_TrustLevel = record.m_TrustLevel;
        localRecord.m_Sequence = record.m_Sequence;
        localRecord.m_CreatedAtUnixSeconds = record.m_CreatedAtUnixSeconds;
        localRecord.m_ExpiresAtUnixSeconds = record.m_ExpiresAtUnixSeconds;
        aIndex.AddRecord( std::move( localRecord ) );
    }

    return true;
}


bool AI_MEMORY_STORE::ApplyRetention( wxString& aError ) const
{
    if( m_Retention.m_MaxRecords == 0 )
    {
        aError.clear();
        return true;
    }

    std::vector<AI_MEMORY_RECORD> records = LoadAll( aError );

    if( !aError.IsEmpty() )
        return false;

    if( records.size() <= m_Retention.m_MaxRecords )
        return true;

    std::sort( records.begin(), records.end(),
               []( const AI_MEMORY_RECORD& aLeft, const AI_MEMORY_RECORD& aRight )
               {
                   if( aLeft.m_Sequence != aRight.m_Sequence )
                       return aLeft.m_Sequence > aRight.m_Sequence;

                   return aLeft.m_Id > aRight.m_Id;
               } );

    records.resize( m_Retention.m_MaxRecords );

    std::sort( records.begin(), records.end(),
               []( const AI_MEMORY_RECORD& aLeft, const AI_MEMORY_RECORD& aRight )
               {
                   if( aLeft.m_Sequence != aRight.m_Sequence )
                       return aLeft.m_Sequence < aRight.m_Sequence;

                   return aLeft.m_Id < aRight.m_Id;
               } );

    return writeRecords( m_Path, records, aError );
}


AI_PROVIDER_INPUT_BLOCK AiMemoryRecordToProviderInputBlock(
        const AI_MEMORY_RECORD& aRecord )
{
    return memoryRecordToProviderInputBlock( aRecord );
}
