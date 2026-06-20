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

#include <kisurf/ai/ai_tool_state_provider.h>

#include <type_traits>


BOOST_AUTO_TEST_SUITE( AiToolStateProvider )


class STATIC_TOOL_STATE_PROVIDER : public AI_TOOL_STATE_PROVIDER
{
public:
    AI_TOOL_STATE_SNAPSHOT BuildToolState( const AI_CONTEXT_VERSION& aContextVersion ) const override
    {
        AI_TOOL_STATE_SNAPSHOT snapshot;
        snapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
        snapshot.m_Kind = AI_TOOL_STATE_KIND::Selecting;
        snapshot.m_ContextVersion = aContextVersion;
        return snapshot;
    }
};


BOOST_AUTO_TEST_CASE( ProviderContractBuildsVersionedToolStateSnapshots )
{
    BOOST_CHECK( std::has_virtual_destructor_v<AI_TOOL_STATE_PROVIDER> );

    AI_CONTEXT_VERSION version;
    version.m_DocumentRevision = 3;
    version.m_SelectionRevision = 2;
    version.m_ViewRevision = 1;

    STATIC_TOOL_STATE_PROVIDER provider;
    AI_TOOL_STATE_SNAPSHOT     snapshot = provider.BuildToolState( version );

    BOOST_CHECK_EQUAL( static_cast<int>( snapshot.m_EditorKind ),
                       static_cast<int>( AI_EDITOR_KIND::Pcb ) );
    BOOST_CHECK_EQUAL( static_cast<int>( snapshot.m_Kind ),
                       static_cast<int>( AI_TOOL_STATE_KIND::Selecting ) );
    BOOST_CHECK_EQUAL( snapshot.m_ContextVersion.m_DocumentRevision, 3 );
    BOOST_CHECK_EQUAL( snapshot.m_ContextVersion.m_SelectionRevision, 2 );
    BOOST_CHECK_EQUAL( snapshot.m_ContextVersion.m_ViewRevision, 1 );
}


BOOST_AUTO_TEST_SUITE_END()
