#include <kisurf/ai/ai_prompt_trace_store.h>

#include <nlohmann/json.hpp>

#include <array>
#include <deque>
#include <utility>

#include <wx/ffile.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>

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


template<typename LineHandler>
bool readJsonlLines( const wxString& aPath, LineHandler aHandler, wxString& aError )
{
    wxFFile file( aPath, wxS( "rb" ) );

    if( !file.IsOpened() )
    {
        aError = wxString::Format( wxS( "Unable to open prompt trace store: %s" ),
                                   aPath );
        return false;
    }

    std::array<char, 64 * 1024> buffer;
    std::string                 currentLine;
    size_t                      lineNumber = 0;

    for( ;; )
    {
        const size_t bytesRead = file.Read( buffer.data(), buffer.size() );

        if( bytesRead == 0 )
            break;

        for( size_t i = 0; i < bytesRead; ++i )
        {
            const char ch = buffer[i];

            if( ch == '\n' )
            {
                if( !currentLine.empty() && currentLine.back() == '\r' )
                    currentLine.pop_back();

                ++lineNumber;

                if( !aHandler( currentLine, lineNumber ) )
                    return false;

                currentLine.clear();
            }
            else
            {
                currentLine.push_back( ch );
            }
        }
    }

    if( !currentLine.empty() )
    {
        ++lineNumber;

        if( !aHandler( currentLine, lineNumber ) )
            return false;
    }

    aError.clear();
    return true;
}


bool writeJsonlLinesReplacingFile( const wxString& aPath,
                                   const std::deque<std::string>& aLines,
                                   wxString& aError )
{
    const wxString tempPath = aPath + wxS( ".tmp" );
    wxFFile        file( tempPath, wxS( "wb" ) );

    if( !file.IsOpened() )
    {
        aError = wxString::Format( wxS( "Unable to open prompt trace temp store: %s" ),
                                   tempPath );
        return false;
    }

    for( const std::string& line : aLines )
    {
        if( !line.empty() && file.Write( line.data(), line.size() ) != line.size() )
        {
            aError = wxString::Format( wxS( "Unable to write prompt trace temp store: %s" ),
                                       tempPath );
            return false;
        }

        if( file.Write( "\n", 1 ) != 1 )
        {
            aError = wxString::Format( wxS( "Unable to write prompt trace temp store: %s" ),
                                       tempPath );
            return false;
        }
    }

    file.Close();

    if( !wxRenameFile( tempPath, aPath, true ) )
    {
        aError = wxString::Format( wxS( "Unable to replace prompt trace store: %s" ),
                                   aPath );
        return false;
    }

    aError.clear();
    return true;
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

    if( !wxFileExists( m_Path ) )
    {
        aError.clear();
        return entries;
    }

    const bool ok = readJsonlLines( m_Path,
            [&]( const std::string& aLine, size_t aLineNumber )
    {
        const wxString line = fromUtf8String( aLine ).Trim().Trim( false );

        if( line.IsEmpty() )
            return true;

        nlohmann::json record = nlohmann::json::parse( aLine, nullptr, false );

        if( record.is_discarded() || !record.is_object() )
        {
            aError = wxString::Format( wxS( "Invalid prompt trace JSONL at line %zu" ),
                                       aLineNumber );
            entries.clear();
            return false;
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

        return true;
    }, aError );

    if( !ok )
        return entries;

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

    std::deque<std::string> retained;
    size_t                  lineCount = 0;

    if( !readJsonlLines( m_Path,
            [&]( const std::string& aLine, size_t )
    {
        ++lineCount;
        retained.push_back( aLine );

        while( retained.size() > m_Retention.m_MaxEntries )
            retained.pop_front();

        return true;
    }, aError ) )
    {
        return false;
    }

    if( lineCount <= m_Retention.m_MaxEntries )
    {
        aError.clear();
        return true;
    }

    if( !writeJsonlLinesReplacingFile( m_Path, retained, aError ) )
        return false;

    aError.clear();
    return true;
}
