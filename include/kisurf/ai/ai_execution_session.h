/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 */

#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_types.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>
#include <wx/string.h>

class AI_SHADOW_BOARD;

enum class AI_SESSION_OPERATION_KIND
{
    Unknown,

    Checkpoint,
    RollbackTo,

    QueryBoardSummary,
    QueryItems,
    QueryItem,
    QuerySelection,
    QueryNets,
    QueryLayers,
    QueryDesignRules,
    QueryViewport,
    QueryActivityTimeline,
    RenderPreview,
    ObserveStep,

    CreateVia,
    CreateTrackSegment,
    CreateTrackPolyline,
    CreateZone,
    CreateShape,
    MoveItems,
    DeleteItems,
    UpdateItemGeometry,
    SetItemNet,
    SetItemLayer,
    SetItemProperties,
    SetMetadata,
    RefillZones,
    RebuildConnectivity,
    RunValidation,

    ApplySurfacePatch
};

enum class AI_SESSION_HANDLE_STATUS
{
    Unknown,
    Live,
    Stale
};

enum class AI_SESSION_STEP_STATUS
{
    Unknown,
    Open,
    Completed,
    RolledBack,
    Failed
};

enum class AI_EXECUTION_SESSION_STATUS
{
    Open,
    Accepted,
    Rejected,
    Cancelled,
    Closed
};

KICOMMON_API wxString AiSessionOperationKindId(
        AI_SESSION_OPERATION_KIND aKind );

KICOMMON_API wxString AiSessionHandleStatusName(
        AI_SESSION_HANDLE_STATUS aStatus );

struct KICOMMON_API AI_SESSION_HANDLE
{
    uint64_t m_SessionId = 0;
    uint64_t m_HandleId = 0;
    uint64_t m_Generation = 0;
    wxString m_Alias;

    bool     IsValid() const;
    wxString AsString() const;
};

struct KICOMMON_API AI_SESSION_OPERATION_RECORD
{
    uint64_t                  m_Id = 0;
    uint64_t                  m_StepId = 0;
    AI_SESSION_OPERATION_KIND m_Kind = AI_SESSION_OPERATION_KIND::Unknown;
    wxString                  m_ArgumentsJson;
    std::vector<AI_SESSION_HANDLE> m_ResolvedHandles;
    std::vector<AI_SESSION_HANDLE> m_CreatedHandles;
    std::vector<wxString>          m_Warnings;
    wxString                       m_ResultJson;
    uint64_t                       m_BeforeEpoch = 0;
    uint64_t                       m_AfterEpoch = 0;

    bool     IsValid() const;
    bool     IsMutation() const;
    wxString OperationId() const;
};

struct KICOMMON_API AI_SESSION_STEP_RECORD
{
    uint64_t               m_Id = 0;
    wxString               m_Label;
    wxString               m_OptionsJson;
    AI_SESSION_STEP_STATUS m_Status = AI_SESSION_STEP_STATUS::Unknown;
    uint64_t               m_StartEpoch = 0;
    uint64_t               m_EndEpoch = 0;
    std::vector<uint64_t>  m_OperationIds;

    bool IsOpen() const { return m_Status == AI_SESSION_STEP_STATUS::Open; }
};

struct KICOMMON_API AI_SESSION_CHECKPOINT
{
    uint64_t m_Id = 0;
    wxString m_Name;
    uint64_t m_Epoch = 0;
    size_t   m_JournalOperationCount = 0;
    uint64_t m_HandleWatermark = 0;
};

struct KICOMMON_API AI_SESSION_OBSERVATION
{
    uint64_t m_StepId = 0;
    uint64_t m_Epoch = 0;
    size_t   m_OperationCount = 0;
    wxString m_Summary;

    wxString AsJsonText() const;
};

class KICOMMON_API AI_SESSION_JOURNAL
{
public:
    const AI_SESSION_OPERATION_RECORD& AppendOperation(
            AI_SESSION_OPERATION_RECORD aOperation );

    const std::vector<AI_SESSION_OPERATION_RECORD>& Operations() const
    {
        return m_Operations;
    }

    std::vector<AI_SESSION_OPERATION_RECORD> OperationsForStep(
            uint64_t aStepId ) const;

    const AI_SESSION_OPERATION_RECORD* FindOperation( uint64_t aOperationId ) const;

    void TruncateOperations( size_t aOperationCount );
    void Clear();
    bool UpdateOperationResult( uint64_t aOperationId, wxString aResultJson,
                                std::vector<wxString> aWarnings );

private:
    std::vector<AI_SESSION_OPERATION_RECORD> m_Operations;
};

class KICOMMON_API AI_EXECUTION_SESSION
{
public:
    struct OPEN_OPTIONS
    {
        uint64_t           m_SessionId = 1;
        wxString           m_BoardId;
        wxString           m_BaseHash;
        AI_EDITOR_KIND     m_EditorKind = AI_EDITOR_KIND::Pcb;
        AI_CONTEXT_VERSION m_ContextVersion;
    };

    explicit AI_EXECUTION_SESSION( OPEN_OPTIONS aOptions );
    ~AI_EXECUTION_SESSION();

    uint64_t BeginStep( wxString aLabel, wxString aOptionsJson = wxS( "{}" ) );
    AI_SESSION_OBSERVATION EndStep( uint64_t aStepId );
    AI_SESSION_OBSERVATION ObserveStep( uint64_t aStepId ) const;
    bool FailStep( uint64_t aStepId, const wxString& aReason );

    uint64_t Checkpoint( wxString aName );
    bool     RollbackTo( uint64_t aCheckpointId );

    AI_SESSION_HANDLE CreateHandle( wxString aAlias = wxEmptyString );
    AI_SESSION_HANDLE_STATUS ResolveHandle( const AI_SESSION_HANDLE& aHandle ) const;
    std::optional<AI_SESSION_HANDLE> ResolveAlias( const wxString& aAlias ) const;

    const AI_SESSION_OPERATION_RECORD& AppendOperation(
            AI_SESSION_OPERATION_RECORD aOperation );
    bool UpdateOperationResult( uint64_t aOperationId, wxString aResultJson,
                                std::vector<wxString> aWarnings );

    bool SelectionRevisionConflicts( const AI_CONTEXT_VERSION& aCurrentContextVersion ) const;
    bool CanAccept( const wxString& aCurrentBaseHash,
                    const AI_CONTEXT_VERSION& aCurrentContextVersion ) const;
    bool AcceptSession( const wxString& aCurrentBaseHash,
                        const AI_CONTEXT_VERSION& aCurrentContextVersion );
    void RejectSession();
    void CancelSession( wxString aReason );
    void CloseSession();

    uint64_t SessionId() const { return m_SessionId; }
    const wxString& BoardId() const { return m_BoardId; }
    const wxString& BaseHash() const { return m_BaseHash; }
    AI_EDITOR_KIND EditorKind() const { return m_EditorKind; }
    const AI_CONTEXT_VERSION& ContextVersion() const { return m_ContextVersion; }
    uint64_t Epoch() const { return m_Epoch; }
    bool HasOpenStep() const { return openStepId() != 0; }
    AI_EXECUTION_SESSION_STATUS Status() const { return m_Status; }
    const wxString& CancelReason() const { return m_CancelReason; }

    AI_SHADOW_BOARD& ShadowBoard();
    const AI_SHADOW_BOARD& ShadowBoard() const;

    const AI_SESSION_JOURNAL& Journal() const { return m_Journal; }
    const std::vector<AI_SESSION_STEP_RECORD>& Steps() const { return m_Steps; }
    const std::vector<AI_SESSION_CHECKPOINT>& Checkpoints() const
    {
        return m_Checkpoints;
    }

private:
    struct HANDLE_STATE
    {
        uint64_t m_Generation = 0;
        wxString m_Alias;
        uint64_t m_CreatedEpoch = 0;
        bool     m_Stale = false;
    };

    AI_SESSION_STEP_RECORD* findStep( uint64_t aStepId );
    const AI_SESSION_CHECKPOINT* findCheckpoint( uint64_t aCheckpointId ) const;
    uint64_t openStepId() const;
    void markHandlesCreatedAfter( const AI_SESSION_CHECKPOINT& aCheckpoint );

    uint64_t                    m_SessionId = 0;
    wxString                    m_BoardId;
    wxString                    m_BaseHash;
    AI_EDITOR_KIND              m_EditorKind = AI_EDITOR_KIND::Unknown;
    AI_CONTEXT_VERSION          m_ContextVersion;
    AI_EXECUTION_SESSION_STATUS m_Status = AI_EXECUTION_SESSION_STATUS::Open;
    wxString                    m_CancelReason;

    uint64_t m_Epoch = 0;
    uint64_t m_NextStepId = 1;
    uint64_t m_NextOperationId = 1;
    uint64_t m_NextCheckpointId = 1;
    uint64_t m_NextHandleId = 1;

    AI_SESSION_JOURNAL                    m_Journal;
    std::vector<AI_SESSION_STEP_RECORD>   m_Steps;
    std::vector<AI_SESSION_CHECKPOINT>    m_Checkpoints;
    std::map<uint64_t, HANDLE_STATE>      m_Handles;
    std::map<wxString, uint64_t>          m_AliasToHandle;
    std::unique_ptr<AI_SHADOW_BOARD>      m_ShadowBoard;
};
