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

#include <vector>
#include <wx/string.h>

struct KICOMMON_API AI_PYTHON_OPERATION_REQUEST
{
    AI_SESSION_OPERATION_KIND m_Kind = AI_SESSION_OPERATION_KIND::Unknown;
    wxString                  m_ArgumentsJson = wxS( "{}" );
};

struct KICOMMON_API AI_PYTHON_EVENT
{
    wxString m_Kind;
    wxString m_Message;
    wxString m_PayloadJson = wxS( "{}" );
};

struct KICOMMON_API AI_PYTHON_CELL_REQUEST
{
    uint64_t m_SessionId = 0;
    wxString m_BoardId;
    wxString m_BaseHash;
    uint64_t m_Epoch = 0;
    wxString m_CellText;
    wxString m_CellId;
};

struct KICOMMON_API AI_PYTHON_CELL_RESULT
{
    bool m_Ok = true;
    wxString m_ErrorCode;
    wxString m_Message;
    wxString m_Stdout;
    wxString m_Stderr;
    wxString m_StepLabel;
    bool     m_RollbackOnError = true;
    wxString m_SdkName;
    wxString m_SdkVersion;
    wxString m_SdkProtocol;
    bool     m_HasSessionContext = false;
    uint64_t m_SessionId = 0;
    wxString m_BoardId;
    wxString m_BaseHash;
    uint64_t m_Epoch = 0;
    std::vector<AI_PYTHON_EVENT> m_Events;
    std::vector<AI_PYTHON_OPERATION_REQUEST> m_Operations;
};

class KICOMMON_API AI_PYTHON_EVENT_SINK
{
public:
    virtual ~AI_PYTHON_EVENT_SINK() = default;

    virtual void OnPythonEvent( const AI_PYTHON_EVENT& aEvent ) = 0;
};

class KICOMMON_API AI_PYTHON_WORKER
{
public:
    virtual ~AI_PYTHON_WORKER() = default;

    virtual void SetEventSink( AI_PYTHON_EVENT_SINK* aSink ) { (void) aSink; }

    virtual bool IsConnected() const = 0;
    virtual AI_PYTHON_CELL_RESULT RunCell( const AI_EXECUTION_SESSION& aSession,
                                           const AI_PYTHON_CELL_REQUEST& aRequest ) = 0;
    virtual void Cancel() {}
    virtual void HardKill() {}
};
