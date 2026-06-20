#include <boost/test/unit_test.hpp>

#include <kisurf/ai/ai_edit_session.h>

#include <vector>
#include <wx/arrstr.h> // for MSVC to see std::vector<wxString> is exported from wx
#include <wx/string.h>

class FAKE_EDIT_ADAPTER : public AI_EDIT_ADAPTER
{
public:
    bool ApplyObject( const AI_OBJECT_REF& aObject ) override
    {
        m_Applied.push_back( aObject.m_Label );
        return m_ShouldApply;
    }

    bool                  m_ShouldApply = true;
    std::vector<wxString> m_Applied;
};

class LIFECYCLE_EDIT_ADAPTER : public AI_EDIT_ADAPTER
{
public:
    bool BeginApply( const AI_VALIDATION_SUMMARY&, size_t aObjectCount ) override
    {
        m_Events.push_back( wxString::Format( wxS( "begin:%llu" ),
                                              static_cast<unsigned long long>( aObjectCount ) ) );
        return m_BeginResult;
    }

    bool ApplyObject( const AI_OBJECT_REF& aObject ) override
    {
        m_Events.push_back( wxS( "apply:" ) + aObject.m_Label );
        return m_ApplyResult;
    }

    bool EndApply() override
    {
        m_Events.push_back( wxS( "end" ) );
        return m_EndResult;
    }

    void AbortApply() override
    {
        m_Events.push_back( wxS( "abort" ) );
    }

    bool                  m_BeginResult = true;
    bool                  m_ApplyResult = true;
    bool                  m_EndResult = true;
    std::vector<wxString> m_Events;
};

BOOST_AUTO_TEST_SUITE( AiEditSession )

BOOST_AUTO_TEST_CASE( ApplyRecordsAcceptedObjectsAndValidation )
{
    FAKE_EDIT_ADAPTER adapter;
    AI_EDIT_SESSION   session( adapter );
    AI_OBJECT_REF     trace( KIID(), PCB_TRACE_T, wxS( "route-a" ) );

    AI_VALIDATION_SUMMARY validation;
    validation.m_Issues.push_back( { AI_VALIDATION_SEVERITY::Info, wxS( "accepted" ), true } );

    BOOST_CHECK( session.Apply( { trace }, validation ) );
    BOOST_REQUIRE_EQUAL( adapter.m_Applied.size(), 1 );
    BOOST_CHECK_EQUAL( adapter.m_Applied.front(), wxString( wxS( "route-a" ) ) );
    BOOST_CHECK( session.LastValidation().WorstSeverity() == AI_VALIDATION_SEVERITY::Info );
}

BOOST_AUTO_TEST_CASE( BlockingValidationPreventsApply )
{
    FAKE_EDIT_ADAPTER adapter;
    AI_EDIT_SESSION   session( adapter );
    AI_OBJECT_REF     trace( KIID(), PCB_TRACE_T, wxS( "blocked-route" ) );

    AI_VALIDATION_SUMMARY validation;
    validation.m_Issues.push_back( { AI_VALIDATION_SEVERITY::Error, wxS( "new clearance" ), true } );

    BOOST_CHECK( !session.Apply( { trace }, validation ) );
    BOOST_CHECK( adapter.m_Applied.empty() );
    BOOST_CHECK( session.LastValidation().WorstSeverity() == AI_VALIDATION_SEVERITY::None );
}

BOOST_AUTO_TEST_CASE( BlockingValidationPreventsAdapterApply )
{
    FAKE_EDIT_ADAPTER adapter;
    AI_EDIT_SESSION   session( adapter );
    AI_OBJECT_REF     trace( KIID(), PCB_TRACE_T, wxS( "route-b" ) );

    AI_VALIDATION_SUMMARY validation;
    validation.m_Issues.push_back( { AI_VALIDATION_SEVERITY::Error, wxS( "new short" ), true } );

    BOOST_CHECK( !session.Apply( { trace }, validation ) );
    BOOST_CHECK( adapter.m_Applied.empty() );
}

BOOST_AUTO_TEST_CASE( AdapterFailureStopsSession )
{
    FAKE_EDIT_ADAPTER adapter;
    adapter.m_ShouldApply = false;

    AI_EDIT_SESSION   session( adapter );
    AI_OBJECT_REF     trace( KIID(), PCB_TRACE_T, wxS( "rejected-route" ) );

    BOOST_CHECK( !session.Apply( { trace }, AI_VALIDATION_SUMMARY() ) );
    BOOST_REQUIRE_EQUAL( adapter.m_Applied.size(), 1 );
    BOOST_CHECK_EQUAL( adapter.m_Applied.front(), wxString( wxS( "rejected-route" ) ) );
    BOOST_CHECK( session.LastValidation().WorstSeverity() == AI_VALIDATION_SEVERITY::None );
}

BOOST_AUTO_TEST_CASE( LifecycleWrapsSuccessfulApply )
{
    LIFECYCLE_EDIT_ADAPTER adapter;
    AI_EDIT_SESSION        session( adapter );
    AI_OBJECT_REF          first( KIID(), PCB_PAD_T, wxS( "first" ) );
    AI_OBJECT_REF          second( KIID(), PCB_PAD_T, wxS( "second" ) );

    BOOST_CHECK( session.Apply( { first, second }, AI_VALIDATION_SUMMARY() ) );
    BOOST_REQUIRE_EQUAL( adapter.m_Events.size(), 4 );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 0 ), wxString( wxS( "begin:2" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 1 ), wxString( wxS( "apply:first" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 2 ), wxString( wxS( "apply:second" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 3 ), wxString( wxS( "end" ) ) );
}


BOOST_AUTO_TEST_CASE( ApplyFailureAbortsAndDoesNotStoreValidation )
{
    LIFECYCLE_EDIT_ADAPTER adapter;
    adapter.m_ApplyResult = false;

    AI_EDIT_SESSION       session( adapter );
    AI_VALIDATION_SUMMARY validation;
    validation.m_Issues.push_back( { AI_VALIDATION_SEVERITY::Info, wxS( "candidate" ),
                                     false } );

    BOOST_CHECK( !session.Apply( { AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "bad" ) ) },
                                 validation ) );
    BOOST_REQUIRE_EQUAL( adapter.m_Events.size(), 3 );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 0 ), wxString( wxS( "begin:1" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 1 ), wxString( wxS( "apply:bad" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 2 ), wxString( wxS( "abort" ) ) );
    BOOST_CHECK( session.LastValidation().WorstSeverity() == AI_VALIDATION_SEVERITY::None );
}


BOOST_AUTO_TEST_CASE( EndFailureAborts )
{
    LIFECYCLE_EDIT_ADAPTER adapter;
    adapter.m_EndResult = false;

    AI_EDIT_SESSION session( adapter );

    BOOST_CHECK( !session.Apply( { AI_OBJECT_REF( KIID(), PCB_PAD_T, wxS( "late" ) ) },
                                 AI_VALIDATION_SUMMARY() ) );
    BOOST_REQUIRE_EQUAL( adapter.m_Events.size(), 4 );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 0 ), wxString( wxS( "begin:1" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 1 ), wxString( wxS( "apply:late" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 2 ), wxString( wxS( "end" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_Events.at( 3 ), wxString( wxS( "abort" ) ) );
}

BOOST_AUTO_TEST_SUITE_END()
