#include <boost/test/unit_test.hpp>
#include <tool/actions.h>

BOOST_AUTO_TEST_SUITE( AiAgentAction )


BOOST_AUTO_TEST_CASE( SharedAgentActionHasStableContract )
{
    BOOST_CHECK_EQUAL( ACTIONS::showAgentPanel.GetName(), "common.Control.showAgentPanel" );
    BOOST_CHECK_EQUAL( ACTIONS::showAgentPanel.GetScope(), AS_GLOBAL );
    BOOST_CHECK( ACTIONS::showAgentPanel.CheckToolbarState( TOOLBAR_STATE::TOGGLE ) );
}


BOOST_AUTO_TEST_SUITE_END()
