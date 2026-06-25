#include <boost/test/unit_test.hpp>

#include <kisurf/ai/ai_accept_applier.h>
#include <kisurf/ai/ai_atomic_operation_executor.h>
#include <kisurf/ai/ai_execution_session.h>
#include <kisurf/ai/ai_structured_surface_apply_adapter.h>

#include <utility>

namespace
{
class RECORDING_ACCEPT_ADAPTER : public AI_ACCEPT_APPLY_ADAPTER
{
public:
    bool BeginTransaction( const AI_EXECUTION_SESSION& aSession,
                           wxString& aError ) override
    {
        wxUnusedVar( aError );
        ++m_BeginCount;
        m_SessionId = aSession.SessionId();
        return true;
    }

    bool ApplyOperation( const AI_SESSION_OPERATION_RECORD& aOperation,
                         wxString& aError ) override
    {
        if( m_FailOnOperationId == aOperation.m_Id )
        {
            aError = wxS( "adapter rejected operation" );
            return false;
        }

        m_OperationIds.push_back( aOperation.m_Id );
        m_OperationKinds.push_back( aOperation.m_Kind );
        return true;
    }

    bool CommitTransaction( wxString& aError ) override
    {
        wxUnusedVar( aError );
        ++m_CommitCount;
        return true;
    }

    bool HasBoardChanges() const override { return m_HasBoardChanges; }

    void AbortTransaction() override
    {
        ++m_AbortCount;
    }

    uint64_t m_SessionId = 0;
    int      m_BeginCount = 0;
    int      m_CommitCount = 0;
    int      m_AbortCount = 0;
    uint64_t m_FailOnOperationId = 0;
    bool     m_HasBoardChanges = true;
    std::vector<uint64_t> m_OperationIds;
    std::vector<AI_SESSION_OPERATION_KIND> m_OperationKinds;
};


AI_EXECUTION_SESSION makeSession()
{
    AI_EXECUTION_SESSION::OPEN_OPTIONS options;
    options.m_SessionId = 88;
    options.m_BoardId = wxS( "pcb-main" );
    options.m_BaseHash = wxS( "base-hash-accept" );
    options.m_EditorKind = AI_EDITOR_KIND::Pcb;
    options.m_ContextVersion.m_DocumentRevision = 1;
    options.m_ContextVersion.m_SelectionRevision = 2;
    options.m_ContextVersion.m_ViewRevision = 3;
    return AI_EXECUTION_SESSION( options );
}


void appendTwoOps( AI_EXECUTION_SESSION& aSession )
{
    const uint64_t stepId = aSession.BeginStep( wxS( "accepted edits" ) );
    BOOST_REQUIRE_NE( stepId, 0 );

    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            aSession, AI_SESSION_OPERATION_KIND::CreateVia,
            wxS( "{\"alias\":\"v0\",\"net\":\"GND\","
                 "\"position\":{\"x\":0,\"y\":0}}" ) )
                           .m_Ok );

    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            aSession, AI_SESSION_OPERATION_KIND::CreateTrackSegment,
            wxS( "{\"alias\":\"t0\",\"net\":\"GND\",\"layer\":\"F.Cu\","
                 "\"start\":{\"x\":0,\"y\":0},\"end\":{\"x\":100,\"y\":0}}" ) )
                           .m_Ok );

    aSession.EndStep( stepId );
}
} // namespace


BOOST_AUTO_TEST_SUITE( AiAcceptApplier )


BOOST_AUTO_TEST_CASE( ReplayJournalAppliesAllOperationsInOneTransaction )
{
    AI_EXECUTION_SESSION session = makeSession();
    appendTwoOps( session );
    RECORDING_ACCEPT_ADAPTER adapter;

    AI_ACCEPT_APPLY_RESULT result = AI_ACCEPT_APPLIER::Apply(
            session, wxS( "base-hash-accept" ), session.ContextVersion(), adapter );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_CHECK_EQUAL( adapter.m_BeginCount, 1 );
    BOOST_CHECK_EQUAL( adapter.m_CommitCount, 1 );
    BOOST_CHECK_EQUAL( adapter.m_AbortCount, 0 );
    BOOST_CHECK_EQUAL( adapter.m_SessionId, 88 );
    BOOST_REQUIRE_EQUAL( adapter.m_OperationIds.size(), 2 );
    BOOST_CHECK_EQUAL( adapter.m_OperationIds[0], 1 );
    BOOST_CHECK_EQUAL( adapter.m_OperationIds[1], 2 );
    BOOST_CHECK( adapter.m_OperationKinds[0] == AI_SESSION_OPERATION_KIND::CreateVia );
    BOOST_CHECK( adapter.m_OperationKinds[1]
                 == AI_SESSION_OPERATION_KIND::CreateTrackSegment );
    BOOST_CHECK_EQUAL( result.m_AppliedOperationCount, 2 );
    BOOST_CHECK( result.m_BoardMutated );
    BOOST_CHECK( session.Status() == AI_EXECUTION_SESSION_STATUS::Accepted );
}


BOOST_AUTO_TEST_CASE( StaleBaseHashRejectsBeforeAdapterBegins )
{
    AI_EXECUTION_SESSION session = makeSession();
    appendTwoOps( session );
    RECORDING_ACCEPT_ADAPTER adapter;

    AI_ACCEPT_APPLY_RESULT result = AI_ACCEPT_APPLIER::Apply(
            session, wxS( "different-base" ), session.ContextVersion(), adapter );

    BOOST_CHECK( !result.m_Ok );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString( wxS( "stale_session" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_BeginCount, 0 );
    BOOST_CHECK_EQUAL( adapter.m_CommitCount, 0 );
    BOOST_CHECK( session.Status() == AI_EXECUTION_SESSION_STATUS::Open );
}


BOOST_AUTO_TEST_CASE( SelectionRevisionConflictRejectsBeforeAdapterBegins )
{
    AI_EXECUTION_SESSION session = makeSession();
    appendTwoOps( session );
    RECORDING_ACCEPT_ADAPTER adapter;
    AI_CONTEXT_VERSION changedVersion = session.ContextVersion();
    changedVersion.m_SelectionRevision = 5;

    AI_ACCEPT_APPLY_RESULT result = AI_ACCEPT_APPLIER::Apply(
            session, wxS( "base-hash-accept" ), changedVersion, adapter );

    BOOST_CHECK( !result.m_Ok );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString( wxS( "selection_conflict" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_BeginCount, 0 );
    BOOST_CHECK_EQUAL( adapter.m_CommitCount, 0 );
    BOOST_CHECK( session.Status() == AI_EXECUTION_SESSION_STATUS::Open );
}


BOOST_AUTO_TEST_CASE( AdapterFailureAbortsAndLeavesSessionOpen )
{
    AI_EXECUTION_SESSION session = makeSession();
    appendTwoOps( session );
    RECORDING_ACCEPT_ADAPTER adapter;
    adapter.m_FailOnOperationId = 2;

    AI_ACCEPT_APPLY_RESULT result = AI_ACCEPT_APPLIER::Apply(
            session, wxS( "base-hash-accept" ), session.ContextVersion(), adapter );

    BOOST_CHECK( !result.m_Ok );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString( wxS( "apply_failed" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_BeginCount, 1 );
    BOOST_CHECK_EQUAL( adapter.m_CommitCount, 0 );
    BOOST_CHECK_EQUAL( adapter.m_AbortCount, 1 );
    BOOST_REQUIRE_EQUAL( adapter.m_OperationIds.size(), 1 );
    BOOST_CHECK_EQUAL( adapter.m_OperationIds.front(), 1 );
    BOOST_CHECK( session.Status() == AI_EXECUTION_SESSION_STATUS::Open );
}


BOOST_AUTO_TEST_CASE( AdapterCanAcceptReplayThatDoesNotMutateBoard )
{
    AI_EXECUTION_SESSION session = makeSession();

    const uint64_t stepId = session.BeginStep( wxS( "validation only" ) );
    BOOST_REQUIRE_NE( stepId, 0 );
    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::RunValidation,
            wxS( "{\"scope\":\"session\",\"level\":\"geometry\"}" ) )
                           .m_Ok );
    session.EndStep( stepId );

    RECORDING_ACCEPT_ADAPTER adapter;
    adapter.m_HasBoardChanges = false;

    AI_ACCEPT_APPLY_RESULT result = AI_ACCEPT_APPLIER::Apply(
            session, wxS( "base-hash-accept" ), session.ContextVersion(), adapter );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_CHECK_EQUAL( adapter.m_BeginCount, 1 );
    BOOST_CHECK_EQUAL( adapter.m_CommitCount, 1 );
    BOOST_CHECK_EQUAL( adapter.m_AbortCount, 0 );
    BOOST_CHECK_EQUAL( result.m_AppliedOperationCount, 1 );
    BOOST_CHECK( !result.m_BoardMutated );
    BOOST_CHECK( session.Status() == AI_EXECUTION_SESSION_STATUS::Accepted );
}


BOOST_AUTO_TEST_CASE( InexactPreviewValidationRejectsBeforeAdapterBegins )
{
    AI_EXECUTION_SESSION session = makeSession();

    const uint64_t stepId = session.BeginStep( wxS( "inexact native validation" ) );
    BOOST_REQUIRE_NE( stepId, 0 );
    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateVia,
            wxS( "{\"alias\":\"v0\",\"net\":\"GND\","
                 "\"position\":{\"x\":0,\"y\":0}}" ) )
                           .m_Ok );

    AI_SESSION_OPERATION_RECORD validation;
    validation.m_Kind = AI_SESSION_OPERATION_KIND::RunValidation;
    validation.m_ArgumentsJson = wxS( "{\"scope\":\"session\",\"level\":\"full_drc\"}" );
    validation.m_ResultJson =
            wxS( "{\"status\":\"validation_completed\",\"validation\":{"
                 "\"level\":\"full_drc\","
                 "\"preview_state_exact\":false,"
                 "\"accept_validation_sufficient\":false,"
                 "\"accept_validation_reason\":\"requires_preview_state_native_drc\"}}" );
    session.AppendOperation( std::move( validation ) );
    session.EndStep( stepId );

    RECORDING_ACCEPT_ADAPTER adapter;

    AI_ACCEPT_APPLY_RESULT result = AI_ACCEPT_APPLIER::Apply(
            session, wxS( "base-hash-accept" ), session.ContextVersion(), adapter );

    BOOST_CHECK( !result.m_Ok );
    BOOST_CHECK_EQUAL( result.m_ErrorCode,
                       wxString( wxS( "validation_not_accept_grade" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_BeginCount, 0 );
    BOOST_CHECK_EQUAL( adapter.m_CommitCount, 0 );
    BOOST_CHECK( session.Status() == AI_EXECUTION_SESSION_STATUS::Open );
}


BOOST_AUTO_TEST_CASE( AcceptSufficientButInexactPreviewValidationRejectsBeforeAdapterBegins )
{
    AI_EXECUTION_SESSION session = makeSession();

    const uint64_t stepId = session.BeginStep( wxS( "inexact accepted validation" ) );
    BOOST_REQUIRE_NE( stepId, 0 );
    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateVia,
            wxS( "{\"alias\":\"v0\",\"net\":\"GND\","
                 "\"position\":{\"x\":0,\"y\":0}}" ) )
                           .m_Ok );

    AI_SESSION_OPERATION_RECORD validation;
    validation.m_Kind = AI_SESSION_OPERATION_KIND::RunValidation;
    validation.m_ArgumentsJson =
            wxS( "{\"scope\":\"session\",\"level\":\"full_drc\",\"gate\":\"accept\"}" );
    validation.m_ResultJson =
            wxS( "{\"status\":\"validation_completed\",\"validation\":{"
                 "\"level\":\"full_drc\","
                 "\"preview_state_exact\":false,"
                 "\"accept_validation_sufficient\":true,"
                 "\"accept_validation_reason\":\"native result was not exact\"}}" );
    session.AppendOperation( std::move( validation ) );
    session.EndStep( stepId );

    RECORDING_ACCEPT_ADAPTER adapter;

    AI_ACCEPT_APPLY_RESULT result = AI_ACCEPT_APPLIER::Apply(
            session, wxS( "base-hash-accept" ), session.ContextVersion(), adapter );

    BOOST_CHECK( !result.m_Ok );
    BOOST_CHECK_EQUAL( result.m_ErrorCode,
                       wxString( wxS( "validation_not_accept_grade" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_BeginCount, 0 );
    BOOST_CHECK_EQUAL( adapter.m_CommitCount, 0 );
    BOOST_CHECK( session.Status() == AI_EXECUTION_SESSION_STATUS::Open );
}


BOOST_AUTO_TEST_CASE( NativeDrcFallbackValidationRejectsBeforeAdapterBegins )
{
    AI_EXECUTION_SESSION session = makeSession();

    const uint64_t stepId = session.BeginStep( wxS( "fallback native validation" ) );
    BOOST_REQUIRE_NE( stepId, 0 );
    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::RunValidation,
            wxS( "{\"scope\":\"session\",\"level\":\"full_drc\"}" ) )
                           .m_Ok );
    session.EndStep( stepId );

    RECORDING_ACCEPT_ADAPTER adapter;
    adapter.m_HasBoardChanges = false;

    AI_ACCEPT_APPLY_RESULT result = AI_ACCEPT_APPLIER::Apply(
            session, wxS( "base-hash-accept" ), session.ContextVersion(), adapter );

    BOOST_CHECK( !result.m_Ok );
    BOOST_CHECK_EQUAL( result.m_ErrorCode,
                       wxString( wxS( "validation_not_accept_grade" ) ) );
    BOOST_CHECK_EQUAL( adapter.m_BeginCount, 0 );
    BOOST_CHECK_EQUAL( adapter.m_CommitCount, 0 );
    BOOST_CHECK( session.Status() == AI_EXECUTION_SESSION_STATUS::Open );
}


BOOST_AUTO_TEST_CASE( ReplaySkipsObservationRecordsButKeepsThemInJournal )
{
    AI_EXECUTION_SESSION session = makeSession();

    const uint64_t stepId = session.BeginStep( wxS( "query before edit" ) );
    BOOST_REQUIRE_NE( stepId, 0 );

    AI_SESSION_OPERATION_RECORD query;
    query.m_Kind = AI_SESSION_OPERATION_KIND::QuerySelection;
    query.m_ArgumentsJson = wxS( "{}" );
    query.m_ResultJson = wxS( "{\"status\":\"selection\",\"selected_count\":0}" );
    session.AppendOperation( std::move( query ) );

    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::CreateVia,
            wxS( "{\"alias\":\"v0\",\"net\":\"GND\","
                 "\"position\":{\"x\":0,\"y\":0}}" ) )
                           .m_Ok );
    session.EndStep( stepId );

    RECORDING_ACCEPT_ADAPTER adapter;

    AI_ACCEPT_APPLY_RESULT result = AI_ACCEPT_APPLIER::Apply(
            session, wxS( "base-hash-accept" ), session.ContextVersion(), adapter );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_REQUIRE_EQUAL( session.Journal().Operations().size(), 2 );
    BOOST_REQUIRE_EQUAL( adapter.m_OperationKinds.size(), 1 );
    BOOST_CHECK( adapter.m_OperationKinds.front()
                 == AI_SESSION_OPERATION_KIND::CreateVia );
    BOOST_CHECK_EQUAL( result.m_AppliedOperationCount, 1 );
    BOOST_CHECK( session.Status() == AI_EXECUTION_SESSION_STATUS::Accepted );
}


BOOST_AUTO_TEST_CASE( SurfacePatchReplayAppliesStructuredSurfaceChanges )
{
    AI_EXECUTION_SESSION session = makeSession();

    const uint64_t stepId = session.BeginStep( wxS( "surface patch accept" ) );
    BOOST_REQUIRE_NE( stepId, 0 );
    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::ApplySurfacePatch,
            wxS( "{\"surface_id\":\"board_setup.clearance\","
                 "\"table_id\":\"clearance.rules\","
                 "\"patch\":{\"kind\":\"SurfacePatch\","
                 "\"operations\":["
                 "{\"op\":\"set_cell\",\"row_id\":\"row.power\","
                 "\"column_id\":\"class\",\"value\":\"Power\"},"
                 "{\"op\":\"set_field\",\"field_id\":\"default_clearance\","
                 "\"value\":\"0.20mm\"}]},"
                 "\"alias\":\"surface_patch_fill_class\"}" ) )
                           .m_Ok );
    session.EndStep( stepId );

    wxString surfaceState =
            wxS( "{\"surfaces\":{\"board_setup.clearance\":{\"tables\":"
                 "{\"clearance.rules\":{\"rows\":{\"row.power\":{\"cells\":"
                 "{\"class\":\"Signal\"}}}}}}}}" );

    AI_STRUCTURED_SURFACE_APPLY_ADAPTER adapter( surfaceState );

    AI_ACCEPT_APPLY_RESULT result = AI_ACCEPT_APPLIER::Apply(
            session, wxS( "base-hash-accept" ), session.ContextVersion(), adapter );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_CHECK_EQUAL( result.m_AppliedOperationCount, 1 );
    BOOST_CHECK( !result.m_BoardMutated );
    BOOST_CHECK( session.Status() == AI_EXECUTION_SESSION_STATUS::Accepted );
    BOOST_CHECK( surfaceState.Contains( wxS( "\"class\":\"Power\"" ) ) );
    BOOST_CHECK( surfaceState.Contains(
            wxS( "\"default_clearance\":\"0.20mm\"" ) ) );
}


BOOST_AUTO_TEST_CASE( SurfacePatchReplayRejectsUnknownPatchOperationAndAborts )
{
    AI_EXECUTION_SESSION session = makeSession();

    const uint64_t stepId = session.BeginStep( wxS( "bad surface patch" ) );
    BOOST_REQUIRE_NE( stepId, 0 );
    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::ApplySurfacePatch,
            wxS( "{\"surface_id\":\"board_setup.clearance\","
                 "\"patch\":{\"kind\":\"SurfacePatch\","
                 "\"operations\":[{\"op\":\"delete_project\","
                 "\"target\":\"all\"}]}}" ) )
                           .m_Ok );
    session.EndStep( stepId );

    wxString surfaceState =
            wxS( "{\"surfaces\":{\"board_setup.clearance\":{\"fields\":"
                 "{\"default_clearance\":\"0.15mm\"}}}}" );
    const wxString originalSurfaceState = surfaceState;

    AI_STRUCTURED_SURFACE_APPLY_ADAPTER adapter( surfaceState );

    AI_ACCEPT_APPLY_RESULT result = AI_ACCEPT_APPLIER::Apply(
            session, wxS( "base-hash-accept" ), session.ContextVersion(), adapter );

    BOOST_CHECK( !result.m_Ok );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString( wxS( "apply_failed" ) ) );
    BOOST_CHECK( result.m_Message.Contains( wxS( "Unsupported SurfacePatch op" ) ) );
    BOOST_CHECK_EQUAL( surfaceState, originalSurfaceState );
    BOOST_CHECK( session.Status() == AI_EXECUTION_SESSION_STATUS::Open );
}


BOOST_AUTO_TEST_CASE( SurfacePatchReplayRejectsStaleSurfaceRevisionAndAborts )
{
    AI_EXECUTION_SESSION session = makeSession();

    const uint64_t stepId = session.BeginStep( wxS( "stale surface patch" ) );
    BOOST_REQUIRE_NE( stepId, 0 );
    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::ApplySurfacePatch,
            wxS( "{\"surface_id\":\"board_setup.clearance\","
                 "\"expected_surface_revision\":7,"
                 "\"patch\":{\"kind\":\"SurfacePatch\","
                 "\"operations\":[{\"op\":\"set_field\","
                 "\"field_id\":\"default_clearance\","
                 "\"value\":\"0.20mm\"}]}}" ) )
                           .m_Ok );
    session.EndStep( stepId );

    wxString surfaceState =
            wxS( "{\"surfaces\":{\"board_setup.clearance\":{"
                 "\"revision\":8,"
                 "\"fields\":{\"default_clearance\":\"0.15mm\"}}}}" );
    const wxString originalSurfaceState = surfaceState;

    AI_STRUCTURED_SURFACE_APPLY_ADAPTER adapter( surfaceState );

    AI_ACCEPT_APPLY_RESULT result = AI_ACCEPT_APPLIER::Apply(
            session, wxS( "base-hash-accept" ), session.ContextVersion(), adapter );

    BOOST_CHECK( !result.m_Ok );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString( wxS( "apply_failed" ) ) );
    BOOST_CHECK( result.m_Message.Contains( wxS( "stale surface revision" ) ) );
    BOOST_CHECK_EQUAL( surfaceState, originalSurfaceState );
    BOOST_CHECK( session.Status() == AI_EXECUTION_SESSION_STATUS::Open );
}


BOOST_AUTO_TEST_SUITE_END()
