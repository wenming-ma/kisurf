#include <boost/test/unit_test.hpp>
#include <json_common.h>
#include <kisurf/ai/ai_provider_input_compiler.h>
#include <kisurf/ai/ai_next_action_runtime.h>
#include <kisurf/ai/ai_provider.h>

#include <wx/utils.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <wx/filename.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace
{
#ifdef _WIN32
class LOOPBACK_SSE_SERVER
{
public:
    LOOPBACK_SSE_SERVER()
    {
        m_WsaStarted = WSAStartup( MAKEWORD( 2, 2 ), &m_WsaData ) == 0;
    }

    ~LOOPBACK_SSE_SERVER()
    {
        Join();

        if( m_Server != INVALID_SOCKET )
            closesocket( m_Server );

        if( m_WsaStarted )
            WSACleanup();
    }

    bool Start( const std::string& aSseBody )
    {
        if( !m_WsaStarted )
            return false;

        m_Response = "HTTP/1.1 200 OK\r\n"
                     "Content-Type: text/event-stream\r\n"
                     "Connection: close\r\n"
                     "Content-Length: "
                     + std::to_string( aSseBody.size() )
                     + "\r\n\r\n"
                     + aSseBody;

        m_Server = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );

        if( m_Server == INVALID_SOCKET )
            return false;

        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
        address.sin_port = 0;

        if( bind( m_Server, reinterpret_cast<sockaddr*>( &address ), sizeof( address ) )
            == SOCKET_ERROR )
        {
            return false;
        }

        int addressLength = sizeof( address );

        if( getsockname( m_Server, reinterpret_cast<sockaddr*>( &address ),
                         &addressLength ) == SOCKET_ERROR )
        {
            return false;
        }

        m_Port = ntohs( address.sin_port );

        if( listen( m_Server, 1 ) == SOCKET_ERROR )
            return false;

        m_Thread = std::thread(
                [this]()
                {
                    run();
                } );
        return true;
    }

    uint16_t Port() const
    {
        return m_Port;
    }

    void Join()
    {
        if( m_Thread.joinable() )
            m_Thread.join();
    }

    const std::string& RequestText() const
    {
        return m_RequestText;
    }

private:
    static size_t contentLengthFromHeaders( const std::string& aRequest )
    {
        std::string lower = aRequest;
        std::transform( lower.begin(), lower.end(), lower.begin(),
                        []( unsigned char aChar )
                        {
                            return static_cast<char>( std::tolower( aChar ) );
                        } );

        constexpr char key[] = "content-length:";
        size_t keyPos = lower.find( key );

        if( keyPos == std::string::npos )
            return 0;

        keyPos += sizeof( key ) - 1;
        size_t endPos = lower.find( "\r\n", keyPos );
        std::string value = lower.substr( keyPos, endPos - keyPos );
        char* end = nullptr;
        unsigned long long parsed = std::strtoull( value.c_str(), &end, 10 );
        return static_cast<size_t>( parsed );
    }

    void run()
    {
        fd_set readSet;
        FD_ZERO( &readSet );
        FD_SET( m_Server, &readSet );

        timeval acceptTimeout = {};
        acceptTimeout.tv_sec = 5;

        if( select( 0, &readSet, nullptr, nullptr, &acceptTimeout ) <= 0 )
            return;

        SOCKET client = accept( m_Server, nullptr, nullptr );

        if( client == INVALID_SOCKET )
            return;

        const DWORD receiveTimeoutMs = 5000;
        setsockopt( client, SOL_SOCKET, SO_RCVTIMEO,
                    reinterpret_cast<const char*>( &receiveTimeoutMs ),
                    sizeof( receiveTimeoutMs ) );

        std::string request;
        char        buffer[2048];

        while( true )
        {
            int bytes = recv( client, buffer, sizeof( buffer ), 0 );

            if( bytes <= 0 )
                break;

            request.append( buffer, static_cast<size_t>( bytes ) );

            size_t headerEnd = request.find( "\r\n\r\n" );

            if( headerEnd == std::string::npos )
                continue;

            size_t bodyStart = headerEnd + 4;
            size_t bodyBytes = request.size() - bodyStart;

            if( bodyBytes >= contentLengthFromHeaders( request ) )
                break;
        }

        m_RequestText = std::move( request );

        const char* remaining = m_Response.data();
        size_t      remainingSize = m_Response.size();

        while( remainingSize > 0 )
        {
            int sent = send( client, remaining,
                             static_cast<int>( std::min<size_t>(
                                     remainingSize, 2048 ) ),
                             0 );

            if( sent <= 0 )
                break;

            remaining += sent;
            remainingSize -= static_cast<size_t>( sent );
        }

        shutdown( client, SD_SEND );
        closesocket( client );
    }

    WSADATA     m_WsaData = {};
    bool        m_WsaStarted = false;
    SOCKET      m_Server = INVALID_SOCKET;
    uint16_t    m_Port = 0;
    std::thread m_Thread;
    std::string m_Response;
    std::string m_RequestText;
};
#endif

class ENV_GUARD
{
public:
    explicit ENV_GUARD( wxString aName ) :
            m_Name( std::move( aName ) ),
            m_HadValue( wxGetEnv( m_Name, &m_Value ) )
    {
    }

    ~ENV_GUARD()
    {
        if( m_HadValue )
            wxSetEnv( m_Name, m_Value );
        else
            wxUnsetEnv( m_Name );
    }

private:
    wxString m_Name;
    wxString m_Value;
    bool     m_HadValue = false;
};


wxString missingModelConfigPath()
{
    wxString path = wxFileName::CreateTempFileName( wxS( "kisurf_ai_provider_model" ) );
    wxRemoveFile( path );
    return path;
}


bool pointSchemaRequiresXY( const nlohmann::json& aSchema )
{
    auto requiresFields =
            []( const nlohmann::json& aCandidate, const char* aX, const char* aY )
            {
                return aCandidate.contains( "required" )
                       && std::find( aCandidate["required"].begin(),
                                     aCandidate["required"].end(), aX )
                                  != aCandidate["required"].end()
                       && std::find( aCandidate["required"].begin(),
                                     aCandidate["required"].end(), aY )
                                  != aCandidate["required"].end();
            };

    if( requiresFields( aSchema, "x", "y" ) )
        return true;

    if( !aSchema.contains( "anyOf" ) || !aSchema["anyOf"].is_array() )
        return false;

    return std::any_of( aSchema["anyOf"].begin(), aSchema["anyOf"].end(),
                        [&]( const nlohmann::json& aVariant )
                        {
                            return requiresFields( aVariant, "x", "y" );
                        } );
}


bool pointSchemaSupportsModelFacingShortcuts( const nlohmann::json& aSchema )
{
    if( !pointSchemaRequiresXY( aSchema ) || !aSchema.contains( "anyOf" )
        || !aSchema["anyOf"].is_array() )
    {
        return false;
    }

    bool sawMmObject = false;
    bool sawMmArray = false;

    for( const nlohmann::json& variant : aSchema["anyOf"] )
    {
        if( variant.value( "type", std::string() ) == "array"
            && variant.value( "minItems", 0 ) == 2
            && variant.value( "maxItems", 0 ) == 2 )
        {
            sawMmArray = true;
        }

        if( variant.contains( "required" )
            && std::find( variant["required"].begin(), variant["required"].end(),
                          "x_mm" )
                       != variant["required"].end()
            && std::find( variant["required"].begin(), variant["required"].end(),
                          "y_mm" )
                       != variant["required"].end() )
        {
            sawMmObject = true;
        }
    }

    return sawMmObject && sawMmArray;
}


bool boxSchemaSupportsCanonicalForms( const nlohmann::json& aSchema )
{
    if( !aSchema.is_object() || !aSchema.contains( "anyOf" )
        || !aSchema["anyOf"].is_array() )
    {
        return false;
    }

    bool sawOriginSizeBox = false;
    bool sawMinMaxBox = false;

    for( const nlohmann::json& variant : aSchema["anyOf"] )
    {
        if( !variant.is_object() || !variant.contains( "properties" )
            || !variant["properties"].is_object() )
        {
            continue;
        }

        const nlohmann::json& properties = variant["properties"];

        sawOriginSizeBox = sawOriginSizeBox
                           || ( properties.contains( "x" )
                                && properties.contains( "y" )
                                && properties.contains( "width" )
                                && properties.contains( "height" ) );

        sawMinMaxBox = sawMinMaxBox
                       || ( properties.contains( "min" )
                            && properties.contains( "max" )
                            && pointSchemaRequiresXY( properties["min"] )
                            && pointSchemaRequiresXY( properties["max"] ) );
    }

    return sawOriginSizeBox && sawMinMaxBox;
}


bool stringEnumContainsAll( const nlohmann::json& aSchema,
                            std::initializer_list<const char*> aValues )
{
    if( !aSchema.is_object() || aSchema.value( "type", std::string() ) != "string"
        || !aSchema.contains( "enum" ) || !aSchema["enum"].is_array() )
    {
        return false;
    }

    for( const char* value : aValues )
    {
        if( std::find( aSchema["enum"].begin(), aSchema["enum"].end(), value )
            == aSchema["enum"].end() )
        {
            return false;
        }
    }

    return true;
}


bool queryFilterSchemaSupportsShadowFilters( const nlohmann::json& aSchema )
{
    if( !aSchema.is_object() || aSchema.value( "type", std::string() ) != "object"
        || aSchema.value( "additionalProperties", true ) != false
        || !aSchema.contains( "properties" )
        || !aSchema["properties"].is_object() )
    {
        return false;
    }

    const nlohmann::json& properties = aSchema["properties"];

    return properties.contains( "type" )
           && properties["type"].value( "type", std::string() ) == "string"
           && properties.contains( "net" )
           && properties["net"].value( "type", std::string() ) == "string"
           && properties.contains( "layer" )
           && properties["layer"].value( "type", std::string() ) == "string"
           && properties.contains( "alias" )
           && properties["alias"].value( "type", std::string() ) == "string"
           && properties.contains( "selection" )
           && properties["selection"].value( "type", std::string() ) == "boolean"
           && properties.contains( "live_board" )
           && properties["live_board"].value( "type", std::string() ) == "boolean"
           && properties.contains( "bbox" )
           && boxSchemaSupportsCanonicalForms( properties["bbox"] )
           && properties.contains( "handle" )
           && properties["handle"].contains( "anyOf" );
}


bool queryHandleSchemaSupportsTypedReferences( const nlohmann::json& aSchema )
{
    if( !aSchema.is_object() || !aSchema.contains( "anyOf" )
        || !aSchema["anyOf"].is_array() )
    {
        return false;
    }

    bool sawAlias = false;
    bool sawHandleId = false;
    bool sawHandleObject = false;

    for( const nlohmann::json& variant : aSchema["anyOf"] )
    {
        if( !variant.is_object() )
            continue;

        const std::string type = variant.value( "type", std::string() );

        if( type == "string" )
            sawAlias = true;

        if( type == "integer" )
            sawHandleId = true;

        if( type == "object" && variant.value( "additionalProperties", true ) == false
            && variant.contains( "properties" )
            && variant["properties"].contains( "handle_id" )
            && variant["properties"].contains( "generation" )
            && variant["properties"].contains( "alias" )
            && variant.contains( "required" )
            && std::find( variant["required"].begin(), variant["required"].end(),
                          "handle_id" ) != variant["required"].end() )
        {
            sawHandleObject = true;
        }
    }

    return sawAlias && sawHandleId && sawHandleObject;
}


bool handleArraySchemaSupportsTypedReferences( const nlohmann::json& aSchema )
{
    return aSchema.is_object() && aSchema.value( "type", std::string() ) == "array"
           && aSchema.contains( "items" )
           && queryHandleSchemaSupportsTypedReferences( aSchema["items"] );
}


bool renderPreviewSchemaDeclaresTypedObservationArgs( const nlohmann::json& aSchema )
{
    if( !aSchema.is_object() || !aSchema.contains( "properties" )
        || !aSchema["properties"].is_object() )
    {
        return false;
    }

    const nlohmann::json& properties = aSchema["properties"];

    return properties.contains( "region" )
           && boxSchemaSupportsCanonicalForms( properties["region"] )
           && properties.contains( "layer_mask" )
           && properties["layer_mask"].value( "type", std::string() ) == "array"
           && properties["layer_mask"].contains( "items" )
           && properties["layer_mask"]["items"].value( "type", std::string() ) == "string"
           && properties.contains( "view_mode" )
           && properties["view_mode"].value( "type", std::string() ) == "string";
}


bool validationSchemaDeclaresTypedObservationArgs( const nlohmann::json& aSchema )
{
    if( !aSchema.is_object() || !aSchema.contains( "properties" )
        || !aSchema["properties"].is_object() )
    {
        return false;
    }

    const nlohmann::json& properties = aSchema["properties"];

    return properties.contains( "scope" )
           && stringEnumContainsAll( properties["scope"],
                                     { "affected_area", "selection", "region" } )
           && std::find( properties["scope"]["enum"].begin(),
                         properties["scope"]["enum"].end(), "session" )
                      == properties["scope"]["enum"].end()
           && properties.contains( "level" )
           && stringEnumContainsAll( properties["level"],
                                     { "geometry", "drc_lite", "full_drc" } )
           && properties.contains( "region" )
           && boxSchemaSupportsCanonicalForms( properties["region"] )
           && properties.contains( "handles" )
           && handleArraySchemaSupportsTypedReferences( properties["handles"] )
           && !properties.contains( "gate" );
}


size_t countSubstring( const std::string& aText, const std::string& aNeedle )
{
    if( aNeedle.empty() )
        return 0;

    size_t count = 0;
    size_t pos = 0;

    while( ( pos = aText.find( aNeedle, pos ) ) != std::string::npos )
    {
        ++count;
        pos += aNeedle.length();
    }

    return count;
}
} // namespace

BOOST_AUTO_TEST_SUITE( AiNativeProvider )


BOOST_AUTO_TEST_CASE( StubProviderReturnsDeterministicChatResponse )
{
    AI_STUB_PROVIDER provider;

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 42;
    request.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_UserText = wxS( "route this trace" );

    AI_PROVIDER_RESPONSE first = provider.Generate( request );
    AI_PROVIDER_RESPONSE second = provider.Generate( request );

    BOOST_CHECK_EQUAL( first.m_RequestId, 42 );
    BOOST_CHECK( first.m_Kind == AI_SUGGESTION_KIND::Chat );
    BOOST_CHECK_EQUAL( first.m_Title, second.m_Title );
    BOOST_CHECK_EQUAL( first.m_Body, second.m_Body );
    BOOST_CHECK( first.m_Body.Contains( wxS( "route this trace" ) ) );
}


BOOST_AUTO_TEST_CASE( StubProviderMentionsContextWhenPresent )
{
    AI_STUB_PROVIDER provider;

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 43;
    request.m_EditorKind = AI_EDITOR_KIND::Schematic;
    request.m_UserText = wxS( "inspect selected symbol" );
    request.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Schematic;
    request.m_ContextSnapshot.m_SelectedObjects.push_back(
            AI_OBJECT_REF( KIID(), SCH_SYMBOL_T, wxS( "U7" ) ) );

    AI_PROVIDER_RESPONSE response = provider.Generate( request );

    BOOST_CHECK( response.m_Body.Contains( wxS( "selected objects: 1" ) ) );
}


BOOST_AUTO_TEST_CASE( DefaultProviderReportsMissingKeyWhenUnconfigured )
{
    ENV_GUARD providerGuard( wxS( "KISURF_AI_PROVIDER" ) );
    ENV_GUARD keyGuard( wxS( "OPENAI_API_KEY" ) );
    ENV_GUARD kisurfBaseGuard( wxS( "KISURF_AI_BASE_URL" ) );
    ENV_GUARD openaiBaseGuard( wxS( "OPENAI_BASE_URL" ) );
    ENV_GUARD lowerBaseGuard( wxS( "base_url" ) );
    ENV_GUARD modelConfigGuard( wxS( "KISURF_AI_MODEL_CONFIG_PATH" ) );

    wxUnsetEnv( wxS( "KISURF_AI_PROVIDER" ) );
    wxUnsetEnv( wxS( "OPENAI_API_KEY" ) );
    wxUnsetEnv( wxS( "KISURF_AI_BASE_URL" ) );
    wxUnsetEnv( wxS( "OPENAI_BASE_URL" ) );
    wxUnsetEnv( wxS( "base_url" ) );
    wxSetEnv( wxS( "KISURF_AI_MODEL_CONFIG_PATH" ), missingModelConfigPath() );

    std::unique_ptr<AI_PROVIDER> provider = MakeDefaultAiProvider();

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 44;
    request.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_UserText = wxS( "connect to model" );

    AI_PROVIDER_RESPONSE response = provider->Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 44 );
    BOOST_CHECK( response.m_Body.Contains( wxS( "Model Settings" ) ) );
    BOOST_CHECK( !response.m_Body.Contains( wxS( "Stub response" ) ) );
}


BOOST_AUTO_TEST_CASE( DefaultProviderIgnoresOpenAiEnvironmentVariables )
{
    ENV_GUARD providerGuard( wxS( "KISURF_AI_PROVIDER" ) );
    ENV_GUARD keyGuard( wxS( "OPENAI_API_KEY" ) );
    ENV_GUARD kisurfBaseGuard( wxS( "KISURF_AI_BASE_URL" ) );
    ENV_GUARD openaiBaseGuard( wxS( "OPENAI_BASE_URL" ) );
    ENV_GUARD lowerBaseGuard( wxS( "base_url" ) );
    ENV_GUARD modelGuard( wxS( "OPENAI_MODEL" ) );
    ENV_GUARD kisurfModelGuard( wxS( "KISURF_AI_MODEL" ) );
    ENV_GUARD modelConfigGuard( wxS( "KISURF_AI_MODEL_CONFIG_PATH" ) );

    wxUnsetEnv( wxS( "KISURF_AI_PROVIDER" ) );
    wxSetEnv( wxS( "OPENAI_API_KEY" ), wxS( "unit-env-key" ) );
    wxSetEnv( wxS( "KISURF_AI_BASE_URL" ), wxS( "https://env.example.test/v1" ) );
    wxSetEnv( wxS( "OPENAI_BASE_URL" ), wxS( "https://openai-env.example.test/v1" ) );
    wxSetEnv( wxS( "base_url" ), wxS( "https://lower-env.example.test/v1" ) );
    wxSetEnv( wxS( "OPENAI_MODEL" ), wxS( "env-model" ) );
    wxSetEnv( wxS( "KISURF_AI_MODEL" ), wxS( "kisurf-env-model" ) );
    wxSetEnv( wxS( "KISURF_AI_MODEL_CONFIG_PATH" ), missingModelConfigPath() );

    std::unique_ptr<AI_PROVIDER> provider = MakeDefaultAiProvider();

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 46;
    request.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_UserText = wxS( "connect using configured model" );

    AI_PROVIDER_RESPONSE response = provider->Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 46 );
    BOOST_CHECK( response.m_Body.Contains( wxS( "Model Settings" ) ) );
    BOOST_CHECK( !response.m_Body.Contains( wxS( "OPENAI_API_KEY" ) ) );
    BOOST_CHECK( !response.m_Body.Contains( wxS( "Stub response" ) ) );
}


BOOST_AUTO_TEST_CASE( DefaultProviderCanBeForcedToStub )
{
    ENV_GUARD providerGuard( wxS( "KISURF_AI_PROVIDER" ) );
    ENV_GUARD keyGuard( wxS( "OPENAI_API_KEY" ) );

    wxSetEnv( wxS( "KISURF_AI_PROVIDER" ), wxS( "stub" ) );
    wxUnsetEnv( wxS( "OPENAI_API_KEY" ) );

    std::unique_ptr<AI_PROVIDER> provider = MakeDefaultAiProvider();

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 45;
    request.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_UserText = wxS( "offline check" );

    AI_PROVIDER_RESPONSE response = provider->Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 45 );
    BOOST_CHECK( response.m_Body.Contains( wxS( "Stub response" ) ) );
}


BOOST_AUTO_TEST_CASE( OpenAiProviderBuildsChatCompletionRequest )
{
    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxS( "https://sub2api.wenming-dev.org/v1" );
    settings.m_ApiKey = wxS( "unit-test-key" );
    settings.m_Model = wxS( "unit-model" );

    bool handlerCalled = false;

    AI_OPENAI_COMPAT_PROVIDER provider(
            settings,
            [&]( const AI_HTTP_REQUEST& aRequest, AI_HTTP_RESPONSE& aResponse, wxString& aError )
            {
                wxUnusedVar( aError );
                handlerCalled = true;

                BOOST_CHECK_EQUAL( aRequest.m_Method, wxString( wxS( "POST" ) ) );
                BOOST_CHECK_EQUAL( aRequest.m_Url,
                                   wxString( wxS( "https://sub2api.wenming-dev.org/v1/chat/completions" ) ) );
                BOOST_CHECK_EQUAL( aRequest.HeaderValue( wxS( "Authorization" ) ),
                                   wxString( wxS( "Bearer unit-test-key" ) ) );
                BOOST_CHECK( aRequest.m_Body.Contains( wxS( "\"model\":\"unit-model\"" ) ) );
                BOOST_CHECK( aRequest.m_Body.Contains( wxS( "route selected trace" ) ) );
                BOOST_CHECK( aRequest.m_Body.Contains(
                        wxS( "Editor context is available on demand through tools" ) ) );

                nlohmann::json body = nlohmann::json::parse( aRequest.m_Body.ToStdString() );
                const std::string systemContent =
                        body["messages"].at( 0 )["content"].get<std::string>();
                BOOST_CHECK( systemContent.find( "manual KiCad UI instructions" )
                             != std::string::npos );
                BOOST_CHECK( systemContent.find( "atomic operation tool path" )
                             != std::string::npos );
                BOOST_CHECK( systemContent.find( "retry_hint" )
                             != std::string::npos );
                BOOST_CHECK( systemContent.find( "expected_arguments" )
                             != std::string::npos );
                BOOST_CHECK( systemContent.find( "do not reuse created_handles" )
                             != std::string::npos );
                BOOST_CHECK( systemContent.find( "use aliases or re-query" )
                             != std::string::npos );
                BOOST_CHECK( systemContent.find( "pcb.delete_items with filter" )
                             != std::string::npos );
                BOOST_CHECK( systemContent.find( "{\"type\":\"tracks\"}" )
                             != std::string::npos );
                BOOST_CHECK( systemContent.find( "returns zero items" )
                             != std::string::npos );
                BOOST_CHECK( systemContent.find( "kisurf_query_board_summary" )
                             != std::string::npos );
                BOOST_CHECK( systemContent.find( "{\"type\":\"route\"}" )
                             != std::string::npos );
                const std::string content =
                        body["messages"].at( 1 )["content"].get<std::string>();
                BOOST_CHECK( content.find( "Structured KiSurf context JSON:" )
                             == std::string::npos );
                BOOST_CHECK( content.find( "\"kisurf_context\"" ) == std::string::npos );
                BOOST_CHECK( content.find( "\"selected_objects\"" ) == std::string::npos );
                BOOST_CHECK( content.find( "kisurf_get_workspace_view" )
                             != std::string::npos );

                aResponse.m_StatusCode = 200;
                aResponse.m_Body = wxS( "{\"choices\":[{\"message\":{\"content\":\"preview ready\"}}]}" );
                return true;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 12;
    request.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_UserText = wxS( "route selected trace" );
    request.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_ContextSnapshot.m_SelectedObjects.push_back(
            AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "U1.1" ) ) );

    AI_PROVIDER_RESPONSE response = provider.Generate( request );

    BOOST_CHECK( handlerCalled );
    BOOST_CHECK_EQUAL( response.m_RequestId, 12 );
    BOOST_CHECK_EQUAL( response.m_Body, wxString( wxS( "preview ready" ) ) );
}


BOOST_AUTO_TEST_CASE( OpenAiProviderSystemPromptRequiresValidationIssueAccuracy )
{
    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxS( "https://sub2api.wenming-dev.org/v1" );
    settings.m_ApiKey = wxS( "unit-test-key" );
    settings.m_Model = wxS( "unit-model" );

    AI_OPENAI_COMPAT_PROVIDER provider(
            settings,
            []( const AI_HTTP_REQUEST& aRequest, AI_HTTP_RESPONSE& aResponse,
                wxString& aError )
            {
                wxUnusedVar( aError );

                BOOST_CHECK( aRequest.m_Body.Contains( wxS( "issue_count" ) ) );
                BOOST_CHECK( aRequest.m_Body.Contains( wxS( "warning" ) ) );
                BOOST_CHECK( aRequest.m_Body.Contains( wxS( "use the supplied tools" ) ) );
                BOOST_CHECK( aRequest.m_Body.Contains(
                        wxS( "use current-board atomic or script tools directly" ) ) );
                BOOST_CHECK( aRequest.m_Body.Contains(
                        wxS( "Do not use preview, accept, reject, checkpoint, "
                             "or rollback workflows" ) ) );
                BOOST_CHECK( aRequest.m_Body.Contains(
                        wxS( "When describing Chat Agent capabilities" ) ) );
                BOOST_CHECK( aRequest.m_Body.Contains(
                        wxS( "describe direct current-board edits and normal "
                             "KiCad undo" ) ) );
                BOOST_CHECK( aRequest.m_Body.Contains(
                        wxS( "do not mention internal staging surfaces, "
                             "preview approval, or Accept buttons" ) ) );
                BOOST_CHECK( aRequest.m_Body.Contains(
                        wxS( "Use kisurf_query_board_summary for board counts" ) ) );
                BOOST_CHECK( aRequest.m_Body.Contains(
                        wxS( "Do not estimate board item counts" ) ) );
                BOOST_CHECK( aRequest.m_Body.Contains(
                        wxS( "script or atomic operation tool path" ) ) );
                BOOST_CHECK( !aRequest.m_Body.Contains(
                        wxS( "session, script, or atomic operation tool path" ) ) );
                BOOST_CHECK( !aRequest.m_Body.Contains( wxS( "session_only" ) ) );
                BOOST_CHECK( !aRequest.m_Body.Contains( wxS( "session handles" ) ) );
                BOOST_CHECK( !aRequest.m_Body.Contains( wxS( "shadow-board" ) ) );
                BOOST_CHECK( !aRequest.m_Body.Contains( wxS( "preview gate" ) ) );
                BOOST_CHECK( !aRequest.m_Body.Contains( wxS( "preview-first" ) ) );

                aResponse.m_StatusCode = 200;
                aResponse.m_Body =
                        wxS( "{\"choices\":[{\"message\":{\"content\":\"ok\"}}]}" );
                return true;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 16;
    request.m_UserText = wxS( "check drc" );

    AI_PROVIDER_RESPONSE response = provider.Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 16 );
    BOOST_CHECK_EQUAL( response.m_Body, wxString( wxS( "ok" ) ) );
}


BOOST_AUTO_TEST_CASE( OpenAiProviderHonorsRequiredToolCallRequest )
{
    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxS( "https://sub2api.wenming-dev.org/v1" );
    settings.m_ApiKey = wxS( "unit-test-key" );
    settings.m_Model = wxS( "unit-model" );

    bool handlerCalled = false;

    AI_OPENAI_COMPAT_PROVIDER provider(
            settings,
            [&handlerCalled]( const AI_HTTP_REQUEST& aRequest,
                              AI_HTTP_RESPONSE& aResponse, wxString& aError )
            {
                wxUnusedVar( aError );
                handlerCalled = true;

                nlohmann::json body = nlohmann::json::parse(
                        aRequest.m_Body.ToStdString() );

                BOOST_REQUIRE( body.contains( "tools" ) );
                BOOST_REQUIRE( body["tools"].is_array() );
                BOOST_REQUIRE( !body["tools"].empty() );
                BOOST_REQUIRE( body.contains( "tool_choice" ) );
                BOOST_CHECK_EQUAL( body["tool_choice"].get<std::string>(),
                                   "required" );

                aResponse.m_StatusCode = 200;
                aResponse.m_Body =
                        wxS( "{\"choices\":[{\"message\":{\"content\":\"ok\"}}]}" );
                return true;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 17;
    request.m_UserText = wxS( "删除所有布线" );
    request.m_RequireToolCall = true;

    AI_PROVIDER_RESPONSE response = provider.Generate( request );

    BOOST_CHECK( handlerCalled );
    BOOST_CHECK_EQUAL( response.m_RequestId, 17 );
}


BOOST_AUTO_TEST_CASE( OpenAiProviderDoesNotCallNetworkWithoutKey )
{
    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxS( "https://sub2api.wenming-dev.org/v1" );
    settings.m_Model = wxS( "unit-model" );

    AI_OPENAI_COMPAT_PROVIDER provider(
            settings,
            []( const AI_HTTP_REQUEST&, AI_HTTP_RESPONSE&, wxString& )
            {
                BOOST_FAIL( "network should not be called without an API key" );
                return false;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 13;
    request.m_UserText = wxS( "hello" );

    AI_PROVIDER_RESPONSE response = provider.Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 13 );
    BOOST_CHECK( response.m_Body.Contains( wxS( "Model Settings" ) ) );
}


BOOST_AUTO_TEST_CASE( OpenAiProviderIncludesHttpErrorBodyExcerpt )
{
    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxS( "https://sub2api.wenming-dev.org/v1" );
    settings.m_ApiKey = wxS( "unit-test-key" );
    settings.m_Model = wxS( "unit-model" );

    AI_OPENAI_COMPAT_PROVIDER provider(
            settings,
            []( const AI_HTTP_REQUEST&, AI_HTTP_RESPONSE& aResponse, wxString& aError )
            {
                wxUnusedVar( aError );
                aResponse.m_StatusCode = 502;
                aResponse.m_Body =
                        wxS( "{\"error\":{\"message\":\"upstream model overloaded\"}}" );
                return true;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 14;
    request.m_UserText = wxS( "hello" );

    AI_PROVIDER_RESPONSE response = provider.Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 14 );
    BOOST_CHECK( response.m_Body.Contains( wxS( "HTTP 502" ) ) );
    BOOST_CHECK( response.m_Body.Contains( wxS( "upstream model overloaded" ) ) );
}


BOOST_AUTO_TEST_CASE( OpenAiProviderStreamsChatCompletionTextDeltas )
{
    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxS( "https://sub2api.wenming-dev.org/v1" );
    settings.m_ApiKey = wxS( "unit-test-key" );
    settings.m_Model = wxS( "unit-model" );

    bool streamHandlerInstalled = false;

    AI_OPENAI_COMPAT_PROVIDER provider(
            settings,
            [&]( const AI_HTTP_REQUEST& aRequest, AI_HTTP_RESPONSE& aResponse,
                 wxString& aError )
            {
                wxUnusedVar( aError );

                nlohmann::json body = nlohmann::json::parse( aRequest.m_Body.ToStdString() );
                BOOST_REQUIRE( body.contains( "stream" ) );
                BOOST_CHECK( body["stream"].get<bool>() );
                BOOST_REQUIRE( static_cast<bool>( aRequest.m_StreamHandler ) );
                streamHandlerInstalled = true;

                aRequest.m_StreamHandler(
                        "data: {\"choices\":[{\"delta\":{\"content\":\"Hel\"}}]}\n\n" );
                aRequest.m_StreamHandler(
                        "data: {\"choices\":[{\"delta\":{\"content\":\"lo\"}}]}\n\n" );
                aRequest.m_StreamHandler( "data: [DONE]\n\n" );

                aResponse.m_StatusCode = 200;
                return true;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 81;
    request.m_UserText = wxS( "say hello" );

    std::vector<wxString> deltas;
    AI_PROVIDER_RESPONSE response = provider.Generate(
            request,
            [&]( const AI_PROVIDER_STREAM_EVENT& aEvent )
            {
                if( !aEvent.m_TextDelta.IsEmpty() )
                    deltas.push_back( aEvent.m_TextDelta );
            } );

    BOOST_CHECK( streamHandlerInstalled );
    BOOST_REQUIRE_EQUAL( deltas.size(), 2 );
    BOOST_CHECK_EQUAL( deltas.at( 0 ), wxString( wxS( "Hel" ) ) );
    BOOST_CHECK_EQUAL( deltas.at( 1 ), wxString( wxS( "lo" ) ) );
    BOOST_CHECK_EQUAL( response.m_Body, wxString( wxS( "Hello" ) ) );
}


#ifdef _WIN32
BOOST_AUTO_TEST_CASE( OpenAiProviderDefaultHttpHandlerStreamsFromLoopbackSse )
{
    LOOPBACK_SSE_SERVER server;
    BOOST_REQUIRE( server.Start(
            "data: {\"choices\":[{\"delta\":{\"content\":\"Lo\"}}]}\n\n"
            "data: {\"choices\":[{\"delta\":{\"content\":\"op\"}}]}\n\n"
            "data: [DONE]\n\n" ) );

    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxString::Format( wxS( "http://127.0.0.1:%u/v1" ),
                                           server.Port() );
    settings.m_ApiKey = wxS( "unit-test-key" );
    settings.m_Model = wxS( "unit-model" );

    AI_OPENAI_COMPAT_PROVIDER provider( settings );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 83;
    request.m_UserText = wxS( "stream from the real default HTTP handler" );

    std::vector<wxString> deltas;
    AI_PROVIDER_RESPONSE response = provider.Generate(
            request,
            [&]( const AI_PROVIDER_STREAM_EVENT& aEvent )
            {
                if( !aEvent.m_TextDelta.IsEmpty() )
                    deltas.push_back( aEvent.m_TextDelta );
            } );

    server.Join();

    BOOST_REQUIRE_EQUAL( deltas.size(), 2 );
    BOOST_CHECK_EQUAL( deltas.at( 0 ), wxString( wxS( "Lo" ) ) );
    BOOST_CHECK_EQUAL( deltas.at( 1 ), wxString( wxS( "op" ) ) );
    BOOST_CHECK_EQUAL( response.m_Body, wxString( wxS( "Loop" ) ) );

    const std::string& rawRequest = server.RequestText();
    BOOST_CHECK( rawRequest.find( "POST /v1/chat/completions" )
                 != std::string::npos );
    BOOST_CHECK( rawRequest.find( "\"stream\":true" ) != std::string::npos );
}
#endif


BOOST_AUTO_TEST_CASE( OpenAiProviderStreamsToolCallDeltas )
{
    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxS( "https://sub2api.wenming-dev.org/v1" );
    settings.m_ApiKey = wxS( "unit-test-key" );
    settings.m_Model = wxS( "unit-model" );

    AI_OPENAI_COMPAT_PROVIDER provider(
            settings,
            []( const AI_HTTP_REQUEST& aRequest, AI_HTTP_RESPONSE& aResponse,
                wxString& aError )
            {
                wxUnusedVar( aError );
                BOOST_REQUIRE( static_cast<bool>( aRequest.m_StreamHandler ) );

                aRequest.m_StreamHandler(
                        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{"
                        "\"index\":0,\"id\":\"call_1\",\"type\":\"function\","
                        "\"function\":{\"name\":\"kisurf_run_action\","
                        "\"arguments\":\"{\\\"action\\\":\\\"\"}}]}}]}\n\n" );
                aRequest.m_StreamHandler(
                        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{"
                        "\"index\":0,\"function\":{\"arguments\":"
                        "\"pcbnew.InteractiveSelectionTool.selectionClear\\\"}\"}}]}}]}\n\n" );
                aRequest.m_StreamHandler( "data: [DONE]\n\n" );

                aResponse.m_StatusCode = 200;
                return true;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 82;
    request.m_UserText = wxS( "clear selection" );

    AI_PROVIDER_RESPONSE response = provider.Generate(
            request,
            []( const AI_PROVIDER_STREAM_EVENT& )
            {
            } );

    BOOST_CHECK_EQUAL( response.m_Body,
                       wxString( wxS( "Tool call requested." ) ) );
    BOOST_REQUIRE_EQUAL( response.m_ToolCalls.size(), 1 );
    BOOST_CHECK_EQUAL( response.m_ToolCalls.front().m_ToolCallId,
                       wxString( wxS( "call_1" ) ) );
    BOOST_CHECK_EQUAL( response.m_ToolCalls.front().m_ToolName,
                       wxString( wxS( "kisurf_run_action" ) ) );
    BOOST_CHECK_EQUAL(
            response.m_ToolCalls.front().m_ArgumentsJson,
            wxString( wxS( "{\"action\":\"pcbnew.InteractiveSelectionTool.selectionClear\"}" ) ) );
}


BOOST_AUTO_TEST_CASE( OpenAiProviderRetriesContextLimitWithShrunkContext )
{
    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxS( "https://sub2api.wenming-dev.org/v1" );
    settings.m_ApiKey = wxS( "unit-test-key" );
    settings.m_Model = wxS( "unit-model" );

    std::vector<size_t> requestSizes;
    std::vector<bool>   requestHasImage;
    std::vector<std::string> requestBodies;
    int                 callCount = 0;

    AI_OPENAI_COMPAT_PROVIDER provider(
            settings,
            [&]( const AI_HTTP_REQUEST& aRequest, AI_HTTP_RESPONSE& aResponse, wxString& aError )
            {
                wxUnusedVar( aError );
                ++callCount;

                nlohmann::json body = nlohmann::json::parse( aRequest.m_Body.ToStdString() );
                requestSizes.push_back( aRequest.m_Body.length() );
                requestBodies.push_back( aRequest.m_Body.ToStdString() );
                requestHasImage.push_back(
                        body["messages"].at( 1 )["content"].is_array() );

                if( callCount == 1 )
                {
                    BOOST_CHECK( aRequest.m_Body.Contains( wxS( "RAW_PAYLOAD_NEEDLE" ) ) );
                    aResponse.m_StatusCode = 400;
                    aResponse.m_Body =
                            wxS( "{\"error\":{\"code\":\"context_length_exceeded\","
                                 "\"message\":\"maximum context length exceeded\"}}" );
                    return true;
                }

                BOOST_CHECK( !aRequest.m_Body.Contains( wxS( "RAW_PAYLOAD_NEEDLE" ) ) );
                aResponse.m_StatusCode = 200;
                aResponse.m_Body =
                        wxS( "{\"choices\":[{\"message\":{\"content\":\"shrunk ok\"}}]}" );
                return true;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 71;
    request.m_UserText = wxS( "summarize after long tool result" );
    request.m_MaxProviderInputChars = 24000;
    request.m_MaxToolResultChars = 4096;
    request.m_ContextSnapshot.m_Visual.m_Source = wxS( "canvas" );
    request.m_ContextSnapshot.m_Visual.m_MimeType = wxS( "image/png" );
    request.m_ContextSnapshot.m_Visual.m_DataUri = wxS( "data:image/png;base64,dW5pdA==" );
    request.m_ContextSnapshot.m_Visual.m_WidthPx = 8;
    request.m_ContextSnapshot.m_Visual.m_HeightPx = 8;
    request.m_ContextSnapshot.m_Visual.m_ByteSize = 32;

    AI_TOOL_CALL_RECORD result;
    result.m_RequestId = 71;
    result.m_ToolCallId = wxS( "call_large" );
    result.m_ToolName = wxS( "kisurf_run_cell" );

    wxString rawPayload;
    for( int i = 0; i < 3000; ++i )
        rawPayload << wxS( "x" );

    result.m_ResultJson = wxS( "{\"stdout\":\"" ) + rawPayload
                          + wxS( "RAW_PAYLOAD_NEEDLE\"}" );
    request.m_ToolResults.push_back( result );

    AI_PROVIDER_RESPONSE response = provider.Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 71 );
    BOOST_CHECK_EQUAL( response.m_Body, wxString( wxS( "shrunk ok" ) ) );
    BOOST_REQUIRE_EQUAL( callCount, 2 );
    BOOST_REQUIRE_EQUAL( requestSizes.size(), 2 );
    BOOST_REQUIRE_EQUAL( requestBodies.size(), 2 );
    BOOST_CHECK_LT( requestSizes.at( 1 ), requestSizes.at( 0 ) );
    BOOST_CHECK_EQUAL( countSubstring( requestBodies.at( 1 ), "User request:" ), 1 );
    BOOST_CHECK_EQUAL( countSubstring( requestBodies.at( 1 ),
                                       "summarize after long tool result" ),
                       1 );
    BOOST_REQUIRE_EQUAL( requestHasImage.size(), 2 );
    BOOST_CHECK( requestHasImage.at( 0 ) );
    BOOST_CHECK( !requestHasImage.at( 1 ) );
    BOOST_CHECK( response.m_ProviderTraceJson.Contains( wxS( "retry_history" ) ) );
    BOOST_CHECK( response.m_ProviderTraceJson.Contains( wxS( "context_limit" ) ) );
    BOOST_CHECK( response.m_ProviderTraceJson.Contains( wxS( "shrunk_retry" ) ) );
}


BOOST_AUTO_TEST_CASE( OpenAiProviderRetriesLarge502WithShrunkContext )
{
    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxS( "https://sub2api.wenming-dev.org/v1" );
    settings.m_ApiKey = wxS( "unit-test-key" );
    settings.m_Model = wxS( "unit-model" );

    int                 callCount = 0;
    std::vector<size_t> requestSizes;

    AI_OPENAI_COMPAT_PROVIDER provider(
            settings,
            [&]( const AI_HTTP_REQUEST& aRequest, AI_HTTP_RESPONSE& aResponse, wxString& aError )
            {
                wxUnusedVar( aError );
                ++callCount;
                requestSizes.push_back( aRequest.m_Body.length() );

                if( callCount == 1 )
                {
                    aResponse.m_StatusCode = 502;
                    aResponse.m_Body = wxS( "{\"error\":{\"message\":\"bad gateway\"}}" );
                    return true;
                }

                aResponse.m_StatusCode = 200;
                aResponse.m_Body =
                        wxS( "{\"choices\":[{\"message\":{\"content\":\"retried small\"}}]}" );
                return true;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 72;
    request.m_UserText = wxS( "large request that may overflow upstream proxy" );
    request.m_MaxProviderInputChars = 50000;

    for( int i = 0; i < 80; ++i )
    {
        AI_ACTIVITY_RECORD activity;
        activity.m_Sequence = static_cast<uint64_t>( i + 1 );
        activity.m_ActionName = wxString::Format( wxS( "activity-%02d" ), i );
        wxString payload = wxS( "long activity payload " );

        for( int j = 0; j < 200; ++j )
            payload << wxS( "z" );

        activity.m_Message = payload;
        request.m_ContextSnapshot.m_RecentActivity.push_back( activity );
    }

    AI_PROVIDER_RESPONSE response = provider.Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 72 );
    BOOST_CHECK_EQUAL( response.m_Body, wxString( wxS( "retried small" ) ) );
    BOOST_REQUIRE_EQUAL( callCount, 2 );
    BOOST_REQUIRE_EQUAL( requestSizes.size(), 2 );
    BOOST_CHECK_LT( requestSizes.at( 1 ), requestSizes.at( 0 ) );
    BOOST_CHECK( response.m_ProviderTraceJson.Contains( wxS( "retry_history" ) ) );
    BOOST_CHECK( response.m_ProviderTraceJson.Contains( wxS( "transient_gateway" ) ) );
    BOOST_CHECK( response.m_ProviderTraceJson.Contains( wxS( "shrunk_retry" ) ) );
}


BOOST_AUTO_TEST_CASE( OpenAiProviderPreflightsLargeRequestWithDefaultBudget )
{
    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxS( "https://sub2api.wenming-dev.org/v1" );
    settings.m_ApiKey = wxS( "unit-test-key" );
    settings.m_Model = wxS( "unit-model" );

    int callCount = 0;

    AI_OPENAI_COMPAT_PROVIDER provider(
            settings,
            [&]( const AI_HTTP_REQUEST& aRequest, AI_HTTP_RESPONSE& aResponse, wxString& aError )
            {
                wxUnusedVar( aError );
                ++callCount;

                BOOST_CHECK( !aRequest.m_Body.Contains( wxS( "RAW_PREFLIGHT_NEEDLE" ) ) );
                BOOST_CHECK( aRequest.m_Body.Contains( wxS( "compressed_tool_result" ) ) );

                aResponse.m_StatusCode = 200;
                aResponse.m_Body =
                        wxS( "{\"choices\":[{\"message\":{\"content\":\"preflight ok\"}}]}" );
                return true;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 73;
    request.m_UserText = wxS( "summarize very large tool output" );
    request.m_MaxProviderInputChars = 50000;
    request.m_MaxToolResultChars = 20000;

    AI_TOOL_CALL_RECORD result;
    result.m_RequestId = 73;
    result.m_ToolCallId = wxS( "call_preflight_large" );
    result.m_ToolName = wxS( "kisurf_run_cell" );

    wxString rawPayload;
    for( int i = 0; i < 10000; ++i )
        rawPayload << wxS( "p" );

    result.m_ResultJson = wxS( "{\"stdout\":\"" ) + rawPayload
                          + wxS( "RAW_PREFLIGHT_NEEDLE\"}" );
    request.m_ToolResults.push_back( result );

    AI_PROVIDER_RESPONSE response = provider.Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 73 );
    BOOST_CHECK_EQUAL( response.m_Body, wxString( wxS( "preflight ok" ) ) );
    BOOST_CHECK_EQUAL( callCount, 1 );
    BOOST_CHECK( response.m_ProviderTraceJson.Contains( wxS( "preflight_budget" ) ) );
    BOOST_CHECK( response.m_ProviderTraceJson.Contains( wxS( "tool_result_budget" ) ) );
}


BOOST_AUTO_TEST_CASE( OpenAiProviderUsesNextActionPreflightBudget )
{
    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxS( "https://sub2api.wenming-dev.org/v1" );
    settings.m_ApiKey = wxS( "unit-test-key" );
    settings.m_Model = wxS( "unit-model" );

    int callCount = 0;

    AI_OPENAI_COMPAT_PROVIDER provider(
            settings,
            [&]( const AI_HTTP_REQUEST& aRequest, AI_HTTP_RESPONSE& aResponse, wxString& aError )
            {
                wxUnusedVar( aError );
                ++callCount;

                nlohmann::json body = nlohmann::json::parse( aRequest.m_Body.ToStdString() );
                const std::string content =
                        body["messages"].at( 1 )["content"].get<std::string>();
                const std::string bodyText = aRequest.m_Body.ToStdString();

                BOOST_CHECK( content.find( "Editor context is available on demand through tools" )
                             != std::string::npos );
                BOOST_CHECK( content.find( "kisurf_get_workspace_view" )
                             != std::string::npos );
                BOOST_CHECK( content.find( "activity-39" ) == std::string::npos );
                BOOST_CHECK( content.find( "activity-00" ) == std::string::npos );
                BOOST_CHECK( bodyText.find( "NEXT_ACTION_RAW_TOOL_RESULT_NEEDLE" )
                             == std::string::npos );
                BOOST_CHECK( bodyText.find( "compressed_tool_result" )
                             != std::string::npos );

                aResponse.m_StatusCode = 200;
                aResponse.m_Body =
                        wxS( "{\"choices\":[{\"message\":{\"content\":\"next action ok\"}}]}" );
                return true;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 74;
    request.m_RequestKind = AI_PROVIDER_REQUEST_KIND::NextActionDecision;
    request.m_UserText = wxS( "next action decision with bounded episode input" );
    request.m_MaxProviderInputChars = 50000;
    request.m_MaxContextActivityRecords = 50;
    request.m_MaxToolResultChars = 20000;
    request.m_MaxRetrievedMemoryChars = 20000;

    for( int i = 0; i < 40; ++i )
    {
        AI_ACTIVITY_RECORD activity;
        activity.m_Sequence = static_cast<uint64_t>( i + 1 );
        activity.m_ActionName = wxString::Format( wxS( "activity-%02d" ), i );
        activity.m_Message = wxS( "bounded next action episode activity" );
        request.m_ContextSnapshot.m_RecentActivity.push_back( activity );
    }

    AI_TOOL_CALL_RECORD result;
    result.m_RequestId = 74;
    result.m_ToolCallId = wxS( "call_next_action_large" );
    result.m_ToolName = wxS( "observation.read" );

    wxString rawPayload;
    for( int i = 0; i < 25000; ++i )
        rawPayload << wxS( "n" );

    result.m_ResultJson = wxS( "{\"observation\":\"" ) + rawPayload
                          + wxS( "NEXT_ACTION_RAW_TOOL_RESULT_NEEDLE\"}" );
    request.m_ToolResults.push_back( result );

    AI_PROVIDER_RESPONSE response = provider.Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 74 );
    BOOST_CHECK_EQUAL( response.m_Body, wxString( wxS( "next action ok" ) ) );
    BOOST_CHECK_EQUAL( callCount, 1 );
    BOOST_CHECK( response.m_ProviderTraceJson.Contains( wxS( "preflight_budget" ) ) );
    BOOST_CHECK( response.m_ProviderTraceJson.Contains( wxS( "tool_result_budget" ) ) );
    BOOST_CHECK( response.m_ProviderTraceJson.Contains( wxS( "activity_record_budget" ) ) );
}


BOOST_AUTO_TEST_CASE( OpenAiProviderSendsVisualSnapshotAsImageUrlContent )
{
    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxS( "https://sub2api.wenming-dev.org/v1" );
    settings.m_ApiKey = wxS( "unit-test-key" );
    settings.m_Model = wxS( "unit-model" );

    const wxString dataUri = wxS( "data:image/png;base64,dW5pdA==" );

    AI_OPENAI_COMPAT_PROVIDER provider(
            settings,
            [&]( const AI_HTTP_REQUEST& aRequest, AI_HTTP_RESPONSE& aResponse, wxString& aError )
            {
                wxUnusedVar( aError );

                nlohmann::json body = nlohmann::json::parse( aRequest.m_Body.ToStdString() );
                const nlohmann::json& content = body["messages"].at( 1 )["content"];

                BOOST_REQUIRE( content.is_array() );
                BOOST_REQUIRE_EQUAL( content.size(), 2 );
                BOOST_CHECK_EQUAL( content.at( 0 )["type"].get<std::string>(), "text" );
                BOOST_CHECK( content.at( 0 )["text"].get<std::string>().find(
                                     "Editor context is available on demand through tools" )
                             != std::string::npos );
                BOOST_CHECK_EQUAL( content.at( 1 )["type"].get<std::string>(), "image_url" );
                BOOST_CHECK_EQUAL( content.at( 1 )["image_url"]["url"].get<std::string>(),
                                   dataUri.ToStdString() );

                aResponse.m_StatusCode = 200;
                aResponse.m_Body = wxS( "{\"choices\":[{\"message\":{\"content\":\"saw image\"}}]}" );
                return true;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 31;
    request.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_UserText = wxS( "inspect visible routing" );
    request.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_ContextSnapshot.m_Visual.m_Source = wxS( "test.image" );
    request.m_ContextSnapshot.m_Visual.m_MimeType = wxS( "image/png" );
    request.m_ContextSnapshot.m_Visual.m_DataUri = dataUri;
    request.m_ContextSnapshot.m_Visual.m_WidthPx = 4;
    request.m_ContextSnapshot.m_Visual.m_HeightPx = 2;
    request.m_ContextSnapshot.m_Visual.m_ByteSize = 8;

    AI_PROVIDER_RESPONSE response = provider.Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 31 );
    BOOST_CHECK_EQUAL( response.m_Body, wxString( wxS( "saw image" ) ) );
}


BOOST_AUTO_TEST_CASE( OpenAiProviderKeepsStringContentWithoutVisualPixels )
{
    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxS( "https://sub2api.wenming-dev.org/v1" );
    settings.m_ApiKey = wxS( "unit-test-key" );
    settings.m_Model = wxS( "unit-model" );

    AI_OPENAI_COMPAT_PROVIDER provider(
            settings,
            []( const AI_HTTP_REQUEST& aRequest, AI_HTTP_RESPONSE& aResponse, wxString& aError )
            {
                wxUnusedVar( aError );

                nlohmann::json body = nlohmann::json::parse( aRequest.m_Body.ToStdString() );
                BOOST_REQUIRE( body["messages"].at( 1 )["content"].is_string() );

                aResponse.m_StatusCode = 200;
                aResponse.m_Body = wxS( "{\"choices\":[{\"message\":{\"content\":\"text only\"}}]}" );
                return true;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 32;
    request.m_UserText = wxS( "inspect text context" );

    AI_PROVIDER_RESPONSE response = provider.Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 32 );
    BOOST_CHECK_EQUAL( response.m_Body, wxString( wxS( "text only" ) ) );
}


BOOST_AUTO_TEST_CASE( ProviderInputCompilerKeepsRecentActivityAndTracesOmissions )
{
    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 81;
    request.m_UserText = wxS( "inspect latest state" );
    request.m_MaxProviderInputChars = 1800;
    request.m_MaxContextActivityRecords = 6;
    request.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_ContextSnapshot.m_Version.m_DocumentRevision = 33;

    for( int i = 0; i < 40; ++i )
    {
        AI_ACTIVITY_RECORD activity;
        activity.m_Sequence = static_cast<uint64_t>( i + 1 );
        activity.m_ActionName = wxString::Format( wxS( "activity-%02d" ), i );
        activity.m_Message = wxString::Format( wxS( "activity-message-%02d" ), i );
        activity.m_Executed = true;
        request.m_ContextSnapshot.m_RecentActivity.push_back( activity );
    }

    AI_PROVIDER_REQUEST compiled = AiCompileProviderInput( request );

    BOOST_CHECK( compiled.m_ContextCompiled );
    BOOST_CHECK( compiled.m_ProviderInputWasShrunk );
    BOOST_CHECK( compiled.m_CompiledUserMessageText.Contains(
            wxS( "Editor context is available on demand through tools" ) ) );
    BOOST_CHECK( !compiled.m_CompiledUserMessageText.Contains( wxS( "activity-39" ) ) );
    BOOST_CHECK( !compiled.m_CompiledUserMessageText.Contains( wxS( "activity-00" ) ) );
    BOOST_CHECK( compiled.m_PromptTraceJson.Contains( wxS( "recent_activity" ) ) );
    BOOST_CHECK( compiled.m_PromptTraceJson.Contains( wxS( "omitted" ) ) );
}


BOOST_AUTO_TEST_CASE( ProviderMessageCompilerIncludesOmittedInputSummary )
{
    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 88;
    request.m_UserText = wxS( "inspect bounded context" );
    request.m_MaxContextActivityRecords = 2;
    request.m_AllowVisualPixels = false;
    request.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_ContextSnapshot.m_Visual.m_Source = wxS( "canvas.viewport" );
    request.m_ContextSnapshot.m_Visual.m_MimeType = wxS( "image/png" );
    request.m_ContextSnapshot.m_Visual.m_DataUri =
            wxS( "data:image/png;base64,dW5pdA==" );
    request.m_ContextSnapshot.m_Visual.m_FrameId = wxS( "viewport-1" );
    request.m_ContextSnapshot.m_Visual.m_FrameKind = wxS( "viewport_raw" );
    request.m_ContextSnapshot.m_Visual.m_WidthPx = 12;
    request.m_ContextSnapshot.m_Visual.m_HeightPx = 8;
    request.m_ContextSnapshot.m_Visual.m_ByteSize = 48;

    for( int i = 0; i < 6; ++i )
    {
        AI_ACTIVITY_RECORD activity;
        activity.m_Sequence = static_cast<uint64_t>( i + 1 );
        activity.m_ActionName = wxString::Format( wxS( "activity-%02d" ), i );
        activity.m_Message = wxString::Format( wxS( "activity-message-%02d" ), i );
        request.m_ContextSnapshot.m_RecentActivity.push_back( activity );
    }

    wxString messagesJson = AiCompileProviderMessagesJson(
            request, wxS( "Default system prompt." ) );
    nlohmann::json messages = nlohmann::json::parse( messagesJson.ToStdString() );

    BOOST_REQUIRE_EQUAL( messages.size(), 2 );
    BOOST_CHECK_EQUAL( messages.at( 1 )["role"].get<std::string>(), "user" );

    const std::string userContent =
            messages.at( 1 )["content"].is_string()
                    ? messages.at( 1 )["content"].get<std::string>()
                    : messages.at( 1 )["content"].dump();

    BOOST_CHECK( userContent.find( "Omitted provider input blocks" )
                 != std::string::npos );
    BOOST_CHECK( userContent.find( "editor.recent_activity" )
                 != std::string::npos );
    BOOST_CHECK( userContent.find( "older_activity_omitted" )
                 != std::string::npos );
    BOOST_CHECK( userContent.find( "visual.frame.pixels" )
                 != std::string::npos );
    BOOST_CHECK( userContent.find( "visual_pixels_disabled" )
                 != std::string::npos );
}


BOOST_AUTO_TEST_CASE( ProviderMessageCompilerIncludesResponseContractAndToolCatalogBlocks )
{
    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 89;
    request.m_UserText = wxS( "review next action contract" );
    request.m_MaxProviderInputChars = 8000;
    request.m_ResponseFormatJson =
            wxS( "{\"type\":\"json_schema\",\"json_schema\":{\"name\":\"next_action\","
                 "\"schema\":{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\"}},"
                 "\"required\":[\"action\"]}}}" );
    request.m_ToolCatalogJson =
            wxS( "[{\"type\":\"function\",\"function\":{\"name\":\"kisurf_test_tool\","
                 "\"description\":\"Test tool\",\"parameters\":{\"type\":\"object\"}}}]" );

    wxString messagesJson = AiCompileProviderMessagesJson(
            request, wxS( "Default system prompt." ) );
    nlohmann::json messages = nlohmann::json::parse( messagesJson.ToStdString() );

    BOOST_REQUIRE_EQUAL( messages.size(), 2 );
    BOOST_CHECK_EQUAL( messages.at( 1 )["role"].get<std::string>(), "user" );

    const std::string userContent =
            messages.at( 1 )["content"].is_string()
                    ? messages.at( 1 )["content"].get<std::string>()
                    : messages.at( 1 )["content"].dump();

    BOOST_CHECK( userContent.find( "Provider response contract" )
                 != std::string::npos );
    BOOST_CHECK( userContent.find( "json_schema" ) != std::string::npos );
    BOOST_CHECK( userContent.find( "\"action\"" ) != std::string::npos );
    BOOST_CHECK( userContent.find( "Provider callable tool catalog" )
                 != std::string::npos );
    BOOST_CHECK( userContent.find( "kisurf_test_tool" ) != std::string::npos );

    AI_PROVIDER_REQUEST compiled = AiCompileProviderInput( request );
    BOOST_CHECK( compiled.m_PromptTraceJson.Contains(
            wxS( "provider.response_format" ) ) );
    BOOST_CHECK( compiled.m_PromptTraceJson.Contains(
            wxS( "provider.tool_catalog" ) ) );
    BOOST_CHECK( compiled.m_PromptTraceJson.Contains( wxS( "response_schema" ) ) );
    BOOST_CHECK( compiled.m_PromptTraceJson.Contains( wxS( "tool_catalog" ) ) );
}


BOOST_AUTO_TEST_CASE( ProviderInputCompilerKeepsFixedContextBeforeLongChatHistory )
{
    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 891;
    request.m_UserText = wxS( "continue the PCB task" );
    request.m_MaxProviderInputChars = 1500;
    request.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_ContextSnapshot.m_Summary =
            wxS( "FIXED_CONTEXT_NEEDLE active PCB board summary" );
    request.m_ToolCatalogJson =
            wxS( "[{\"type\":\"function\",\"function\":{\"name\":\"FIXED_TOOL_NEEDLE\","
                 "\"description\":\"fixed callable tool\",\"parameters\":{\"type\":\"object\"}}}]" );

    AI_PROVIDER_INPUT_BLOCK chatHistory;
    chatHistory.m_Id = wxS( "chat.recent_turns" );
    chatHistory.m_Kind = wxS( "chat_recent_turns" );
    chatHistory.m_Source = wxS( "chat_session" );
    chatHistory.m_Text = wxS( "Previous chat turns:\nassistant: " );

    for( int i = 0; i < 4000; ++i )
        chatHistory.m_Text << wxS( "x" );

    chatHistory.m_Text << wxS( " OLD_CHAT_TAIL_SHOULD_BE_TRUNCATED" );
    request.m_ProviderInputBlocks.push_back( chatHistory );

    AI_PROVIDER_REQUEST compiled = AiCompileProviderInput( request );

    BOOST_CHECK( compiled.m_CompiledUserMessageText.Contains(
            wxS( "Editor context is available on demand through tools" ) ) );
    BOOST_CHECK( !compiled.m_CompiledUserMessageText.Contains(
            wxS( "FIXED_CONTEXT_NEEDLE" ) ) );
    BOOST_CHECK( compiled.m_CompiledUserMessageText.Contains(
            wxS( "FIXED_TOOL_NEEDLE" ) ) );
    BOOST_CHECK( compiled.m_PromptTraceJson.Contains(
            wxS( "chat.recent_turns" ) ) );
    BOOST_CHECK( compiled.m_PromptTraceJson.Contains(
            wxS( "truncated_budget" ) )
                 || compiled.m_PromptTraceJson.Contains(
                            wxS( "omitted_budget" ) ) );
    BOOST_CHECK( !compiled.m_CompiledUserMessageText.Contains(
            wxS( "OLD_CHAT_TAIL_SHOULD_BE_TRUNCATED" ) ) );
}


BOOST_AUTO_TEST_CASE( ProviderMessageCompilerSummarizesLargeToolCatalogBlock )
{
    nlohmann::json tools = nlohmann::json::array();

    for( int i = 0; i < 32; ++i )
    {
        tools.push_back( {
            { "type", "function" },
            { "function",
              {
                  { "name", "kisurf_large_tool_" + std::to_string( i ) },
                  { "description",
                    "RAW_VERBOSE_TOOL_DESCRIPTION_NEEDLE_" + std::to_string( i )
                            + std::string( 160, 'x' ) },
                  { "parameters",
                    {
                        { "type", "object" },
                        { "properties",
                          {
                              { "argument",
                                {
                                    { "type", "string" },
                                    { "description",
                                      "RAW_VERBOSE_SCHEMA_NEEDLE_" + std::to_string( i )
                                              + std::string( 160, 'y' ) }
                                } }
                          } }
                    } }
              } } } );
    }

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 90;
    request.m_UserText = wxS( "review compact tool catalog" );
    request.m_MaxProviderInputChars = 24000;
    request.m_ToolCatalogJson = wxString::FromUTF8( tools.dump().c_str() );

    wxString messagesJson = AiCompileProviderMessagesJson(
            request, wxS( "Default system prompt." ) );
    nlohmann::json messages = nlohmann::json::parse( messagesJson.ToStdString() );

    BOOST_REQUIRE_EQUAL( messages.size(), 2 );

    const std::string userContent =
            messages.at( 1 )["content"].is_string()
                    ? messages.at( 1 )["content"].get<std::string>()
                    : messages.at( 1 )["content"].dump();

    BOOST_CHECK( userContent.find( "Provider callable tool catalog summary" )
                 != std::string::npos );
    BOOST_CHECK( userContent.find( "summarized_tool_catalog" )
                 != std::string::npos );
    BOOST_CHECK( userContent.find( "\"tool_count\":32" ) != std::string::npos );
    BOOST_CHECK( userContent.find( "kisurf_large_tool_31" )
                 != std::string::npos );
    BOOST_CHECK( userContent.find( "hash" ) != std::string::npos );
    BOOST_CHECK( userContent.find( "RAW_VERBOSE_TOOL_DESCRIPTION_NEEDLE_31" )
                 == std::string::npos );
    BOOST_CHECK( userContent.find( "RAW_VERBOSE_SCHEMA_NEEDLE_31" )
                 == std::string::npos );

    AI_PROVIDER_REQUEST compiled = AiCompileProviderInput( request );
    BOOST_CHECK_EQUAL( compiled.m_ToolCatalogJson, request.m_ToolCatalogJson );
    BOOST_CHECK( compiled.m_PromptTraceJson.Contains( wxS( "provider.tool_catalog" ) ) );
    BOOST_CHECK( compiled.m_PromptTraceJson.Contains( wxS( "summarized" ) ) );
}


BOOST_AUTO_TEST_CASE( ProviderMessageCompilerSummarizesLargeResponseSchemaBlock )
{
    nlohmann::json properties = nlohmann::json::object();
    nlohmann::json required = nlohmann::json::array();

    for( int i = 0; i < 32; ++i )
    {
        const std::string name = "field_" + std::to_string( i );
        properties[name] = {
            { "type", "string" },
            { "description",
              "RAW_VERBOSE_RESPONSE_SCHEMA_NEEDLE_" + std::to_string( i )
                      + std::string( 180, 'z' ) }
        };
        required.push_back( name );
    }

    nlohmann::json responseFormat = {
        { "type", "json_schema" },
        { "json_schema",
          {
              { "name", "large_next_action_review" },
              { "schema",
                {
                    { "type", "object" },
                    { "properties", properties },
                    { "required", required },
                    { "additionalProperties", false }
                } }
          } }
    };

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 91;
    request.m_UserText = wxS( "review compact response schema" );
    request.m_MaxProviderInputChars = 24000;
    request.m_ResponseFormatJson =
            wxString::FromUTF8( responseFormat.dump().c_str() );

    wxString messagesJson = AiCompileProviderMessagesJson(
            request, wxS( "Default system prompt." ) );
    nlohmann::json messages = nlohmann::json::parse( messagesJson.ToStdString() );

    BOOST_REQUIRE_EQUAL( messages.size(), 2 );

    const std::string userContent =
            messages.at( 1 )["content"].is_string()
                    ? messages.at( 1 )["content"].get<std::string>()
                    : messages.at( 1 )["content"].dump();

    BOOST_CHECK( userContent.find( "Provider response contract summary" )
                 != std::string::npos );
    BOOST_CHECK( userContent.find( "summarized_response_schema" )
                 != std::string::npos );
    BOOST_CHECK( userContent.find( "large_next_action_review" )
                 != std::string::npos );
    BOOST_CHECK( userContent.find( "\"property_count\":32" )
                 != std::string::npos );
    BOOST_CHECK( userContent.find( "field_31" ) != std::string::npos );
    BOOST_CHECK( userContent.find( "hash" ) != std::string::npos );
    BOOST_CHECK( userContent.find( "RAW_VERBOSE_RESPONSE_SCHEMA_NEEDLE_31" )
                 == std::string::npos );

    AI_PROVIDER_REQUEST compiled = AiCompileProviderInput( request );
    BOOST_CHECK_EQUAL( compiled.m_ResponseFormatJson, request.m_ResponseFormatJson );
    BOOST_CHECK( compiled.m_PromptTraceJson.Contains(
            wxS( "provider.response_format" ) ) );
    BOOST_CHECK( compiled.m_PromptTraceJson.Contains(
            wxS( "summarized_response_schema" ) ) );
}


BOOST_AUTO_TEST_CASE( ProviderInputCompilerCompressesLargeToolResults )
{
    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 82;
    request.m_UserText = wxS( "continue after tool" );
    request.m_MaxToolResultChars = 180;

    AI_TOOL_CALL_RECORD result;
    result.m_RequestId = 82;
    result.m_ToolCallId = wxS( "call_large" );
    result.m_ToolName = wxS( "generic_verbose_tool" );
    result.m_ArgumentsJson = wxS( "{\"mode\":\"large\"}" );
    wxString rawPayload;

    for( int i = 0; i < 4000; ++i )
        rawPayload << wxS( "x" );

    result.m_ResultJson = wxS( "{\"stdout\":\"" )
                          + rawPayload
                          + wxS( "RAW_PAYLOAD_NEEDLE\"}" );
    request.m_ToolResults.push_back( result );

    AI_PROVIDER_REQUEST compiled = AiCompileProviderInput( request );

    BOOST_REQUIRE_EQUAL( compiled.m_ToolResults.size(), 1 );
    BOOST_CHECK( compiled.m_ProviderInputWasShrunk );
    BOOST_CHECK_LE( compiled.m_ToolResults.front().m_ResultJson.length(),
                    request.m_MaxToolResultChars + 256 );
    BOOST_CHECK( compiled.m_ToolResults.front().m_ResultJson.Contains(
            wxS( "compressed_tool_result" ) ) );
    BOOST_CHECK( !compiled.m_ToolResults.front().m_ResultJson.Contains(
            wxS( "RAW_PAYLOAD_NEEDLE" ) ) );
    BOOST_CHECK( compiled.m_PromptTraceJson.Contains( wxS( "tool_result" ) ) );
}


BOOST_AUTO_TEST_CASE( ProviderInputCompilerPreservesValidationSummaryInLargeToolResults )
{
    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 83;
    request.m_UserText = wxS( "summarize validation" );
    request.m_MaxToolResultChars = 320;

    nlohmann::json issues = nlohmann::json::array();

    for( int i = 0; i < 16; ++i )
    {
        issues.push_back( {
                { "severity", i == 0 ? "error" : "warning" },
                { "title", i == 0 ? "Clearance violation"
                                   : "Footprint not found in libraries" },
                { "message", "Synthetic validation issue with verbose details" },
                { "source", "pcbnew.drc_engine" } } );
    }

    nlohmann::json payload = {
        { "status", "validation_completed" },
        { "tool", "kisurf_run_validation" },
        { "validation",
          { { "issue_count", issues.size() },
            { "level", "full_drc" },
            { "status", "native_checked" },
            { "issues", issues } } } };

    wxString padding;

    for( int i = 0; i < 5000; ++i )
        padding << wxS( "x" );

    payload["padding"] = padding.ToStdString();

    AI_TOOL_CALL_RECORD result;
    result.m_RequestId = 83;
    result.m_ToolCallId = wxS( "call_validation" );
    result.m_ToolName = wxS( "kisurf_run_validation" );
    result.m_ArgumentsJson = wxS( "{\"level\":\"full_drc\"}" );
    result.m_ResultJson = wxString::FromUTF8( payload.dump().c_str() );
    request.m_ToolResults.push_back( result );

    AI_PROVIDER_REQUEST compiled = AiCompileProviderInput( request );

    BOOST_REQUIRE_EQUAL( compiled.m_ToolResults.size(), 1 );

    nlohmann::json compressed = nlohmann::json::parse(
            compiled.m_ToolResults.front().m_ResultJson.ToStdString() );

    BOOST_CHECK_EQUAL( compressed["status"].get<std::string>(),
                       "compressed_tool_result" );
    BOOST_REQUIRE( compressed.contains( "validation_summary" ) );
    BOOST_CHECK_EQUAL( compressed["validation_summary"]["issue_count"].get<int>(),
                       16 );
    BOOST_CHECK_EQUAL(
            compressed["validation_summary"]["severity_counts"]["error"].get<int>(),
            1 );
    BOOST_CHECK_EQUAL(
            compressed["validation_summary"]["severity_counts"]["warning"].get<int>(),
            15 );
    BOOST_CHECK_EQUAL(
            compressed["validation_summary"]["sample_issues"].at( 0 )["title"]
                    .get<std::string>(),
            "Clearance violation" );
}


BOOST_AUTO_TEST_CASE( ProviderInputCompilerPreservesQueryItemCountsInLargeToolResults )
{
    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 84;
    request.m_UserText = wxS( "count current board items" );
    request.m_MaxToolResultChars = 260;

    nlohmann::json items = nlohmann::json::array();

    for( int i = 0; i < 64; ++i )
    {
        items.push_back( {
                { "handle", "session:1:" + std::to_string( i + 1 ) },
                { "type", i < 40 ? "track_segment" : "via" },
                { "net", "GND" },
                { "layer", i < 40 ? "F.Cu" : "F.Cu/B.Cu" },
                { "geometry", { { "start", { { "x", i * 100 }, { "y", 0 } } },
                                { "end", { { "x", i * 100 + 50 }, { "y", 0 } } } } } } );
    }

    nlohmann::json payload = {
        { "status", "items" },
        { "total_count", 64 },
        { "returned_count", 64 },
        { "truncated", false },
        { "filter", { { "type", "track_segment" } } },
        { "items", items },
        { "board_mutated", false } };

    AI_TOOL_CALL_RECORD result;
    result.m_RequestId = 84;
    result.m_ToolCallId = wxS( "call_query_items" );
    result.m_ToolName = wxS( "kisurf_query_items" );
    result.m_ArgumentsJson = wxS( "{\"filter\":{\"type\":\"track_segment\"}}" );
    result.m_ResultJson = wxString::FromUTF8( payload.dump().c_str() );
    request.m_ToolResults.push_back( result );

    AI_PROVIDER_REQUEST compiled = AiCompileProviderInput( request );

    BOOST_REQUIRE_EQUAL( compiled.m_ToolResults.size(), 1 );
    nlohmann::json compressed = nlohmann::json::parse(
            compiled.m_ToolResults.front().m_ResultJson.ToStdString() );

    BOOST_CHECK_EQUAL( compressed["status"].get<std::string>(),
                       "compressed_tool_result" );
    BOOST_REQUIRE( compressed.contains( "semantic_summary" ) );
    BOOST_CHECK_EQUAL( compressed["semantic_summary"]["status"].get<std::string>(),
                       "items" );
    BOOST_CHECK_EQUAL( compressed["semantic_summary"]["total_count"].get<int>(), 64 );
    BOOST_CHECK_EQUAL( compressed["semantic_summary"]["returned_count"].get<int>(), 64 );
    BOOST_CHECK( !compressed["semantic_summary"]["truncated"].get<bool>() );
    BOOST_CHECK_EQUAL( compressed["semantic_summary"]["filter"]["type"].get<std::string>(),
                       "track_segment" );
}


BOOST_AUTO_TEST_CASE( ProviderInputCompilerAddsArtifactReferenceForLargeToolResults )
{
    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 86;
    request.m_UserText = wxS( "review long tool output" );
    request.m_MaxToolResultChars = 220;

    AI_TOOL_CALL_RECORD result;
    result.m_RequestId = 86;
    result.m_ToolCallId = wxS( "call_artifact" );
    result.m_ToolName = wxS( "generic_verbose_tool" );
    result.m_ArgumentsJson = wxS( "{\"mode\":\"long\"}" );

    wxString rawPayload;

    for( int i = 0; i < 6000; ++i )
        rawPayload << wxS( "a" );

    result.m_ResultJson = wxS( "{\"stdout\":\"" ) + rawPayload + wxS( "\"}" );
    request.m_ToolResults.push_back( result );

    AI_PROVIDER_REQUEST compiled = AiCompileProviderInput( request );

    BOOST_REQUIRE_EQUAL( compiled.m_ToolResults.size(), 1 );

    nlohmann::json compressed = nlohmann::json::parse(
            compiled.m_ToolResults.front().m_ResultJson.ToStdString() );

    BOOST_CHECK_EQUAL( compressed["status"].get<std::string>(),
                       "compressed_tool_result" );
    BOOST_REQUIRE( compressed.contains( "artifact_ref" ) );
    BOOST_CHECK_EQUAL( compressed["artifact_ref"]["kind"].get<std::string>(),
                       "tool_result" );
    BOOST_CHECK_EQUAL( compressed["artifact_ref"]["retention"].get<std::string>(),
                       "trace" );
    BOOST_CHECK( compressed["artifact_ref"]["uri"].get<std::string>().find(
                         "kisurf-artifact://tool-result/" )
                 == 0 );
    BOOST_CHECK_EQUAL( compressed["artifact_ref"]["tool_call_id"].get<std::string>(),
                       "call_artifact" );

    nlohmann::json promptTrace = nlohmann::json::parse(
            compiled.m_PromptTraceJson.ToStdString() );

    bool sawArtifactRef = false;

    for( const nlohmann::json& block : promptTrace["blocks"] )
    {
        if( block.value( "id", std::string() ) != "call_artifact" )
            continue;

        sawArtifactRef = block.contains( "metadata" )
                         && block["metadata"].contains( "artifact_ref" )
                         && block["metadata"]["artifact_ref"].value(
                                    "retention", std::string() )
                            == "trace";
    }

    BOOST_CHECK( sawArtifactRef );
}


BOOST_AUTO_TEST_CASE( ProviderInputCompilerUsesScriptOutputReferenceForLargeScriptResults )
{
    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 87;
    request.m_UserText = wxS( "review long script output" );
    request.m_MaxToolResultChars = 220;

    AI_TOOL_CALL_RECORD result;
    result.m_RequestId = 87;
    result.m_ToolCallId = wxS( "call_script_artifact" );
    result.m_ToolName = wxS( "kisurf_run_cell" );
    result.m_ArgumentsJson =
            wxS( "{\"cell_id\":\"long-script\",\"cell_text\":\"print('x')\"}" );

    wxString rawPayload;

    for( int i = 0; i < 6000; ++i )
        rawPayload << wxS( "s" );

    result.m_ResultJson = wxS( "{\"stdout\":\"" ) + rawPayload + wxS( "\"}" );
    request.m_ToolResults.push_back( result );

    AI_PROVIDER_REQUEST compiled = AiCompileProviderInput( request );

    BOOST_REQUIRE_EQUAL( compiled.m_ToolResults.size(), 1 );

    nlohmann::json compressed = nlohmann::json::parse(
            compiled.m_ToolResults.front().m_ResultJson.ToStdString() );

    BOOST_CHECK_EQUAL( compressed["status"].get<std::string>(),
                       "compressed_tool_result" );
    BOOST_REQUIRE( compressed.contains( "artifact_ref" ) );
    BOOST_CHECK_EQUAL( compressed["artifact_ref"]["kind"].get<std::string>(),
                       "script_output" );
    BOOST_CHECK( compressed["artifact_ref"]["uri"].get<std::string>().find(
                         "kisurf-artifact://script-output/" )
                 == 0 );
    BOOST_CHECK_EQUAL( compressed["artifact_ref"]["tool_call_id"].get<std::string>(),
                       "call_script_artifact" );
}


BOOST_AUTO_TEST_CASE( ProviderInputCompilerIncludesRetrievedLocalMemoryWithTrace )
{
    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 83;
    request.m_UserText = wxS( "route USB pair near connector" );
    request.m_MaxProviderInputChars = 3000;
    request.m_MaxRetrievedMemoryRecords = 4;
    request.m_MaxRetrievedMemoryChars = 1200;

    AI_PROVIDER_INPUT_BLOCK memory;
    memory.m_Id = wxS( "memory.rule.usb-clearance" );
    memory.m_Kind = wxS( "retrieved_memory" );
    memory.m_Source = wxS( "local_text_memory" );
    memory.m_Text = wxS( "Rule memory: USB differential pair clearance is 0.20 mm" );
    memory.m_MetadataJson =
            wxS( "{\"project_id\":\"project-a\",\"document_id\":\"board-1\","
                 "\"type\":\"rule_memory\"}" );
    request.m_RetrievedMemoryBlocks.push_back( memory );

    AI_PROVIDER_REQUEST compiled = AiCompileProviderInput( request );

    BOOST_CHECK( compiled.m_ContextCompiled );
    BOOST_CHECK( compiled.m_CompiledUserMessageText.Contains(
            wxS( "Retrieved local memory" ) ) );
    BOOST_CHECK( compiled.m_CompiledUserMessageText.Contains(
            wxS( "USB differential pair clearance" ) ) );
    BOOST_CHECK( compiled.m_PromptTraceJson.Contains(
            wxS( "memory.rule.usb-clearance" ) ) );
    BOOST_CHECK( compiled.m_PromptTraceJson.Contains( wxS( "retrieved_memory" ) ) );
}


BOOST_AUTO_TEST_CASE( ProviderInputCompilerIncludesVisualObservationSidecar )
{
    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 84;
    request.m_UserText = wxS( "inspect placement preview" );
    request.m_MaxProviderInputChars = 4000;
    request.m_ContextSnapshot.m_Visual.m_Source = wxS( "canvas.annotated_roi" );
    request.m_ContextSnapshot.m_Visual.m_MimeType = wxS( "image/png" );
    request.m_ContextSnapshot.m_Visual.m_DataUri = wxS( "data:image/png;base64,dW5pdA==" );
    request.m_ContextSnapshot.m_Visual.m_FrameId = wxS( "frame-1" );
    request.m_ContextSnapshot.m_Visual.m_FrameKind = wxS( "annotated_roi" );
    request.m_ContextSnapshot.m_Visual.m_WidthPx = 64;
    request.m_ContextSnapshot.m_Visual.m_HeightPx = 64;
    request.m_ContextSnapshot.m_Visual.m_ByteSize = 128;
    request.m_ContextSnapshot.m_Visual.m_SidecarJson =
            wxS( "{\"frame_id\":\"frame-1\",\"frame_kind\":\"annotated_roi\","
                 "\"anchors\":[{\"anchor_id\":\"A1\",\"handle\":\"handle-U3\"}]}" );

    AI_PROVIDER_REQUEST compiled = AiCompileProviderInput( request );

    BOOST_CHECK( compiled.m_ContextCompiled );
    BOOST_CHECK( compiled.m_CompiledUserMessageText.Contains(
            wxS( "Visual observation artifact" ) ) );
    BOOST_CHECK( compiled.m_CompiledUserMessageText.Contains( wxS( "frame-1" ) ) );
    BOOST_CHECK( compiled.m_CompiledUserMessageText.Contains( wxS( "handle-U3" ) ) );
    BOOST_CHECK( compiled.m_PromptTraceJson.Contains(
            wxS( "visual_observation_artifact" ) ) );
    BOOST_CHECK( compiled.m_PromptTraceJson.Contains( wxS( "frame-1" ) ) );
}


BOOST_AUTO_TEST_CASE( ProviderMessageCompilerBuildsStandardMessageFlow )
{
    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 85;
    request.m_SystemPromptOverride = wxS( "System override." );
    request.m_UserText = wxS( "review this attempt" );
    request.m_ContextSnapshot.m_Visual.m_Source = wxS( "canvas.preview_after" );
    request.m_ContextSnapshot.m_Visual.m_MimeType = wxS( "image/png" );
    request.m_ContextSnapshot.m_Visual.m_DataUri = wxS( "data:image/png;base64,dW5pdA==" );
    request.m_ContextSnapshot.m_Visual.m_FrameId = wxS( "frame-review-1" );
    request.m_ContextSnapshot.m_Visual.m_FrameKind = wxS( "preview_after" );
    request.m_ContextSnapshot.m_Visual.m_WidthPx = 12;
    request.m_ContextSnapshot.m_Visual.m_HeightPx = 8;
    request.m_ContextSnapshot.m_Visual.m_ByteSize = 48;
    request.m_ContextSnapshot.m_Visual.m_SidecarJson =
            wxS( "{\"frame_id\":\"frame-review-1\","
                 "\"anchors\":[{\"anchor_id\":\"A1\",\"handle\":\"handle-track\"}]}" );

    AI_TOOL_CALL_RECORD toolResult;
    toolResult.m_ToolCallId = wxS( "call-1" );
    toolResult.m_ToolName = wxS( "validate_hidden_attempt" );
    toolResult.m_ArgumentsJson = wxS( "{\"grade\":\"preview\"}" );
    toolResult.m_ResultJson = wxS( "{\"validation_passed\":true}" );
    toolResult.m_Allowed = true;
    toolResult.m_Executed = true;
    request.m_ToolResults.push_back( toolResult );

    wxString messagesJson = AiCompileProviderMessagesJson(
            request, wxS( "Default system prompt." ) );
    nlohmann::json messages = nlohmann::json::parse( messagesJson.ToStdString() );

    BOOST_REQUIRE( messages.is_array() );
    BOOST_REQUIRE_EQUAL( messages.size(), 4 );
    BOOST_CHECK_EQUAL( messages.at( 0 )["role"].get<std::string>(), "system" );
    BOOST_CHECK_EQUAL( messages.at( 0 )["content"].get<std::string>(), "System override." );
    BOOST_CHECK_EQUAL( messages.at( 1 )["role"].get<std::string>(), "user" );
    BOOST_REQUIRE( messages.at( 1 )["content"].is_array() );
    BOOST_CHECK_EQUAL( messages.at( 1 )["content"].at( 0 )["type"].get<std::string>(),
                       "text" );
    BOOST_CHECK( messages.at( 1 )["content"].at( 0 )["text"].get<std::string>().find(
                         "Visual observation artifact" )
                 != std::string::npos );
    BOOST_CHECK( messages.at( 1 )["content"].at( 0 )["text"].get<std::string>().find(
                         "handle-track" )
                 != std::string::npos );
    BOOST_CHECK_EQUAL( messages.at( 1 )["content"].at( 1 )["type"].get<std::string>(),
                       "image_url" );
    BOOST_CHECK_EQUAL( messages.at( 2 )["role"].get<std::string>(), "assistant" );
    BOOST_CHECK( messages.at( 2 ).contains( "tool_calls" ) );
    BOOST_CHECK_EQUAL( messages.at( 3 )["role"].get<std::string>(), "tool" );
    BOOST_CHECK_EQUAL( messages.at( 3 )["tool_call_id"].get<std::string>(), "call-1" );
    BOOST_CHECK( messages.at( 3 )["content"].get<std::string>().find(
                         "validation_passed" )
                 != std::string::npos );
}


BOOST_AUTO_TEST_CASE( ProviderMessageCompilerAddsToolResultBoardStateProvenance )
{
    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 101;
    request.m_UserText = wxS( "continue after validation" );
    request.m_ContextVersion.m_DocumentRevision = 42;
    request.m_ContextVersion.m_SelectionRevision = 7;
    request.m_ContextVersion.m_ViewRevision = 9;
    request.m_ContextSnapshot.m_Version = request.m_ContextVersion;

    AI_TOOL_CALL_RECORD toolResult;
    toolResult.m_ToolCallId = wxS( "call-provenance" );
    toolResult.m_ToolName = wxS( "validate_hidden_attempt" );
    toolResult.m_ResultJson = wxS( "{\"validation_passed\":true}" );
    toolResult.m_Allowed = true;
    toolResult.m_Executed = true;
    request.m_ToolResults.push_back( toolResult );

    AI_PROVIDER_REQUEST compiled = AiCompileProviderInput( request );

    BOOST_CHECK( compiled.m_PromptTraceJson.Contains(
            wxS( "board_state_version" ) ) );
    BOOST_CHECK( compiled.m_PromptTraceJson.Contains(
            wxS( "\"document_revision\":42" ) ) );
    BOOST_CHECK( compiled.m_PromptTraceJson.Contains(
            wxS( "call-provenance" ) ) );

    wxString messagesJson = AiCompileProviderMessagesJson(
            request, wxS( "Default system prompt." ) );
    nlohmann::json messages = nlohmann::json::parse( messagesJson.ToStdString() );

    BOOST_REQUIRE_EQUAL( messages.size(), 4 );
    nlohmann::json toolContent = nlohmann::json::parse(
            messages.at( 3 )["content"].get<std::string>() );
    BOOST_REQUIRE( toolContent.contains( "provenance" ) );
    BOOST_CHECK_EQUAL(
            toolContent["provenance"]["tool_call_id"].get<std::string>(),
            "call-provenance" );
    BOOST_CHECK_EQUAL(
            toolContent["provenance"]["board_state_version"]["document_revision"]
                    .get<uint64_t>(),
            42 );
    BOOST_CHECK( toolContent["result"].contains( "validation_passed" ) );
}


BOOST_AUTO_TEST_CASE( OpenAiProviderDeclaresKiSurfTools )
{
    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxS( "https://sub2api.wenming-dev.org/v1" );
    settings.m_ApiKey = wxS( "unit-test-key" );
    settings.m_Model = wxS( "unit-model" );

    AI_OPENAI_COMPAT_PROVIDER provider(
            settings,
            []( const AI_HTTP_REQUEST& aRequest, AI_HTTP_RESPONSE& aResponse, wxString& aError )
            {
                wxUnusedVar( aError );

                nlohmann::json body = nlohmann::json::parse( aRequest.m_Body.ToStdString() );

                BOOST_REQUIRE( body.contains( "tools" ) );
                BOOST_REQUIRE( body["tools"].is_array() );
                BOOST_REQUIRE_EQUAL( body["tools"].size(), 17 );

                std::vector<std::string> toolNames;
                nlohmann::json           toolByName;

                for( const nlohmann::json& tool : body["tools"] )
                {
                    BOOST_CHECK_EQUAL( tool["type"].get<std::string>(), "function" );
                    const std::string name = tool["function"]["name"].get<std::string>();
                    BOOST_CHECK( name.find( '.' ) == std::string::npos );
                    toolNames.push_back( name );
                    toolByName[name] = tool;
                }

                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_run_action" ) != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_check_action" ) != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_get_context_snapshot" )
                             == toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_get_visual_frame" )
                             == toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_get_activity_timeline" )
                             == toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_get_workspace_view" )
                             != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_invoke_semantic_ui_action" )
                             != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_run_cell" ) != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_run_atomic_operation" ) != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_query_board_summary" )
                             != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_query_items" ) != toolNames.end() );
                const nlohmann::json& queryItemsParameters =
                        toolByName["kisurf_query_items"]["function"]["parameters"];
                BOOST_REQUIRE( queryItemsParameters["properties"].contains( "filter" ) );
                BOOST_CHECK( !queryItemsParameters["properties"].contains(
                        "live_board" ) );
                BOOST_CHECK( !queryItemsParameters["properties"].contains(
                        "session_only" ) );
                BOOST_CHECK_EQUAL( queryItemsParameters.dump().find( "session_only" ),
                                   std::string::npos );
                BOOST_CHECK_EQUAL( queryItemsParameters.dump().find( "session_id" ),
                                   std::string::npos );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_query_item" ) != toolNames.end() );
                const nlohmann::json& queryItemParameters =
                        toolByName["kisurf_query_item"]["function"]["parameters"];
                BOOST_REQUIRE( queryItemParameters["properties"].contains( "handle" ) );
                BOOST_CHECK( queryHandleSchemaSupportsTypedReferences(
                        queryItemParameters["properties"]["handle"] ) );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_query_unplaced_footprints" )
                             != toolNames.end() );
                BOOST_CHECK_EQUAL(
                        toolByName["kisurf_query_unplaced_footprints"]["function"]
                                ["parameters"]
                                        .value( "type", std::string() ),
                        "object" );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_query_selection" ) != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_query_nets" ) != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_query_layers" ) != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_query_design_rules" ) != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_query_viewport" ) != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_query_activity_timeline" )
                             != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_run_validation" ) != toolNames.end() );

                for( const std::string& removedTool :
                     { "kisurf_open_session",
                       "kisurf_close_session",
                       "kisurf_begin_step",
                       "kisurf_end_step",
                       "kisurf_checkpoint",
                       "kisurf_rollback_to",
                       "kisurf_cancel_session",
                       "kisurf_reject_session",
                       "kisurf_accept_session",
                       "kisurf_observe_step",
                       "kisurf_render_preview" } )
                {
                    BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                            removedTool )
                                 == toolNames.end() );
                }
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_preview_move_selected" ) == toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_preview_create_copper_zone" )
                             == toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_preview_route_to_anchor" )
                             == toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_preview_panel_fill_column" )
                             == toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_preview_anchor_focus" )
                             == toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "pcb_create_track_segment" ) == toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "pcb_create_via" ) == toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "pcb_create_shape" ) == toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "pcb_create_zone" ) == toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "pcb_move_objects" ) == toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "pcb_fill_via_matrix" ) == toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "script_run_operation_bundle" ) == toolNames.end() );
                const nlohmann::json& runCellParameters =
                        toolByName["kisurf_run_cell"]["function"]["parameters"];
                const std::string runCellDescription =
                        toolByName["kisurf_run_cell"]["function"]["description"]
                                .get<std::string>();
                BOOST_CHECK( runCellDescription.find( "pcb.create_via" )
                             != std::string::npos );
                BOOST_CHECK( runCellDescription.find( "pcb.create_track_segment" )
                             != std::string::npos );
                BOOST_CHECK( runCellDescription.find( "surface.apply_patch" )
                             != std::string::npos );
                BOOST_CHECK( runCellDescription.find( "apply_surface_patch_ops" )
                             != std::string::npos );
                BOOST_CHECK( runCellDescription.find( "surface_fill_row_op" )
                             != std::string::npos );
                BOOST_CHECK( runCellDescription.find( "surface_fill_column_op" )
                             != std::string::npos );
                BOOST_CHECK( runCellDescription.find( "surface_fill_range_op" )
                             != std::string::npos );
                BOOST_CHECK( runCellDescription.find( "surface_set_property_op" )
                             != std::string::npos );
                BOOST_CHECK( runCellDescription.find( "script_run_operation_bundle" )
                             == std::string::npos );
                BOOST_CHECK( runCellDescription.find( "pcb_fill_via_matrix" )
                             == std::string::npos );
                BOOST_CHECK( runCellDescription.find( "current board" )
                             != std::string::npos );
                BOOST_CHECK( runCellDescription.find( "session cell" )
                             == std::string::npos );
                BOOST_CHECK( runCellDescription.find( "cannot publish" )
                             == std::string::npos );
                BOOST_CHECK( runCellParameters["required"].dump().find( "cell_text" )
                             != std::string::npos );
                BOOST_CHECK( runCellParameters["properties"].contains( "cell_id" ) );
                BOOST_CHECK( runCellParameters["properties"].contains(
                                     "max_operation_count" ) );
                BOOST_CHECK_EQUAL(
                        runCellParameters["properties"]["max_operation_count"]["minimum"]
                                .get<int>(),
                        1 );
                BOOST_CHECK_EQUAL(
                        runCellParameters["properties"]["max_operation_count"]["maximum"]
                                .get<int>(),
                        256 );
                BOOST_CHECK( !runCellParameters["additionalProperties"].get<bool>() );
                BOOST_CHECK( runCellDescription.find( "max_operation_count" )
                             != std::string::npos );
                const nlohmann::json& atomicParameters =
                        toolByName["kisurf_run_atomic_operation"]["function"]["parameters"];
                const std::string atomicDescription =
                        toolByName["kisurf_run_atomic_operation"]["function"]["description"]
                                .get<std::string>();
                BOOST_CHECK( atomicDescription.find( "typed KiSurf atomic operation" )
                             != std::string::npos );
                BOOST_CHECK( atomicDescription.find( "directly to the current board" )
                             != std::string::npos );
                BOOST_CHECK( atomicParameters["required"].dump().find( "kind" )
                             != std::string::npos );
                BOOST_CHECK( atomicParameters["required"].dump().find( "arguments" )
                             != std::string::npos );
                BOOST_CHECK( atomicParameters["properties"]["kind"]["enum"].dump().find(
                                     "pcb.create_via" ) != std::string::npos );
                BOOST_CHECK( atomicParameters["properties"]["kind"]["enum"].dump().find(
                                     "surface.apply_patch" ) != std::string::npos );
                BOOST_REQUIRE( atomicParameters.contains( "$defs" ) );
                BOOST_REQUIRE( atomicParameters["$defs"].contains( "operation_contracts" ) );
                BOOST_CHECK( atomicParameters.dump().find( "shadow-board" )
                             == std::string::npos );
                BOOST_CHECK( atomicParameters.dump().find( "session handles" )
                             == std::string::npos );
                BOOST_CHECK( atomicParameters.dump().find( "Session handle" )
                             == std::string::npos );
                BOOST_CHECK( atomicParameters.dump().find( "session_id" )
                             == std::string::npos );
                BOOST_CHECK( toolByName["kisurf_query_item"]["function"]
                                     ["description"]
                                             .get<std::string>()
                                     .find( "shadow-board" )
                             == std::string::npos );
                const nlohmann::json& operationContracts =
                        atomicParameters["$defs"]["operation_contracts"];
                BOOST_REQUIRE( operationContracts.contains( "pcb.create_via" ) );
                BOOST_REQUIRE( operationContracts.contains( "pcb.create_track_segment" ) );
                BOOST_REQUIRE( operationContracts.contains( "pcb.move_items" ) );
                BOOST_REQUIRE( operationContracts.contains( "surface.apply_patch" ) );
                BOOST_CHECK( operationContracts["pcb.create_via"]["required"].dump().find(
                                     "position" ) != std::string::npos );
                BOOST_CHECK( operationContracts["pcb.create_via"]["properties"].contains(
                        "layer_pair" ) );
                BOOST_CHECK( operationContracts["pcb.create_track_segment"]["required"]
                                     .dump()
                                     .find( "start" )
                             != std::string::npos );
                BOOST_CHECK( operationContracts["pcb.create_track_segment"]["properties"]
                                     .contains( "width" ) );
                BOOST_CHECK( operationContracts["pcb.move_items"]["properties"].contains(
                        "target_positions" ) );
                BOOST_REQUIRE( operationContracts.contains( "pcb.delete_items" ) );
                BOOST_CHECK( operationContracts["pcb.delete_items"]["properties"].contains(
                        "filter" ) );
                BOOST_CHECK( operationContracts["pcb.delete_items"]["required"]
                                     .dump()
                                     .find( "handles" )
                             == std::string::npos );
                BOOST_REQUIRE( operationContracts.contains( "pcb.create_zone" ) );
                BOOST_REQUIRE( operationContracts["pcb.create_zone"]["properties"]
                                       .contains( "outline" ) );
                const nlohmann::json& zoneOutlineContract =
                        operationContracts["pcb.create_zone"]["properties"]["outline"];
                BOOST_REQUIRE( zoneOutlineContract.contains( "properties" ) );
                BOOST_REQUIRE( zoneOutlineContract["properties"].contains( "points" ) );
                const nlohmann::json& zoneOutlinePointsContract =
                        zoneOutlineContract["properties"]["points"];
                BOOST_CHECK_EQUAL( zoneOutlinePointsContract.value( "minItems", 0 ), 3 );
                BOOST_REQUIRE( zoneOutlinePointsContract.contains( "items" ) );
                BOOST_CHECK( pointSchemaRequiresXY(
                        zoneOutlinePointsContract["items"] ) );
                BOOST_CHECK( pointSchemaSupportsModelFacingShortcuts(
                        zoneOutlinePointsContract["items"] ) );
                BOOST_CHECK( operationContracts["surface.apply_patch"]["required"]
                                     .dump()
                                     .find( "surface_id" )
                             != std::string::npos );
                BOOST_CHECK( operationContracts["surface.apply_patch"]["required"]
                                     .dump()
                                     .find( "patch" )
                             != std::string::npos );
                BOOST_CHECK( operationContracts["surface.apply_patch"]["properties"]
                                     ["expected_surface_revision"].is_object() );
                BOOST_CHECK( operationContracts["surface.apply_patch"]["properties"]
                                     ["expected_schema_version"].is_object() );
                BOOST_REQUIRE( operationContracts.contains(
                        "pcb.update_item_geometry" ) );
                const nlohmann::json& updateGeometryContract =
                        operationContracts["pcb.update_item_geometry"];
                BOOST_REQUIRE( updateGeometryContract["properties"].contains(
                        "geometry_patch" ) );
                const nlohmann::json& geometryPatchContract =
                        updateGeometryContract["properties"]["geometry_patch"];
                BOOST_REQUIRE( geometryPatchContract.contains( "properties" ) );
                const nlohmann::json& geometryPatchProperties =
                        geometryPatchContract["properties"];
                BOOST_CHECK( geometryPatchProperties.contains( "start" ) );
                BOOST_CHECK( geometryPatchProperties.contains( "end" ) );
                BOOST_CHECK( geometryPatchProperties.contains( "center" ) );
                BOOST_CHECK( geometryPatchProperties.contains( "mid" ) );
                BOOST_CHECK( geometryPatchProperties.contains( "radius" ) );
                BOOST_REQUIRE( geometryPatchProperties.contains( "points" ) );
                BOOST_CHECK_EQUAL( geometryPatchProperties["points"].value(
                                           "minItems", 0 ),
                                   3 );
                BOOST_REQUIRE( geometryPatchProperties["points"].contains(
                        "items" ) );
                BOOST_CHECK( pointSchemaRequiresXY(
                        geometryPatchProperties["points"]["items"] ) );
                BOOST_REQUIRE( operationContracts.contains(
                        "pcb.set_item_properties" ) );
                BOOST_REQUIRE( operationContracts["pcb.set_item_properties"]
                                       ["properties"]
                                               .contains( "typed_props" ) );
                const nlohmann::json& typedPropsContract =
                        operationContracts["pcb.set_item_properties"]["properties"]
                                          ["typed_props"];
                BOOST_REQUIRE( typedPropsContract.contains( "properties" ) );
                for( const std::string& propName :
                     { "diameter", "drill", "width", "fill", "clearance",
                       "priority", "fill_mode", "reference", "value", "side",
                       "orientation_degrees" } )
                {
                    BOOST_CHECK( typedPropsContract["properties"].contains(
                            propName ) );
                }
                BOOST_CHECK_EQUAL( typedPropsContract["properties"]["fill"]["type"],
                                   "boolean" );
                BOOST_CHECK_EQUAL( typedPropsContract["properties"]["reference"]
                                                     ["type"],
                                   "string" );
                BOOST_CHECK_EQUAL( typedPropsContract["properties"]["side"]["type"],
                                   "string" );
                BOOST_REQUIRE( typedPropsContract["properties"]["fill_mode"].contains(
                        "enum" ) );
                BOOST_CHECK( std::find(
                                     typedPropsContract["properties"]["fill_mode"]
                                                       ["enum"].begin(),
                                     typedPropsContract["properties"]["fill_mode"]
                                                       ["enum"].end(),
                                     "hatch_pattern" )
                             != typedPropsContract["properties"]["fill_mode"]
                                                  ["enum"].end() );
                BOOST_REQUIRE( operationContracts.contains( "pcb.refill_zones" ) );
                BOOST_REQUIRE( operationContracts["pcb.refill_zones"]["properties"]
                                       .contains( "affected_area" ) );
                BOOST_CHECK( boxSchemaSupportsCanonicalForms(
                        operationContracts["pcb.refill_zones"]["properties"]
                                          ["affected_area"] ) );
                BOOST_REQUIRE( operationContracts.contains(
                        "pcb.rebuild_connectivity" ) );
                BOOST_REQUIRE( operationContracts["pcb.rebuild_connectivity"]
                                       ["properties"]
                                               .contains( "scope" ) );
                BOOST_CHECK( stringEnumContainsAll(
                        operationContracts["pcb.rebuild_connectivity"]["properties"]
                                          ["scope"],
                        { "affected_area", "selection", "region" } ) );
                BOOST_CHECK( std::find(
                                     operationContracts["pcb.rebuild_connectivity"]
                                                       ["properties"]["scope"]["enum"]
                                                               .begin(),
                                     operationContracts["pcb.rebuild_connectivity"]
                                                       ["properties"]["scope"]["enum"]
                                                               .end(),
                                     "session" )
                             == operationContracts["pcb.rebuild_connectivity"]
                                                 ["properties"]["scope"]["enum"]
                                                         .end() );
                BOOST_REQUIRE( operationContracts.contains( "pcb.run_validation" ) );
                BOOST_REQUIRE( operationContracts["pcb.run_validation"]["properties"]
                                       .contains( "scope" ) );
                BOOST_CHECK( stringEnumContainsAll(
                        operationContracts["pcb.run_validation"]["properties"]["scope"],
                        { "affected_area", "selection", "region" } ) );
                BOOST_CHECK( std::find(
                                     operationContracts["pcb.run_validation"]["properties"]
                                                       ["scope"]["enum"]
                                                               .begin(),
                                     operationContracts["pcb.run_validation"]["properties"]
                                                       ["scope"]["enum"]
                                                               .end(),
                                     "session" )
                             == operationContracts["pcb.run_validation"]["properties"]
                                                 ["scope"]["enum"]
                                                         .end() );
                BOOST_CHECK( stringEnumContainsAll(
                        operationContracts["pcb.run_validation"]["properties"]["level"],
                        { "geometry", "drc_lite", "full_drc" } ) );
                BOOST_CHECK( !atomicParameters["additionalProperties"].get<bool>() );
                const nlohmann::json& validationParameters =
                        toolByName["kisurf_run_validation"]["function"]["parameters"];
                BOOST_CHECK( validationSchemaDeclaresTypedObservationArgs(
                        validationParameters ) );
                const nlohmann::json& workspaceParameters =
                        toolByName["kisurf_get_workspace_view"]["function"]["parameters"];
                BOOST_CHECK( workspaceParameters["required"].empty() );
                BOOST_CHECK( !workspaceParameters["additionalProperties"].get<bool>() );
                BOOST_CHECK( workspaceParameters["properties"].contains( "views" ) );
                BOOST_CHECK( workspaceParameters["properties"].contains( "context" ) );
                BOOST_CHECK( workspaceParameters["properties"].contains( "visual" ) );
                BOOST_CHECK( workspaceParameters["properties"].contains( "activity" ) );
                const nlohmann::json& workspaceVisualParameters =
                        workspaceParameters["properties"]["visual"];
                BOOST_CHECK( workspaceVisualParameters["properties"].contains(
                        "include_anchor_overlays" ) );
                BOOST_CHECK( workspaceVisualParameters["properties"].contains(
                        "max_anchor_overlays" ) );
                BOOST_CHECK( workspaceVisualParameters["properties"].contains(
                        "focus_layer" ) );
                BOOST_CHECK( workspaceVisualParameters["properties"].contains(
                        "focus_net" ) );
                BOOST_CHECK( workspaceVisualParameters["properties"].contains(
                        "dim_unfocused_layers" ) );
                BOOST_CHECK( workspaceVisualParameters["properties"].contains(
                        "highlight_anchor_ids" ) );
                const nlohmann::json& uiActionParameters =
                        toolByName["kisurf_invoke_semantic_ui_action"]["function"]
                                  ["parameters"];
                const std::string uiActionRequired =
                        uiActionParameters["required"].dump();
                BOOST_CHECK( uiActionRequired.find( "node_id" ) != std::string::npos );
                BOOST_CHECK( uiActionRequired.find( "action" ) != std::string::npos );
                BOOST_CHECK( !uiActionParameters["additionalProperties"].get<bool>() );
                BOOST_CHECK( uiActionParameters["properties"].contains( "node_id" ) );
                BOOST_CHECK( uiActionParameters["properties"].contains( "action" ) );
                BOOST_CHECK( uiActionParameters["properties"].contains( "text" ) );
                BOOST_CHECK( uiActionParameters["properties"].contains( "checked" ) );
                BOOST_CHECK( !uiActionParameters["properties"].contains(
                        "user_confirmed" ) );
                BOOST_REQUIRE( body.contains( "parallel_tool_calls" ) );
                BOOST_CHECK( !body["parallel_tool_calls"].get<bool>() );

                aResponse.m_StatusCode = 200;
                aResponse.m_Body = wxS( "{\"choices\":[{\"message\":{\"content\":\"ready\"}}]}" );
                return true;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 21;
    request.m_UserText = wxS( "inspect" );

    AI_PROVIDER_RESPONSE response = provider.Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 21 );
    BOOST_CHECK_EQUAL( response.m_ToolCalls.size(), 0 );
}


BOOST_AUTO_TEST_CASE( OpenAiProviderUsesRequestSpecificToolCatalog )
{
    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxS( "https://sub2api.wenming-dev.org/v1" );
    settings.m_ApiKey = wxS( "unit-test-key" );
    settings.m_Model = wxS( "unit-model" );

    AI_OPENAI_COMPAT_PROVIDER provider(
            settings,
            []( const AI_HTTP_REQUEST& aRequest, AI_HTTP_RESPONSE& aResponse, wxString& aError )
            {
                wxUnusedVar( aError );

                nlohmann::json body = nlohmann::json::parse( aRequest.m_Body.ToStdString() );

                BOOST_REQUIRE( body.contains( "tools" ) );
                BOOST_REQUIRE( body["tools"].is_array() );
                BOOST_REQUIRE_EQUAL( body["tools"].size(), 1 );
                BOOST_CHECK_EQUAL( body["tools"].at( 0 )["function"]["name"].get<std::string>(),
                                   "kisurf_next_action_observe" );

                aResponse.m_StatusCode = 200;
                aResponse.m_Body = wxS( "{\"choices\":[{\"message\":{\"content\":\"ready\"}}]}" );
                return true;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 24;
    request.m_UserText = wxS( "next action decision" );
    request.m_ToolCatalogJson =
            wxS( "[{\"type\":\"function\",\"function\":{\"name\":\"kisurf_next_action_observe\","
                 "\"description\":\"Observe a bounded semantic event.\","
                 "\"parameters\":{\"type\":\"object\",\"properties\":{},"
                 "\"additionalProperties\":false}}}]" );

    AI_PROVIDER_RESPONSE response = provider.Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 24 );
    BOOST_CHECK_EQUAL( response.m_Body, wxString( wxS( "ready" ) ) );
}


BOOST_AUTO_TEST_CASE( OpenAiProviderRequestsAutomaticToolChoiceWhenToolsAreAvailable )
{
    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxS( "https://sub2api.wenming-dev.org/v1" );
    settings.m_ApiKey = wxS( "unit-test-key" );
    settings.m_Model = wxS( "unit-model" );

    AI_OPENAI_COMPAT_PROVIDER provider(
            settings,
            []( const AI_HTTP_REQUEST& aRequest, AI_HTTP_RESPONSE& aResponse, wxString& aError )
            {
                wxUnusedVar( aError );

                nlohmann::json body = nlohmann::json::parse( aRequest.m_Body.ToStdString() );

                BOOST_REQUIRE( body.contains( "tools" ) );
                BOOST_REQUIRE( body["tools"].is_array() );
                BOOST_CHECK( !body["tools"].empty() );
                BOOST_REQUIRE( body.contains( "tool_choice" ) );
                BOOST_CHECK_EQUAL( body["tool_choice"].get<std::string>(), "auto" );

                aResponse.m_StatusCode = 200;
                aResponse.m_Body = wxS( "{\"choices\":[{\"message\":{\"content\":\"ready\"}}]}" );
                return true;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 2401;
    request.m_UserText = wxS( "create a via preview" );

    AI_PROVIDER_RESPONSE response = provider.Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 2401 );
    BOOST_CHECK_EQUAL( response.m_Body, wxString( wxS( "ready" ) ) );
}


BOOST_AUTO_TEST_CASE( OpenAiProviderKeepsRequestToolCatalogWhenDefaultToolsDisabled )
{
    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxS( "https://sub2api.wenming-dev.org/v1" );
    settings.m_ApiKey = wxS( "unit-test-key" );
    settings.m_Model = wxS( "unit-model" );

    AI_OPENAI_COMPAT_PROVIDER provider(
            settings,
            []( const AI_HTTP_REQUEST& aRequest, AI_HTTP_RESPONSE& aResponse, wxString& aError )
            {
                wxUnusedVar( aError );

                nlohmann::json body = nlohmann::json::parse( aRequest.m_Body.ToStdString() );

                BOOST_REQUIRE( body.contains( "tools" ) );
                BOOST_REQUIRE( body["tools"].is_array() );
                BOOST_REQUIRE_EQUAL( body["tools"].size(), 1 );
                BOOST_CHECK_EQUAL( body["tools"].at( 0 )["function"]["name"].get<std::string>(),
                                   "script_run_bounded_plan" );

                aResponse.m_StatusCode = 200;
                aResponse.m_Body = wxS( "{\"choices\":[{\"message\":{\"content\":\"ready\"}}]}" );
                return true;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 25;
    request.m_UserText = wxS( "next action review" );
    request.m_DisableDefaultTools = true;
    request.m_ToolCatalogJson =
            wxS( "[{\"type\":\"function\",\"function\":{\"name\":\"script_run_bounded_plan\","
                 "\"description\":\"Run a bounded Next Action script plan.\","
                 "\"parameters\":{\"type\":\"object\",\"properties\":{},"
                 "\"additionalProperties\":false}}}]" );

    AI_PROVIDER_RESPONSE response = provider.Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 25 );
    BOOST_CHECK_EQUAL( response.m_Body, wxString( wxS( "ready" ) ) );
}


BOOST_AUTO_TEST_CASE( OpenAiProviderAcceptsNextActionCallableToolCatalog )
{
    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxS( "https://sub2api.wenming-dev.org/v1" );
    settings.m_ApiKey = wxS( "unit-test-key" );
    settings.m_Model = wxS( "unit-model" );

    AI_OPENAI_COMPAT_PROVIDER provider(
            settings,
            []( const AI_HTTP_REQUEST& aRequest, AI_HTTP_RESPONSE& aResponse, wxString& aError )
            {
                wxUnusedVar( aError );

                nlohmann::json body = nlohmann::json::parse( aRequest.m_Body.ToStdString() );

                BOOST_REQUIRE( body.contains( "tools" ) );
                BOOST_REQUIRE( body["tools"].is_array() );
                BOOST_CHECK( !body["tools"].empty() );

                bool sawScriptTool = false;

                for( const nlohmann::json& tool : body["tools"] )
                {
                    BOOST_REQUIRE( tool.is_object() );
                    BOOST_CHECK_EQUAL( tool.value( "type", std::string() ), "function" );
                    BOOST_REQUIRE( tool.contains( "function" ) );
                    BOOST_REQUIRE( tool["function"].contains( "name" ) );

                    const std::string name =
                            tool["function"]["name"].get<std::string>();
                    BOOST_CHECK( name.find( '.' ) == std::string::npos );
                    BOOST_CHECK_NE( name, "publish_preview" );
                    BOOST_CHECK_NE( name, "publish.preview" );

                    if( name == "script_run_bounded_plan" )
                        sawScriptTool = true;
                }

                BOOST_CHECK( sawScriptTool );

                aResponse.m_StatusCode = 200;
                aResponse.m_Body = wxS( "{\"choices\":[{\"message\":{\"content\":\"ready\"}}]}" );
                return true;
            } );

    AI_NEXT_ACTION_TOOL_REGISTRY tools;
    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 26;
    request.m_UserText = wxS( "next action decision" );
    request.m_DisableDefaultTools = true;
    request.m_ToolCatalogJson = tools.CallableToolCatalogJson();

    AI_PROVIDER_RESPONSE response = provider.Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 26 );
    BOOST_CHECK_EQUAL( response.m_Body, wxString( wxS( "ready" ) ) );
}


BOOST_AUTO_TEST_CASE( OpenAiProviderParsesFunctionToolCalls )
{
    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxS( "https://sub2api.wenming-dev.org/v1" );
    settings.m_ApiKey = wxS( "unit-test-key" );
    settings.m_Model = wxS( "unit-model" );

    AI_OPENAI_COMPAT_PROVIDER provider(
            settings,
            []( const AI_HTTP_REQUEST&, AI_HTTP_RESPONSE& aResponse, wxString& aError )
            {
                wxUnusedVar( aError );

                aResponse.m_StatusCode = 200;
                aResponse.m_Body =
                        wxS( "{\"choices\":[{\"message\":{\"content\":null,"
                             "\"tool_calls\":[{\"id\":\"call_123\",\"type\":\"function\","
                             "\"function\":{\"name\":\"kisurf_run_action\","
                             "\"arguments\":\"{\\\"action\\\":\\\"pcbnew.InteractiveSelectionTool.selectionClear\\\"}\"}}]}}]}" );
                return true;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 22;
    request.m_UserText = wxS( "clear selection" );

    AI_PROVIDER_RESPONSE response = provider.Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 22 );
    BOOST_CHECK_EQUAL( response.m_Body, wxString( wxS( "Tool call requested." ) ) );
    BOOST_REQUIRE_EQUAL( response.m_ToolCalls.size(), 1 );
    BOOST_CHECK_EQUAL( response.m_ToolCalls.front().m_RequestId, 22 );
    BOOST_CHECK_EQUAL( response.m_ToolCalls.front().m_ToolCallId,
                       wxString( wxS( "call_123" ) ) );
    BOOST_CHECK_EQUAL( response.m_ToolCalls.front().m_ToolName,
                       wxString( wxS( "kisurf_run_action" ) ) );
    BOOST_CHECK( response.m_ToolCalls.front().m_ArgumentsJson.Contains(
            wxS( "pcbnew.InteractiveSelectionTool.selectionClear" ) ) );
}


BOOST_AUTO_TEST_CASE( OpenAiProviderSendsToolResultContinuationMessages )
{
    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxS( "https://sub2api.wenming-dev.org/v1" );
    settings.m_ApiKey = wxS( "unit-test-key" );
    settings.m_Model = wxS( "unit-model" );

    AI_OPENAI_COMPAT_PROVIDER provider(
            settings,
            []( const AI_HTTP_REQUEST& aRequest, AI_HTTP_RESPONSE& aResponse, wxString& aError )
            {
                wxUnusedVar( aError );

                nlohmann::json body = nlohmann::json::parse( aRequest.m_Body.ToStdString() );
                const nlohmann::json& messages = body["messages"];

                BOOST_REQUIRE_EQUAL( messages.size(), 4 );
                BOOST_CHECK_EQUAL( messages.at( 0 )["role"].get<std::string>(), "system" );
                BOOST_CHECK_EQUAL( messages.at( 1 )["role"].get<std::string>(), "user" );
                BOOST_CHECK_EQUAL( messages.at( 2 )["role"].get<std::string>(), "assistant" );
                BOOST_CHECK( messages.at( 2 )["content"].is_null() );

                const nlohmann::json& toolCall = messages.at( 2 )["tool_calls"].at( 0 );
                BOOST_CHECK_EQUAL( toolCall["id"].get<std::string>(), "call_456" );
                BOOST_CHECK_EQUAL( toolCall["type"].get<std::string>(), "function" );
                BOOST_CHECK_EQUAL( toolCall["function"]["name"].get<std::string>(),
                                   "kisurf_check_action" );
                BOOST_CHECK_EQUAL( toolCall["function"]["arguments"].get<std::string>(),
                                   "{\"action\":\"common.Control.showAgentPanel\"}" );

                BOOST_CHECK_EQUAL( messages.at( 3 )["role"].get<std::string>(), "tool" );
                BOOST_CHECK_EQUAL( messages.at( 3 )["tool_call_id"].get<std::string>(),
                                   "call_456" );
                nlohmann::json toolContent = nlohmann::json::parse(
                        messages.at( 3 )["content"].get<std::string>() );
                BOOST_CHECK( toolContent.contains( "result" ) );
                BOOST_CHECK( toolContent.contains( "provenance" ) );
                BOOST_CHECK( toolContent["result"]["allowed"].get<bool>() );
                BOOST_CHECK( !toolContent["result"]["executed"].get<bool>() );
                BOOST_CHECK_EQUAL(
                        toolContent["provenance"]["tool_call_id"].get<std::string>(),
                        "call_456" );
                BOOST_CHECK_EQUAL(
                        toolContent["provenance"]["board_state_version"]
                                ["document_revision"].get<uint64_t>(),
                        0 );

                aResponse.m_StatusCode = 200;
                aResponse.m_Body = wxS( "{\"choices\":[{\"message\":{\"content\":\"checked\"}}]}" );
                return true;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 23;
    request.m_UserText = wxS( "check action" );

    AI_TOOL_CALL_RECORD result;
    result.m_RequestId = 23;
    result.m_ToolCallId = wxS( "call_456" );
    result.m_ToolName = wxS( "kisurf_check_action" );
    result.m_ArgumentsJson = wxS( "{\"action\":\"common.Control.showAgentPanel\"}" );
    result.m_ResultJson = wxS( "{\"allowed\":true,\"executed\":false}" );
    result.m_Allowed = true;
    result.m_Executed = false;
    request.m_ToolResults.push_back( result );

    AI_PROVIDER_RESPONSE response = provider.Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 23 );
    BOOST_CHECK_EQUAL( response.m_Body, wxString( wxS( "checked" ) ) );
}


BOOST_AUTO_TEST_CASE( OpenAiProviderRejectsMalformedToolCalls )
{
    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxS( "https://sub2api.wenming-dev.org/v1" );
    settings.m_ApiKey = wxS( "unit-test-key" );
    settings.m_Model = wxS( "unit-model" );

    AI_OPENAI_COMPAT_PROVIDER provider(
            settings,
            []( const AI_HTTP_REQUEST&, AI_HTTP_RESPONSE& aResponse, wxString& aError )
            {
                wxUnusedVar( aError );

                aResponse.m_StatusCode = 200;
                aResponse.m_Body =
                        wxS( "{\"choices\":[{\"message\":{\"content\":null,"
                             "\"tool_calls\":[{\"id\":\"call_bad\",\"type\":\"function\","
                             "\"function\":{\"arguments\":\"{}\"}}]}}]}" );
                return true;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 23;
    request.m_UserText = wxS( "bad call" );

    AI_PROVIDER_RESPONSE response = provider.Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 23 );
    BOOST_CHECK( response.m_Body.Contains( wxS( "malformed tool call" ) ) );
    BOOST_CHECK_EQUAL( response.m_ToolCalls.size(), 0 );
}


BOOST_AUTO_TEST_SUITE_END()
