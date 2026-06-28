#include <boost/test/unit_test.hpp>
#include <json_common.h>
#include <kisurf/ai/ai_local_text_memory.h>

#include <wx/filename.h>
#include <wx/ffile.h>

BOOST_AUTO_TEST_SUITE( AiLocalTextMemory )

namespace
{
void writeTextFile( const wxString& aPath, const wxString& aText )
{
    wxFFile file( aPath, wxS( "wb" ) );
    BOOST_REQUIRE( file.IsOpened() );
    file.Write( aText );
}
}


BOOST_AUTO_TEST_CASE( SearchAppliesProjectDocumentAgentAndTypeFilters )
{
    AI_LOCAL_TEXT_MEMORY_INDEX index;

    AI_LOCAL_TEXT_MEMORY_RECORD accepted;
    accepted.m_Id = wxS( "accepted-edit-1" );
    accepted.m_ProjectId = wxS( "project-a" );
    accepted.m_DocumentId = wxS( "board-1" );
    accepted.m_AgentKind = wxS( "chat" );
    accepted.m_Type = wxS( "accepted_edit" );
    accepted.m_Text = wxS( "Accepted edit: GND via stitching around regulator" );
    accepted.m_Sequence = 10;
    index.AddRecord( accepted );

    AI_LOCAL_TEXT_MEMORY_RECORD wrongProject = accepted;
    wrongProject.m_Id = wxS( "wrong-project" );
    wrongProject.m_ProjectId = wxS( "project-b" );
    wrongProject.m_Text = wxS( "GND via stitching in another project" );
    wrongProject.m_Sequence = 11;
    index.AddRecord( wrongProject );

    AI_LOCAL_TEXT_MEMORY_RECORD wrongAgent = accepted;
    wrongAgent.m_Id = wxS( "wrong-agent" );
    wrongAgent.m_AgentKind = wxS( "nextaction" );
    wrongAgent.m_Text = wxS( "GND via stitching from background preview" );
    wrongAgent.m_Sequence = 12;
    index.AddRecord( wrongAgent );

    AI_LOCAL_TEXT_MEMORY_QUERY query;
    query.m_Text = wxS( "GND via stitching" );
    query.m_ProjectId = wxS( "project-a" );
    query.m_DocumentId = wxS( "board-1" );
    query.m_AgentKind = wxS( "chat" );
    query.m_Types.push_back( wxS( "accepted_edit" ) );

    std::vector<AI_LOCAL_TEXT_MEMORY_RESULT> results = index.Search( query, 8 );

    BOOST_REQUIRE_EQUAL( results.size(), 1 );
    BOOST_CHECK_EQUAL( results.front().m_Record.m_Id,
                       wxString( wxS( "accepted-edit-1" ) ) );
    BOOST_CHECK_GT( results.front().m_Score, 0 );
}


BOOST_AUTO_TEST_CASE( SearchRanksLexicalMatchesBeforeRecency )
{
    AI_LOCAL_TEXT_MEMORY_INDEX index;

    AI_LOCAL_TEXT_MEMORY_RECORD olderExact;
    olderExact.m_Id = wxS( "older-exact" );
    olderExact.m_ProjectId = wxS( "project-a" );
    olderExact.m_DocumentId = wxS( "board-1" );
    olderExact.m_Type = wxS( "validation_fact" );
    olderExact.m_Text = wxS( "DRC fact: GND clearance violation near U3" );
    olderExact.m_Sequence = 5;
    index.AddRecord( olderExact );

    AI_LOCAL_TEXT_MEMORY_RECORD newerPartial = olderExact;
    newerPartial.m_Id = wxS( "newer-partial" );
    newerPartial.m_Text = wxS( "Recent fact: GND topology changed" );
    newerPartial.m_Sequence = 100;
    index.AddRecord( newerPartial );

    AI_LOCAL_TEXT_MEMORY_RECORD newestUnrelated = olderExact;
    newestUnrelated.m_Id = wxS( "newest-unrelated" );
    newestUnrelated.m_Text = wxS( "Recent fact: silkscreen text moved" );
    newestUnrelated.m_Sequence = 200;
    index.AddRecord( newestUnrelated );

    AI_LOCAL_TEXT_MEMORY_QUERY query;
    query.m_Text = wxS( "GND clearance" );
    query.m_ProjectId = wxS( "project-a" );
    query.m_DocumentId = wxS( "board-1" );

    std::vector<AI_LOCAL_TEXT_MEMORY_RESULT> results = index.Search( query, 4 );

    BOOST_REQUIRE_EQUAL( results.size(), 2 );
    BOOST_CHECK_EQUAL( results.at( 0 ).m_Record.m_Id,
                       wxString( wxS( "older-exact" ) ) );
    BOOST_CHECK_EQUAL( results.at( 1 ).m_Record.m_Id,
                       wxString( wxS( "newer-partial" ) ) );
    BOOST_CHECK_GT( results.at( 0 ).m_Score, results.at( 1 ).m_Score );
}


BOOST_AUTO_TEST_CASE( LoadJsonlFileIndexesLocalTextArtifacts )
{
    wxString path = wxFileName::CreateTempFileName( wxS( "kisurf_ai_memory" ) );

    {
        wxFFile file( path, wxS( "wb" ) );
        BOOST_REQUIRE( file.IsOpened() );
        file.Write( wxS( "{\"id\":\"rule-1\",\"project_id\":\"project-a\","
                         "\"document_id\":\"board-1\",\"agent_kind\":\"shared\","
                         "\"type\":\"rule_memory\",\"text\":\"Netclass USB clearance is 0.20 mm\","
                         "\"sequence\":7}\n" ) );
        file.Write( wxS( "{\"id\":\"rule-2\",\"project_id\":\"project-b\","
                         "\"document_id\":\"board-2\",\"agent_kind\":\"shared\","
                         "\"type\":\"rule_memory\",\"text\":\"Unrelated board rule\","
                         "\"sequence\":9}\n" ) );
    }

    AI_LOCAL_TEXT_MEMORY_INDEX index;
    wxString error;
    BOOST_REQUIRE( index.LoadJsonlFile( path, error ) );

    AI_LOCAL_TEXT_MEMORY_QUERY query;
    query.m_Text = wxS( "USB clearance" );
    query.m_ProjectId = wxS( "project-a" );
    query.m_DocumentId = wxS( "board-1" );

    std::vector<AI_LOCAL_TEXT_MEMORY_RESULT> results = index.Search( query, 4 );

    BOOST_REQUIRE_EQUAL( results.size(), 1 );
    BOOST_CHECK_EQUAL( results.front().m_Record.m_Id, wxString( wxS( "rule-1" ) ) );

    wxRemoveFile( path );
}


BOOST_AUTO_TEST_CASE( LoadPlainTextFileIndexesLocalProjectResearch )
{
    wxString path = wxFileName::CreateTempFileName( wxS( "kisurf_ai_text" ) );

    {
        wxFFile file( path, wxS( "wb" ) );
        BOOST_REQUIRE( file.IsOpened() );
        file.Write( wxS( "# Routing preference\n"
                         "Use a 45 degree escape route for USB differential pair DP/DM "
                         "near connector J2.\n" ) );
    }

    AI_LOCAL_TEXT_MEMORY_INDEX index;
    AI_LOCAL_TEXT_FILE_RECORD_OPTIONS options;
    options.m_ProjectId = wxS( "project-a" );
    options.m_DocumentId = wxS( "board-1" );
    options.m_AgentKind = wxS( "shared" );
    options.m_Type = wxS( "project_research" );
    options.m_Source = wxS( "local_file" );
    options.m_AcceptanceState = wxS( "accepted" );
    options.m_TrustLevel = 80;
    options.m_Sequence = 55;

    wxString error;
    BOOST_REQUIRE( index.LoadTextFile( path, options, error ) );

    AI_LOCAL_TEXT_MEMORY_QUERY query;
    query.m_Text = wxS( "USB differential escape route" );
    query.m_ProjectId = wxS( "project-a" );
    query.m_DocumentId = wxS( "board-1" );
    query.m_Types.push_back( wxS( "project_research" ) );
    query.m_AcceptanceState = wxS( "accepted" );

    std::vector<AI_LOCAL_TEXT_MEMORY_RESULT> results = index.Search( query, 4 );

    BOOST_REQUIRE_EQUAL( results.size(), 1 );
    BOOST_CHECK_EQUAL( results.front().m_Record.m_ProjectId,
                       wxString( wxS( "project-a" ) ) );
    BOOST_CHECK_EQUAL( results.front().m_Record.m_Type,
                       wxString( wxS( "project_research" ) ) );
    BOOST_CHECK_EQUAL( results.front().m_Record.m_Source,
                       wxString( wxS( "local_file" ) ) );
    BOOST_CHECK_EQUAL( results.front().m_Record.m_TrustLevel, 80 );
    BOOST_CHECK_EQUAL( results.front().m_Record.m_Sequence, 55 );
    BOOST_CHECK( results.front().m_Record.m_Text.Contains( wxS( "DP/DM" ) ) );
    nlohmann::json provenance = nlohmann::json::parse(
            results.front().m_Record.m_ProvenanceJson.ToStdString() );
    BOOST_CHECK_EQUAL( provenance["kind"].get<std::string>(),
                       "local_text_file" );
    BOOST_CHECK_EQUAL( provenance["path"].get<std::string>(), path.ToStdString() );
    BOOST_CHECK( results.front().m_Record.m_Id.Contains(
            wxS( "local-file:" ) ) );

    wxRemoveFile( path );
}


BOOST_AUTO_TEST_CASE( LoadTextDirectoryRecursivelyIndexesResearchFiles )
{
    wxString root = wxFileName::CreateTempFileName( wxS( "kisurf_ai_research_dir" ) );
    wxRemoveFile( root );
    BOOST_REQUIRE( wxFileName::Mkdir( root ) );

    wxFileName nestedDir( root, wxEmptyString );
    nestedDir.AppendDir( wxS( "nested" ) );
    BOOST_REQUIRE( wxFileName::Mkdir( nestedDir.GetPath() ) );

    wxFileName usbFile( root, wxS( "usb-routing.md" ) );
    wxFileName powerFile( nestedDir.GetPath(), wxS( "power.txt" ) );
    wxFileName ignoredFile( root, wxS( "ignored.bin" ) );

    writeTextFile( usbFile.GetFullPath(),
                   wxS( "# USB routing\n"
                        "RESEARCH_FOLDER_USB_NEEDLE use symmetric DP DM escape.\n" ) );
    writeTextFile( powerFile.GetFullPath(),
                   wxS( "RESEARCH_FOLDER_POWER_NEEDLE keep regulator loop compact.\n" ) );
    writeTextFile( ignoredFile.GetFullPath(),
                   wxS( "RESEARCH_FOLDER_IGNORED_NEEDLE should not be indexed.\n" ) );

    AI_LOCAL_TEXT_MEMORY_INDEX index;
    AI_LOCAL_TEXT_FILE_RECORD_OPTIONS options;
    options.m_ProjectId = wxS( "project-a" );
    options.m_DocumentId = wxS( "board-1" );
    options.m_AgentKind = wxS( "shared" );
    options.m_Type = wxS( "project_research" );
    options.m_Source = wxS( "research_folder" );
    options.m_AcceptanceState = wxS( "accepted" );
    options.m_TrustLevel = 75;
    options.m_Sequence = 100;

    wxString error;
    BOOST_REQUIRE( index.LoadTextDirectory( root, options, error ) );
    BOOST_CHECK_EQUAL( index.Records().size(), 2 );

    AI_LOCAL_TEXT_MEMORY_QUERY usbQuery;
    usbQuery.m_Text = wxS( "RESEARCH_FOLDER_USB_NEEDLE" );
    usbQuery.m_ProjectId = wxS( "project-a" );
    usbQuery.m_DocumentId = wxS( "board-1" );
    usbQuery.m_Types.push_back( wxS( "project_research" ) );

    std::vector<AI_LOCAL_TEXT_MEMORY_RESULT> usbResults = index.Search( usbQuery, 4 );
    BOOST_REQUIRE_EQUAL( usbResults.size(), 1 );
    BOOST_CHECK_EQUAL( usbResults.front().m_Record.m_Source,
                       wxString( wxS( "research_folder" ) ) );
    BOOST_CHECK_EQUAL( usbResults.front().m_Record.m_TrustLevel, 75 );

    AI_LOCAL_TEXT_MEMORY_QUERY powerQuery = usbQuery;
    powerQuery.m_Text = wxS( "RESEARCH_FOLDER_POWER_NEEDLE" );
    BOOST_CHECK_EQUAL( index.Search( powerQuery, 4 ).size(), 1 );

    AI_LOCAL_TEXT_MEMORY_QUERY ignoredQuery = usbQuery;
    ignoredQuery.m_Text = wxS( "RESEARCH_FOLDER_IGNORED_NEEDLE" );
    BOOST_CHECK( index.Search( ignoredQuery, 4 ).empty() );

    writeTextFile( usbFile.GetFullPath(),
                   wxS( "# USB routing\n"
                        "RESEARCH_FOLDER_USB_UPDATED_NEEDLE keep DP DM length matched.\n" ) );
    wxRemoveFile( powerFile.GetFullPath() );

    BOOST_REQUIRE( index.LoadTextDirectory( root, options, error ) );
    BOOST_CHECK_EQUAL( index.Records().size(), 1 );

    AI_LOCAL_TEXT_MEMORY_QUERY updatedQuery = usbQuery;
    updatedQuery.m_Text = wxS( "RESEARCH_FOLDER_USB_UPDATED_NEEDLE" );
    BOOST_CHECK_EQUAL( index.Search( updatedQuery, 4 ).size(), 1 );

    AI_LOCAL_TEXT_MEMORY_QUERY staleQuery = usbQuery;
    staleQuery.m_Text = wxS( "RESEARCH_FOLDER_POWER_NEEDLE" );
    BOOST_CHECK( index.Search( staleQuery, 4 ).empty() );

    wxFileName::Rmdir( root, wxPATH_RMDIR_RECURSIVE );
}


BOOST_AUTO_TEST_SUITE_END()
