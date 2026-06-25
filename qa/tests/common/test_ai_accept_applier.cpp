#include <boost/test/unit_test.hpp>

#include <kisurf/ai/ai_accept_applier.h>
#include <kisurf/ai/ai_atomic_operation_executor.h>
#include <kisurf/ai/ai_execution_session.h>
#include <kisurf/ai/ai_structured_surface_apply_adapter.h>

#include <memory>
#include <type_traits>
#include <utility>
#include <wx/arrstr.h> // for MSVC to see std::vector<wxString> is exported from wx

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


class RECORDING_STRUCTURED_SURFACE_BACKEND :
        public AI_STRUCTURED_SURFACE_STATE_BACKEND
{
public:
    bool BeginSurfaceTransaction( const AI_EXECUTION_SESSION& aSession,
                                  wxString& aSurfaceStateJson,
                                  wxString& aError ) override
    {
        wxUnusedVar( aSession );
        ++m_BeginCount;
        aSurfaceStateJson = m_StateJson;
        aError.clear();
        return m_BeginOk;
    }

    bool CommitSurfaceTransaction( const wxString& aSurfaceStateJson,
                                   bool aChanged,
                                   wxString& aError ) override
    {
        ++m_CommitCount;
        m_LastChanged = aChanged;

        if( !m_CommitOk )
        {
            aError = wxS( "backend commit failed" );
            return false;
        }

        m_StateJson = aSurfaceStateJson;
        aError.clear();
        return true;
    }

    void AbortSurfaceTransaction() override
    {
        ++m_AbortCount;
    }

    wxString m_StateJson;
    int      m_BeginCount = 0;
    int      m_CommitCount = 0;
    int      m_AbortCount = 0;
    bool     m_LastChanged = false;
    bool     m_BeginOk = true;
    bool     m_CommitOk = true;
};


class RECORDING_GRID_IO : public AI_STRUCTURED_SURFACE_GRID_IO
{
public:
    explicit RECORDING_GRID_IO( std::vector<std::vector<wxString>> aCells ) :
            m_Cells( std::move( aCells ) )
    {
        m_RowLabels.resize( m_Cells.size() );

        const size_t columnCount = m_Cells.empty() ? 0 : m_Cells.front().size();
        m_ColumnLabels.resize( columnCount );
    }

    int RowCount() const override
    {
        return static_cast<int>( m_Cells.size() );
    }

    int ColumnCount() const override
    {
        return m_Cells.empty() ? 0 : static_cast<int>( m_Cells.front().size() );
    }

    wxString RowLabel( int aRow ) const override
    {
        return m_RowLabels.at( aRow );
    }

    wxString ColumnLabel( int aColumn ) const override
    {
        return m_ColumnLabels.at( aColumn );
    }

    wxString CellValue( int aRow, int aColumn ) const override
    {
        return m_Cells.at( aRow ).at( aColumn );
    }

    void SetCellValue( int aRow, int aColumn,
                       const wxString& aValue ) override
    {
        m_Cells.at( aRow ).at( aColumn ) = aValue;
        ++m_SetCellValueCount;
    }

    void SetRowLabel( int aRow, const wxString& aLabel )
    {
        m_RowLabels.at( aRow ) = aLabel;
    }

    void SetColumnLabel( int aColumn, const wxString& aLabel )
    {
        m_ColumnLabels.at( aColumn ) = aLabel;
    }

    std::vector<std::vector<wxString>> m_Cells;
    std::vector<wxString>              m_RowLabels;
    std::vector<wxString>              m_ColumnLabels;
    int                                m_SetCellValueCount = 0;
};


class RECORDING_FIELD_IO : public AI_STRUCTURED_SURFACE_FIELD_IO
{
public:
    explicit RECORDING_FIELD_IO(
            std::vector<std::pair<std::string, wxString>> aFields ) :
            m_Fields( std::move( aFields ) )
    {
    }

    wxArrayString FieldIds() const override
    {
        wxArrayString ids;

        for( const auto& [id, value] : m_Fields )
        {
            wxUnusedVar( value );
            ids.Add( wxString::FromUTF8( id.c_str() ) );
        }

        return ids;
    }

    wxString FieldValue( const wxString& aFieldId ) const override
    {
        const std::string fieldId = aFieldId.ToStdString();

        for( const auto& [id, value] : m_Fields )
        {
            if( id == fieldId )
                return value;
        }

        return wxEmptyString;
    }

    void SetFieldValue( const wxString& aFieldId,
                        const wxString& aValue ) override
    {
        const std::string fieldId = aFieldId.ToStdString();

        for( auto& [id, value] : m_Fields )
        {
            if( id == fieldId )
            {
                value = aValue;
                ++m_SetFieldValueCount;
                return;
            }
        }

        m_Fields.emplace_back( fieldId, aValue );
        ++m_SetFieldValueCount;
    }

    std::vector<std::pair<std::string, wxString>> m_Fields;
    int                                          m_SetFieldValueCount = 0;
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


BOOST_AUTO_TEST_CASE( SurfacePatchReplayCommitsThroughStructuredSurfaceBackend )
{
    AI_EXECUTION_SESSION session = makeSession();

    const uint64_t stepId = session.BeginStep( wxS( "surface backend accept" ) );
    BOOST_REQUIRE_NE( stepId, 0 );
    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::ApplySurfacePatch,
            wxS( "{\"surface_id\":\"board_setup.clearance\","
                 "\"patch\":{\"kind\":\"SurfacePatch\","
                 "\"operations\":[{\"op\":\"set_field\","
                 "\"field_id\":\"default_clearance\","
                 "\"value\":\"0.20mm\"}]}}" ) )
                           .m_Ok );
    session.EndStep( stepId );

    RECORDING_STRUCTURED_SURFACE_BACKEND backend;
    backend.m_StateJson =
            wxS( "{\"surfaces\":{\"board_setup.clearance\":{\"fields\":"
                 "{\"default_clearance\":\"0.15mm\"}}}}" );

    AI_STRUCTURED_SURFACE_APPLY_ADAPTER adapter( backend );

    AI_ACCEPT_APPLY_RESULT result = AI_ACCEPT_APPLIER::Apply(
            session, wxS( "base-hash-accept" ), session.ContextVersion(), adapter );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_CHECK_EQUAL( backend.m_BeginCount, 1 );
    BOOST_CHECK_EQUAL( backend.m_CommitCount, 1 );
    BOOST_CHECK_EQUAL( backend.m_AbortCount, 0 );
    BOOST_CHECK( backend.m_LastChanged );
    BOOST_CHECK( backend.m_StateJson.Contains(
            wxS( "\"default_clearance\":\"0.20mm\"" ) ) );
}


BOOST_AUTO_TEST_CASE( SurfacePatchReplayCommitsIntoGridStateBackend )
{
    AI_EXECUTION_SESSION session = makeSession();

    const uint64_t stepId = session.BeginStep( wxS( "wx grid surface accept" ) );
    BOOST_REQUIRE_NE( stepId, 0 );
    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::ApplySurfacePatch,
            wxS( "{\"surface_id\":\"board_setup.netclasses\","
                 "\"table_id\":\"netclass.assignments\","
                 "\"expected_surface_revision\":4,"
                 "\"expected_schema_version\":\"netclasses-grid-v1\","
                 "\"expected_selection_fingerprint\":\"cell:r1:c1\","
                 "\"expected_overlap_set\":[\"r1\"],"
                 "\"patch\":{\"kind\":\"SurfacePatch\","
                 "\"operations\":[{\"op\":\"set_cell\","
                 "\"row_id\":\"r1\","
                 "\"column_id\":\"c1\","
                 "\"value\":\"Power\"}]}}" ) )
                           .m_Ok );
    session.EndStep( stepId );

    auto gridIo = std::make_unique<RECORDING_GRID_IO>(
            std::vector<std::vector<wxString>>{
                    { wxS( "GND" ), wxS( "Signal" ) },
                    { wxS( "VCC" ), wxS( "Signal" ) } } );
    RECORDING_GRID_IO* grid = gridIo.get();
    grid->SetRowLabel( 0, wxS( "Signal" ) );
    grid->SetRowLabel( 1, wxS( "Power" ) );
    grid->SetColumnLabel( 0, wxS( "Net" ) );
    grid->SetColumnLabel( 1, wxS( "Class" ) );

    AI_STRUCTURED_SURFACE_GRID_STATE_BACKEND backend(
            std::move( gridIo ), wxS( "board_setup.netclasses" ),
            wxS( "netclass.assignments" ) );
    backend.SetSurfaceRevision( 4 );
    backend.SetSchemaVersion( wxS( "netclasses-grid-v1" ) );
    backend.SetSelectionFingerprint( wxS( "cell:r1:c1" ) );
    backend.SetOverlapSet( { wxS( "r1" ) } );

    AI_STRUCTURED_SURFACE_APPLY_ADAPTER adapter( backend );

    AI_ACCEPT_APPLY_RESULT result = AI_ACCEPT_APPLIER::Apply(
            session, wxS( "base-hash-accept" ), session.ContextVersion(), adapter );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_CHECK_EQUAL( result.m_AppliedOperationCount, 1 );
    BOOST_CHECK_EQUAL( grid->m_Cells[1][1], wxString( wxS( "Power" ) ) );
    BOOST_CHECK_EQUAL( grid->m_SetCellValueCount, 1 );
    BOOST_CHECK_EQUAL( backend.SurfaceRevision(), 5 );
}


BOOST_AUTO_TEST_CASE( WxGridBackendImplementsStructuredSurfaceBackendContract )
{
    BOOST_CHECK( ( std::is_base_of_v<AI_STRUCTURED_SURFACE_STATE_BACKEND,
                                     AI_STRUCTURED_SURFACE_WX_GRID_BACKEND> ) );
}


BOOST_AUTO_TEST_CASE( SurfacePatchReplayCommitsIntoFieldStateBackend )
{
    AI_EXECUTION_SESSION session = makeSession();

    const uint64_t stepId = session.BeginStep( wxS( "field surface accept" ) );
    BOOST_REQUIRE_NE( stepId, 0 );
    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::ApplySurfacePatch,
            wxS( "{\"surface_id\":\"board_setup.rules\","
                 "\"expected_surface_revision\":10,"
                 "\"expected_schema_version\":\"rule-fields-v1\","
                 "\"expected_selection_fingerprint\":\"field:default_clearance\","
                 "\"expected_overlap_set\":[\"default_clearance\"],"
                 "\"patch\":{\"kind\":\"SurfacePatch\","
                 "\"operations\":[{\"op\":\"set_field\","
                 "\"field_id\":\"default_clearance\","
                 "\"value\":\"0.20mm\"}]}}" ) )
                           .m_Ok );
    session.EndStep( stepId );

    auto fieldIo = std::make_unique<RECORDING_FIELD_IO>(
            std::vector<std::pair<std::string, wxString>>{
                    { "default_clearance", wxS( "0.15mm" ) },
                    { "neckdown", wxS( "disabled" ) } } );
    RECORDING_FIELD_IO* fields = fieldIo.get();

    AI_STRUCTURED_SURFACE_FIELD_STATE_BACKEND backend(
            std::move( fieldIo ), wxS( "board_setup.rules" ) );
    backend.SetSurfaceRevision( 10 );
    backend.SetSchemaVersion( wxS( "rule-fields-v1" ) );
    backend.SetSelectionFingerprint( wxS( "field:default_clearance" ) );
    backend.SetOverlapSet( { wxS( "default_clearance" ) } );

    AI_STRUCTURED_SURFACE_APPLY_ADAPTER adapter( backend );

    AI_ACCEPT_APPLY_RESULT result = AI_ACCEPT_APPLIER::Apply(
            session, wxS( "base-hash-accept" ), session.ContextVersion(), adapter );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_CHECK_EQUAL( result.m_AppliedOperationCount, 1 );
    BOOST_CHECK_EQUAL( fields->FieldValue( wxS( "default_clearance" ) ),
                       wxString( wxS( "0.20mm" ) ) );
    BOOST_CHECK_EQUAL( fields->m_SetFieldValueCount, 1 );
    BOOST_CHECK_EQUAL( backend.SurfaceRevision(), 11 );
}


BOOST_AUTO_TEST_CASE( WxPropertyGridBackendImplementsStructuredSurfaceBackendContract )
{
    BOOST_CHECK( ( std::is_base_of_v<AI_STRUCTURED_SURFACE_STATE_BACKEND,
                                     AI_STRUCTURED_SURFACE_WX_PROPERTY_GRID_BACKEND> ) );
}


BOOST_AUTO_TEST_CASE( SurfacePatchBackendCommitFailureAbortsAccept )
{
    AI_EXECUTION_SESSION session = makeSession();

    const uint64_t stepId = session.BeginStep( wxS( "surface backend failure" ) );
    BOOST_REQUIRE_NE( stepId, 0 );
    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::ApplySurfacePatch,
            wxS( "{\"surface_id\":\"board_setup.clearance\","
                 "\"patch\":{\"kind\":\"SurfacePatch\","
                 "\"operations\":[{\"op\":\"set_field\","
                 "\"field_id\":\"default_clearance\","
                 "\"value\":\"0.20mm\"}]}}" ) )
                           .m_Ok );
    session.EndStep( stepId );

    RECORDING_STRUCTURED_SURFACE_BACKEND backend;
    backend.m_StateJson =
            wxS( "{\"surfaces\":{\"board_setup.clearance\":{\"fields\":"
                 "{\"default_clearance\":\"0.15mm\"}}}}" );
    const wxString originalState = backend.m_StateJson;
    backend.m_CommitOk = false;

    AI_STRUCTURED_SURFACE_APPLY_ADAPTER adapter( backend );

    AI_ACCEPT_APPLY_RESULT result = AI_ACCEPT_APPLIER::Apply(
            session, wxS( "base-hash-accept" ), session.ContextVersion(), adapter );

    BOOST_CHECK( !result.m_Ok );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString( wxS( "commit_failed" ) ) );
    BOOST_CHECK_EQUAL( backend.m_BeginCount, 1 );
    BOOST_CHECK_EQUAL( backend.m_CommitCount, 1 );
    BOOST_CHECK_EQUAL( backend.m_AbortCount, 1 );
    BOOST_CHECK_EQUAL( backend.m_StateJson, originalState );
    BOOST_CHECK( session.Status() == AI_EXECUTION_SESSION_STATUS::Open );
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


BOOST_AUTO_TEST_CASE( SurfacePatchReplayAdvancesNumericSurfaceRevisionOnCommit )
{
    AI_EXECUTION_SESSION session = makeSession();

    const uint64_t stepId = session.BeginStep( wxS( "surface revision advance" ) );
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
                 "\"revision\":7,"
                 "\"fields\":{\"default_clearance\":\"0.15mm\"}}}}" );

    AI_STRUCTURED_SURFACE_APPLY_ADAPTER adapter( surfaceState );

    AI_ACCEPT_APPLY_RESULT result = AI_ACCEPT_APPLIER::Apply(
            session, wxS( "base-hash-accept" ), session.ContextVersion(), adapter );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_CHECK( surfaceState.Contains( wxS( "\"revision\":8" ) ) );
    BOOST_CHECK( surfaceState.Contains(
            wxS( "\"default_clearance\":\"0.20mm\"" ) ) );
}


BOOST_AUTO_TEST_CASE( SurfacePatchReplayRejectsStaleSchemaVersionAndAborts )
{
    AI_EXECUTION_SESSION session = makeSession();

    const uint64_t stepId = session.BeginStep( wxS( "stale surface schema" ) );
    BOOST_REQUIRE_NE( stepId, 0 );
    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::ApplySurfacePatch,
            wxS( "{\"surface_id\":\"board_setup.clearance\","
                 "\"expected_schema_version\":\"net-class-v1\","
                 "\"patch\":{\"kind\":\"SurfacePatch\","
                 "\"operations\":[{\"op\":\"set_field\","
                 "\"field_id\":\"default_clearance\","
                 "\"value\":\"0.20mm\"}]}}" ) )
                           .m_Ok );
    session.EndStep( stepId );

    wxString surfaceState =
            wxS( "{\"surfaces\":{\"board_setup.clearance\":{"
                 "\"schema_version\":\"net-class-v2\","
                 "\"fields\":{\"default_clearance\":\"0.15mm\"}}}}" );
    const wxString originalSurfaceState = surfaceState;

    AI_STRUCTURED_SURFACE_APPLY_ADAPTER adapter( surfaceState );

    AI_ACCEPT_APPLY_RESULT result = AI_ACCEPT_APPLIER::Apply(
            session, wxS( "base-hash-accept" ), session.ContextVersion(), adapter );

    BOOST_CHECK( !result.m_Ok );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString( wxS( "apply_failed" ) ) );
    BOOST_CHECK( result.m_Message.Contains( wxS( "stale schema version" ) ) );
    BOOST_CHECK_EQUAL( surfaceState, originalSurfaceState );
    BOOST_CHECK( session.Status() == AI_EXECUTION_SESSION_STATUS::Open );
}


BOOST_AUTO_TEST_CASE( SurfacePatchReplayRejectsStaleSelectionFingerprintAndAborts )
{
    AI_EXECUTION_SESSION session = makeSession();

    const uint64_t stepId = session.BeginStep( wxS( "stale surface selection" ) );
    BOOST_REQUIRE_NE( stepId, 0 );
    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::ApplySurfacePatch,
            wxS( "{\"surface_id\":\"board_setup.clearance\","
                 "\"expected_selection_fingerprint\":\"cell:row.power:class\","
                 "\"patch\":{\"kind\":\"SurfacePatch\","
                 "\"operations\":[{\"op\":\"set_field\","
                 "\"field_id\":\"default_clearance\","
                 "\"value\":\"0.20mm\"}]}}" ) )
                           .m_Ok );
    session.EndStep( stepId );

    wxString surfaceState =
            wxS( "{\"surfaces\":{\"board_setup.clearance\":{"
                 "\"selection_fingerprint\":\"cell:row.gpio:class\","
                 "\"fields\":{\"default_clearance\":\"0.15mm\"}}}}" );
    const wxString originalSurfaceState = surfaceState;

    AI_STRUCTURED_SURFACE_APPLY_ADAPTER adapter( surfaceState );

    AI_ACCEPT_APPLY_RESULT result = AI_ACCEPT_APPLIER::Apply(
            session, wxS( "base-hash-accept" ), session.ContextVersion(), adapter );

    BOOST_CHECK( !result.m_Ok );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString( wxS( "apply_failed" ) ) );
    BOOST_CHECK( result.m_Message.Contains( wxS( "stale selection fingerprint" ) ) );
    BOOST_CHECK_EQUAL( surfaceState, originalSurfaceState );
    BOOST_CHECK( session.Status() == AI_EXECUTION_SESSION_STATUS::Open );
}


BOOST_AUTO_TEST_CASE( SurfacePatchReplayRejectsStaleOverlapSetAndAborts )
{
    AI_EXECUTION_SESSION session = makeSession();

    const uint64_t stepId = session.BeginStep( wxS( "stale surface overlap set" ) );
    BOOST_REQUIRE_NE( stepId, 0 );
    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::ApplySurfacePatch,
            wxS( "{\"surface_id\":\"board_setup.clearance\","
                 "\"expected_overlap_set\":[\"row.power\"],"
                 "\"patch\":{\"kind\":\"SurfacePatch\","
                 "\"operations\":[{\"op\":\"set_field\","
                 "\"field_id\":\"default_clearance\","
                 "\"value\":\"0.20mm\"}]}}" ) )
                           .m_Ok );
    session.EndStep( stepId );

    wxString surfaceState =
            wxS( "{\"surfaces\":{\"board_setup.clearance\":{"
                 "\"overlap_set\":[\"row.power\",\"row.gpio\"],"
                 "\"fields\":{\"default_clearance\":\"0.15mm\"}}}}" );
    const wxString originalSurfaceState = surfaceState;

    AI_STRUCTURED_SURFACE_APPLY_ADAPTER adapter( surfaceState );

    AI_ACCEPT_APPLY_RESULT result = AI_ACCEPT_APPLIER::Apply(
            session, wxS( "base-hash-accept" ), session.ContextVersion(), adapter );

    BOOST_CHECK( !result.m_Ok );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString( wxS( "apply_failed" ) ) );
    BOOST_CHECK( result.m_Message.Contains( wxS( "stale overlap set" ) ) );
    BOOST_CHECK_EQUAL( surfaceState, originalSurfaceState );
    BOOST_CHECK( session.Status() == AI_EXECUTION_SESSION_STATUS::Open );
}


BOOST_AUTO_TEST_CASE( SurfacePatchReplayAcceptsEquivalentOverlapSetOrdering )
{
    AI_EXECUTION_SESSION session = makeSession();

    const uint64_t stepId = session.BeginStep( wxS( "equivalent overlap set" ) );
    BOOST_REQUIRE_NE( stepId, 0 );
    BOOST_REQUIRE( AI_ATOMIC_OPERATION_EXECUTOR::Execute(
            session, AI_SESSION_OPERATION_KIND::ApplySurfacePatch,
            wxS( "{\"surface_id\":\"board_setup.clearance\","
                 "\"expected_overlap_set\":[\"row.gpio\",\"row.power\"],"
                 "\"patch\":{\"kind\":\"SurfacePatch\","
                 "\"operations\":[{\"op\":\"set_field\","
                 "\"field_id\":\"default_clearance\","
                 "\"value\":\"0.20mm\"}]}}" ) )
                           .m_Ok );
    session.EndStep( stepId );

    wxString surfaceState =
            wxS( "{\"surfaces\":{\"board_setup.clearance\":{"
                 "\"overlap_set\":[\"row.power\",\"row.gpio\"],"
                 "\"fields\":{\"default_clearance\":\"0.15mm\"}}}}" );

    AI_STRUCTURED_SURFACE_APPLY_ADAPTER adapter( surfaceState );

    AI_ACCEPT_APPLY_RESULT result = AI_ACCEPT_APPLIER::Apply(
            session, wxS( "base-hash-accept" ), session.ContextVersion(), adapter );

    BOOST_REQUIRE( result.m_Ok );
    BOOST_CHECK_EQUAL( result.m_AppliedOperationCount, 1 );
    BOOST_CHECK( session.Status() == AI_EXECUTION_SESSION_STATUS::Accepted );
    BOOST_CHECK( surfaceState.Contains(
            wxS( "\"default_clearance\":\"0.20mm\"" ) ) );
}


BOOST_AUTO_TEST_SUITE_END()
