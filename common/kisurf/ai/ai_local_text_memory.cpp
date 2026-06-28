#include <kisurf/ai/ai_local_text_memory.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <set>
#include <string>
#include <utility>

#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/ffile.h>
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


bool isSupportedTextFile( const wxString& aPath )
{
    wxString extension = wxFileName( aPath ).GetExt().Lower();

    return extension == wxS( "md" )
           || extension == wxS( "markdown" )
           || extension == wxS( "txt" )
           || extension == wxS( "text" )
           || extension == wxS( "rst" );
}


bool collectSupportedTextFiles( const wxString& aDirectory,
                                bool aRecursive,
                                std::vector<wxString>& aFiles,
                                wxString& aError )
{
    wxDir dir( aDirectory );

    if( !dir.IsOpened() )
    {
        aError = wxString::Format( wxS( "Unable to open local text directory: %s" ),
                                   aDirectory );
        return false;
    }

    wxString name;
    bool     hasEntry = dir.GetFirst( &name, wxEmptyString,
                                      wxDIR_FILES | wxDIR_DIRS );

    while( hasEntry )
    {
        const wxString childPath = wxFileName( aDirectory, name ).GetFullPath();

        if( wxDirExists( childPath ) )
        {
            if( aRecursive
                && !collectSupportedTextFiles( childPath, aRecursive, aFiles, aError ) )
            {
                return false;
            }
        }
        else if( wxFileExists( childPath ) && isSupportedTextFile( childPath ) )
        {
            aFiles.push_back( childPath );
        }

        hasEntry = dir.GetNext( &name );
    }

    return true;
}


bool recordMatchesFilter( const AI_LOCAL_TEXT_MEMORY_RECORD& aRecord,
                          const AI_LOCAL_TEXT_MEMORY_QUERY&  aQuery )
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

    return true;
}


int lexicalScore( const AI_LOCAL_TEXT_MEMORY_RECORD& aRecord,
                  const AI_LOCAL_TEXT_MEMORY_QUERY&  aQuery )
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


wxString jsonStringValue( const nlohmann::json& aJson, const char* aKey )
{
    const auto it = aJson.find( aKey );

    if( it == aJson.end() || !it->is_string() )
        return wxString();

    return fromUtf8String( it->get<std::string>() );
}


uint64_t jsonSequenceValue( const nlohmann::json& aJson )
{
    const auto it = aJson.find( "sequence" );

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


int jsonIntValue( const nlohmann::json& aJson, const char* aKey )
{
    const auto it = aJson.find( aKey );

    if( it == aJson.end() || !it->is_number_integer() )
        return 0;

    return it->get<int>();
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


uint64_t fnv1a64( const wxString& aText )
{
    constexpr uint64_t FNV_OFFSET = 14695981039346656037ull;
    constexpr uint64_t FNV_PRIME = 1099511628211ull;

    uint64_t hash = FNV_OFFSET;
    const std::string bytes = toUtf8String( aText );

    for( unsigned char ch : bytes )
    {
        hash ^= ch;
        hash *= FNV_PRIME;
    }

    return hash;
}


wxString localTextFileRecordId( const wxString& aPath, const wxString& aText )
{
    wxUnusedVar( aText );

    const wxString key = wxFileName( aPath ).GetFullPath();
    return wxString::Format( wxS( "local-file:%016llx" ),
                             static_cast<unsigned long long>( fnv1a64( key ) ) );
}


wxString localTextFileProvenanceJson( const wxString& aPath )
{
    nlohmann::json provenance = {
        { "kind", "local_text_file" },
        { "path", toUtf8String( wxFileName( aPath ).GetFullPath() ) },
        { "file_name", toUtf8String( wxFileName( aPath ).GetFullName() ) }
    };

    return fromUtf8String( provenance.dump() );
}
}


void AI_LOCAL_TEXT_MEMORY_INDEX::AddRecord( AI_LOCAL_TEXT_MEMORY_RECORD aRecord )
{
    if( !aRecord.m_Id.IsEmpty() )
    {
        for( AI_LOCAL_TEXT_MEMORY_RECORD& record : m_Records )
        {
            if( record.m_Id == aRecord.m_Id )
            {
                record = std::move( aRecord );
                return;
            }
        }
    }

    m_Records.push_back( std::move( aRecord ) );
}


std::vector<AI_LOCAL_TEXT_MEMORY_RESULT> AI_LOCAL_TEXT_MEMORY_INDEX::Search(
        const AI_LOCAL_TEXT_MEMORY_QUERY& aQuery, size_t aLimit ) const
{
    std::vector<AI_LOCAL_TEXT_MEMORY_RESULT> results;

    if( aLimit == 0 )
        return results;

    for( const AI_LOCAL_TEXT_MEMORY_RECORD& record : m_Records )
    {
        if( !recordMatchesFilter( record, aQuery ) )
            continue;

        const int score = lexicalScore( record, aQuery );

        if( score <= 0 )
            continue;

        AI_LOCAL_TEXT_MEMORY_RESULT result;
        result.m_Record = record;
        result.m_Score = score;
        results.push_back( std::move( result ) );
    }

    std::sort( results.begin(), results.end(),
               []( const AI_LOCAL_TEXT_MEMORY_RESULT& aLeft,
                   const AI_LOCAL_TEXT_MEMORY_RESULT& aRight )
               {
                   if( aLeft.m_Score != aRight.m_Score )
                       return aLeft.m_Score > aRight.m_Score;

                   if( aLeft.m_Record.m_Sequence != aRight.m_Record.m_Sequence )
                       return aLeft.m_Record.m_Sequence > aRight.m_Record.m_Sequence;

                   return aLeft.m_Record.m_Id < aRight.m_Record.m_Id;
               } );

    if( results.size() > aLimit )
        results.resize( aLimit );

    return results;
}


bool AI_LOCAL_TEXT_MEMORY_INDEX::LoadJsonlFile( const wxString& aPath, wxString& aError )
{
    wxTextFile file;

    if( !file.Open( aPath ) )
    {
        aError = wxString::Format( wxS( "Unable to open local text memory file: %s" ),
                                   aPath );
        return false;
    }

    for( size_t lineIndex = 0; lineIndex < file.GetLineCount(); ++lineIndex )
    {
        const wxString line = file.GetLine( lineIndex ).Trim().Trim( false );

        if( line.IsEmpty() )
            continue;

        nlohmann::json json = nlohmann::json::parse( toUtf8String( line ), nullptr, false );

        if( json.is_discarded() || !json.is_object() )
        {
            aError = wxString::Format( wxS( "Invalid local text memory JSONL at line %zu" ),
                                       lineIndex + 1 );
            return false;
        }

        AI_LOCAL_TEXT_MEMORY_RECORD record;
        record.m_Id = jsonStringValue( json, "id" );
        record.m_ProjectId = jsonStringValue( json, "project_id" );
        record.m_DocumentId = jsonStringValue( json, "document_id" );
        record.m_AgentKind = jsonStringValue( json, "agent_kind" );
        record.m_Type = jsonStringValue( json, "type" );
        record.m_Text = jsonStringValue( json, "text" );
        record.m_Source = jsonStringValue( json, "source" );
        record.m_ProvenanceJson = jsonStringValue( json, "provenance" );
        record.m_BoardStateVersion = jsonStringValue( json, "board_state_version" );
        record.m_AcceptanceState = jsonStringValue( json, "acceptance_state" );
        record.m_ObjectIds = jsonStringArrayValue( json, "object_ids" );
        record.m_TrustLevel = jsonIntValue( json, "trust_level" );
        record.m_Sequence = jsonSequenceValue( json );
        record.m_CreatedAtUnixSeconds = jsonInt64Value( json, "created_at_unix_seconds" );
        record.m_ExpiresAtUnixSeconds = jsonInt64Value( json, "expires_at_unix_seconds" );

        AddRecord( std::move( record ) );
    }

    aError.clear();
    return true;
}


bool AI_LOCAL_TEXT_MEMORY_INDEX::LoadTextFile(
        const wxString& aPath,
        const AI_LOCAL_TEXT_FILE_RECORD_OPTIONS& aOptions,
        wxString& aError )
{
    wxFFile file( aPath, wxS( "rb" ) );

    if( !file.IsOpened() )
    {
        aError = wxString::Format( wxS( "Unable to open local text file: %s" ),
                                   aPath );
        return false;
    }

    wxString text;

    if( !file.ReadAll( &text ) )
    {
        aError = wxString::Format( wxS( "Unable to read local text file: %s" ),
                                   aPath );
        return false;
    }

    text.Trim().Trim( false );

    if( text.IsEmpty() )
    {
        aError = wxString::Format( wxS( "Local text file is empty: %s" ),
                                   aPath );
        return false;
    }

    AI_LOCAL_TEXT_MEMORY_RECORD record;
    record.m_Id = localTextFileRecordId( aPath, text );
    record.m_ProjectId = aOptions.m_ProjectId;
    record.m_DocumentId = aOptions.m_DocumentId;
    record.m_AgentKind = aOptions.m_AgentKind;
    record.m_Type = aOptions.m_Type;
    record.m_Text = text;
    record.m_Source = aOptions.m_Source.IsEmpty() ? wxString( wxS( "local_file" ) )
                                                  : aOptions.m_Source;
    record.m_ProvenanceJson = localTextFileProvenanceJson( aPath );
    record.m_BoardStateVersion = aOptions.m_BoardStateVersion;
    record.m_AcceptanceState = aOptions.m_AcceptanceState;
    record.m_ObjectIds = aOptions.m_ObjectIds;
    record.m_TrustLevel = aOptions.m_TrustLevel;
    record.m_Sequence = aOptions.m_Sequence;
    record.m_CreatedAtUnixSeconds = aOptions.m_CreatedAtUnixSeconds;
    record.m_ExpiresAtUnixSeconds = aOptions.m_ExpiresAtUnixSeconds;

    AddRecord( std::move( record ) );
    aError.clear();
    return true;
}


bool AI_LOCAL_TEXT_MEMORY_INDEX::LoadTextDirectory(
        const wxString& aDirectory,
        const AI_LOCAL_TEXT_FILE_RECORD_OPTIONS& aOptions,
        wxString& aError,
        bool aRecursive )
{
    if( !wxFileName::DirExists( aDirectory ) )
    {
        aError = wxString::Format( wxS( "Local text directory does not exist: %s" ),
                                   aDirectory );
        return false;
    }

    std::vector<wxString> files;

    if( !collectSupportedTextFiles( aDirectory, aRecursive, files, aError ) )
        return false;

    std::sort( files.begin(), files.end() );

    uint64_t offset = 0;
    AI_LOCAL_TEXT_MEMORY_INDEX loadedRecords;

    for( const wxString& file : files )
    {
        AI_LOCAL_TEXT_FILE_RECORD_OPTIONS fileOptions = aOptions;

        if( fileOptions.m_Sequence != 0 )
            fileOptions.m_Sequence += offset;

        if( !loadedRecords.LoadTextFile( file, fileOptions, aError ) )
            return false;

        ++offset;
    }

    const wxString source = aOptions.m_Source.IsEmpty()
                            ? wxString( wxS( "local_file" ) )
                            : aOptions.m_Source;

    m_Records.erase(
            std::remove_if( m_Records.begin(), m_Records.end(),
                            [&]( const AI_LOCAL_TEXT_MEMORY_RECORD& aRecord )
                            {
                                return aRecord.m_ProjectId == aOptions.m_ProjectId
                                       && aRecord.m_DocumentId == aOptions.m_DocumentId
                                       && aRecord.m_AgentKind == aOptions.m_AgentKind
                                       && aRecord.m_Type == aOptions.m_Type
                                       && aRecord.m_Source == source;
                            } ),
            m_Records.end() );

    for( const AI_LOCAL_TEXT_MEMORY_RECORD& record : loadedRecords.Records() )
        AddRecord( record );

    aError.clear();
    return true;
}


AI_PROVIDER_INPUT_BLOCK AiLocalTextMemoryResultToProviderInputBlock(
        const AI_LOCAL_TEXT_MEMORY_RESULT& aResult )
{
    const AI_LOCAL_TEXT_MEMORY_RECORD& record = aResult.m_Record;

    AI_PROVIDER_INPUT_BLOCK block;
    block.m_Id = wxS( "local." ) + record.m_Id;
    block.m_Kind = wxS( "retrieved_memory" );
    block.m_Source = record.m_Source.IsEmpty() ? wxString( wxS( "local_text_memory" ) )
                                               : record.m_Source;
    block.m_Text = record.m_Text;
    block.m_OriginalChars = record.m_Text.length();

    nlohmann::json metadata = {
        { "id", toUtf8String( record.m_Id ) },
        { "project_id", toUtf8String( record.m_ProjectId ) },
        { "document_id", toUtf8String( record.m_DocumentId ) },
        { "agent_kind", toUtf8String( record.m_AgentKind ) },
        { "type", toUtf8String( record.m_Type ) },
        { "source", toUtf8String( record.m_Source ) },
        { "provenance", toUtf8String( record.m_ProvenanceJson ) },
        { "board_state_version", toUtf8String( record.m_BoardStateVersion ) },
        { "acceptance_state", toUtf8String( record.m_AcceptanceState ) },
        { "object_ids", stringArrayJson( record.m_ObjectIds ) },
        { "trust_level", record.m_TrustLevel },
        { "sequence", record.m_Sequence },
        { "lexical_score", aResult.m_Score },
        { "retrieval_backend", "local_text_memory" }
    };

    block.m_MetadataJson = fromUtf8String( metadata.dump() );
    return block;
}
