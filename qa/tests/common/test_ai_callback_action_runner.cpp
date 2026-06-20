#include <boost/test/unit_test.hpp>

#include <kisurf/ai/ai_callback_action_runner.h>


BOOST_AUTO_TEST_SUITE( AiCallbackActionRunner )


BOOST_AUTO_TEST_CASE( EmptyCallbackFailsCleanly )
{
    AI_CALLBACK_ACTION_RUNNER runner{ AI_CALLBACK_ACTION_RUNNER::ACTION_CALLBACK() };
    wxString                  error;

    BOOST_CHECK( !runner.RunActionByName( wxS( "common.Control.showAgentPanel" ), error ) );
    BOOST_CHECK( error.Contains( wxS( "callback" ) ) );
}


BOOST_AUTO_TEST_CASE( CallbackReceivesActionNameAndCanSucceed )
{
    wxString receivedAction;
    AI_CALLBACK_ACTION_RUNNER runner(
            [&]( const wxString& aActionName, wxString& )
            {
                receivedAction = aActionName;
                return true;
            } );
    wxString error;

    BOOST_CHECK( runner.RunActionByName( wxS( "common.Control.showAgentPanel" ), error ) );
    BOOST_CHECK_EQUAL( receivedAction, wxString( wxS( "common.Control.showAgentPanel" ) ) );
    BOOST_CHECK( error.IsEmpty() );
}


BOOST_AUTO_TEST_CASE( CallbackFailurePreservesError )
{
    AI_CALLBACK_ACTION_RUNNER runner(
            []( const wxString&, wxString& aError )
            {
                aError = wxS( "Action was not handled." );
                return false;
            } );
    wxString error;

    BOOST_CHECK( !runner.RunActionByName( wxS( "common.Control.showAgentPanel" ), error ) );
    BOOST_CHECK_EQUAL( error, wxString( wxS( "Action was not handled." ) ) );
}


BOOST_AUTO_TEST_SUITE_END()
