#include <boost/test/unit_test.hpp>
#include <json_common.h>
#include <kisurf/ai/ai_model_config.h>
#include <kisurf/ai/ai_provider.h>
#include <oauth/secure_token_store.h>

#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <wx/filename.h>

namespace
{
class MEMORY_SECRET_BACKEND : public OAUTH_SECRET_BACKEND
{
public:
    using SECRET_MAP = std::map<std::string, wxString>;

    explicit MEMORY_SECRET_BACKEND( std::shared_ptr<SECRET_MAP> aSecrets ) :
            m_Secrets( std::move( aSecrets ) )
    {
    }

    bool StoreSecret( const wxString& aService, const wxString& aKey,
                      const wxString& aSecret ) override
    {
        ( *m_Secrets )[keyFor( aService, aKey )] = aSecret;
        return true;
    }

    bool GetSecret( const wxString& aService, const wxString& aKey,
                    wxString& aSecret ) const override
    {
        auto it = m_Secrets->find( keyFor( aService, aKey ) );

        if( it == m_Secrets->end() )
            return false;

        aSecret = it->second;
        return true;
    }

    bool DeleteSecret( const wxString& aService, const wxString& aKey ) override
    {
        m_Secrets->erase( keyFor( aService, aKey ) );
        return true;
    }

private:
    static std::string keyFor( const wxString& aService, const wxString& aKey )
    {
        return aService.ToStdString() + "|" + aKey.ToStdString();
    }

    std::shared_ptr<SECRET_MAP> m_Secrets;
};


wxString tempConfigPath()
{
    wxString path = wxFileName::CreateTempFileName( wxS( "kisurf_ai_model" ) );
    wxRemoveFile( path );
    return path;
}


std::string readFile( const wxString& aPath )
{
    std::ifstream stream( aPath.ToStdString(), std::ios::binary );
    return std::string( std::istreambuf_iterator<char>( stream ),
                        std::istreambuf_iterator<char>() );
}
} // namespace

BOOST_AUTO_TEST_SUITE( AiModelConfig )


BOOST_AUTO_TEST_CASE( DefaultsUseOpenAiCompatibleEndpoint )
{
    AI_MODEL_CONFIG config;
    config.Normalize();

    BOOST_CHECK( config.m_ProviderKind == AI_MODEL_PROVIDER_KIND::OpenAiCompatible );
    BOOST_CHECK_EQUAL( config.m_BaseUrl,
                       wxString( wxS( "https://sub2api.wenming-dev.org/v1" ) ) );
    BOOST_CHECK_EQUAL( config.m_Model, AI_PROVIDER_SETTINGS::DefaultModel() );
    BOOST_CHECK( config.m_ApiKey.IsEmpty() );
}


BOOST_AUTO_TEST_CASE( StorePersistsNonSecretJsonAndSecretBackedApiKey )
{
    wxString path = tempConfigPath();
    auto secrets = std::make_shared<MEMORY_SECRET_BACKEND::SECRET_MAP>();

    AI_MODEL_CONFIG config;
    config.m_ProviderKind = AI_MODEL_PROVIDER_KIND::OpenAiCompatible;
    config.m_BaseUrl = wxS( "https://unit.example.test/v1/" );
    config.m_Model = wxS( "unit-model" );
    config.m_ApiKey = wxS( "unit-test-key" );

    wxString error;
    AI_MODEL_CONFIG_STORE saveStore(
            path, std::make_unique<MEMORY_SECRET_BACKEND>( secrets ) );

    BOOST_REQUIRE_MESSAGE( saveStore.Save( config, &error ), error.ToStdString() );

    std::string payload = readFile( path );
    BOOST_CHECK( payload.find( "unit-test-key" ) == std::string::npos );
    BOOST_CHECK( payload.find( "openai-compatible" ) != std::string::npos );
    BOOST_CHECK( payload.find( "https://unit.example.test/v1" ) != std::string::npos );

    AI_MODEL_CONFIG loaded;
    AI_MODEL_CONFIG_STORE loadStore(
            path, std::make_unique<MEMORY_SECRET_BACKEND>( secrets ) );

    BOOST_REQUIRE_MESSAGE( loadStore.Load( loaded, &error ), error.ToStdString() );
    BOOST_CHECK( loaded.m_ProviderKind == AI_MODEL_PROVIDER_KIND::OpenAiCompatible );
    BOOST_CHECK_EQUAL( loaded.m_BaseUrl,
                       wxString( wxS( "https://unit.example.test/v1" ) ) );
    BOOST_CHECK_EQUAL( loaded.m_Model, wxString( wxS( "unit-model" ) ) );
    BOOST_CHECK_EQUAL( loaded.m_ApiKey, wxString( wxS( "unit-test-key" ) ) );
}


BOOST_AUTO_TEST_CASE( MissingSettingsFileDoesNotLoadOrphanSecret )
{
    wxString path = tempConfigPath();
    auto secrets = std::make_shared<MEMORY_SECRET_BACKEND::SECRET_MAP>();

    ( *secrets )[AI_MODEL_CONFIG_STORE::SecretServiceName().ToStdString() + "|"
                 + AI_MODEL_CONFIG_STORE::SecretKeyForProvider(
                           AI_MODEL_PROVIDER_KIND::OpenAiCompatible )
                           .ToStdString()] = wxS( "orphan-key" );

    AI_MODEL_CONFIG loaded;
    wxString        error;
    AI_MODEL_CONFIG_STORE store(
            path, std::make_unique<MEMORY_SECRET_BACKEND>( secrets ) );

    BOOST_REQUIRE_MESSAGE( store.Load( loaded, &error ), error.ToStdString() );
    BOOST_CHECK( loaded.m_ApiKey.IsEmpty() );
}


BOOST_AUTO_TEST_CASE( OpenAiFactoryUsesConfiguredEndpointModelAndKey )
{
    AI_MODEL_CONFIG config;
    config.m_ProviderKind = AI_MODEL_PROVIDER_KIND::OpenAiCompatible;
    config.m_BaseUrl = wxS( "https://unit.example.test/v1" );
    config.m_Model = wxS( "unit-model" );
    config.m_ApiKey = wxS( "unit-test-key" );

    bool handlerCalled = false;
    std::unique_ptr<AI_PROVIDER> provider = MakeAiProviderFromModelConfig(
            config,
            [&]( const AI_HTTP_REQUEST& aRequest, AI_HTTP_RESPONSE& aResponse,
                 wxString& aError )
            {
                wxUnusedVar( aError );
                handlerCalled = true;

                BOOST_CHECK_EQUAL(
                        aRequest.m_Url,
                        wxString( wxS( "https://unit.example.test/v1/chat/completions" ) ) );
                BOOST_CHECK_EQUAL( aRequest.HeaderValue( wxS( "Authorization" ) ),
                                   wxString( wxS( "Bearer unit-test-key" ) ) );

                nlohmann::json body = nlohmann::json::parse( aRequest.m_Body.ToStdString() );
                BOOST_CHECK_EQUAL( body["model"].get<std::string>(), "unit-model" );

                aResponse.m_StatusCode = 200;
                aResponse.m_Body =
                        wxS( "{\"choices\":[{\"message\":{\"content\":\"configured\"}}]}" );
                return true;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_UserText = wxS( "hello configured model" );

    AI_PROVIDER_RESPONSE response = provider->Generate( request );

    BOOST_CHECK( handlerCalled );
    BOOST_CHECK_EQUAL( response.m_Body, wxString( wxS( "configured" ) ) );
}


BOOST_AUTO_TEST_CASE( AnthropicCompatibleConfigReportsUnsupportedRuntime )
{
    AI_MODEL_CONFIG config;
    config.m_ProviderKind = AI_MODEL_PROVIDER_KIND::AnthropicCompatible;
    config.m_BaseUrl = wxS( "https://anthropic.example.test/v1" );
    config.m_Model = wxS( "claude-test" );
    config.m_ApiKey = wxS( "unit-test-key" );

    std::unique_ptr<AI_PROVIDER> provider = MakeAiProviderFromModelConfig( config );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 9;
    request.m_UserText = wxS( "try anthropic" );

    AI_PROVIDER_RESPONSE response = provider->Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 9 );
    BOOST_CHECK( response.m_Body.Contains( wxS( "Anthropic-compatible" ) ) );
    BOOST_CHECK( response.m_Body.Contains( wxS( "not implemented" ) ) );
}


BOOST_AUTO_TEST_SUITE_END()
