#include <boost/test/unit_test.hpp>
#include <kisurf/ai/ai_semantic_ui.h>

BOOST_AUTO_TEST_SUITE( AiSemanticUi )


BOOST_AUTO_TEST_CASE( RedactsSensitiveText )
{
    const wxString secret = wxString( wxS( "token: " ) ) + wxS( "abc123" )
                            + wxS( " " ) + wxS( "sk-" )
                            + wxS( "12345678901234567890" )
                            + wxS( " " ) + wxS( "OPENAI_API_KEY" )
                            + wxS( "=" ) + wxS( "secret-value" );

    const wxString redacted = RedactSemanticUiText( secret );

    BOOST_CHECK( !redacted.Contains( wxS( "12345678901234567890" ) ) );
    BOOST_CHECK( !redacted.Contains( wxS( "secret-value" ) ) );
    BOOST_CHECK( redacted.Contains( wxS( "redacted" ) ) );
}


BOOST_AUTO_TEST_CASE( TreeFindsNodeById )
{
    AI_SEMANTIC_UI_TREE tree;
    tree.m_FrameId = wxS( "agent" );
    tree.m_Nodes.push_back( { wxS( "agent.root" ), wxEmptyString, wxS( "panel" ),
                              wxS( "Agent" ) } );
    tree.m_Nodes.push_back( { wxS( "agent.send" ), wxS( "agent.root" ),
                              wxS( "button" ), wxS( "Send" ) } );

    const AI_SEMANTIC_UI_NODE* send = tree.FindNode( wxS( "agent.send" ) );

    BOOST_REQUIRE( send );
    BOOST_CHECK_EQUAL( send->m_Label, wxString( wxS( "Send" ) ) );
    BOOST_CHECK( tree.FindNode( wxS( "missing" ) ) == nullptr );
}


BOOST_AUTO_TEST_SUITE_END()
