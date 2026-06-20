#include <boost/test/unit_test.hpp>

#include <kisurf/ai/ai_validation.h>

BOOST_AUTO_TEST_SUITE( AiValidation )

BOOST_AUTO_TEST_CASE( ChatOnlyScopeNeverBlocks )
{
    AI_VALIDATION_POLICY policy;

    AI_VALIDATION_SUMMARY summary;
    summary.m_Issues.push_back( { AI_VALIDATION_SEVERITY::Error,
                                  wxS( "existing issue" ), false } );

    BOOST_CHECK( !policy.BlocksApply( AI_VALIDATION_SCOPE::None, summary ) );
}

BOOST_AUTO_TEST_CASE( NewErrorBlocksApply )
{
    AI_VALIDATION_POLICY policy;

    AI_VALIDATION_SUMMARY summary;
    summary.m_Issues.push_back( { AI_VALIDATION_SEVERITY::Error, wxS( "new short" ), true } );

    BOOST_CHECK( policy.BlocksApply( AI_VALIDATION_SCOPE::LocalPreflight, summary ) );
}

BOOST_AUTO_TEST_CASE( ExistingErrorDoesNotBlockAiApply )
{
    AI_VALIDATION_POLICY policy;

    AI_VALIDATION_SUMMARY summary;
    summary.m_Issues.push_back( { AI_VALIDATION_SEVERITY::Error,
                                  wxS( "pre-existing short" ), false } );

    BOOST_CHECK( !policy.BlocksApply( AI_VALIDATION_SCOPE::PostApplyLocal, summary ) );
}

BOOST_AUTO_TEST_CASE( DiffMarksOnlyNewIssueAsNew )
{
    AI_VALIDATION_DIFF diff;

    diff.m_Before.push_back( { AI_VALIDATION_SEVERITY::Error, wxS( "old short" ), false } );
    diff.m_After.push_back( { AI_VALIDATION_SEVERITY::Error, wxS( "old short" ), false } );
    diff.m_After.push_back( { AI_VALIDATION_SEVERITY::Warning,
                              wxS( "new clearance" ), false } );

    AI_VALIDATION_SUMMARY summary = diff.Classify();

    BOOST_REQUIRE_EQUAL( summary.m_Issues.size(), 2 );
    BOOST_CHECK( !summary.m_Issues.at( 0 ).m_IsNew );
    BOOST_CHECK( summary.m_Issues.at( 1 ).m_IsNew );
}

BOOST_AUTO_TEST_SUITE_END()
