/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 */

#include <kisurf/ai/ai_python_worker_protocol.h>

#include <ai/session.pb.h>

#include <cstring>
#include <exception>
#include <string>
#include <utility>

namespace
{
constexpr const char* KISURF_AI_FRAME_PREFIX = "KISURF_AI_FRAME_V1 ";


std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


wxString fromUtf8( const std::string& aText )
{
    return wxString::FromUTF8( aText.c_str() );
}


AI_PYTHON_CELL_RESULT protocolError( const wxString& aCode, const wxString& aMessage,
                                     wxString* aError )
{
    if( aError )
        *aError = aMessage;

    AI_PYTHON_CELL_RESULT result;
    result.m_Ok = false;
    result.m_ErrorCode = aCode;
    result.m_Message = aMessage;
    return result;
}


void fillSessionContext( kiapi::ai::session::SessionContext* aProto,
                         const AI_PYTHON_CELL_REQUEST& aRequest )
{
    aProto->set_id( aRequest.m_SessionId );
    aProto->set_board_id( toUtf8String( aRequest.m_BoardId ) );
    aProto->set_base_hash( toUtf8String( aRequest.m_BaseHash ) );
    aProto->set_epoch( aRequest.m_Epoch );
}


AI_SESSION_OPERATION_KIND operationKindFromProto(
        kiapi::ai::session::OperationKind aKind )
{
    using kiapi::ai::session::OperationKind;

    switch( aKind )
    {
    case OperationKind::SESSION_CHECKPOINT:          return AI_SESSION_OPERATION_KIND::Checkpoint;
    case OperationKind::SESSION_ROLLBACK_TO:         return AI_SESSION_OPERATION_KIND::RollbackTo;
    case OperationKind::QUERY_BOARD_SUMMARY:         return AI_SESSION_OPERATION_KIND::QueryBoardSummary;
    case OperationKind::QUERY_ITEMS:                 return AI_SESSION_OPERATION_KIND::QueryItems;
    case OperationKind::QUERY_ITEM:                  return AI_SESSION_OPERATION_KIND::QueryItem;
    case OperationKind::QUERY_SELECTION:             return AI_SESSION_OPERATION_KIND::QuerySelection;
    case OperationKind::QUERY_NETS:                  return AI_SESSION_OPERATION_KIND::QueryNets;
    case OperationKind::QUERY_LAYERS:                return AI_SESSION_OPERATION_KIND::QueryLayers;
    case OperationKind::QUERY_DESIGN_RULES:          return AI_SESSION_OPERATION_KIND::QueryDesignRules;
    case OperationKind::QUERY_VIEWPORT:              return AI_SESSION_OPERATION_KIND::QueryViewport;
    case OperationKind::QUERY_ACTIVITY_TIMELINE:     return AI_SESSION_OPERATION_KIND::QueryActivityTimeline;
    case OperationKind::RENDER_PREVIEW:              return AI_SESSION_OPERATION_KIND::RenderPreview;
    case OperationKind::OBSERVE_STEP:                return AI_SESSION_OPERATION_KIND::ObserveStep;
    case OperationKind::PCB_CREATE_VIA:              return AI_SESSION_OPERATION_KIND::CreateVia;
    case OperationKind::PCB_CREATE_TRACK_SEGMENT:    return AI_SESSION_OPERATION_KIND::CreateTrackSegment;
    case OperationKind::PCB_CREATE_TRACK_POLYLINE:   return AI_SESSION_OPERATION_KIND::CreateTrackPolyline;
    case OperationKind::PCB_CREATE_ZONE:             return AI_SESSION_OPERATION_KIND::CreateZone;
    case OperationKind::PCB_CREATE_SHAPE:            return AI_SESSION_OPERATION_KIND::CreateShape;
    case OperationKind::PCB_MOVE_ITEMS:              return AI_SESSION_OPERATION_KIND::MoveItems;
    case OperationKind::PCB_DELETE_ITEMS:            return AI_SESSION_OPERATION_KIND::DeleteItems;
    case OperationKind::PCB_UPDATE_ITEM_GEOMETRY:    return AI_SESSION_OPERATION_KIND::UpdateItemGeometry;
    case OperationKind::PCB_SET_ITEM_NET:            return AI_SESSION_OPERATION_KIND::SetItemNet;
    case OperationKind::PCB_SET_ITEM_LAYER:          return AI_SESSION_OPERATION_KIND::SetItemLayer;
    case OperationKind::PCB_SET_ITEM_PROPERTIES:     return AI_SESSION_OPERATION_KIND::SetItemProperties;
    case OperationKind::PCB_SET_METADATA:            return AI_SESSION_OPERATION_KIND::SetMetadata;
    case OperationKind::PCB_REFILL_ZONES:            return AI_SESSION_OPERATION_KIND::RefillZones;
    case OperationKind::PCB_REBUILD_CONNECTIVITY:    return AI_SESSION_OPERATION_KIND::RebuildConnectivity;
    case OperationKind::PCB_RUN_VALIDATION:          return AI_SESSION_OPERATION_KIND::RunValidation;
    case OperationKind::OPERATION_KIND_UNSPECIFIED:  return AI_SESSION_OPERATION_KIND::Unknown;
    case OperationKind::OperationKind_INT_MIN_SENTINEL_DO_NOT_USE_:
    case OperationKind::OperationKind_INT_MAX_SENTINEL_DO_NOT_USE_:
        return AI_SESSION_OPERATION_KIND::Unknown;
    }

    return AI_SESSION_OPERATION_KIND::Unknown;
}
} // namespace


std::string AI_PYTHON_WORKER_PROTOCOL::EncodeFrame( const std::string& aPayload )
{
    return std::string( KISURF_AI_FRAME_PREFIX ) + std::to_string( aPayload.size() )
           + "\n" + aPayload;
}


bool AI_PYTHON_WORKER_PROTOCOL::DecodeFrame( const std::string& aFrame,
                                             std::string* aPayload,
                                             wxString* aError )
{
    if( aPayload )
        aPayload->clear();

    if( aError )
        aError->clear();

    const size_t newline = aFrame.find( '\n' );

    if( newline == std::string::npos )
    {
        if( aError )
            *aError = wxS( "Python worker frame is missing its header terminator." );

        return false;
    }

    const std::string header = aFrame.substr( 0, newline );

    if( header.rfind( KISURF_AI_FRAME_PREFIX, 0 ) != 0 )
    {
        if( aError )
            *aError = wxS( "Python worker frame has an unsupported header." );

        return false;
    }

    const std::string lengthText = header.substr( std::strlen( KISURF_AI_FRAME_PREFIX ) );
    size_t            expectedLength = 0;

    try
    {
        expectedLength = static_cast<size_t>( std::stoull( lengthText ) );
    }
    catch( const std::exception& )
    {
        if( aError )
            *aError = wxS( "Python worker frame has an invalid payload length." );

        return false;
    }

    const size_t payloadOffset = newline + 1;
    const size_t availableLength = aFrame.size() - payloadOffset;

    if( availableLength < expectedLength )
    {
        if( aError )
            *aError = wxS( "Python worker frame payload is truncated." );

        return false;
    }

    if( availableLength > expectedLength )
    {
        if( aError )
            *aError = wxS( "Python worker frame has trailing bytes after payload." );

        return false;
    }

    if( aPayload )
        *aPayload = aFrame.substr( payloadOffset, expectedLength );

    return true;
}


std::string AI_PYTHON_WORKER_PROTOCOL::EncodeRunCellRequest(
        const AI_PYTHON_CELL_REQUEST& aRequest )
{
    kiapi::ai::session::WorkerRequest payload;
    payload.set_protocol( "kisurf.ai.session.v1" );
    fillSessionContext( payload.mutable_run_cell()->mutable_session(), aRequest );
    payload.mutable_run_cell()->mutable_cell()->set_id( toUtf8String( aRequest.m_CellId ) );
    payload.mutable_run_cell()->mutable_cell()->set_text( toUtf8String( aRequest.m_CellText ) );
    return payload.SerializeAsString();
}


std::string AI_PYTHON_WORKER_PROTOCOL::EncodeCancelSessionRequest(
        uint64_t aSessionId, const wxString& aReason )
{
    kiapi::ai::session::WorkerRequest payload;
    payload.set_protocol( "kisurf.ai.session.v1" );
    payload.mutable_cancel_session()->mutable_session()->set_id( aSessionId );
    payload.mutable_cancel_session()->set_reason( toUtf8String( aReason ) );
    return payload.SerializeAsString();
}


std::string AI_PYTHON_WORKER_PROTOCOL::EncodeShutdownRequest()
{
    kiapi::ai::session::WorkerRequest payload;
    payload.set_protocol( "kisurf.ai.session.v1" );
    payload.mutable_shutdown();
    return payload.SerializeAsString();
}


AI_PYTHON_CELL_RESULT AI_PYTHON_WORKER_PROTOCOL::DecodeCellResult(
        const std::string& aResponse, wxString* aError )
{
    if( aError )
        aError->clear();

    kiapi::ai::session::WorkerResponse payload;

    if( !payload.ParseFromString( aResponse ) )
    {
        return protocolError( wxS( "malformed_python_response" ),
                              wxS( "Python worker response must be a protobuf WorkerResponse." ),
                              aError );
    }

    if( payload.protocol() != "kisurf.ai.session.v1" )
    {
        return protocolError( wxS( "unsupported_python_protocol" ),
                              wxS( "Python worker response must use kisurf.ai.session.v1." ),
                              aError );
    }

    if( payload.has_error() )
    {
        return protocolError( fromUtf8( payload.error().error_code() ),
                              fromUtf8( payload.error().message() ), aError );
    }

    if( !payload.has_cell_result() )
    {
        return protocolError( wxS( "malformed_python_response" ),
                              wxS( "Python worker response type must be cell_result." ),
                              aError );
    }

    const kiapi::ai::session::CellResult& cellResult = payload.cell_result();

    AI_PYTHON_CELL_RESULT result;
    result.m_Ok = cellResult.ok();
    result.m_ErrorCode = fromUtf8( cellResult.error_code() );
    result.m_Message = fromUtf8( cellResult.message() );
    result.m_Stdout = fromUtf8( cellResult.stdout_text() );
    result.m_Stderr = fromUtf8( cellResult.stderr_text() );
    result.m_StepLabel = fromUtf8( cellResult.step_label() );
    result.m_RollbackOnError = cellResult.rollback_on_error();
    result.m_SdkName = fromUtf8( cellResult.sdk().name() );
    result.m_SdkVersion = fromUtf8( cellResult.sdk().version() );
    result.m_SdkProtocol = fromUtf8( cellResult.sdk().protocol() );
    result.m_SessionId = cellResult.session().id();
    result.m_Epoch = cellResult.session().epoch();

    result.m_HasSessionContext = true;
    result.m_BoardId = fromUtf8( cellResult.session().board_id() );
    result.m_BaseHash = fromUtf8( cellResult.session().base_hash() );

    if( result.m_BoardId.IsEmpty() || result.m_BaseHash.IsEmpty() )
    {
        return protocolError(
                wxS( "malformed_python_response" ),
                wxS( "Python worker session requires board_id and base_hash." ),
                aError );
    }

    for( const kiapi::ai::session::WorkerEvent& event : cellResult.events() )
    {
        if( event.kind().empty() )
        {
            return protocolError( wxS( "malformed_python_response" ),
                                  wxS( "Python worker event requires kind." ), aError );
        }

        AI_PYTHON_EVENT decodedEvent;
        decodedEvent.m_Kind = fromUtf8( event.kind() );
        decodedEvent.m_Message = fromUtf8( event.message() );
        decodedEvent.m_PayloadJson = event.payload_json().empty()
                                             ? wxString( wxS( "{}" ) )
                                             : fromUtf8( event.payload_json() );
        result.m_Events.push_back( std::move( decodedEvent ) );
    }

    for( const kiapi::ai::session::OperationRequest& operation :
         cellResult.operations() )
    {
        const AI_SESSION_OPERATION_KIND kind = operationKindFromProto( operation.kind() );

        if( kind == AI_SESSION_OPERATION_KIND::Unknown )
        {
            return protocolError(
                    wxS( "unknown_operation_kind" ),
                    wxString::Format( wxS( "Unknown Python worker operation kind: %d" ),
                                      static_cast<int>( operation.kind() ) ),
                    aError );
        }

        AI_PYTHON_OPERATION_REQUEST request;
        request.m_Kind = kind;
        request.m_ArgumentsJson = operation.arguments_json().empty()
                                          ? wxString( wxS( "{}" ) )
                                          : fromUtf8( operation.arguments_json() );

        result.m_Operations.push_back( std::move( request ) );
    }

    return result;
}


bool AI_PYTHON_WORKER_PROTOCOL::DecodeEvent( const std::string& aResponse,
                                             AI_PYTHON_EVENT* aEvent,
                                             wxString* aError )
{
    if( aError )
        aError->clear();

    if( aEvent )
        *aEvent = AI_PYTHON_EVENT();

    kiapi::ai::session::WorkerEvent payload;

    if( !payload.ParseFromString( aResponse ) )
    {
        if( aError )
            *aError = wxS( "Python worker event must be a protobuf WorkerEvent." );

        return false;
    }

    if( payload.kind().empty() )
    {
        if( aError )
            *aError = wxS( "Python worker event requires kind." );

        return false;
    }

    if( aEvent )
    {
        aEvent->m_Kind = fromUtf8( payload.kind() );
        aEvent->m_Message = fromUtf8( payload.message() );
        aEvent->m_PayloadJson = payload.payload_json().empty()
                                        ? wxString( wxS( "{}" ) )
                                        : fromUtf8( payload.payload_json() );
    }

    return true;
}
