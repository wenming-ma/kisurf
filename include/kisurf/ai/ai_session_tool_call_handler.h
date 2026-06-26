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
#include <kisurf/ai/ai_execution_session.h>
#include <kisurf/ai/ai_python_worker.h>
#include <kisurf/ai/ai_runtime.h>

#include <nlohmann/json_fwd.hpp>

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

class AI_ACCEPT_APPLY_ADAPTER;

class KICOMMON_API AI_SESSION_SHADOW_BOARD_SEEDER
{
public:
    virtual ~AI_SESSION_SHADOW_BOARD_SEEDER() = default;

    virtual void Seed( AI_EXECUTION_SESSION& aSession ) = 0;
};

struct KICOMMON_API AI_SESSION_PREVIEW_RESULT
{
    bool     m_Ok = true;
    wxString m_ErrorCode;
    wxString m_Message;
    uint64_t m_PreviewId = 0;
    size_t   m_RenderedItemCount = 0;
    wxString m_ResultJson = wxS( "{}" );
};

class KICOMMON_API AI_SESSION_PREVIEW_SERVICE
{
public:
    virtual ~AI_SESSION_PREVIEW_SERVICE() = default;

    virtual AI_SESSION_PREVIEW_RESULT RenderPreview(
            const AI_EXECUTION_SESSION& aSession,
            const wxString& aArgumentsJson ) = 0;
    virtual void ClearPreview( uint64_t aSessionId ) = 0;
};

struct KICOMMON_API AI_SESSION_VALIDATION_RESULT
{
    bool                  m_Ok = true;
    wxString              m_ErrorCode;
    wxString              m_Message;
    wxString              m_ResultJson = wxS( "{}" );
    std::vector<wxString> m_Warnings;
};

class KICOMMON_API AI_SESSION_VALIDATION_SERVICE
{
public:
    virtual ~AI_SESSION_VALIDATION_SERVICE() = default;

    virtual AI_SESSION_VALIDATION_RESULT RunValidation(
            const AI_EXECUTION_SESSION& aSession,
            const wxString& aArgumentsJson,
            const wxString& aCurrentResultJson ) = 0;
};

class KICOMMON_API AI_SESSION_TOOL_CALL_HANDLER : public AI_TOOL_CALL_HANDLER,
                                                  public AI_PYTHON_EVENT_SINK
{
public:
    AI_SESSION_TOOL_CALL_HANDLER() = default;
    explicit AI_SESSION_TOOL_CALL_HANDLER(
            std::unique_ptr<AI_PYTHON_WORKER> aPythonWorker );
    AI_SESSION_TOOL_CALL_HANDLER( std::unique_ptr<AI_PYTHON_WORKER> aPythonWorker,
                                  AI_ACCEPT_APPLY_ADAPTER* aAcceptAdapter );
    AI_SESSION_TOOL_CALL_HANDLER( std::unique_ptr<AI_PYTHON_WORKER> aPythonWorker,
                                  AI_ACCEPT_APPLY_ADAPTER* aAcceptAdapter,
                                  AI_SESSION_PREVIEW_SERVICE* aPreviewService );
    AI_SESSION_TOOL_CALL_HANDLER( std::unique_ptr<AI_PYTHON_WORKER> aPythonWorker,
                                  AI_ACCEPT_APPLY_ADAPTER* aAcceptAdapter,
                                  AI_SESSION_PREVIEW_SERVICE* aPreviewService,
                                  AI_SESSION_SHADOW_BOARD_SEEDER* aShadowBoardSeeder );
    AI_SESSION_TOOL_CALL_HANDLER( std::unique_ptr<AI_PYTHON_WORKER> aPythonWorker,
                                  AI_ACCEPT_APPLY_ADAPTER* aAcceptAdapter,
                                  AI_SESSION_PREVIEW_SERVICE* aPreviewService,
                                  AI_SESSION_SHADOW_BOARD_SEEDER* aShadowBoardSeeder,
                                  AI_SESSION_VALIDATION_SERVICE* aValidationService );

    AI_TOOL_INVOCATION_RESULT HandleToolCall(
            const AI_PROVIDER_REQUEST& aRequest,
            const AI_TOOL_CALL_RECORD& aToolCall ) override;
    void OnPythonEvent( const AI_PYTHON_EVENT& aEvent ) override;

    const AI_EXECUTION_SESSION* ActiveSession() const;
    wxString ToolCatalogJson() const;

private:
    struct PREVIEW_RESTORE_STATE
    {
        bool     m_HasPreview = false;
        wxString m_ArgumentsJson = wxS( "{}" );
    };

    struct RECORDED_PYTHON_EVENT
    {
        uint64_t        m_Sequence = 0;
        wxString        m_CellId;
        wxString        m_Source;
        AI_PYTHON_EVENT m_Event;
    };

    AI_EXECUTION_SESSION& openSessionFromRequest(
            const AI_PROVIDER_REQUEST& aRequest, const wxString& aBoardId,
            const wxString& aBaseHash );

    void rememberCheckpointPreviewState( uint64_t aCheckpointId );
    void rememberRenderedPreview( const wxString& aArgumentsJson );
    nlohmann::json restorePreviewForCheckpoint( uint64_t aCheckpointId );
    void clearPreviewState();
    void beginPythonEventCapture( const wxString& aCellId );
    void finishPythonEventCapture();
    void recordPythonCellResultEvents( const wxString& aCellId,
                                       const std::vector<AI_PYTHON_EVENT>& aEvents );
    nlohmann::json currentPythonEventsJson() const;
    nlohmann::json pythonEventTimelineJson() const;

    uint64_t                           m_NextSessionId = 1;
    std::optional<AI_EXECUTION_SESSION> m_Session;
    std::unique_ptr<AI_PYTHON_WORKER>   m_PythonWorker;
    AI_ACCEPT_APPLY_ADAPTER*            m_AcceptAdapter = nullptr;
    AI_SESSION_PREVIEW_SERVICE*          m_PreviewService = nullptr;
    AI_SESSION_SHADOW_BOARD_SEEDER*      m_ShadowBoardSeeder = nullptr;
    AI_SESSION_VALIDATION_SERVICE*       m_ValidationService = nullptr;
    PREVIEW_RESTORE_STATE               m_CurrentPreviewState;
    std::map<uint64_t, PREVIEW_RESTORE_STATE> m_CheckpointPreviewStates;
    mutable std::mutex                  m_PythonEventMutex;
    uint64_t                            m_NextPythonEventSequence = 1;
    wxString                            m_ActivePythonEventCellId;
    std::vector<RECORDED_PYTHON_EVENT>  m_PythonEventTimeline;
    std::vector<RECORDED_PYTHON_EVENT>  m_CurrentPythonEvents;
};
