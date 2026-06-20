#include <boost/test/unit_test.hpp>

#include <kisurf/ai/ai_activity_log.h>

BOOST_AUTO_TEST_SUITE( AiActivityLog )


BOOST_AUTO_TEST_CASE( AppendAssignsIncreasingSequenceNumbers )
{
    AI_ACTIVITY_LOG log( 8 );

    AI_ACTIVITY_RECORD first;
    first.m_ActionName = wxS( "one" );

    AI_ACTIVITY_RECORD second;
    second.m_ActionName = wxS( "two" );

    BOOST_CHECK_EQUAL( log.Append( first ).m_Sequence, 1 );
    BOOST_CHECK_EQUAL( log.Append( second ).m_Sequence, 2 );

    std::vector<AI_ACTIVITY_RECORD> records = log.Records();
    BOOST_REQUIRE_EQUAL( records.size(), 2 );
    BOOST_CHECK_EQUAL( records.at( 0 ).m_ActionName, wxString( wxS( "one" ) ) );
    BOOST_CHECK_EQUAL( records.at( 1 ).m_ActionName, wxString( wxS( "two" ) ) );
}


BOOST_AUTO_TEST_CASE( LogKeepsOnlyConfiguredCapacity )
{
    AI_ACTIVITY_LOG log( 2 );

    for( int i = 0; i < 3; ++i )
    {
        AI_ACTIVITY_RECORD record;
        record.m_ActionName = wxString::Format( wxS( "action-%d" ), i );
        log.Append( record );
    }

    std::vector<AI_ACTIVITY_RECORD> records = log.Records();
    BOOST_REQUIRE_EQUAL( records.size(), 2 );
    BOOST_CHECK_EQUAL( records.at( 0 ).m_Sequence, 2 );
    BOOST_CHECK_EQUAL( records.at( 1 ).m_Sequence, 3 );
    BOOST_CHECK_EQUAL( records.at( 0 ).m_ActionName, wxString( wxS( "action-1" ) ) );
}


BOOST_AUTO_TEST_SUITE_END()
