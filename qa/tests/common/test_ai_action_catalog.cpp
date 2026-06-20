#include <boost/test/unit_test.hpp>

#include <kisurf/ai/ai_action_catalog.h>

#include <tool/action_manager.h>
#include <tool/actions.h>

#include <algorithm>

BOOST_AUTO_TEST_SUITE( AiActionCatalog )


BOOST_AUTO_TEST_CASE( DescribesKnownToolAction )
{
    AI_ACTION_DESCRIPTOR descriptor =
            AI_ACTION_CATALOG::DescribeAction( ACTIONS::showAgentPanel, AI_EDITOR_KIND::Pcb );

    BOOST_CHECK( descriptor.IsValid() );
    BOOST_CHECK_EQUAL( descriptor.m_Name, wxString( wxS( "common.Control.showAgentPanel" ) ) );
    BOOST_CHECK_EQUAL( descriptor.m_FriendlyName, wxString( wxS( "Agent" ) ) );
    BOOST_CHECK( descriptor.m_EditorKind == AI_EDITOR_KIND::Pcb );
    BOOST_CHECK( descriptor.m_Safety == AI_ACTION_SAFETY::ReadOnly );
}


BOOST_AUTO_TEST_CASE( ClassifiesDestructiveActionsConservatively )
{
    AI_ACTION_DESCRIPTOR descriptor =
            AI_ACTION_CATALOG::DescribeAction( ACTIONS::doDelete, AI_EDITOR_KIND::Pcb );

    BOOST_CHECK( descriptor.m_Safety == AI_ACTION_SAFETY::Destructive );
}


BOOST_AUTO_TEST_CASE( BuildsCatalogFromActionManager )
{
    ACTION_MANAGER actionManager( nullptr );
    TOOL_ACTION    unitAction( TOOL_ACTION_ARGS()
                               .Name( "common.Control.unitCatalogAction" )
                               .Scope( AS_GLOBAL )
                               .FriendlyName( "Unit Catalog Action" )
                               .Tooltip( "Unit catalog action" ) );

    actionManager.RegisterAction( &unitAction );

    std::vector<AI_ACTION_DESCRIPTOR> actions =
            AI_ACTION_CATALOG::Build( &actionManager, AI_EDITOR_KIND::Schematic );

    BOOST_CHECK( !actions.empty() );

    const bool containsAgentAction =
            std::any_of( actions.begin(), actions.end(),
                         []( const AI_ACTION_DESCRIPTOR& aDescriptor )
                         {
                             return aDescriptor.m_Name == wxS( "common.Control.unitCatalogAction" );
                         } );

    BOOST_CHECK( containsAgentAction );
}


BOOST_AUTO_TEST_CASE( BuildsCatalogInStableSafetyOrder )
{
    ACTION_MANAGER actionManager( nullptr );
    TOOL_ACTION    destructiveAction( TOOL_ACTION_ARGS()
                                      .Name( "unit.Control.deleteCopper" )
                                      .Scope( AS_GLOBAL )
                                      .FriendlyName( "Delete Copper" )
                                      .Tooltip( "Delete copper" ) );
    TOOL_ACTION    readOnlyBAction( TOOL_ACTION_ARGS()
                                    .Name( "unit.Control.showBoard" )
                                    .Scope( AS_GLOBAL )
                                    .FriendlyName( "Show Board" )
                                    .Tooltip( "Show board" ) );
    TOOL_ACTION    modifyingAction( TOOL_ACTION_ARGS()
                                    .Name( "unit.Control.routeTrace" )
                                    .Scope( AS_GLOBAL )
                                    .FriendlyName( "Route Trace" )
                                    .Tooltip( "Route trace" ) );
    TOOL_ACTION    readOnlyAAction( TOOL_ACTION_ARGS()
                                    .Name( "unit.Control.findPad" )
                                    .Scope( AS_GLOBAL )
                                    .FriendlyName( "Find Pad" )
                                    .Tooltip( "Find pad" ) );

    actionManager.RegisterAction( &destructiveAction );
    actionManager.RegisterAction( &readOnlyBAction );
    actionManager.RegisterAction( &modifyingAction );
    actionManager.RegisterAction( &readOnlyAAction );

    std::vector<AI_ACTION_DESCRIPTOR> actions =
            AI_ACTION_CATALOG::Build( &actionManager, AI_EDITOR_KIND::Pcb );

    const auto indexOf =
            [&]( const wxString& aActionName )
            {
                const auto it =
                        std::find_if( actions.begin(), actions.end(),
                                      [&]( const AI_ACTION_DESCRIPTOR& aDescriptor )
                                      {
                                          return aDescriptor.m_Name == aActionName;
                                      } );

                BOOST_REQUIRE( it != actions.end() );
                return std::distance( actions.begin(), it );
            };

    const auto findPadIndex = indexOf( wxS( "unit.Control.findPad" ) );
    const auto showBoardIndex = indexOf( wxS( "unit.Control.showBoard" ) );
    const auto routeTraceIndex = indexOf( wxS( "unit.Control.routeTrace" ) );
    const auto deleteCopperIndex = indexOf( wxS( "unit.Control.deleteCopper" ) );

    BOOST_CHECK_LT( findPadIndex, showBoardIndex );
    BOOST_CHECK_LT( showBoardIndex, routeTraceIndex );
    BOOST_CHECK_LT( routeTraceIndex, deleteCopperIndex );
}


BOOST_AUTO_TEST_CASE( BuildsCatalogAppliesLimitAfterStableOrder )
{
    ACTION_MANAGER actionManager( nullptr );
    TOOL_ACTION    destructiveAction( TOOL_ACTION_ARGS()
                                      .Name( "unit.Control.deleteCopperForLimit" )
                                      .Scope( AS_GLOBAL )
                                      .FriendlyName( "Delete Copper For Limit" )
                                      .Tooltip( "Delete copper for limit" ) );
    TOOL_ACTION    firstReadOnlyAction( TOOL_ACTION_ARGS()
                                        .Name( "000.Control.findPadForLimit" )
                                        .Scope( AS_GLOBAL )
                                        .FriendlyName( "Find Pad For Limit" )
                                        .Tooltip( "Find pad for limit" ) );
    TOOL_ACTION    modifyingAction( TOOL_ACTION_ARGS()
                                    .Name( "unit.Control.routeTraceForLimit" )
                                    .Scope( AS_GLOBAL )
                                    .FriendlyName( "Route Trace For Limit" )
                                    .Tooltip( "Route trace for limit" ) );

    actionManager.RegisterAction( &destructiveAction );
    actionManager.RegisterAction( &modifyingAction );
    actionManager.RegisterAction( &firstReadOnlyAction );

    std::vector<AI_ACTION_DESCRIPTOR> actions =
            AI_ACTION_CATALOG::Build( &actionManager, AI_EDITOR_KIND::Pcb, 1 );

    BOOST_REQUIRE_EQUAL( actions.size(), 1 );
    BOOST_CHECK_EQUAL( actions.front().m_Name, wxString( wxS( "000.Control.findPadForLimit" ) ) );
    BOOST_CHECK( actions.front().m_Safety == AI_ACTION_SAFETY::ReadOnly );
}


BOOST_AUTO_TEST_SUITE_END()
