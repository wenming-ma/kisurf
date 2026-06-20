/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 */

#include <boost/test/unit_test.hpp>

#include <tool/tool_manager.h>

#include <string>
#include <vector>


BOOST_AUTO_TEST_SUITE( ToolManagerEventObserver )


BOOST_AUTO_TEST_CASE( ObserverSeesProcessedToolEvents )
{
    TOOL_MANAGER manager;
    std::vector<std::string> seen;

    manager.AddEventObserver( [&]( const TOOL_EVENT& aEvent )
    {
        seen.push_back( aEvent.CommandString() );
    } );

    manager.ProcessEvent( TOOL_EVENT( TC_MESSAGE, TA_ACTION, "test.event" ) );

    BOOST_REQUIRE_EQUAL( seen.size(), 1 );
    BOOST_CHECK_EQUAL( seen.front(), "test.event" );
}


BOOST_AUTO_TEST_CASE( RemovedObserverStopsReceivingEvents )
{
    TOOL_MANAGER manager;
    int count = 0;

    const uint64_t id = manager.AddEventObserver( [&]( const TOOL_EVENT& )
    {
        ++count;
    } );

    manager.RemoveEventObserver( id );
    manager.ProcessEvent( TOOL_EVENT( TC_MESSAGE, TA_ACTION, "test.event" ) );

    BOOST_CHECK_EQUAL( count, 0 );
}


BOOST_AUTO_TEST_SUITE_END()
