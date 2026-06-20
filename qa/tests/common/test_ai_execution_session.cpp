#include <boost/test/unit_test.hpp>

#include <json_common.h>
#include <kisurf/ai/ai_execution_session.h>

namespace
{
AI_EXECUTION_SESSION makeSession()
{
    AI_EXECUTION_SESSION::OPEN_OPTIONS options;
    options.m_SessionId = 42;
    options.m_BoardId = wxS( "pcb-main" );
    options.m_BaseHash = wxS( "base-hash-a" );
    options.m_EditorKind = AI_EDITOR_KIND::Pcb;
    options.m_ContextVersion.m_DocumentRevision = 7;
    options.m_ContextVersion.m_SelectionRevision = 2;
    options.m_ContextVersion.m_ViewRevision = 3;
    return AI_EXECUTION_SESSION( options );
}


AI_SESSION_OPERATION_RECORD createViaOperation(
        const AI_SESSION_HANDLE& aCreatedHandle = AI_SESSION_HANDLE() )
{
    AI_SESSION_OPERATION_RECORD operation;
    operation.m_Kind = AI_SESSION_OPERATION_KIND::CreateVia;
    operation.m_ArgumentsJson =
            wxS( "{\"net\":\"GND\",\"position\":{\"x\":100,\"y\":200}}" );

    if( aCreatedHandle.IsValid() )
        operation.m_CreatedHandles.push_back( aCreatedHandle );

    return operation;
}
} // namespace


BOOST_AUTO_TEST_SUITE( AiExecutionSession )


BOOST_AUTO_TEST_CASE( SessionJournalGroupsAtomicOpsByStepAndEpoch )
{
    AI_EXECUTION_SESSION session = makeSession();

    const uint64_t stepId = session.BeginStep( wxS( "create via ring" ) );
    BOOST_REQUIRE_NE( stepId, 0 );

    AI_SESSION_HANDLE via = session.CreateHandle( wxS( "ring-via-0" ) );
    const AI_SESSION_OPERATION_RECORD& appended =
            session.AppendOperation( createViaOperation( via ) );

    BOOST_CHECK_EQUAL( appended.m_Id, 1 );
    BOOST_CHECK_EQUAL( appended.m_StepId, stepId );
    BOOST_CHECK_EQUAL( appended.m_BeforeEpoch, 0 );
    BOOST_CHECK_EQUAL( appended.m_AfterEpoch, 1 );
    BOOST_CHECK( appended.IsMutation() );
    BOOST_CHECK_EQUAL( appended.OperationId(), wxString( wxS( "pcb.create_via" ) ) );

    AI_SESSION_OBSERVATION observation = session.EndStep( stepId );
    BOOST_CHECK_EQUAL( observation.m_StepId, stepId );
    BOOST_CHECK_EQUAL( observation.m_Epoch, 1 );
    BOOST_CHECK_EQUAL( observation.m_OperationCount, 1 );

    nlohmann::json json =
            nlohmann::json::parse( observation.AsJsonText().ToStdString() );
    BOOST_CHECK_EQUAL( json["step_id"].get<uint64_t>(), stepId );
    BOOST_CHECK_EQUAL( json["operation_count"].get<size_t>(), 1 );

    BOOST_REQUIRE_EQUAL( session.Journal().Operations().size(), 1 );
    BOOST_REQUIRE_EQUAL( session.Journal().OperationsForStep( stepId ).size(), 1 );
    BOOST_CHECK( session.ResolveHandle( via ) == AI_SESSION_HANDLE_STATUS::Live );
    BOOST_REQUIRE( session.ResolveAlias( wxS( "ring-via-0" ) ).has_value() );
}


BOOST_AUTO_TEST_CASE( RollbackTruncatesJournalAndMakesCreatedHandlesStale )
{
    AI_EXECUTION_SESSION session = makeSession();

    AI_SESSION_HANDLE preexisting = session.CreateHandle( wxS( "pad-a" ) );
    const uint64_t checkpoint = session.Checkpoint( wxS( "before trial geometry" ) );

    const uint64_t stepId = session.BeginStep( wxS( "try wrong clearance" ) );
    AI_SESSION_HANDLE trialVia = session.CreateHandle( wxS( "trial-via" ) );
    session.AppendOperation( createViaOperation( trialVia ) );
    session.EndStep( stepId );

    BOOST_CHECK_EQUAL( session.Epoch(), 1 );
    BOOST_CHECK_EQUAL( session.Journal().Operations().size(), 1 );
    BOOST_CHECK( session.ResolveHandle( preexisting ) == AI_SESSION_HANDLE_STATUS::Live );
    BOOST_CHECK( session.ResolveHandle( trialVia ) == AI_SESSION_HANDLE_STATUS::Live );
    BOOST_REQUIRE( session.ResolveAlias( wxS( "trial-via" ) ).has_value() );

    BOOST_REQUIRE( session.RollbackTo( checkpoint ) );

    BOOST_CHECK_EQUAL( session.Epoch(), 0 );
    BOOST_CHECK( session.Journal().Operations().empty() );
    BOOST_CHECK( session.ResolveHandle( preexisting ) == AI_SESSION_HANDLE_STATUS::Live );
    BOOST_CHECK( session.ResolveHandle( trialVia ) == AI_SESSION_HANDLE_STATUS::Stale );
    BOOST_CHECK( !session.ResolveAlias( wxS( "trial-via" ) ).has_value() );
}


BOOST_AUTO_TEST_CASE( AcceptRequiresMatchingBaseHashAndNoOpenStep )
{
    AI_EXECUTION_SESSION session = makeSession();
    const AI_CONTEXT_VERSION version = session.ContextVersion();

    BOOST_CHECK( session.CanAccept( wxS( "base-hash-a" ), version ) );
    BOOST_CHECK( !session.CanAccept( wxS( "other-hash" ), version ) );

    const uint64_t stepId = session.BeginStep( wxS( "unfinished" ) );
    BOOST_REQUIRE_NE( stepId, 0 );
    BOOST_CHECK( !session.CanAccept( wxS( "base-hash-a" ), version ) );

    session.EndStep( stepId );
    BOOST_CHECK( !session.AcceptSession( wxS( "other-hash" ), version ) );
    BOOST_CHECK( session.AcceptSession( wxS( "base-hash-a" ), version ) );
    BOOST_CHECK( session.Status() == AI_EXECUTION_SESSION_STATUS::Accepted );
}


BOOST_AUTO_TEST_CASE( AcceptRejectsChangedSelectionRevision )
{
    AI_EXECUTION_SESSION session = makeSession();
    AI_CONTEXT_VERSION changedVersion = session.ContextVersion();
    changedVersion.m_SelectionRevision = 99;

    BOOST_CHECK( session.SelectionRevisionConflicts( changedVersion ) );
    BOOST_CHECK( !session.CanAccept( wxS( "base-hash-a" ), changedVersion ) );
    BOOST_CHECK( !session.AcceptSession( wxS( "base-hash-a" ), changedVersion ) );
    BOOST_CHECK( session.Status() == AI_EXECUTION_SESSION_STATUS::Open );
}


BOOST_AUTO_TEST_CASE( AcceptAllowsPureViewRevisionChange )
{
    AI_EXECUTION_SESSION session = makeSession();
    AI_CONTEXT_VERSION changedVersion = session.ContextVersion();
    changedVersion.m_ViewRevision = 99;

    BOOST_CHECK( !session.SelectionRevisionConflicts( changedVersion ) );
    BOOST_CHECK( session.CanAccept( wxS( "base-hash-a" ), changedVersion ) );
    BOOST_CHECK( session.AcceptSession( wxS( "base-hash-a" ), changedVersion ) );
    BOOST_CHECK( session.Status() == AI_EXECUTION_SESSION_STATUS::Accepted );
}


BOOST_AUTO_TEST_CASE( CancelledSessionCannotAccept )
{
    AI_EXECUTION_SESSION session = makeSession();
    const AI_CONTEXT_VERSION version = session.ContextVersion();

    session.CancelSession( wxS( "user interrupted cell" ) );

    BOOST_CHECK( session.Status() == AI_EXECUTION_SESSION_STATUS::Cancelled );
    BOOST_CHECK_EQUAL( session.CancelReason(),
                       wxString( wxS( "user interrupted cell" ) ) );
    BOOST_CHECK( !session.CanAccept( wxS( "base-hash-a" ), version ) );
}


BOOST_AUTO_TEST_SUITE_END()
