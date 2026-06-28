#include <boost/test/unit_test.hpp>
#include <kisurf/ai/ai_local_text_memory.h>
#include <kisurf/ai/ai_memory_store.h>

#include <wx/filefn.h>
#include <wx/filename.h>


BOOST_AUTO_TEST_SUITE( AiMemoryStore )


static wxString uniqueMemoryStorePath( const wxString& aPrefix )
{
    wxString path = wxFileName::CreateTempFileName( aPrefix );
    wxRemoveFile( path );
    return path;
}


BOOST_AUTO_TEST_CASE( AppendLoadAndQueryDurableTypedRecords )
{
    wxString path = uniqueMemoryStorePath( wxS( "kisurf_ai_memory_store_query" ) );

    AI_MEMORY_STORE store( path );

    AI_MEMORY_RECORD accepted;
    accepted.m_Id = wxS( "accepted-edit-1" );
    accepted.m_ProjectId = wxS( "project-a" );
    accepted.m_DocumentId = wxS( "board-1" );
    accepted.m_AgentKind = wxS( "chat" );
    accepted.m_Type = wxS( "accepted_edit" );
    accepted.m_Text = wxS( "Accepted edit: placed GND via stitching around U3" );
    accepted.m_Source = wxS( "accept_replay" );
    accepted.m_ProvenanceJson = wxS( "{\"preview_id\":\"preview-1\"}" );
    accepted.m_BoardStateVersion = wxS( "rev-42" );
    accepted.m_AcceptanceState = wxS( "accepted" );
    accepted.m_ObjectIds.push_back( wxS( "U3" ) );
    accepted.m_TrustLevel = 90;
    accepted.m_Sequence = 10;
    accepted.m_CreatedAtUnixSeconds = 1000;
    accepted.m_ExpiresAtUnixSeconds = 2000;

    AI_MEMORY_RECORD lowTrust = accepted;
    lowTrust.m_Id = wxS( "low-trust" );
    lowTrust.m_TrustLevel = 30;
    lowTrust.m_Sequence = 11;

    AI_MEMORY_RECORD expired = accepted;
    expired.m_Id = wxS( "expired" );
    expired.m_ExpiresAtUnixSeconds = 1200;
    expired.m_Sequence = 12;

    AI_MEMORY_RECORD wrongObject = accepted;
    wrongObject.m_Id = wxS( "wrong-object" );
    wrongObject.m_ObjectIds.clear();
    wrongObject.m_ObjectIds.push_back( wxS( "U7" ) );
    wrongObject.m_Sequence = 13;

    wxString error;
    BOOST_REQUIRE( store.Append( accepted, error ) );
    BOOST_REQUIRE( store.Append( lowTrust, error ) );
    BOOST_REQUIRE( store.Append( expired, error ) );
    BOOST_REQUIRE( store.Append( wrongObject, error ) );

    AI_MEMORY_STORE reloaded( path );
    AI_MEMORY_QUERY query;
    query.m_ProjectId = wxS( "project-a" );
    query.m_DocumentId = wxS( "board-1" );
    query.m_AgentKind = wxS( "chat" );
    query.m_Type = wxS( "accepted_edit" );
    query.m_AcceptanceState = wxS( "accepted" );
    query.m_ObjectIds.push_back( wxS( "U3" ) );
    query.m_MinTrustLevel = 80;
    query.m_NowUnixSeconds = 1500;

    std::vector<AI_MEMORY_RECORD> records = reloaded.Query( query, error );

    BOOST_REQUIRE_EQUAL( records.size(), 1 );
    BOOST_CHECK_EQUAL( records.front().m_Id, wxString( wxS( "accepted-edit-1" ) ) );
    BOOST_CHECK_EQUAL( records.front().m_Source, wxString( wxS( "accept_replay" ) ) );
    BOOST_CHECK_EQUAL( records.front().m_ProvenanceJson,
                       wxString( wxS( "{\"preview_id\":\"preview-1\"}" ) ) );
    BOOST_CHECK_EQUAL( records.front().m_TrustLevel, 90 );

    wxRemoveFile( path );
}


BOOST_AUTO_TEST_CASE( RetentionKeepsNewestDurableRecords )
{
    wxString path = uniqueMemoryStorePath( wxS( "kisurf_ai_memory_store_retention" ) );

    AI_MEMORY_RETENTION_POLICY retention;
    retention.m_MaxRecords = 2;

    AI_MEMORY_STORE store( path, retention );

    wxString error;

    for( int i = 1; i <= 3; ++i )
    {
        AI_MEMORY_RECORD record;
        record.m_Id = wxString::Format( wxS( "record-%d" ), i );
        record.m_ProjectId = wxS( "project-a" );
        record.m_DocumentId = wxS( "board-1" );
        record.m_Type = wxS( "rule_memory" );
        record.m_Text = wxString::Format( wxS( "Rule memory %d" ), i );
        record.m_Sequence = static_cast<uint64_t>( i );
        record.m_TrustLevel = 70;
        BOOST_REQUIRE( store.Append( record, error ) );
    }

    AI_MEMORY_QUERY query;
    query.m_ProjectId = wxS( "project-a" );
    query.m_DocumentId = wxS( "board-1" );

    std::vector<AI_MEMORY_RECORD> records = store.Query( query, error );

    BOOST_REQUIRE_EQUAL( records.size(), 2 );
    BOOST_CHECK_EQUAL( records.at( 0 ).m_Id, wxString( wxS( "record-3" ) ) );
    BOOST_CHECK_EQUAL( records.at( 1 ).m_Id, wxString( wxS( "record-2" ) ) );

    wxRemoveFile( path );
}


BOOST_AUTO_TEST_CASE( DefaultPathUsesAiMemoryJsonl )
{
    wxString path = AI_MEMORY_STORE::DefaultPath();

    BOOST_CHECK( path.Contains( wxS( "ai" ) ) );
    BOOST_CHECK( path.EndsWith( wxS( "memory.jsonl" ) ) );
}


BOOST_AUTO_TEST_CASE( ExportToLocalTextIndexPreservesSearchMetadata )
{
    wxString path = uniqueMemoryStorePath( wxS( "kisurf_ai_memory_store_export" ) );

    AI_MEMORY_STORE store( path );

    AI_MEMORY_RECORD rule;
    rule.m_Id = wxS( "rule-1" );
    rule.m_ProjectId = wxS( "project-a" );
    rule.m_DocumentId = wxS( "board-1" );
    rule.m_AgentKind = wxS( "shared" );
    rule.m_Type = wxS( "rule_memory" );
    rule.m_Text = wxS( "USB differential pair clearance is 0.20 mm" );
    rule.m_Source = wxS( "design_rules" );
    rule.m_ProvenanceJson = wxS( "{\"rule_id\":\"usb-clearance\"}" );
    rule.m_AcceptanceState = wxS( "accepted" );
    rule.m_ObjectIds.push_back( wxS( "Net-(USB_D+)" ) );
    rule.m_TrustLevel = 95;
    rule.m_Sequence = 5;

    wxString error;
    BOOST_REQUIRE( store.Append( rule, error ) );

    AI_MEMORY_QUERY exportQuery;
    exportQuery.m_ProjectId = wxS( "project-a" );
    exportQuery.m_DocumentId = wxS( "board-1" );
    exportQuery.m_MinTrustLevel = 90;

    AI_LOCAL_TEXT_MEMORY_INDEX index;
    BOOST_REQUIRE( store.ExportToLocalTextIndex( index, exportQuery, error ) );

    AI_LOCAL_TEXT_MEMORY_QUERY search;
    search.m_Text = wxS( "USB clearance" );
    search.m_ProjectId = wxS( "project-a" );
    search.m_DocumentId = wxS( "board-1" );
    search.m_ObjectIds.push_back( wxS( "Net-(USB_D+)" ) );

    std::vector<AI_LOCAL_TEXT_MEMORY_RESULT> results = index.Search( search, 4 );

    BOOST_REQUIRE_EQUAL( results.size(), 1 );
    BOOST_CHECK_EQUAL( results.front().m_Record.m_Id, wxString( wxS( "rule-1" ) ) );
    BOOST_CHECK_EQUAL( results.front().m_Record.m_Source, wxString( wxS( "design_rules" ) ) );
    BOOST_CHECK_EQUAL( results.front().m_Record.m_ProvenanceJson,
                       wxString( wxS( "{\"rule_id\":\"usb-clearance\"}" ) ) );
    BOOST_CHECK_EQUAL( results.front().m_Record.m_TrustLevel, 95 );

    wxRemoveFile( path );
}


BOOST_AUTO_TEST_SUITE_END()
