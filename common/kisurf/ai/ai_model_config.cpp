#include <kisurf/ai/ai_model_config.h>
#include <kisurf/ai/ai_provider.h>

#include <kiplatform/io.h>
#include <nlohmann/json.hpp>
#include <paths.h>

#include <iomanip>
#include <sstream>
#include <utility>
#include <wx/filename.h>
#include <wx/stdstream.h>
#include <wx/utils.h>
#include <wx/wfstream.h>

namespace
{
wxString normalizedUrl( const wxString& aUrl )
{
    wxString url = aUrl;
    url.Trim( true ).Trim( false );

    while( url.EndsWith( wxS( "/" ) ) )
        url.RemoveLast();

    return url;
}


std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


wxString fromUtf8String( const std::string& aText )
{
    return wxString::FromUTF8( aText.c_str() );
}


bool jsonString( const nlohmann::json& aJson, const char* aKey, wxString& aOut )
{
    if( !aJson.contains( aKey ) || !aJson[aKey].is_string() )
        return false;

    aOut = fromUtf8String( aJson[aKey].get<std::string>() );
    return true;
}


bool ensureParentDirectory( const wxString& aPath, wxString* aError )
{
    wxFileName fileName( aPath );
    const wxString dir = fileName.GetPath();

    if( dir.IsEmpty() || wxFileName::DirExists( dir ) )
        return true;

    if( wxFileName::Mkdir( dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL ) )
        return true;

    if( aError )
        *aError = wxString::Format( wxS( "Unable to create settings directory '%s'." ), dir );

    return false;
}
} // namespace


void AI_MODEL_CONFIG::Normalize()
{
    m_BaseUrl = normalizedUrl( m_BaseUrl );

    if( m_BaseUrl.IsEmpty() )
        m_BaseUrl = AI_PROVIDER_SETTINGS::DefaultBaseUrl();

    m_Model.Trim( true ).Trim( false );

    if( m_Model.IsEmpty() )
        m_Model = AI_PROVIDER_SETTINGS::DefaultModel();

    m_ApiKey.Trim( true ).Trim( false );
}


wxString AiModelProviderKindToken( AI_MODEL_PROVIDER_KIND aKind )
{
    switch( aKind )
    {
    case AI_MODEL_PROVIDER_KIND::OpenAiCompatible:
        return wxS( "openai-compatible" );

    case AI_MODEL_PROVIDER_KIND::AnthropicCompatible:
        return wxS( "anthropic-compatible" );
    }

    return wxS( "openai-compatible" );
}


wxString AiModelProviderKindLabel( AI_MODEL_PROVIDER_KIND aKind )
{
    switch( aKind )
    {
    case AI_MODEL_PROVIDER_KIND::OpenAiCompatible:
        return wxS( "OpenAI-compatible" );

    case AI_MODEL_PROVIDER_KIND::AnthropicCompatible:
        return wxS( "Anthropic-compatible" );
    }

    return wxS( "OpenAI-compatible" );
}


AI_MODEL_PROVIDER_KIND AiModelProviderKindFromToken( const wxString& aToken )
{
    wxString token = aToken;
    token.Trim( true ).Trim( false );

    if( token.CmpNoCase( wxS( "anthropic-compatible" ) ) == 0
        || token.CmpNoCase( wxS( "anthropic" ) ) == 0 )
    {
        return AI_MODEL_PROVIDER_KIND::AnthropicCompatible;
    }

    return AI_MODEL_PROVIDER_KIND::OpenAiCompatible;
}


AI_MODEL_CONFIG_STORE::AI_MODEL_CONFIG_STORE() :
        AI_MODEL_CONFIG_STORE( DefaultConfigPath() )
{
}


AI_MODEL_CONFIG_STORE::AI_MODEL_CONFIG_STORE(
        wxString aPath, std::unique_ptr<OAUTH_SECRET_BACKEND> aSecretBackend ) :
        m_Path( std::move( aPath ) ),
        m_SecretBackend( std::move( aSecretBackend ) )
{
}


bool AI_MODEL_CONFIG_STORE::Load( AI_MODEL_CONFIG& aConfig, wxString* aError ) const
{
    aConfig = AI_MODEL_CONFIG();
    bool loadedFile = false;

    if( !m_Path.IsEmpty() && wxFileName::FileExists( m_Path ) )
    {
        try
        {
            wxFFileInputStream fp( m_Path, wxS( "rt" ) );
            wxStdInputStream   stream( fp );

            if( !fp.IsOk() )
            {
                if( aError )
                    *aError = wxString::Format( wxS( "Unable to open '%s'." ), m_Path );

                return false;
            }

            nlohmann::json parsed = nlohmann::json::parse( stream, nullptr, true, true );

            if( !parsed.is_object() )
            {
                if( aError )
                    *aError = wxString::Format( wxS( "'%s' is not a JSON object." ), m_Path );

                return false;
            }

            wxString providerToken;

            if( jsonString( parsed, "provider", providerToken ) )
                aConfig.m_ProviderKind = AiModelProviderKindFromToken( providerToken );

            jsonString( parsed, "base_url", aConfig.m_BaseUrl );
            jsonString( parsed, "model", aConfig.m_Model );
            loadedFile = true;
        }
        catch( const std::exception& e )
        {
            if( aError )
            {
                *aError = wxString::Format( wxS( "Unable to load '%s': %s" ),
                                            m_Path, wxString::FromUTF8( e.what() ) );
            }

            return false;
        }
    }

    wxString apiKey;

    if( loadedFile && m_SecretBackend
        && m_SecretBackend->GetSecret( SecretServiceName(),
                                       SecretKeyForProvider( aConfig.m_ProviderKind ),
                                       apiKey ) )
    {
        aConfig.m_ApiKey = apiKey;
    }

    aConfig.Normalize();
    return loadedFile || !aConfig.m_BaseUrl.IsEmpty();
}


bool AI_MODEL_CONFIG_STORE::Save( const AI_MODEL_CONFIG& aConfig, wxString* aError ) const
{
    AI_MODEL_CONFIG config = aConfig;
    config.Normalize();

    if( m_Path.IsEmpty() )
    {
        if( aError )
            *aError = wxS( "AI model settings path is empty." );

        return false;
    }

    if( !ensureParentDirectory( m_Path, aError ) )
        return false;

    nlohmann::json payload = {
        { "provider", toUtf8String( AiModelProviderKindToken( config.m_ProviderKind ) ) },
        { "base_url", toUtf8String( config.m_BaseUrl ) },
        { "model", toUtf8String( config.m_Model ) },
        { "api_key_ref", toUtf8String( SecretKeyForProvider( config.m_ProviderKind ) ) }
    };

    std::stringstream buffer;
    buffer << std::setw( 2 ) << payload << std::endl;
    const std::string text = buffer.str();

    wxString writeError;

    if( !KIPLATFORM::IO::AtomicWriteFile( m_Path, text.data(), text.size(), &writeError ) )
    {
        if( aError )
            *aError = writeError;

        return false;
    }

    if( !m_SecretBackend )
        return true;

    const wxString secretKey = SecretKeyForProvider( config.m_ProviderKind );

    if( config.m_ApiKey.IsEmpty() )
    {
        m_SecretBackend->DeleteSecret( SecretServiceName(), secretKey );
        return true;
    }

    if( !m_SecretBackend->StoreSecret( SecretServiceName(), secretKey, config.m_ApiKey ) )
    {
        if( aError )
            *aError = wxS( "Unable to save AI model API key to the local secret store." );

        return false;
    }

    return true;
}


wxString AI_MODEL_CONFIG_STORE::DefaultConfigPath()
{
    wxString overridePath;

    if( wxGetEnv( wxS( "KISURF_AI_MODEL_CONFIG_PATH" ), &overridePath ) )
    {
        overridePath.Trim( true ).Trim( false );

        if( !overridePath.IsEmpty() )
            return overridePath;
    }

    wxFileName path( PATHS::GetUserSettingsPath(), wxS( "kisurf_ai_model" ) );
    path.SetExt( wxS( "json" ) );
    return path.GetFullPath();
}


AI_MODEL_CONFIG AI_MODEL_CONFIG_STORE::LoadUserConfig( wxString* aError )
{
    AI_MODEL_CONFIG       config;
    AI_MODEL_CONFIG_STORE store;
    store.Load( config, aError );
    return config;
}


wxString AI_MODEL_CONFIG_STORE::SecretServiceName()
{
    return wxS( "org.kicad.kisurf.ai_model" );
}


wxString AI_MODEL_CONFIG_STORE::SecretKeyForProvider( AI_MODEL_PROVIDER_KIND aKind )
{
    return AiModelProviderKindToken( aKind ) + wxS( ".default_api_key" );
}
