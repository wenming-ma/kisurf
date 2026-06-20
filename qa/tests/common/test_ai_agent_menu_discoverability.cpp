#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>

#ifndef QA_SRC_ROOT
#error QA_SRC_ROOT must be defined for AI agent menu discoverability tests.
#endif

BOOST_AUTO_TEST_SUITE( AiAgentMenuDiscoverability )


static std::string readSourceFile( const std::string& aRelativePath )
{
    const std::string path = std::string( QA_SRC_ROOT ) + "/" + aRelativePath;
    std::ifstream     in( path, std::ios::binary );

    BOOST_REQUIRE_MESSAGE( in.good(), "Unable to read source file: " << path );

    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}


static std::string withoutWhitespace( std::string aText )
{
    aText.erase( std::remove_if( aText.begin(), aText.end(),
                                 []( unsigned char aChar )
                                 {
                                     return std::isspace( aChar ) != 0;
                                 } ),
                 aText.end() );

    return aText;
}


static bool containsIgnoringWhitespace( const std::string& aSource, const std::string& aNeedle )
{
    return withoutWhitespace( aSource ).find( withoutWhitespace( aNeedle ) )
           != std::string::npos;
}


static void requireTopLevelAiAgentMenu( const std::string& aSource, const char* aEditorName )
{
    BOOST_TEST_CONTEXT( aEditorName )
    {
        BOOST_CHECK( containsIgnoringWhitespace( aSource,
                                                 "aiMenu->SetTitle( _( \"AI\" ) );" ) );
        BOOST_CHECK( containsIgnoringWhitespace(
                aSource, "aiMenu->Add( ACTIONS::showAgentPanel, ACTION_MENU::CHECK );" ) );
        BOOST_CHECK( containsIgnoringWhitespace(
                aSource, "aiMenu->Add( ACTIONS::showAiModelSettings );" ) );
        BOOST_CHECK( containsIgnoringWhitespace( aSource,
                                                 "menuBar->Append( aiMenu, _( \"&AI\" ) );" ) );
    }
}


BOOST_AUTO_TEST_CASE( PcbAndSchematicExposeTopLevelAiAgentEntry )
{
    requireTopLevelAiAgentMenu( readSourceFile( "pcbnew/menubar_pcb_editor.cpp" ),
                                "PCB editor" );
    requireTopLevelAiAgentMenu( readSourceFile( "eeschema/menubar.cpp" ),
                                "Schematic editor" );
}


BOOST_AUTO_TEST_CASE( PcbAndSchematicBindTopLevelAiModelSettingsEntry )
{
    const std::string actionsHeader = readSourceFile( "include/tool/actions.h" );
    const std::string actionsSource = readSourceFile( "common/tool/actions.cpp" );
    const std::string pcbControlHeader =
            readSourceFile( "pcbnew/tools/board_editor_control.h" );
    const std::string pcbControlSource =
            readSourceFile( "pcbnew/tools/board_editor_control.cpp" );
    const std::string schControlHeader =
            readSourceFile( "eeschema/tools/sch_editor_control.h" );
    const std::string schControlSource =
            readSourceFile( "eeschema/tools/sch_editor_control.cpp" );

    BOOST_CHECK( containsIgnoringWhitespace( actionsHeader,
                                             "static TOOL_ACTION showAiModelSettings;" ) );
    BOOST_CHECK( containsIgnoringWhitespace(
            actionsSource,
            "TOOL_ACTION ACTIONS::showAiModelSettings( TOOL_ACTION_ARGS()" ) );
    BOOST_CHECK( actionsSource.find( "common.Control.showAiModelSettings" )
                 != std::string::npos );
    BOOST_CHECK( actionsSource.find( "Model Settings..." ) != std::string::npos );

    BOOST_CHECK( containsIgnoringWhitespace(
            pcbControlHeader,
            "int ShowAiModelSettings( const TOOL_EVENT& aEvent );" ) );
    BOOST_CHECK( containsIgnoringWhitespace(
            pcbControlSource,
            "Go( &BOARD_EDITOR_CONTROL::ShowAiModelSettings, "
            "ACTIONS::showAiModelSettings.MakeEvent() );" ) );
    BOOST_CHECK( containsIgnoringWhitespace(
            schControlHeader,
            "int ShowAiModelSettings( const TOOL_EVENT& aEvent );" ) );
    BOOST_CHECK( containsIgnoringWhitespace(
            schControlSource,
            "Go( &SCH_EDITOR_CONTROL::ShowAiModelSettings, "
            "ACTIONS::showAiModelSettings.MakeEvent() );" ) );
}


BOOST_AUTO_TEST_SUITE_END()
