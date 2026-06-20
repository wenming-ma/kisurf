#include <boost/test/unit_test.hpp>
#include <json_common.h>
#include <kisurf/ai/ai_provider.h>

#include <wx/utils.h>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>
#include <wx/filename.h>

namespace
{
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


BOOST_AUTO_TEST_CASE( ProviderSettingsReadEnvironmentWithDefaults )
{
    wxString oldBaseUrl;
    wxString oldModel;
    wxString oldKey;
    const bool hadBaseUrl = wxGetEnv( wxS( "KISURF_AI_BASE_URL" ), &oldBaseUrl );
    const bool hadModel = wxGetEnv( wxS( "KISURF_AI_MODEL" ), &oldModel );
    const bool hadKey = wxGetEnv( wxS( "OPENAI_API_KEY" ), &oldKey );

    wxSetEnv( wxS( "KISURF_AI_BASE_URL" ), wxS( "https://unit.example.test/v1/" ) );
    wxSetEnv( wxS( "KISURF_AI_MODEL" ), wxS( "unit-model" ) );
    wxSetEnv( wxS( "OPENAI_API_KEY" ), wxS( "unit-test-key" ) );

    AI_PROVIDER_SETTINGS settings = AI_PROVIDER_SETTINGS::FromEnvironment();

    BOOST_CHECK_EQUAL( settings.m_BaseUrl, wxString( wxS( "https://unit.example.test/v1" ) ) );
    BOOST_CHECK_EQUAL( settings.m_Model, wxString( wxS( "unit-model" ) ) );
    BOOST_CHECK( settings.HasApiKey() );

    if( hadBaseUrl )
        wxSetEnv( wxS( "KISURF_AI_BASE_URL" ), oldBaseUrl );
    else
        wxUnsetEnv( wxS( "KISURF_AI_BASE_URL" ) );

    if( hadModel )
        wxSetEnv( wxS( "KISURF_AI_MODEL" ), oldModel );
    else
        wxUnsetEnv( wxS( "KISURF_AI_MODEL" ) );

    if( hadKey )
        wxSetEnv( wxS( "OPENAI_API_KEY" ), oldKey );
    else
        wxUnsetEnv( wxS( "OPENAI_API_KEY" ) );
}


BOOST_AUTO_TEST_CASE( ProviderSettingsReadOpenAiBaseUrlAlias )
{
    ENV_GUARD kisurfBaseGuard( wxS( "KISURF_AI_BASE_URL" ) );
    ENV_GUARD openaiBaseGuard( wxS( "OPENAI_BASE_URL" ) );
    ENV_GUARD lowerBaseGuard( wxS( "base_url" ) );

    wxUnsetEnv( wxS( "KISURF_AI_BASE_URL" ) );
    wxSetEnv( wxS( "OPENAI_BASE_URL" ), wxS( "https://openai.example.test/v1/" ) );
    wxSetEnv( wxS( "base_url" ), wxS( "https://lower.example.test/v1/" ) );

    AI_PROVIDER_SETTINGS settings = AI_PROVIDER_SETTINGS::FromEnvironment();

    BOOST_CHECK_EQUAL( settings.m_BaseUrl,
                       wxString( wxS( "https://openai.example.test/v1" ) ) );
}


BOOST_AUTO_TEST_CASE( ProviderSettingsReadLowercaseBaseUrlAlias )
{
    ENV_GUARD kisurfBaseGuard( wxS( "KISURF_AI_BASE_URL" ) );
    ENV_GUARD openaiBaseGuard( wxS( "OPENAI_BASE_URL" ) );
    ENV_GUARD lowerBaseGuard( wxS( "base_url" ) );

    wxUnsetEnv( wxS( "KISURF_AI_BASE_URL" ) );
    wxUnsetEnv( wxS( "OPENAI_BASE_URL" ) );
    wxSetEnv( wxS( "base_url" ), wxS( "https://lower.example.test/v1/" ) );

    AI_PROVIDER_SETTINGS settings = AI_PROVIDER_SETTINGS::FromEnvironment();

    BOOST_CHECK_EQUAL( settings.m_BaseUrl,
                       wxString( wxS( "https://lower.example.test/v1" ) ) );
}


BOOST_AUTO_TEST_CASE( ProviderSettingsPrefersKiSurfBaseUrlAlias )
{
    ENV_GUARD kisurfBaseGuard( wxS( "KISURF_AI_BASE_URL" ) );
    ENV_GUARD openaiBaseGuard( wxS( "OPENAI_BASE_URL" ) );
    ENV_GUARD lowerBaseGuard( wxS( "base_url" ) );

    wxSetEnv( wxS( "KISURF_AI_BASE_URL" ), wxS( "https://kisurf.example.test/v1/" ) );
    wxSetEnv( wxS( "OPENAI_BASE_URL" ), wxS( "https://openai.example.test/v1/" ) );
    wxSetEnv( wxS( "base_url" ), wxS( "https://lower.example.test/v1/" ) );

    AI_PROVIDER_SETTINGS settings = AI_PROVIDER_SETTINGS::FromEnvironment();

    BOOST_CHECK_EQUAL( settings.m_BaseUrl,
                       wxString( wxS( "https://kisurf.example.test/v1" ) ) );
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
                BOOST_CHECK( aRequest.m_Body.Contains( wxS( "selected objects: 1" ) ) );

                nlohmann::json body = nlohmann::json::parse( aRequest.m_Body.ToStdString() );
                const std::string content =
                        body["messages"].at( 1 )["content"].get<std::string>();
                BOOST_CHECK( content.find( "Structured KiSurf context JSON:" )
                             != std::string::npos );
                BOOST_CHECK( content.find( "\"kisurf_context\"" ) != std::string::npos );
                BOOST_CHECK( content.find( "\"selected_objects\"" ) != std::string::npos );

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
                                     "visual: test.image image/png pixels=yes" )
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
                BOOST_REQUIRE_EQUAL( body["tools"].size(), 29 );

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
                             != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_get_visual_frame" )
                             != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_get_activity_timeline" )
                             != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_get_workspace_view" )
                             != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_invoke_semantic_ui_action" )
                             != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_open_session" ) != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_close_session" ) != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_run_cell" ) != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_begin_step" ) != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_end_step" ) != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_checkpoint" ) != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_rollback_to" ) != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_cancel_session" ) != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_reject_session" ) != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_accept_session" ) != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_observe_step" ) != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_query_board_summary" )
                             != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_query_items" ) != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_query_item" ) != toolNames.end() );
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
                                        "kisurf_render_preview" ) != toolNames.end() );
                BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                                        "kisurf_run_validation" ) != toolNames.end() );
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
                BOOST_CHECK( runCellParameters["required"].dump().find( "cell_text" )
                             != std::string::npos );
                BOOST_CHECK( runCellParameters["properties"].contains( "cell_id" ) );
                BOOST_CHECK( !runCellParameters["additionalProperties"].get<bool>() );
                const nlohmann::json& rollbackParameters =
                        toolByName["kisurf_rollback_to"]["function"]["parameters"];
                BOOST_CHECK( rollbackParameters["required"].dump().find( "checkpoint_id" )
                             != std::string::npos );
                BOOST_CHECK( !rollbackParameters["additionalProperties"].get<bool>() );
                const nlohmann::json& validationParameters =
                        toolByName["kisurf_run_validation"]["function"]["parameters"];
                BOOST_CHECK( validationParameters["properties"]["level"]["enum"].dump().find(
                                     "drc_lite" ) != std::string::npos );
                const nlohmann::json& renderPreviewParameters =
                        toolByName["kisurf_render_preview"]["function"]["parameters"];
                BOOST_CHECK( renderPreviewParameters["properties"]["mode"]["enum"].dump().find(
                                     "native" ) != std::string::npos );
                const nlohmann::json& contextParameters =
                        toolByName["kisurf_get_context_snapshot"]["function"]["parameters"];
                BOOST_CHECK( contextParameters["required"].empty() );
                BOOST_CHECK( !contextParameters["additionalProperties"].get<bool>() );
                BOOST_CHECK( contextParameters["properties"].contains(
                        "include_visible_objects" ) );
                BOOST_CHECK( contextParameters["properties"].contains(
                        "include_selected_objects" ) );
                BOOST_CHECK( contextParameters["properties"].contains(
                        "include_actions" ) );
                BOOST_CHECK( contextParameters["properties"].contains(
                        "include_recent_activity" ) );
                BOOST_CHECK( contextParameters["properties"].contains(
                        "include_tool_state" ) );
                BOOST_CHECK( contextParameters["properties"].contains(
                        "include_anchors" ) );
                BOOST_CHECK( contextParameters["properties"].contains(
                        "include_panels" ) );
                BOOST_CHECK( contextParameters["properties"].contains( "include_visual" ) );
                BOOST_CHECK( contextParameters["properties"].contains( "max_objects" ) );
                BOOST_CHECK( contextParameters["properties"].contains( "max_actions" ) );
                BOOST_CHECK( contextParameters["properties"].contains( "max_activity" ) );
                BOOST_CHECK( contextParameters["properties"].contains( "max_anchors" ) );
                BOOST_CHECK( contextParameters["properties"].contains( "max_panels" ) );
                const nlohmann::json& visualParameters =
                        toolByName["kisurf_get_visual_frame"]["function"]["parameters"];
                BOOST_CHECK( visualParameters["required"].empty() );
                BOOST_CHECK( !visualParameters["additionalProperties"].get<bool>() );
                BOOST_CHECK( visualParameters["properties"].contains( "include_pixels" ) );
                BOOST_CHECK( visualParameters["properties"].contains( "max_bytes" ) );
                BOOST_CHECK( visualParameters["properties"].contains(
                        "include_anchor_overlays" ) );
                BOOST_CHECK( visualParameters["properties"].contains(
                        "max_anchor_overlays" ) );
                BOOST_CHECK( visualParameters["properties"].contains( "focus_layer" ) );
                BOOST_CHECK( visualParameters["properties"].contains( "focus_net" ) );
                BOOST_CHECK( visualParameters["properties"].contains(
                        "dim_unfocused_layers" ) );
                BOOST_CHECK( visualParameters["properties"].contains(
                        "highlight_anchor_ids" ) );
                const nlohmann::json& activityParameters =
                        toolByName["kisurf_get_activity_timeline"]["function"]["parameters"];
                BOOST_CHECK( activityParameters["required"].empty() );
                BOOST_CHECK( !activityParameters["additionalProperties"].get<bool>() );
                BOOST_CHECK( activityParameters["properties"].contains( "max_activity" ) );
                BOOST_CHECK( activityParameters["properties"].contains( "kind" ) );
                BOOST_CHECK( activityParameters["properties"].contains( "action_contains" ) );
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
                BOOST_CHECK_EQUAL( messages.at( 3 )["content"].get<std::string>(),
                                   "{\"allowed\":true,\"executed\":false}" );

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
