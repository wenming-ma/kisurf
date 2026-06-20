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
#include <kisurf/ai/ai_python_worker.h>

#include <atomic>
#include <memory>
#include <string>
#include <wx/string.h>

struct AI_PYTHON_LOCAL_PROCESS;

class KICOMMON_API AI_PYTHON_LOCAL_WORKER : public AI_PYTHON_WORKER
{
public:
    AI_PYTHON_LOCAL_WORKER( wxString aInterpreterPath, wxString aSdkRootPath,
                            long aResponseTimeoutMs = 30000 );
    ~AI_PYTHON_LOCAL_WORKER() override;

    static wxString DefaultInterpreterPath();
    static wxString DefaultSdkRootPath();
    static wxString InstalledSdkRootPath();
    static std::unique_ptr<AI_PYTHON_WORKER> CreateDefault();

    bool IsConnected() const override;
    void SetEventSink( AI_PYTHON_EVENT_SINK* aSink ) override;

    AI_PYTHON_CELL_RESULT RunCell( const AI_EXECUTION_SESSION& aSession,
                                   const AI_PYTHON_CELL_REQUEST& aRequest ) override;

    void Cancel() override;
    void HardKill() override;

    const wxString& InterpreterPath() const { return m_InterpreterPath; }
    const wxString& SdkRootPath() const { return m_SdkRootPath; }
    long ResponseTimeoutMs() const { return m_ResponseTimeoutMs; }

private:
    bool ensureProcess( wxString* aError );
    bool writeRequestFrame( const std::string& aRequestPayload, wxString* aError );
    void writeControlFrameBestEffort( const std::string& aRequestPayload );
    bool readResponseFrame( std::string* aResponsePayload, wxString* aError );
    void readEventFrames( AI_PYTHON_LOCAL_PROCESS* aProcess );
    void stopProcess( bool aHardKill );

    wxString m_InterpreterPath;
    wxString m_SdkRootPath;
    long m_ResponseTimeoutMs = 30000;
    std::unique_ptr<AI_PYTHON_LOCAL_PROCESS> m_Process;
    AI_PYTHON_EVENT_SINK* m_EventSink = nullptr;
    long m_Pid = 0;
    std::atomic<uint64_t> m_LastSessionId{ 0 };
    std::atomic<uint64_t> m_CancelledSessionId{ 0 };
    std::atomic_bool m_CellRunning{ false };
    std::atomic_bool m_CancelRequested{ false };
};
