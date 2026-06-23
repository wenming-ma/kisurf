/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 */

#include <kisurf/ai/ai_execution_session.h>
#include <kisurf/ai/ai_shadow_board.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <utility>

namespace
{
std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


wxString fromJson( const nlohmann::json& aJson )
{
    return wxString::FromUTF8( aJson.dump().c_str() );
}


bool isMutationKind( AI_SESSION_OPERATION_KIND aKind )
{
    switch( aKind )
    {
    case AI_SESSION_OPERATION_KIND::CreateVia:
    case AI_SESSION_OPERATION_KIND::CreateTrackSegment:
    case AI_SESSION_OPERATION_KIND::CreateTrackPolyline:
    case AI_SESSION_OPERATION_KIND::CreateZone:
    case AI_SESSION_OPERATION_KIND::CreateShape:
    case AI_SESSION_OPERATION_KIND::MoveItems:
    case AI_SESSION_OPERATION_KIND::DeleteItems:
    case AI_SESSION_OPERATION_KIND::UpdateItemGeometry:
    case AI_SESSION_OPERATION_KIND::SetItemNet:
    case AI_SESSION_OPERATION_KIND::SetItemLayer:
    case AI_SESSION_OPERATION_KIND::SetItemProperties:
    case AI_SESSION_OPERATION_KIND::SetMetadata:
    case AI_SESSION_OPERATION_KIND::RefillZones:
    case AI_SESSION_OPERATION_KIND::RebuildConnectivity:
    case AI_SESSION_OPERATION_KIND::RunValidation:
    case AI_SESSION_OPERATION_KIND::ApplySurfacePatch:
        return true;

    case AI_SESSION_OPERATION_KIND::Unknown:
    case AI_SESSION_OPERATION_KIND::Checkpoint:
    case AI_SESSION_OPERATION_KIND::RollbackTo:
    case AI_SESSION_OPERATION_KIND::QueryBoardSummary:
    case AI_SESSION_OPERATION_KIND::QueryItems:
    case AI_SESSION_OPERATION_KIND::QueryItem:
    case AI_SESSION_OPERATION_KIND::QuerySelection:
    case AI_SESSION_OPERATION_KIND::QueryNets:
    case AI_SESSION_OPERATION_KIND::QueryLayers:
    case AI_SESSION_OPERATION_KIND::QueryDesignRules:
    case AI_SESSION_OPERATION_KIND::QueryViewport:
    case AI_SESSION_OPERATION_KIND::QueryActivityTimeline:
    case AI_SESSION_OPERATION_KIND::RenderPreview:
    case AI_SESSION_OPERATION_KIND::ObserveStep:
    default:
        return false;
    }
}
} // namespace


wxString AiSessionOperationKindId( AI_SESSION_OPERATION_KIND aKind )
{
    switch( aKind )
    {
    case AI_SESSION_OPERATION_KIND::Checkpoint:
        return wxS( "session.checkpoint" );

    case AI_SESSION_OPERATION_KIND::RollbackTo:
        return wxS( "session.rollback_to" );

    case AI_SESSION_OPERATION_KIND::QueryBoardSummary:
        return wxS( "query.board_summary" );

    case AI_SESSION_OPERATION_KIND::QueryItems:
        return wxS( "query.items" );

    case AI_SESSION_OPERATION_KIND::QueryItem:
        return wxS( "query.item" );

    case AI_SESSION_OPERATION_KIND::QuerySelection:
        return wxS( "query.selection" );

    case AI_SESSION_OPERATION_KIND::QueryNets:
        return wxS( "query.nets" );

    case AI_SESSION_OPERATION_KIND::QueryLayers:
        return wxS( "query.layers" );

    case AI_SESSION_OPERATION_KIND::QueryDesignRules:
        return wxS( "query.design_rules" );

    case AI_SESSION_OPERATION_KIND::QueryViewport:
        return wxS( "query.viewport" );

    case AI_SESSION_OPERATION_KIND::QueryActivityTimeline:
        return wxS( "query.activity_timeline" );

    case AI_SESSION_OPERATION_KIND::RenderPreview:
        return wxS( "render.preview" );

    case AI_SESSION_OPERATION_KIND::ObserveStep:
        return wxS( "observe.step" );

    case AI_SESSION_OPERATION_KIND::CreateVia:
        return wxS( "pcb.create_via" );

    case AI_SESSION_OPERATION_KIND::CreateTrackSegment:
        return wxS( "pcb.create_track_segment" );

    case AI_SESSION_OPERATION_KIND::CreateTrackPolyline:
        return wxS( "pcb.create_track_polyline" );

    case AI_SESSION_OPERATION_KIND::CreateZone:
        return wxS( "pcb.create_zone" );

    case AI_SESSION_OPERATION_KIND::CreateShape:
        return wxS( "pcb.create_shape" );

    case AI_SESSION_OPERATION_KIND::MoveItems:
        return wxS( "pcb.move_items" );

    case AI_SESSION_OPERATION_KIND::DeleteItems:
        return wxS( "pcb.delete_items" );

    case AI_SESSION_OPERATION_KIND::UpdateItemGeometry:
        return wxS( "pcb.update_item_geometry" );

    case AI_SESSION_OPERATION_KIND::SetItemNet:
        return wxS( "pcb.set_item_net" );

    case AI_SESSION_OPERATION_KIND::SetItemLayer:
        return wxS( "pcb.set_item_layer" );

    case AI_SESSION_OPERATION_KIND::SetItemProperties:
        return wxS( "pcb.set_item_properties" );

    case AI_SESSION_OPERATION_KIND::SetMetadata:
        return wxS( "pcb.set_metadata" );

    case AI_SESSION_OPERATION_KIND::RefillZones:
        return wxS( "pcb.refill_zones" );

    case AI_SESSION_OPERATION_KIND::RebuildConnectivity:
        return wxS( "pcb.rebuild_connectivity" );

    case AI_SESSION_OPERATION_KIND::RunValidation:
        return wxS( "pcb.run_validation" );

    case AI_SESSION_OPERATION_KIND::ApplySurfacePatch:
        return wxS( "surface.apply_patch" );

    case AI_SESSION_OPERATION_KIND::Unknown:
    default:
        return wxS( "unknown" );
    }
}


wxString AiSessionHandleStatusName( AI_SESSION_HANDLE_STATUS aStatus )
{
    switch( aStatus )
    {
    case AI_SESSION_HANDLE_STATUS::Live:
        return wxS( "live" );

    case AI_SESSION_HANDLE_STATUS::Stale:
        return wxS( "stale" );

    case AI_SESSION_HANDLE_STATUS::Unknown:
    default:
        return wxS( "unknown" );
    }
}


bool AI_SESSION_HANDLE::IsValid() const
{
    return m_SessionId != 0 && m_HandleId != 0 && m_Generation != 0;
}


wxString AI_SESSION_HANDLE::AsString() const
{
    wxString text;
    text << wxS( "session:" ) << m_SessionId << wxS( "/handle:" ) << m_HandleId
         << wxS( "/gen:" ) << m_Generation;

    if( !m_Alias.IsEmpty() )
        text << wxS( "/" ) << m_Alias;

    return text;
}


bool AI_SESSION_OPERATION_RECORD::IsValid() const
{
    return m_Kind != AI_SESSION_OPERATION_KIND::Unknown && !m_ArgumentsJson.IsEmpty();
}


bool AI_SESSION_OPERATION_RECORD::IsMutation() const
{
    return isMutationKind( m_Kind );
}


wxString AI_SESSION_OPERATION_RECORD::OperationId() const
{
    return AiSessionOperationKindId( m_Kind );
}


wxString AI_SESSION_OBSERVATION::AsJsonText() const
{
    nlohmann::json payload = {
        { "step_id", m_StepId },
        { "epoch", m_Epoch },
        { "operation_count", m_OperationCount },
        { "summary", toUtf8String( m_Summary ) }
    };

    return fromJson( payload );
}


const AI_SESSION_OPERATION_RECORD& AI_SESSION_JOURNAL::AppendOperation(
        AI_SESSION_OPERATION_RECORD aOperation )
{
    m_Operations.push_back( std::move( aOperation ) );
    return m_Operations.back();
}


std::vector<AI_SESSION_OPERATION_RECORD> AI_SESSION_JOURNAL::OperationsForStep(
        uint64_t aStepId ) const
{
    std::vector<AI_SESSION_OPERATION_RECORD> result;

    for( const AI_SESSION_OPERATION_RECORD& operation : m_Operations )
    {
        if( operation.m_StepId == aStepId )
            result.push_back( operation );
    }

    return result;
}


const AI_SESSION_OPERATION_RECORD* AI_SESSION_JOURNAL::FindOperation(
        uint64_t aOperationId ) const
{
    const auto it = std::find_if( m_Operations.begin(), m_Operations.end(),
                                  [&]( const AI_SESSION_OPERATION_RECORD& aOperation )
                                  {
                                      return aOperation.m_Id == aOperationId;
                                  } );

    return it == m_Operations.end() ? nullptr : &( *it );
}


void AI_SESSION_JOURNAL::TruncateOperations( size_t aOperationCount )
{
    if( aOperationCount < m_Operations.size() )
        m_Operations.resize( aOperationCount );
}


void AI_SESSION_JOURNAL::Clear()
{
    m_Operations.clear();
}


bool AI_SESSION_JOURNAL::UpdateOperationResult(
        uint64_t aOperationId, wxString aResultJson,
        std::vector<wxString> aWarnings )
{
    for( AI_SESSION_OPERATION_RECORD& operation : m_Operations )
    {
        if( operation.m_Id != aOperationId )
            continue;

        operation.m_ResultJson = std::move( aResultJson );
        operation.m_Warnings = std::move( aWarnings );
        return true;
    }

    return false;
}


AI_EXECUTION_SESSION::AI_EXECUTION_SESSION( OPEN_OPTIONS aOptions ) :
        m_SessionId( aOptions.m_SessionId ),
        m_BoardId( std::move( aOptions.m_BoardId ) ),
        m_BaseHash( std::move( aOptions.m_BaseHash ) ),
        m_EditorKind( aOptions.m_EditorKind ),
        m_ContextVersion( aOptions.m_ContextVersion ),
        m_ShadowBoard( std::make_unique<AI_SHADOW_BOARD>() )
{
    if( m_SessionId == 0 )
        m_SessionId = 1;
}


AI_EXECUTION_SESSION::~AI_EXECUTION_SESSION() = default;


uint64_t AI_EXECUTION_SESSION::BeginStep( wxString aLabel, wxString aOptionsJson )
{
    if( m_Status != AI_EXECUTION_SESSION_STATUS::Open || openStepId() != 0 )
        return 0;

    AI_SESSION_STEP_RECORD step;
    step.m_Id = m_NextStepId++;
    step.m_Label = std::move( aLabel );
    step.m_OptionsJson = std::move( aOptionsJson );
    step.m_Status = AI_SESSION_STEP_STATUS::Open;
    step.m_StartEpoch = m_Epoch;
    step.m_EndEpoch = m_Epoch;

    m_Steps.push_back( std::move( step ) );
    return m_Steps.back().m_Id;
}


AI_SESSION_OBSERVATION AI_EXECUTION_SESSION::EndStep( uint64_t aStepId )
{
    AI_SESSION_OBSERVATION observation;
    observation.m_StepId = aStepId;
    observation.m_Epoch = m_Epoch;

    AI_SESSION_STEP_RECORD* step = findStep( aStepId );

    if( !step || step->m_Status != AI_SESSION_STEP_STATUS::Open )
    {
        observation.m_Summary = wxS( "step_not_open" );
        return observation;
    }

    step->m_Status = AI_SESSION_STEP_STATUS::Completed;
    step->m_EndEpoch = m_Epoch;
    observation.m_OperationCount = step->m_OperationIds.size();
    observation.m_Summary = wxString::Format(
            wxS( "Step %llu completed with %llu operation(s)." ),
            static_cast<unsigned long long>( aStepId ),
            static_cast<unsigned long long>( observation.m_OperationCount ) );
    return observation;
}


AI_SESSION_OBSERVATION AI_EXECUTION_SESSION::ObserveStep( uint64_t aStepId ) const
{
    AI_SESSION_OBSERVATION observation;
    observation.m_StepId = aStepId;
    observation.m_Epoch = m_Epoch;

    const auto it = std::find_if( m_Steps.begin(), m_Steps.end(),
                                  [&]( const AI_SESSION_STEP_RECORD& aStep )
                                  {
                                      return aStep.m_Id == aStepId;
                                  } );

    if( it == m_Steps.end() )
    {
        observation.m_Summary = wxS( "step_not_found" );
        return observation;
    }

    observation.m_OperationCount = it->m_OperationIds.size();

    wxString status;

    switch( it->m_Status )
    {
    case AI_SESSION_STEP_STATUS::Open:
        status = wxS( "open" );
        break;

    case AI_SESSION_STEP_STATUS::Completed:
        status = wxS( "completed" );
        break;

    case AI_SESSION_STEP_STATUS::RolledBack:
        status = wxS( "rolled_back" );
        break;

    case AI_SESSION_STEP_STATUS::Failed:
        status = wxS( "failed" );
        break;

    case AI_SESSION_STEP_STATUS::Unknown:
    default:
        status = wxS( "unknown" );
        break;
    }

    observation.m_Summary = wxString::Format(
            wxS( "Step %llu %s with %llu operation(s)." ),
            static_cast<unsigned long long>( aStepId ), status,
            static_cast<unsigned long long>( observation.m_OperationCount ) );
    return observation;
}


bool AI_EXECUTION_SESSION::FailStep( uint64_t aStepId, const wxString& aReason )
{
    wxUnusedVar( aReason );

    AI_SESSION_STEP_RECORD* step = findStep( aStepId );

    if( !step || step->m_Status != AI_SESSION_STEP_STATUS::Open )
        return false;

    step->m_Status = AI_SESSION_STEP_STATUS::Failed;
    step->m_EndEpoch = m_Epoch;
    return true;
}


uint64_t AI_EXECUTION_SESSION::Checkpoint( wxString aName )
{
    AI_SESSION_CHECKPOINT checkpoint;
    checkpoint.m_Id = m_NextCheckpointId++;
    checkpoint.m_Name = std::move( aName );
    checkpoint.m_Epoch = m_Epoch;
    checkpoint.m_JournalOperationCount = m_Journal.Operations().size();
    checkpoint.m_HandleWatermark = m_NextHandleId - 1;
    m_Checkpoints.push_back( std::move( checkpoint ) );
    m_ShadowBoard->CaptureCheckpoint( m_Checkpoints.back() );
    return m_Checkpoints.back().m_Id;
}


bool AI_EXECUTION_SESSION::RollbackTo( uint64_t aCheckpointId )
{
    const AI_SESSION_CHECKPOINT* checkpoint = findCheckpoint( aCheckpointId );

    if( !checkpoint || m_Status != AI_EXECUTION_SESSION_STATUS::Open )
        return false;

    m_Epoch = checkpoint->m_Epoch;
    m_Journal.TruncateOperations( checkpoint->m_JournalOperationCount );
    markHandlesCreatedAfter( *checkpoint );
    m_ShadowBoard->RollbackTo( *checkpoint );

    for( AI_SESSION_STEP_RECORD& step : m_Steps )
    {
        if( step.m_StartEpoch > checkpoint->m_Epoch
            || step.m_EndEpoch > checkpoint->m_Epoch
            || step.m_Status == AI_SESSION_STEP_STATUS::Open )
        {
            step.m_Status = AI_SESSION_STEP_STATUS::RolledBack;
            step.m_EndEpoch = checkpoint->m_Epoch;
        }

        step.m_OperationIds.erase(
                std::remove_if( step.m_OperationIds.begin(), step.m_OperationIds.end(),
                                [&]( uint64_t aOperationId )
                                {
                                    return m_Journal.FindOperation( aOperationId ) == nullptr;
                                } ),
                step.m_OperationIds.end() );
    }

    return true;
}


AI_SESSION_HANDLE AI_EXECUTION_SESSION::CreateHandle( wxString aAlias )
{
    AI_SESSION_HANDLE handle;
    handle.m_SessionId = m_SessionId;
    handle.m_HandleId = m_NextHandleId++;
    handle.m_Generation = 1;
    handle.m_Alias = std::move( aAlias );

    HANDLE_STATE state;
    state.m_Generation = handle.m_Generation;
    state.m_Alias = handle.m_Alias;
    state.m_CreatedEpoch = m_Epoch;
    m_Handles[handle.m_HandleId] = state;

    if( !handle.m_Alias.IsEmpty() )
        m_AliasToHandle[handle.m_Alias] = handle.m_HandleId;

    return handle;
}


AI_SESSION_HANDLE_STATUS AI_EXECUTION_SESSION::ResolveHandle(
        const AI_SESSION_HANDLE& aHandle ) const
{
    if( aHandle.m_SessionId != m_SessionId || !aHandle.IsValid() )
        return AI_SESSION_HANDLE_STATUS::Unknown;

    const auto it = m_Handles.find( aHandle.m_HandleId );

    if( it == m_Handles.end() || it->second.m_Generation != aHandle.m_Generation )
        return AI_SESSION_HANDLE_STATUS::Unknown;

    if( it->second.m_Stale )
        return AI_SESSION_HANDLE_STATUS::Stale;

    return AI_SESSION_HANDLE_STATUS::Live;
}


std::optional<AI_SESSION_HANDLE> AI_EXECUTION_SESSION::ResolveAlias(
        const wxString& aAlias ) const
{
    const auto aliasIt = m_AliasToHandle.find( aAlias );

    if( aliasIt == m_AliasToHandle.end() )
        return std::nullopt;

    const auto handleIt = m_Handles.find( aliasIt->second );

    if( handleIt == m_Handles.end() || handleIt->second.m_Stale )
        return std::nullopt;

    AI_SESSION_HANDLE handle;
    handle.m_SessionId = m_SessionId;
    handle.m_HandleId = aliasIt->second;
    handle.m_Generation = handleIt->second.m_Generation;
    handle.m_Alias = handleIt->second.m_Alias;
    return handle;
}


AI_SHADOW_BOARD& AI_EXECUTION_SESSION::ShadowBoard()
{
    return *m_ShadowBoard;
}


const AI_SHADOW_BOARD& AI_EXECUTION_SESSION::ShadowBoard() const
{
    return *m_ShadowBoard;
}


const AI_SESSION_OPERATION_RECORD& AI_EXECUTION_SESSION::AppendOperation(
        AI_SESSION_OPERATION_RECORD aOperation )
{
    const uint64_t stepId = openStepId();

    if( aOperation.m_ArgumentsJson.IsEmpty() )
        aOperation.m_ArgumentsJson = wxS( "{}" );

    aOperation.m_Id = m_NextOperationId++;
    aOperation.m_StepId = stepId;
    aOperation.m_BeforeEpoch = m_Epoch;
    aOperation.m_AfterEpoch = ++m_Epoch;

    if( AI_SESSION_STEP_RECORD* step = findStep( stepId ) )
    {
        step->m_OperationIds.push_back( aOperation.m_Id );
        step->m_EndEpoch = aOperation.m_AfterEpoch;
    }

    return m_Journal.AppendOperation( std::move( aOperation ) );
}


bool AI_EXECUTION_SESSION::UpdateOperationResult( uint64_t aOperationId,
                                                  wxString aResultJson,
                                                  std::vector<wxString> aWarnings )
{
    return m_Journal.UpdateOperationResult( aOperationId, std::move( aResultJson ),
                                            std::move( aWarnings ) );
}


bool AI_EXECUTION_SESSION::SelectionRevisionConflicts(
        const AI_CONTEXT_VERSION& aCurrentContextVersion ) const
{
    if( m_ContextVersion.m_SelectionRevision == 0
        || aCurrentContextVersion.m_SelectionRevision == 0 )
    {
        return false;
    }

    return m_ContextVersion.m_SelectionRevision
           != aCurrentContextVersion.m_SelectionRevision;
}


bool AI_EXECUTION_SESSION::CanAccept( const wxString& aCurrentBaseHash,
                                      const AI_CONTEXT_VERSION& aCurrentContextVersion ) const
{
    return m_Status == AI_EXECUTION_SESSION_STATUS::Open
           && !m_BaseHash.IsEmpty()
           && aCurrentBaseHash == m_BaseHash
           && !SelectionRevisionConflicts( aCurrentContextVersion )
           && openStepId() == 0;
}


bool AI_EXECUTION_SESSION::AcceptSession( const wxString& aCurrentBaseHash,
                                          const AI_CONTEXT_VERSION& aCurrentContextVersion )
{
    if( !CanAccept( aCurrentBaseHash, aCurrentContextVersion ) )
        return false;

    m_Status = AI_EXECUTION_SESSION_STATUS::Accepted;
    return true;
}


void AI_EXECUTION_SESSION::RejectSession()
{
    if( m_Status == AI_EXECUTION_SESSION_STATUS::Open )
        m_Status = AI_EXECUTION_SESSION_STATUS::Rejected;
}


void AI_EXECUTION_SESSION::CancelSession( wxString aReason )
{
    if( m_Status == AI_EXECUTION_SESSION_STATUS::Open )
    {
        m_Status = AI_EXECUTION_SESSION_STATUS::Cancelled;
        m_CancelReason = std::move( aReason );
    }
}


void AI_EXECUTION_SESSION::CloseSession()
{
    if( m_Status == AI_EXECUTION_SESSION_STATUS::Open )
        m_Status = AI_EXECUTION_SESSION_STATUS::Closed;
}


AI_SESSION_STEP_RECORD* AI_EXECUTION_SESSION::findStep( uint64_t aStepId )
{
    const auto it = std::find_if( m_Steps.begin(), m_Steps.end(),
                                  [&]( const AI_SESSION_STEP_RECORD& aStep )
                                  {
                                      return aStep.m_Id == aStepId;
                                  } );

    return it == m_Steps.end() ? nullptr : &( *it );
}


const AI_SESSION_CHECKPOINT* AI_EXECUTION_SESSION::findCheckpoint(
        uint64_t aCheckpointId ) const
{
    const auto it = std::find_if( m_Checkpoints.begin(), m_Checkpoints.end(),
                                  [&]( const AI_SESSION_CHECKPOINT& aCheckpoint )
                                  {
                                      return aCheckpoint.m_Id == aCheckpointId;
                                  } );

    return it == m_Checkpoints.end() ? nullptr : &( *it );
}


uint64_t AI_EXECUTION_SESSION::openStepId() const
{
    const auto it = std::find_if( m_Steps.begin(), m_Steps.end(),
                                  []( const AI_SESSION_STEP_RECORD& aStep )
                                  {
                                      return aStep.m_Status
                                             == AI_SESSION_STEP_STATUS::Open;
                                  } );

    return it == m_Steps.end() ? 0 : it->m_Id;
}


void AI_EXECUTION_SESSION::markHandlesCreatedAfter(
        const AI_SESSION_CHECKPOINT& aCheckpoint )
{
    for( auto& [id, state] : m_Handles )
    {
        if( id > aCheckpoint.m_HandleWatermark
            || state.m_CreatedEpoch > aCheckpoint.m_Epoch )
        {
            state.m_Stale = true;

            if( !state.m_Alias.IsEmpty() )
                m_AliasToHandle.erase( state.m_Alias );
        }
    }
}
