#include <boost/test/unit_test.hpp>

#include <kisurf_ai_agent_panel_ui.h>


BOOST_AUTO_TEST_SUITE( AiAgentPanelUi )

BOOST_AUTO_TEST_CASE( SavedPaneWidthIsRestoredOnlyWhenPositive )
{
    BOOST_TEST( !KisurfShouldRestoreAgentPaneWidth( -1 ) );
    BOOST_TEST( !KisurfShouldRestoreAgentPaneWidth( 0 ) );
    BOOST_TEST( KisurfShouldRestoreAgentPaneWidth( 260 ) );
}

BOOST_AUTO_TEST_SUITE_END()
