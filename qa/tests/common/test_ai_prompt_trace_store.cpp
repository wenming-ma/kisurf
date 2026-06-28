#include <boost/test/unit_test.hpp>
#include <kisurf/ai/ai_provider_input_compiler.h>
#include <kisurf/ai/ai_prompt_trace_store.h>

#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/utils.h>

BOOST_AUTO_TEST_SUITE( AiPromptTraceStore )

namespace
{
wxString uniquePromptTracePath( const wxString& aSuffix )
{
    wxString base = wxFileName::CreateTempFileName( wxS( "kst" ) );

    if( wxFileExists( base ) )
        wxRemoveFile( base );

    wxFileName path( base );
    path.SetFullName( wxString::Format( wxS( "kisurf_prompt_trace_%s_%lu.jsonl" ),
                                        aSuffix,
                                        static_cast<unsigned long>( wxGetProcessId() ) ) );

    if( wxFileExists( path.GetFullPath() ) )
        wxRemoveFile( path.GetFullPath() );

    return path.GetFullPath();
}


AI_PROVIDER_REQUEST compiledTraceRequest( uint64_t aRequestId )
{
    AI_PROVIDER_REQUEST request;
    request.m_RequestId = aRequestId;
    request.m_ConversationId = 7;
    request.m_UserText = wxString::Format( wxS( "trace request %llu" ),
                                           static_cast<unsigned long long>( aRequestId ) );

    return AiCompileProviderInput( request );
}
}


BOOST_AUTO_TEST_CASE( AppendAndLoadCompiledProviderRequestTrace )
{
    wxString path = uniquePromptTracePath( wxS( "append" ) );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 101;
    request.m_ConversationId = 7;
    request.m_UserText = wxS( "inspect USB rule" );

    AI_PROVIDER_INPUT_BLOCK memory;
    memory.m_Id = wxS( "memory.rule.usb-clearance" );
    memory.m_Kind = wxS( "retrieved_memory" );
    memory.m_Source = wxS( "local_text_memory" );
    memory.m_Text = wxS( "USB clearance memory" );
    request.m_RetrievedMemoryBlocks.push_back( memory );

    AI_PROVIDER_REQUEST compiled = AiCompileProviderInput( request );

    AI_PROMPT_TRACE_STORE store( path );
    wxString error;
    BOOST_REQUIRE( store.Append( compiled, wxS( "sent" ), error ) );

    std::vector<AI_PROMPT_TRACE_ENTRY> entries = store.LoadAll( error );

    BOOST_REQUIRE_EQUAL( entries.size(), 1 );
    BOOST_CHECK_EQUAL( entries.front().m_RequestId, 101 );
    BOOST_CHECK_EQUAL( entries.front().m_ConversationId, 7 );
    BOOST_CHECK_EQUAL( entries.front().m_ProviderStatus, wxString( wxS( "sent" ) ) );
    BOOST_CHECK( entries.front().m_PromptTraceJson.Contains(
            wxS( "memory.rule.usb-clearance" ) ) );

    if( wxFileExists( path ) )
        wxRemoveFile( path );
}


BOOST_AUTO_TEST_CASE( RejectsUncompiledRequestWithoutPromptTrace )
{
    wxString path = uniquePromptTracePath( wxS( "reject" ) );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 102;
    request.m_UserText = wxS( "raw request" );

    AI_PROMPT_TRACE_STORE store( path );
    wxString error;

    BOOST_CHECK( !store.Append( request, wxS( "sent" ), error ) );
    BOOST_CHECK( error.Contains( wxS( "compiled" ) ) );

    if( wxFileExists( path ) )
        wxRemoveFile( path );
}


BOOST_AUTO_TEST_CASE( DefaultPathUsesAiPromptTraceJsonl )
{
    wxFileName path( AI_PROMPT_TRACE_STORE::DefaultPath() );

    BOOST_CHECK_EQUAL( path.GetFullName(), wxString( wxS( "prompt_trace.jsonl" ) ) );
    BOOST_CHECK( path.GetPath().Contains( wxS( "ai" ) ) );
}


BOOST_AUTO_TEST_CASE( RetentionKeepsNewestTraceEntries )
{
    wxString path = uniquePromptTracePath( wxS( "retention" ) );

    AI_PROMPT_TRACE_RETENTION_POLICY policy;
    policy.m_MaxEntries = 2;

    AI_PROMPT_TRACE_STORE store( path, policy );
    wxString              error;

    BOOST_REQUIRE( store.Append( compiledTraceRequest( 201 ), wxS( "sent" ), error ) );
    BOOST_REQUIRE( store.Append( compiledTraceRequest( 202 ), wxS( "sent" ), error ) );
    BOOST_REQUIRE( store.Append( compiledTraceRequest( 203 ), wxS( "sent" ), error ) );

    std::vector<AI_PROMPT_TRACE_ENTRY> entries = store.LoadAll( error );

    BOOST_REQUIRE_EQUAL( entries.size(), 2 );
    BOOST_CHECK_EQUAL( entries.at( 0 ).m_RequestId, 202 );
    BOOST_CHECK_EQUAL( entries.at( 1 ).m_RequestId, 203 );

    if( wxFileExists( path ) )
        wxRemoveFile( path );
}


BOOST_AUTO_TEST_CASE( AppendPersistsProviderTraceJson )
{
    wxString path = uniquePromptTracePath( wxS( "provider_trace" ) );

    AI_PROMPT_TRACE_STORE store( path );
    wxString              error;
    AI_PROVIDER_REQUEST   request = compiledTraceRequest( 301 );
    wxString              providerTrace =
            wxS( "{\"retry_history\":[{\"reason\":\"context_limit\","
                 "\"action\":\"shrunk_retry\"}]}" );

    BOOST_REQUIRE( store.Append( request, wxS( "provider_response" ),
                                 providerTrace, error ) );

    std::vector<AI_PROMPT_TRACE_ENTRY> entries = store.LoadAll( error );

    BOOST_REQUIRE_EQUAL( entries.size(), 1 );
    BOOST_CHECK( entries.front().m_ProviderTraceJson.Contains(
            wxS( "retry_history" ) ) );
    BOOST_CHECK( entries.front().m_ProviderTraceJson.Contains(
            wxS( "shrunk_retry" ) ) );

    if( wxFileExists( path ) )
        wxRemoveFile( path );
}


BOOST_AUTO_TEST_SUITE_END()
