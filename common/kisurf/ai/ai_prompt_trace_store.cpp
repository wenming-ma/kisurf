#include <kisurf/ai/ai_prompt_trace_store.h>

#include <nlohmann/json.hpp>

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


nlohmann::json parseJsonOrString( const wxString& aText )
{
    nlohmann::json parsed = nlohmann::json::parse( toUtf8String( aText ), nullptr, false );

    if( parsed.is_discarded() )
        return toUtf8String( aText );

    return parsed;
}
}


AI_PROMPT_TRACE_STORE::AI_PROMPT_TRACE_STORE() :
        AI_PROMPT_TRACE_STORE( DefaultPath() )
{
}


AI_PROMPT_TRACE_STORE::AI_PROMPT_TRACE_STORE(
        wxString aPath, AI_PROMPT_TRACE_RETENTION_POLICY aRetention ) :
        m_Path( std::move( aPath ) ),
        m_Retention( aRetention )
{
}


wxString AI_PROMPT_TRACE_STORE::DefaultPath()
{
    wxString base = wxStandardPaths::Get().GetUserLocalDataDir();

    if( base.IsEmpty() )
        base = wxStandardPaths::Get().GetUserDataDir();

    if( base.IsEmpty() )
        base = wxStandardPaths::Get().GetTempDir();

    wxFileName path;
    path.AssignDir( base );
    path.AppendDir( wxS( "ai" ) );
    path.SetFullName( wxS( "prompt_trace.jsonl" ) );
    return path.GetFullPath();
}


bool AI_PROMPT_TRACE_STORE::Append( const AI_PROVIDER_REQUEST& aRequest,
                                    const wxString& aProviderStatus,
                                    wxString& aError )
{
    return Append( aRequest, aProviderStatus, wxString(), aError );
}


bool AI_PROMPT_TRACE_STORE::Append( const AI_PROVIDER_REQUEST& aRequest,
                                    const wxString& aProviderStatus,
                                    const wxString& aProviderTraceJson,
                                    wxString& aError )
{
    if( !aRequest.m_ContextCompiled || aRequest.m_PromptTraceJson.IsEmpty() )
    {
        aError = wxS( "Provider request must be compiled before prompt trace storage." );
        return false;
    }

    wxFileName fileName( m_Path );

    if( !fileName.GetPath().IsEmpty() && !fileName.DirExists() )
    {
        if( !fileName.Mkdir( wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL ) )
        {
            aError = wxString::Format( wxS( "Unable to create prompt trace directory: %s" ),
                                       fileName.GetPath() );
            return false;
        }
    }

    wxFFile file( m_Path, wxS( "ab" ) );

    if( !file.IsOpened() )
    {
        aError = wxString::Format( wxS( "Unable to open prompt trace store: %s" ),
                                   m_Path );
        return false;
    }

    nlohmann::json record = {
        { "schema", { { "name", "kisurf.ai.prompt_trace_record" }, { "version", 1 } } },
        { "request_id", aRequest.m_RequestId },
        { "conversation_id", aRequest.m_ConversationId },
        { "request_kind", static_cast<int>( aRequest.m_RequestKind ) },
        { "context_estimated_chars", aRequest.m_ContextEstimatedChars },
        { "provider_status", toUtf8String( aProviderStatus ) },
        { "prompt_trace", parseJsonOrString( aRequest.m_PromptTraceJson ) }
    };

    if( !aProviderTraceJson.IsEmpty() )
        record["provider_trace"] = parseJsonOrString( aProviderTraceJson );

    file.Write( fromUtf8String( record.dump() + "\n" ), wxConvUTF8 );
    file.Close();

    if( !ApplyRetention( aError ) )
        return false;

    aError.clear();
    return true;
}


std::vector<AI_PROMPT_TRACE_ENTRY> AI_PROMPT_TRACE_STORE::LoadAll( wxString& aError ) const
{
    std::vector<AI_PROMPT_TRACE_ENTRY> entries;
    wxTextFile                         file;

    if( !wxFileExists( m_Path ) )
    {
        aError.clear();
        return entries;
    }

    if( !file.Open( m_Path ) )
    {
        aError = wxString::Format( wxS( "Unable to open prompt trace store: %s" ),
                                   m_Path );
        return entries;
    }

    for( size_t lineIndex = 0; lineIndex < file.GetLineCount(); ++lineIndex )
    {
        const wxString line = file.GetLine( lineIndex ).Trim().Trim( false );

        if( line.IsEmpty() )
            continue;

        nlohmann::json record = nlohmann::json::parse( toUtf8String( line ), nullptr, false );

        if( record.is_discarded() || !record.is_object() )
        {
            aError = wxString::Format( wxS( "Invalid prompt trace JSONL at line %zu" ),
                                       lineIndex + 1 );
            entries.clear();
            return entries;
        }

        AI_PROMPT_TRACE_ENTRY entry;
        entry.m_RequestId = record.value( "request_id", 0ull );
        entry.m_ConversationId = record.value( "conversation_id", 0ull );
        entry.m_RequestKind = static_cast<AI_PROVIDER_REQUEST_KIND>(
                record.value( "request_kind", static_cast<int>( AI_PROVIDER_REQUEST_KIND::Chat ) ) );
        entry.m_ContextEstimatedChars = record.value( "context_estimated_chars", 0ull );
        entry.m_ProviderStatus =
                fromUtf8String( record.value( "provider_status", std::string() ) );

        const auto traceIt = record.find( "prompt_trace" );

        if( traceIt != record.end() )
            entry.m_PromptTraceJson = fromUtf8String( traceIt->dump() );

        const auto providerTraceIt = record.find( "provider_trace" );

        if( providerTraceIt != record.end() )
            entry.m_ProviderTraceJson = fromUtf8String( providerTraceIt->dump() );

        entries.push_back( std::move( entry ) );
    }

    aError.clear();
    return entries;
}


bool AI_PROMPT_TRACE_STORE::ApplyRetention( wxString& aError ) const
{
    if( m_Retention.m_MaxEntries == 0 || !wxFileExists( m_Path ) )
    {
        aError.clear();
        return true;
    }

    wxTextFile file;

    if( !file.Open( m_Path ) )
    {
        aError = wxString::Format( wxS( "Unable to open prompt trace store: %s" ),
                                   m_Path );
        return false;
    }

    const size_t lineCount = file.GetLineCount();

    if( lineCount <= m_Retention.m_MaxEntries )
    {
        aError.clear();
        return true;
    }

    std::vector<wxString> retained;
    retained.reserve( m_Retention.m_MaxEntries );

    const size_t firstRetained = lineCount - m_Retention.m_MaxEntries;

    for( size_t lineIndex = firstRetained; lineIndex < lineCount; ++lineIndex )
        retained.push_back( file.GetLine( lineIndex ) );

    file.Clear();

    for( const wxString& line : retained )
        file.AddLine( line );

    if( !file.Write() )
    {
        aError = wxString::Format( wxS( "Unable to apply prompt trace retention: %s" ),
                                   m_Path );
        return false;
    }

    aError.clear();
    return true;
}
