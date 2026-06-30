#include <boost/test/unit_test.hpp>

#include <kisurf_ai_agent_panel_ui.h>

#include <wx/aui/aui.h>


BOOST_AUTO_TEST_SUITE( AiAgentPanelUi )

BOOST_AUTO_TEST_CASE( SavedPaneWidthIsRestoredOnlyWhenPositive )
{
    BOOST_TEST( !KisurfShouldRestoreAgentPaneWidth( -1 ) );
    BOOST_TEST( !KisurfShouldRestoreAgentPaneWidth( 0 ) );
    BOOST_TEST( KisurfShouldRestoreAgentPaneWidth( 260 ) );
}


BOOST_AUTO_TEST_CASE( VisiblePaneIsRestoredToDefaultRightDock )
{
    wxAuiPaneInfo pane;
    pane.Float().FloatingPosition( wxPoint( 4000, 4000 ) ).Left().Layer( 0 )
            .Position( 7 ).Hide();

    KisurfPrepareVisibleAgentPane( pane );

    BOOST_TEST( pane.IsShown() );
    BOOST_TEST( pane.IsDocked() );
    BOOST_TEST( pane.dock_direction == wxAUI_DOCK_RIGHT );
    BOOST_TEST( pane.dock_layer == 5 );
    BOOST_TEST( pane.dock_pos == 1 );
}

BOOST_AUTO_TEST_SUITE_END()
