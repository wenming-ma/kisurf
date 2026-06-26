#include <boost/test/unit_test.hpp>

#include <json_common.h>
#include <ai/session.pb.h>
#include <kisurf/ai/ai_python_local_worker.h>
#include <kisurf/ai/ai_python_worker_protocol.h>

#include <nlohmann/json.hpp>

#include <string>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <wx/filename.h>
#include <wx/stopwatch.h>
#include <wx/utils.h>

namespace
{
std::string toStdString( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


kiapi::ai::session::WorkerResponse makeCellResponse()
{
    kiapi::ai::session::WorkerResponse response;
    response.set_protocol( "kisurf.ai.session.v1" );

    kiapi::ai::session::CellResult* result = response.mutable_cell_result();
    result->set_ok( true );
    result->set_rollback_on_error( true );
    result->mutable_session()->set_id( 42 );
    result->mutable_session()->set_board_id( "board-main" );
    result->mutable_session()->set_base_hash( "h0" );
    result->mutable_session()->set_epoch( 7 );

    return response;
}


std::string serialize( const kiapi::ai::session::WorkerResponse& aResponse )
{
    return aResponse.SerializeAsString();
}


void addOperation( kiapi::ai::session::CellResult* aResult,
                   kiapi::ai::session::OperationKind aKind,
                   const std::string& aArgumentsJson = "{}" )
{
    kiapi::ai::session::OperationRequest* op = aResult->add_operations();
    op->set_kind( aKind );
    op->set_arguments_json( aArgumentsJson );
}


class RECORDING_EVENT_SINK : public AI_PYTHON_EVENT_SINK
{
public:
    void OnPythonEvent( const AI_PYTHON_EVENT& aEvent ) override
    {
        {
            std::lock_guard<std::mutex> lock( m_Mutex );
            m_Events.push_back( aEvent );
        }

        m_Condition.notify_all();
    }

    bool WaitForEventCount( size_t aCount, long aTimeoutMs )
    {
        std::unique_lock<std::mutex> lock( m_Mutex );
        return m_Condition.wait_for(
                lock, std::chrono::milliseconds( aTimeoutMs ),
                [&]() { return m_Events.size() >= aCount; } );
    }

    std::vector<AI_PYTHON_EVENT> Events() const
    {
        std::lock_guard<std::mutex> lock( m_Mutex );
        return m_Events;
    }

private:
    mutable std::mutex             m_Mutex;
    std::condition_variable        m_Condition;
    std::vector<AI_PYTHON_EVENT>   m_Events;
};
} // namespace


BOOST_AUTO_TEST_SUITE( AiPythonWorkerProtocol )


BOOST_AUTO_TEST_CASE( EncodeRunCellRequestCarriesSessionAndCellContext )
{
    AI_PYTHON_CELL_REQUEST request;
    request.m_SessionId = 42;
    request.m_BoardId = wxS( "board-main" );
    request.m_BaseHash = wxS( "doc:10/sel:2/view:4" );
    request.m_Epoch = 7;
    request.m_CellId = wxS( "cell-1" );
    request.m_CellText = wxS( "session.create_via(position=(1, 2), net='GND')" );

    kiapi::ai::session::WorkerRequest payload;
    BOOST_REQUIRE( payload.ParseFromString(
            AI_PYTHON_WORKER_PROTOCOL::EncodeRunCellRequest( request ) ) );

    BOOST_CHECK_EQUAL( payload.protocol(), "kisurf.ai.session.v1" );
    BOOST_REQUIRE( payload.has_run_cell() );
    BOOST_CHECK_EQUAL( payload.run_cell().session().id(), 42 );
    BOOST_CHECK_EQUAL( payload.run_cell().session().board_id(), "board-main" );
    BOOST_CHECK_EQUAL( payload.run_cell().session().base_hash(),
                       "doc:10/sel:2/view:4" );
    BOOST_CHECK_EQUAL( payload.run_cell().session().epoch(), 7 );
    BOOST_CHECK_EQUAL( payload.run_cell().cell().id(), "cell-1" );
    BOOST_CHECK_EQUAL( payload.run_cell().cell().text(),
                       "session.create_via(position=(1, 2), net='GND')" );
}


BOOST_AUTO_TEST_CASE( EncodeRunCellRequestUsesProtobufPayload )
{
    AI_PYTHON_CELL_REQUEST request;
    request.m_SessionId = 42;
    request.m_BoardId = wxS( "board-main" );
    request.m_BaseHash = wxS( "doc:10/sel:2/view:4" );
    request.m_Epoch = 7;
    request.m_CellId = wxS( "cell-1" );
    request.m_CellText = wxS( "session.create_via(position=(1, 2), net='GND')" );

    const std::string payload = AI_PYTHON_WORKER_PROTOCOL::EncodeRunCellRequest( request );

    BOOST_REQUIRE( !payload.empty() );
    BOOST_CHECK_NE( payload.front(), '{' );

    kiapi::ai::session::WorkerRequest parsed;
    BOOST_REQUIRE( parsed.ParseFromString( payload ) );
    BOOST_CHECK_EQUAL( parsed.protocol(), "kisurf.ai.session.v1" );
    BOOST_REQUIRE( parsed.has_run_cell() );
    BOOST_CHECK_EQUAL( parsed.run_cell().session().id(), 42 );
    BOOST_CHECK_EQUAL( parsed.run_cell().session().board_id(), "board-main" );
    BOOST_CHECK_EQUAL( parsed.run_cell().session().base_hash(), "doc:10/sel:2/view:4" );
    BOOST_CHECK_EQUAL( parsed.run_cell().session().epoch(), 7 );
    BOOST_CHECK_EQUAL( parsed.run_cell().cell().id(), "cell-1" );
    BOOST_CHECK_EQUAL( parsed.run_cell().cell().text(),
                       "session.create_via(position=(1, 2), net='GND')" );
}


BOOST_AUTO_TEST_CASE( EncodeCancelSessionRequestCarriesSessionAndReason )
{
    const std::string payloadText =
            AI_PYTHON_WORKER_PROTOCOL::EncodeCancelSessionRequest(
                    42, wxS( "user rejected preview" ) );
    kiapi::ai::session::WorkerRequest payload;
    BOOST_REQUIRE( payload.ParseFromString( payloadText ) );

    BOOST_CHECK_EQUAL( payload.protocol(), "kisurf.ai.session.v1" );
    BOOST_REQUIRE( payload.has_cancel_session() );
    BOOST_CHECK_EQUAL( payload.cancel_session().session().id(), 42 );
    BOOST_CHECK_EQUAL( payload.cancel_session().reason(),
                       "user rejected preview" );
}


BOOST_AUTO_TEST_CASE( EncodeFrameWrapsProtobufPayloadWithExplicitByteLength )
{
    const std::string payload( "abc\0def", 7 );

    const std::string frame = AI_PYTHON_WORKER_PROTOCOL::EncodeFrame( payload );

    BOOST_CHECK( frame.rfind( "KISURF_AI_FRAME_V1 ", 0 ) == 0 );
    BOOST_CHECK( frame.find( "\nabc" ) != std::string::npos );

    std::string decoded;
    wxString error;

    BOOST_REQUIRE(
            AI_PYTHON_WORKER_PROTOCOL::DecodeFrame( frame, &decoded, &error ) );
    BOOST_CHECK( error.IsEmpty() );
    BOOST_CHECK_EQUAL( decoded, payload );
}


BOOST_AUTO_TEST_CASE( DecodeFrameRejectsTruncatedPayload )
{
    std::string decoded;
    wxString error;

    BOOST_CHECK( !AI_PYTHON_WORKER_PROTOCOL::DecodeFrame(
            "KISURF_AI_FRAME_V1 20\nabc", &decoded, &error ) );
    BOOST_CHECK( error.Contains( wxS( "truncated" ) ) );
    BOOST_CHECK( decoded.empty() );
}


BOOST_AUTO_TEST_CASE( DecodeCellResultMapsSdkOperationsToAtomicRequests )
{
    kiapi::ai::session::WorkerResponse response = makeCellResponse();
    response.mutable_cell_result()->set_step_label( "python via ring" );
    response.mutable_cell_result()->set_stdout_text( "placed" );
    addOperation( response.mutable_cell_result(), kiapi::ai::session::PCB_CREATE_VIA,
                  "{\"alias\":\"v0\",\"position\":{\"x\":1,\"y\":2},\"net\":\"GND\"}" );
    addOperation( response.mutable_cell_result(), kiapi::ai::session::PCB_CREATE_TRACK_POLYLINE,
                  "{\"alias\":\"route\",\"points\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":1}]}" );

    wxString error;
    AI_PYTHON_CELL_RESULT result =
            AI_PYTHON_WORKER_PROTOCOL::DecodeCellResult( serialize( response ), &error );

    BOOST_CHECK( error.IsEmpty() );
    BOOST_CHECK( result.m_Ok );
    BOOST_CHECK( result.m_HasSessionContext );
    BOOST_CHECK_EQUAL( result.m_SessionId, 42 );
    BOOST_CHECK_EQUAL( result.m_BoardId, wxString( wxS( "board-main" ) ) );
    BOOST_CHECK_EQUAL( result.m_BaseHash, wxString( wxS( "h0" ) ) );
    BOOST_CHECK_EQUAL( result.m_Epoch, 7 );
    BOOST_CHECK_EQUAL( result.m_StepLabel, wxString( wxS( "python via ring" ) ) );
    BOOST_CHECK_EQUAL( result.m_Stdout, wxString( wxS( "placed" ) ) );
    BOOST_REQUIRE_EQUAL( result.m_Operations.size(), 2 );
    BOOST_CHECK( result.m_Operations[0].m_Kind == AI_SESSION_OPERATION_KIND::CreateVia );
    BOOST_CHECK( result.m_Operations[1].m_Kind
                 == AI_SESSION_OPERATION_KIND::CreateTrackPolyline );

    nlohmann::json viaArgs = nlohmann::json::parse(
            toStdString( result.m_Operations[0].m_ArgumentsJson ) );
    BOOST_CHECK_EQUAL( viaArgs["alias"].get<std::string>(), "v0" );
    BOOST_CHECK_EQUAL( viaArgs["net"].get<std::string>(), "GND" );
}


BOOST_AUTO_TEST_CASE( DecodeCellResultCapturesWorkerEvents )
{
    kiapi::ai::session::WorkerResponse response = makeCellResponse();
    kiapi::ai::session::WorkerEvent* progress =
            response.mutable_cell_result()->add_events();
    progress->set_kind( "progress" );
    progress->set_message( "placed guard ring" );
    progress->set_payload_json( "{\"step\":\"zone\",\"count\":1}" );

    kiapi::ai::session::WorkerEvent* inspection =
            response.mutable_cell_result()->add_events();
    inspection->set_kind( "inspection" );
    inspection->set_message( "needs clearance review" );
    inspection->set_payload_json( "{\"severity\":\"warning\"}" );

    wxString error;
    AI_PYTHON_CELL_RESULT result =
            AI_PYTHON_WORKER_PROTOCOL::DecodeCellResult( serialize( response ), &error );

    BOOST_CHECK( error.IsEmpty() );
    BOOST_CHECK( result.m_Ok );
    BOOST_REQUIRE_EQUAL( result.m_Events.size(), 2 );
    BOOST_CHECK_EQUAL( result.m_Events[0].m_Kind, wxString( wxS( "progress" ) ) );
    BOOST_CHECK_EQUAL( result.m_Events[0].m_Message,
                       wxString( wxS( "placed guard ring" ) ) );

    nlohmann::json payload =
            nlohmann::json::parse( toStdString( result.m_Events[0].m_PayloadJson ) );
    BOOST_CHECK_EQUAL( payload["step"].get<std::string>(), "zone" );
    BOOST_CHECK_EQUAL( payload["count"].get<int>(), 1 );
    BOOST_CHECK_EQUAL( result.m_Events[1].m_Kind, wxString( wxS( "inspection" ) ) );
    BOOST_CHECK_EQUAL( result.m_Events[1].m_Message,
                       wxString( wxS( "needs clearance review" ) ) );
}


BOOST_AUTO_TEST_CASE( DecodeCellResultCapturesSdkRuntimeMetadata )
{
    kiapi::ai::session::WorkerResponse response = makeCellResponse();
    response.mutable_cell_result()->mutable_sdk()->set_name(
            "kisurf-ai-session-sdk" );
    response.mutable_cell_result()->mutable_sdk()->set_version( "0.1.0" );
    response.mutable_cell_result()->mutable_sdk()->set_protocol(
            "kisurf.ai.session.v1" );

    wxString error;
    AI_PYTHON_CELL_RESULT result =
            AI_PYTHON_WORKER_PROTOCOL::DecodeCellResult( serialize( response ), &error );

    BOOST_CHECK( error.IsEmpty() );
    BOOST_CHECK( result.m_Ok );
    BOOST_CHECK_EQUAL( result.m_SdkName,
                       wxString( wxS( "kisurf-ai-session-sdk" ) ) );
    BOOST_CHECK_EQUAL( result.m_SdkVersion, wxString( wxS( "0.1.0" ) ) );
    BOOST_CHECK_EQUAL( result.m_SdkProtocol,
                       wxString( wxS( "kisurf.ai.session.v1" ) ) );
}


BOOST_AUTO_TEST_CASE( DecodeCellResultMapsPrimitiveMutationAndMaintenanceRequests )
{
    kiapi::ai::session::WorkerResponse response = makeCellResponse();
    addOperation( response.mutable_cell_result(), kiapi::ai::session::PCB_CREATE_TRACK_SEGMENT );
    addOperation( response.mutable_cell_result(), kiapi::ai::session::PCB_CREATE_ZONE );
    addOperation( response.mutable_cell_result(), kiapi::ai::session::PCB_CREATE_SHAPE );
    addOperation( response.mutable_cell_result(), kiapi::ai::session::PCB_MOVE_ITEMS );
    addOperation( response.mutable_cell_result(), kiapi::ai::session::PCB_DELETE_ITEMS );
    addOperation( response.mutable_cell_result(), kiapi::ai::session::PCB_UPDATE_ITEM_GEOMETRY );
    addOperation( response.mutable_cell_result(), kiapi::ai::session::PCB_SET_ITEM_NET );
    addOperation( response.mutable_cell_result(), kiapi::ai::session::PCB_SET_ITEM_LAYER );
    addOperation( response.mutable_cell_result(), kiapi::ai::session::PCB_SET_ITEM_PROPERTIES );
    addOperation( response.mutable_cell_result(), kiapi::ai::session::PCB_SET_METADATA );
    addOperation( response.mutable_cell_result(), kiapi::ai::session::PCB_REFILL_ZONES );
    addOperation( response.mutable_cell_result(), kiapi::ai::session::PCB_REBUILD_CONNECTIVITY );
    addOperation( response.mutable_cell_result(), kiapi::ai::session::PCB_RUN_VALIDATION );

    wxString error;
    AI_PYTHON_CELL_RESULT result =
            AI_PYTHON_WORKER_PROTOCOL::DecodeCellResult( serialize( response ), &error );

    const std::vector<AI_SESSION_OPERATION_KIND> expected = {
        AI_SESSION_OPERATION_KIND::CreateTrackSegment,
        AI_SESSION_OPERATION_KIND::CreateZone,
        AI_SESSION_OPERATION_KIND::CreateShape,
        AI_SESSION_OPERATION_KIND::MoveItems,
        AI_SESSION_OPERATION_KIND::DeleteItems,
        AI_SESSION_OPERATION_KIND::UpdateItemGeometry,
        AI_SESSION_OPERATION_KIND::SetItemNet,
        AI_SESSION_OPERATION_KIND::SetItemLayer,
        AI_SESSION_OPERATION_KIND::SetItemProperties,
        AI_SESSION_OPERATION_KIND::SetMetadata,
        AI_SESSION_OPERATION_KIND::RefillZones,
        AI_SESSION_OPERATION_KIND::RebuildConnectivity,
        AI_SESSION_OPERATION_KIND::RunValidation
    };

    BOOST_CHECK( error.IsEmpty() );
    BOOST_CHECK( result.m_Ok );
    BOOST_REQUIRE_EQUAL( result.m_Operations.size(), expected.size() );

    for( size_t i = 0; i < expected.size(); ++i )
        BOOST_CHECK( result.m_Operations[i].m_Kind == expected[i] );
}


BOOST_AUTO_TEST_CASE( DecodeCellResultMapsSurfacePatchRequest )
{
    kiapi::ai::session::WorkerResponse response = makeCellResponse();
    addOperation(
            response.mutable_cell_result(),
            static_cast<kiapi::ai::session::OperationKind>( 200 ),
            "{\"surface_id\":\"board_setup.clearance\","
            "\"patch\":{\"kind\":\"SurfacePatch\","
            "\"operations\":[{\"op\":\"set_cell\",\"row_id\":\"row.power\","
            "\"column_id\":\"class\",\"value\":\"Power\"}]},"
            "\"alias\":\"surface_patch_fill_class\"}" );

    wxString error;
    AI_PYTHON_CELL_RESULT result =
            AI_PYTHON_WORKER_PROTOCOL::DecodeCellResult( serialize( response ), &error );

    BOOST_CHECK( error.IsEmpty() );
    BOOST_CHECK( result.m_Ok );
    BOOST_REQUIRE_EQUAL( result.m_Operations.size(), 1 );
    BOOST_CHECK( result.m_Operations.front().m_Kind
                 == AI_SESSION_OPERATION_KIND::ApplySurfacePatch );

    nlohmann::json args = nlohmann::json::parse(
            toStdString( result.m_Operations.front().m_ArgumentsJson ) );
    BOOST_CHECK_EQUAL( args["surface_id"].get<std::string>(),
                       "board_setup.clearance" );
    BOOST_CHECK_EQUAL( args["alias"].get<std::string>(),
                       "surface_patch_fill_class" );
}


BOOST_AUTO_TEST_CASE( LocalWorkerRunsSurfacePatchFillHelpers )
{
    const wxString interpreter = wxS( "python" );

    wxFileName sdkRoot( wxString::FromUTF8( QA_SRC_ROOT ), wxEmptyString );
    sdkRoot.AppendDir( wxS( "common" ) );
    sdkRoot.AppendDir( wxS( "kisurf" ) );
    sdkRoot.AppendDir( wxS( "ai" ) );
    sdkRoot.AppendDir( wxS( "python" ) );

    AI_PYTHON_LOCAL_WORKER worker( interpreter, sdkRoot.GetPath() );
    BOOST_REQUIRE( worker.IsConnected() );

    AI_EXECUTION_SESSION::OPEN_OPTIONS options;
    options.m_SessionId = 14;
    options.m_BoardId = wxS( "board-main" );
    options.m_BaseHash = wxS( "h0" );
    AI_EXECUTION_SESSION session( options );

    AI_PYTHON_CELL_REQUEST request;
    request.m_SessionId = session.SessionId();
    request.m_BoardId = session.BoardId();
    request.m_BaseHash = session.BaseHash();
    request.m_Epoch = session.Epoch();
    request.m_CellId = wxS( "cell-surface-fill-helpers" );
    request.m_CellText = wxS(
            "ops = [\n"
            "    session.surface_fill_row_op(\n"
            "        row_id='row.power',\n"
            "        values={'class': 'Power', 'width': '0.30mm'}),\n"
            "    session.surface_fill_column_op(\n"
            "        column_id='priority',\n"
            "        values={'row.power': 1, 'row.gpio': 2}),\n"
            "    session.surface_fill_range_op(\n"
            "        cells=[{'row_id': 'row.gpio', 'column_id': 'class',\n"
            "                'value': 'GPIO'}]),\n"
            "    session.surface_set_property_op(\n"
            "        property_id='default_clearance', value='0.20mm'),\n"
            "]\n"
            "session.apply_surface_patch_ops(\n"
            "    surface_id='board_setup.clearance',\n"
            "    table_id='clearance.rules',\n"
            "    operations=ops,\n"
            "    write_policy='fill_empty_only',\n"
            "    alias='surface_patch_fill_helpers')\n" );

    AI_PYTHON_CELL_RESULT result = worker.RunCell( session, request );

    BOOST_REQUIRE_MESSAGE( result.m_Ok,
                           "error_code=" << toStdString( result.m_ErrorCode )
                                         << " message=" << toStdString( result.m_Message )
                                         << " stderr=" << toStdString( result.m_Stderr ) );
    BOOST_REQUIRE_EQUAL( result.m_Operations.size(), 1 );
    BOOST_CHECK( result.m_Operations[0].m_Kind
                 == AI_SESSION_OPERATION_KIND::ApplySurfacePatch );

    nlohmann::json args = nlohmann::json::parse(
            toStdString( result.m_Operations[0].m_ArgumentsJson ) );
    BOOST_CHECK_EQUAL( args["surface_id"].get<std::string>(),
                       "board_setup.clearance" );
    BOOST_CHECK_EQUAL( args["table_id"].get<std::string>(),
                       "clearance.rules" );
    BOOST_CHECK_EQUAL( args["write_policy"].get<std::string>(),
                       "fill_empty_only" );
    BOOST_CHECK_EQUAL( args["alias"].get<std::string>(),
                       "surface_patch_fill_helpers" );
    BOOST_REQUIRE_EQUAL( args["patch"]["operations"].size(), 4 );
    BOOST_CHECK_EQUAL( args["patch"]["operations"][0]["op"].get<std::string>(),
                       "fill_row" );
    BOOST_CHECK_EQUAL( args["patch"]["operations"][1]["op"].get<std::string>(),
                       "fill_column" );
    BOOST_CHECK_EQUAL( args["patch"]["operations"][2]["op"].get<std::string>(),
                       "fill_range" );
    BOOST_CHECK_EQUAL( args["patch"]["operations"][3]["op"].get<std::string>(),
                       "set_property" );
}


BOOST_AUTO_TEST_CASE( DecodeCellResultMapsSdkControlRequests )
{
    kiapi::ai::session::WorkerResponse response = makeCellResponse();
    addOperation( response.mutable_cell_result(), kiapi::ai::session::SESSION_CHECKPOINT,
                  "{\"name\":\"before trial\"}" );
    addOperation( response.mutable_cell_result(), kiapi::ai::session::SESSION_ROLLBACK_TO,
                  "{\"checkpoint_id\":3}" );

    wxString error;
    AI_PYTHON_CELL_RESULT result =
            AI_PYTHON_WORKER_PROTOCOL::DecodeCellResult( serialize( response ), &error );

    BOOST_CHECK( error.IsEmpty() );
    BOOST_CHECK( result.m_Ok );
    BOOST_REQUIRE_EQUAL( result.m_Operations.size(), 2 );
    BOOST_CHECK( result.m_Operations[0].m_Kind
                 == AI_SESSION_OPERATION_KIND::Checkpoint );
    BOOST_CHECK( result.m_Operations[1].m_Kind
                 == AI_SESSION_OPERATION_KIND::RollbackTo );

    nlohmann::json checkpointArgs = nlohmann::json::parse(
            toStdString( result.m_Operations[0].m_ArgumentsJson ) );
    nlohmann::json rollbackArgs = nlohmann::json::parse(
            toStdString( result.m_Operations[1].m_ArgumentsJson ) );
    BOOST_CHECK_EQUAL( checkpointArgs["name"].get<std::string>(), "before trial" );
    BOOST_CHECK_EQUAL( rollbackArgs["checkpoint_id"].get<uint64_t>(), 3 );
}


BOOST_AUTO_TEST_CASE( DecodeCellResultRejectsMissingProtocolAndSessionContext )
{
    kiapi::ai::session::WorkerResponse response;
    response.mutable_cell_result()->set_ok( true );

    wxString error;
    AI_PYTHON_CELL_RESULT result =
            AI_PYTHON_WORKER_PROTOCOL::DecodeCellResult( serialize( response ), &error );

    BOOST_CHECK( !result.m_Ok );
    BOOST_CHECK_EQUAL( result.m_ErrorCode,
                       wxString( wxS( "unsupported_python_protocol" ) ) );
    BOOST_CHECK( error.Contains( wxS( "kisurf.ai.session.v1" ) ) );
    BOOST_CHECK( result.m_Operations.empty() );
}


BOOST_AUTO_TEST_CASE( DecodeCellResultRejectsUnknownOperationKind )
{
    kiapi::ai::session::WorkerResponse response = makeCellResponse();
    addOperation( response.mutable_cell_result(),
                  static_cast<kiapi::ai::session::OperationKind>( 999 ) );

    wxString error;
    AI_PYTHON_CELL_RESULT result =
            AI_PYTHON_WORKER_PROTOCOL::DecodeCellResult( serialize( response ), &error );

    BOOST_CHECK( !result.m_Ok );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString( wxS( "unknown_operation_kind" ) ) );
    BOOST_CHECK( error.Contains( wxS( "999" ) ) );
    BOOST_CHECK( result.m_Operations.empty() );
}


BOOST_AUTO_TEST_CASE( LocalWorkerRunsSdkCellThroughPythonProcess )
{
    const wxString interpreter = wxS( "python" );

    wxFileName sdkRoot( wxString::FromUTF8( QA_SRC_ROOT ), wxEmptyString );
    sdkRoot.AppendDir( wxS( "common" ) );
    sdkRoot.AppendDir( wxS( "kisurf" ) );
    sdkRoot.AppendDir( wxS( "ai" ) );
    sdkRoot.AppendDir( wxS( "python" ) );

    AI_PYTHON_LOCAL_WORKER worker( interpreter, sdkRoot.GetPath() );
    BOOST_REQUIRE( worker.IsConnected() );

    AI_EXECUTION_SESSION::OPEN_OPTIONS options;
    options.m_SessionId = 9;
    options.m_BoardId = wxS( "board-main" );
    options.m_BaseHash = wxS( "h0" );
    AI_EXECUTION_SESSION session( options );

    AI_PYTHON_CELL_REQUEST request;
    request.m_SessionId = session.SessionId();
    request.m_BoardId = session.BoardId();
    request.m_BaseHash = session.BaseHash();
    request.m_Epoch = session.Epoch();
    request.m_CellId = wxS( "cell-local" );
    request.m_CellText = wxS(
            "with session.step('local process via'):\n"
            "    session.create_via(position={'x': 3, 'y': 4}, net='GND', alias='lv0')\n" );

    AI_PYTHON_CELL_RESULT result = worker.RunCell( session, request );

    BOOST_CHECK( result.m_Ok );
    BOOST_CHECK( result.m_HasSessionContext );
    BOOST_CHECK_EQUAL( result.m_SessionId, 9 );
    BOOST_CHECK_EQUAL( result.m_BoardId, wxString( wxS( "board-main" ) ) );
    BOOST_CHECK_EQUAL( result.m_BaseHash, wxString( wxS( "h0" ) ) );
    BOOST_CHECK_EQUAL( result.m_Epoch, 0 );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString() );
    BOOST_CHECK_EQUAL( result.m_StepLabel, wxString( wxS( "local process via" ) ) );
    BOOST_REQUIRE_EQUAL( result.m_Operations.size(), 1 );
    BOOST_CHECK( result.m_Operations[0].m_Kind == AI_SESSION_OPERATION_KIND::CreateVia );

    nlohmann::json args = nlohmann::json::parse(
            toStdString( result.m_Operations[0].m_ArgumentsJson ) );
    BOOST_CHECK_EQUAL( args["alias"].get<std::string>(), "lv0" );
    BOOST_CHECK_EQUAL( args["position"]["x"].get<int>(), 3 );
    BOOST_CHECK_EQUAL( args["position"]["y"].get<int>(), 4 );
}


BOOST_AUTO_TEST_CASE( LocalWorkerRunsCompositeAnnularZoneCell )
{
    const wxString interpreter = wxS( "python" );

    wxFileName sdkRoot( wxString::FromUTF8( QA_SRC_ROOT ), wxEmptyString );
    sdkRoot.AppendDir( wxS( "common" ) );
    sdkRoot.AppendDir( wxS( "kisurf" ) );
    sdkRoot.AppendDir( wxS( "ai" ) );
    sdkRoot.AppendDir( wxS( "python" ) );

    AI_PYTHON_LOCAL_WORKER worker( interpreter, sdkRoot.GetPath() );
    BOOST_REQUIRE( worker.IsConnected() );

    AI_EXECUTION_SESSION::OPEN_OPTIONS options;
    options.m_SessionId = 12;
    options.m_BoardId = wxS( "board-main" );
    options.m_BaseHash = wxS( "h0" );
    AI_EXECUTION_SESSION session( options );

    AI_PYTHON_CELL_REQUEST request;
    request.m_SessionId = session.SessionId();
    request.m_BoardId = session.BoardId();
    request.m_BaseHash = session.BaseHash();
    request.m_Epoch = session.Epoch();
    request.m_CellId = wxS( "cell-annular" );
    request.m_CellText = wxS(
            "with session.step('annular copper with via stitching'):\n"
            "    session.create_annular_zone(\n"
            "        center={'x': 1000, 'y': 2000},\n"
            "        inner_radius=250,\n"
            "        outer_radius=500,\n"
            "        segments=16,\n"
            "        layer_set=['F.Cu'],\n"
            "        net='GND',\n"
            "        alias='guard-ring')\n"
            "    session.create_via_ring(\n"
            "        center={'x': 1000, 'y': 2000},\n"
            "        radius=650,\n"
            "        count=8,\n"
            "        net='GND',\n"
            "        alias_prefix='stitch')\n" );

    AI_PYTHON_CELL_RESULT result = worker.RunCell( session, request );

    BOOST_REQUIRE_MESSAGE( result.m_Ok,
                           "error_code=" << toStdString( result.m_ErrorCode )
                                         << " message=" << toStdString( result.m_Message )
                                         << " stderr=" << toStdString( result.m_Stderr ) );
    BOOST_CHECK_EQUAL( result.m_StepLabel,
                       wxString( wxS( "annular copper with via stitching" ) ) );
    BOOST_REQUIRE_EQUAL( result.m_Operations.size(), 9 );
    BOOST_CHECK( result.m_Operations[0].m_Kind == AI_SESSION_OPERATION_KIND::CreateZone );
    BOOST_CHECK( result.m_Operations[1].m_Kind == AI_SESSION_OPERATION_KIND::CreateVia );
}


BOOST_AUTO_TEST_CASE( LocalWorkerKeepsNamespaceAcrossCellsInOneProcess )
{
    const wxString interpreter = wxS( "python" );

    wxFileName sdkRoot( wxString::FromUTF8( QA_SRC_ROOT ), wxEmptyString );
    sdkRoot.AppendDir( wxS( "common" ) );
    sdkRoot.AppendDir( wxS( "kisurf" ) );
    sdkRoot.AppendDir( wxS( "ai" ) );
    sdkRoot.AppendDir( wxS( "python" ) );

    AI_PYTHON_LOCAL_WORKER worker( interpreter, sdkRoot.GetPath() );
    BOOST_REQUIRE( worker.IsConnected() );

    AI_EXECUTION_SESSION::OPEN_OPTIONS options;
    options.m_SessionId = 10;
    options.m_BoardId = wxS( "board-main" );
    options.m_BaseHash = wxS( "h0" );
    AI_EXECUTION_SESSION session( options );

    AI_PYTHON_CELL_REQUEST first;
    first.m_SessionId = session.SessionId();
    first.m_BoardId = session.BoardId();
    first.m_BaseHash = session.BaseHash();
    first.m_Epoch = session.Epoch();
    first.m_CellId = wxS( "cell-define" );
    first.m_CellText = wxS( "pitch = 41\n" );

    AI_PYTHON_CELL_RESULT firstResult = worker.RunCell( session, first );
    BOOST_REQUIRE( firstResult.m_Ok );
    BOOST_CHECK( firstResult.m_Operations.empty() );

    AI_PYTHON_CELL_REQUEST second = first;
    second.m_CellId = wxS( "cell-use" );
    second.m_CellText = wxS(
            "with session.step('persistent namespace'):\n"
            "    session.create_via(position={'x': pitch, 'y': pitch + 1}, alias='pv0')\n" );

    AI_PYTHON_CELL_RESULT secondResult = worker.RunCell( session, second );
    BOOST_REQUIRE( secondResult.m_Ok );
    BOOST_CHECK_EQUAL( secondResult.m_StepLabel,
                       wxString( wxS( "persistent namespace" ) ) );
    BOOST_REQUIRE_EQUAL( secondResult.m_Operations.size(), 1 );

    nlohmann::json args = nlohmann::json::parse(
            toStdString( secondResult.m_Operations[0].m_ArgumentsJson ) );
    BOOST_CHECK_EQUAL( args["position"]["x"].get<int>(), 41 );
    BOOST_CHECK_EQUAL( args["position"]["y"].get<int>(), 42 );
}


BOOST_AUTO_TEST_CASE( LocalWorkerTimeoutHardKillsAndRestartsProcess )
{
    const wxString interpreter = wxS( "python" );

    wxFileName sdkRoot( wxString::FromUTF8( QA_SRC_ROOT ), wxEmptyString );
    sdkRoot.AppendDir( wxS( "common" ) );
    sdkRoot.AppendDir( wxS( "kisurf" ) );
    sdkRoot.AppendDir( wxS( "ai" ) );
    sdkRoot.AppendDir( wxS( "python" ) );

    AI_PYTHON_LOCAL_WORKER worker( interpreter, sdkRoot.GetPath(), 8000 );
    BOOST_REQUIRE( worker.IsConnected() );

    AI_EXECUTION_SESSION::OPEN_OPTIONS options;
    options.m_SessionId = 11;
    options.m_BoardId = wxS( "board-main" );
    options.m_BaseHash = wxS( "h0" );
    AI_EXECUTION_SESSION session( options );

    AI_PYTHON_CELL_REQUEST slow;
    slow.m_SessionId = session.SessionId();
    slow.m_BoardId = session.BoardId();
    slow.m_BaseHash = session.BaseHash();
    slow.m_Epoch = session.Epoch();
    slow.m_CellId = wxS( "cell-timeout" );
    slow.m_CellText = wxS( "import time\ntime.sleep(20)\n" );

    wxStopWatch timer;
    AI_PYTHON_CELL_RESULT timedOut = worker.RunCell( session, slow );

    BOOST_CHECK( !timedOut.m_Ok );
    BOOST_CHECK_EQUAL( timedOut.m_ErrorCode,
                       wxString( wxS( "python_worker_timeout" ) ) );
    BOOST_CHECK( timedOut.m_Message.Contains( wxS( "Timed out" ) ) );
    BOOST_CHECK_LT( timer.Time(), 12000 );

    AI_PYTHON_CELL_REQUEST afterTimeout = slow;
    afterTimeout.m_CellId = wxS( "cell-after-timeout" );
    afterTimeout.m_CellText = wxS(
            "with session.step('after timeout'):\n"
            "    session.create_via(position={'x': 12, 'y': 13}, alias='after_timeout')\n" );

    AI_PYTHON_CELL_RESULT recovered = worker.RunCell( session, afterTimeout );

    BOOST_REQUIRE_MESSAGE( recovered.m_Ok,
                           "error_code=" << toStdString( recovered.m_ErrorCode )
                                         << " message=" << toStdString( recovered.m_Message )
                                         << " stderr=" << toStdString( recovered.m_Stderr ) );
    BOOST_CHECK_EQUAL( recovered.m_StepLabel, wxString( wxS( "after timeout" ) ) );
    BOOST_REQUIRE_EQUAL( recovered.m_Operations.size(), 1 );
    BOOST_CHECK( recovered.m_Operations[0].m_Kind == AI_SESSION_OPERATION_KIND::CreateVia );
}


BOOST_AUTO_TEST_CASE( LocalWorkerCancelInterruptsRunningCellWithoutTimeout )
{
    const wxString interpreter = wxS( "python" );

    wxFileName sdkRoot( wxString::FromUTF8( QA_SRC_ROOT ), wxEmptyString );
    sdkRoot.AppendDir( wxS( "common" ) );
    sdkRoot.AppendDir( wxS( "kisurf" ) );
    sdkRoot.AppendDir( wxS( "ai" ) );
    sdkRoot.AppendDir( wxS( "python" ) );

    AI_PYTHON_LOCAL_WORKER worker( interpreter, sdkRoot.GetPath(), 20000 );
    BOOST_REQUIRE( worker.IsConnected() );

    AI_EXECUTION_SESSION::OPEN_OPTIONS options;
    options.m_SessionId = 13;
    options.m_BoardId = wxS( "board-main" );
    options.m_BaseHash = wxS( "h0" );
    AI_EXECUTION_SESSION session( options );

    AI_PYTHON_CELL_REQUEST slow;
    slow.m_SessionId = session.SessionId();
    slow.m_BoardId = session.BoardId();
    slow.m_BaseHash = session.BaseHash();
    slow.m_Epoch = session.Epoch();
    slow.m_CellId = wxS( "cell-cancel" );
    slow.m_CellText = wxS(
            "import time\n"
            "session.event('progress', message='about to sleep')\n"
            "time.sleep(20)\n" );

    AI_PYTHON_CELL_RESULT cancelled;
    wxStopWatch timer;
    std::thread runner( [&]() { cancelled = worker.RunCell( session, slow ); } );

    wxMilliSleep( 1000 );
    worker.Cancel();
    runner.join();

    BOOST_CHECK_LT( timer.Time(), 6000 );
    BOOST_CHECK_MESSAGE( !cancelled.m_Ok,
                         "unexpected ok stdout=" << toStdString( cancelled.m_Stdout )
                                                 << " stderr=" << toStdString( cancelled.m_Stderr ) );
    BOOST_CHECK_EQUAL( cancelled.m_ErrorCode,
                       wxString( wxS( "python_cancelled" ) ) );
    BOOST_CHECK_MESSAGE( cancelled.m_Message.Contains( wxS( "session cancelled" ) ),
                         "message=" << toStdString( cancelled.m_Message )
                                    << " stdout=" << toStdString( cancelled.m_Stdout )
                                    << " stderr=" << toStdString( cancelled.m_Stderr ) );
    BOOST_CHECK( cancelled.m_Operations.empty() );

    AI_PYTHON_CELL_REQUEST recovered = slow;
    recovered.m_CellId = wxS( "cell-after-cancel" );
    recovered.m_CellText = wxS(
            "with session.step('after cancel'):\n"
            "    session.create_via(position={'x': 14, 'y': 15}, alias='after_cancel')\n" );

    AI_PYTHON_CELL_RESULT afterCancel = worker.RunCell( session, recovered );

    BOOST_REQUIRE_MESSAGE( afterCancel.m_Ok,
                           "error_code=" << toStdString( afterCancel.m_ErrorCode )
                                         << " message=" << toStdString( afterCancel.m_Message )
                                         << " stderr=" << toStdString( afterCancel.m_Stderr ) );
    BOOST_CHECK_EQUAL( afterCancel.m_StepLabel, wxString( wxS( "after cancel" ) ) );
    BOOST_REQUIRE_EQUAL( afterCancel.m_Operations.size(), 1 );
    BOOST_CHECK( afterCancel.m_Operations[0].m_Kind == AI_SESSION_OPERATION_KIND::CreateVia );
}


BOOST_AUTO_TEST_CASE( LocalWorkerStreamsEventsBeforeCellCompletes )
{
    const wxString interpreter = wxS( "python" );

    wxFileName sdkRoot( wxString::FromUTF8( QA_SRC_ROOT ), wxEmptyString );
    sdkRoot.AppendDir( wxS( "common" ) );
    sdkRoot.AppendDir( wxS( "kisurf" ) );
    sdkRoot.AppendDir( wxS( "ai" ) );
    sdkRoot.AppendDir( wxS( "python" ) );

    AI_PYTHON_LOCAL_WORKER worker( interpreter, sdkRoot.GetPath(), 12000 );
    BOOST_REQUIRE( worker.IsConnected() );

    RECORDING_EVENT_SINK sink;
    worker.SetEventSink( &sink );

    AI_EXECUTION_SESSION::OPEN_OPTIONS options;
    options.m_SessionId = 14;
    options.m_BoardId = wxS( "board-main" );
    options.m_BaseHash = wxS( "h0" );
    AI_EXECUTION_SESSION session( options );

    AI_PYTHON_CELL_REQUEST request;
    request.m_SessionId = session.SessionId();
    request.m_BoardId = session.BoardId();
    request.m_BaseHash = session.BaseHash();
    request.m_Epoch = session.Epoch();
    request.m_CellId = wxS( "cell-stream-event" );
    request.m_CellText = wxS(
            "import time\n"
            "session.event('progress', message='streamed before completion',"
            " payload={'phase':'before_sleep'})\n"
            "time.sleep(2)\n"
            "with session.step('after streamed event'):\n"
            "    session.create_via(position={'x': 16, 'y': 17}, alias='streamed')\n" );

    std::atomic_bool finished{ false };
    AI_PYTHON_CELL_RESULT result;
    std::thread runner(
            [&]()
            {
                result = worker.RunCell( session, request );
                finished.store( true );
            } );

    BOOST_REQUIRE( sink.WaitForEventCount( 1, 5000 ) );
    BOOST_CHECK( !finished.load() );

    runner.join();

    BOOST_REQUIRE_MESSAGE( result.m_Ok,
                           "error_code=" << toStdString( result.m_ErrorCode )
                                         << " message=" << toStdString( result.m_Message )
                                         << " stderr=" << toStdString( result.m_Stderr ) );

    std::vector<AI_PYTHON_EVENT> events = sink.Events();
    BOOST_REQUIRE_EQUAL( events.size(), 1 );
    BOOST_CHECK_EQUAL( events.front().m_Kind, wxString( wxS( "progress" ) ) );
    BOOST_CHECK_EQUAL( events.front().m_Message,
                       wxString( wxS( "streamed before completion" ) ) );

    nlohmann::json payload =
            nlohmann::json::parse( toStdString( events.front().m_PayloadJson ) );
    BOOST_CHECK_EQUAL( payload["phase"].get<std::string>(), "before_sleep" );

    BOOST_REQUIRE_EQUAL( result.m_Events.size(), 1 );
    BOOST_REQUIRE_EQUAL( result.m_Operations.size(), 1 );
    BOOST_CHECK( result.m_Operations[0].m_Kind == AI_SESSION_OPERATION_KIND::CreateVia );
}


BOOST_AUTO_TEST_CASE( DefaultLocalWorkerFactoryFindsBundledSdkRoot )
{
    const wxString sdkRoot = AI_PYTHON_LOCAL_WORKER::DefaultSdkRootPath();

    BOOST_CHECK( !sdkRoot.IsEmpty() );
    BOOST_CHECK( wxDirExists( sdkRoot ) );
    BOOST_CHECK( sdkRoot.Contains( wxS( "common" ) ) );
    BOOST_CHECK( sdkRoot.Contains( wxS( "kisurf" ) ) );
}


BOOST_AUTO_TEST_CASE( LocalWorkerExposesInstallableSdkRootFallback )
{
    const wxString installedRoot = AI_PYTHON_LOCAL_WORKER::InstalledSdkRootPath();

    BOOST_CHECK( !installedRoot.IsEmpty() );
    BOOST_CHECK( installedRoot.Contains( wxS( "kisurf_ai_session_sdk" ) ) );
}


BOOST_AUTO_TEST_SUITE_END()
