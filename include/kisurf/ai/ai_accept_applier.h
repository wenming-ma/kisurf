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

#include <cstddef>
#include <wx/string.h>

class KICOMMON_API AI_ACCEPT_APPLY_ADAPTER
{
public:
    virtual ~AI_ACCEPT_APPLY_ADAPTER() = default;

    virtual bool BeginTransaction( const AI_EXECUTION_SESSION& aSession,
                                   wxString& aError ) = 0;
    virtual bool ApplyOperation( const AI_SESSION_OPERATION_RECORD& aOperation,
                                 wxString& aError ) = 0;
    virtual bool CommitTransaction( wxString& aError ) = 0;
    virtual bool HasBoardChanges() const { return true; }
    virtual void AbortTransaction() {}
};

struct KICOMMON_API AI_CURRENT_BOARD_TOOL_RESULT
{
    bool     m_Ok = false;
    bool     m_Executed = false;
    wxString m_ErrorCode;
    wxString m_Message;
    wxString m_ResultJson = wxS( "{}" );
};

class KICOMMON_API AI_CURRENT_BOARD_TOOL_ADAPTER
{
public:
    virtual ~AI_CURRENT_BOARD_TOOL_ADAPTER() = default;

    virtual AI_CURRENT_BOARD_TOOL_RESULT QueryCurrentBoardSummary() = 0;
    virtual AI_CURRENT_BOARD_TOOL_RESULT QueryCurrentBoardItems(
            const wxString& aFilterJson ) = 0;
    virtual AI_CURRENT_BOARD_TOOL_RESULT QueryCurrentBoardNets() = 0;
    virtual AI_CURRENT_BOARD_TOOL_RESULT RunCurrentBoardAtomicOperation(
            AI_SESSION_OPERATION_KIND aKind, const wxString& aArgumentsJson ) = 0;
};

struct KICOMMON_API AI_ACCEPT_APPLY_RESULT
{
    bool     m_Ok = false;
    wxString m_ErrorCode;
    wxString m_Message;
    size_t   m_AppliedOperationCount = 0;
    bool     m_BoardMutated = false;
};

class KICOMMON_API AI_ACCEPT_APPLIER
{
public:
    static AI_ACCEPT_APPLY_RESULT Apply( AI_EXECUTION_SESSION& aSession,
            const wxString& aCurrentBaseHash,
            const AI_CONTEXT_VERSION& aCurrentContextVersion,
            AI_ACCEPT_APPLY_ADAPTER& aAdapter );

    static AI_ACCEPT_APPLY_RESULT ApplyDirectLive( AI_EXECUTION_SESSION& aSession,
            AI_ACCEPT_APPLY_ADAPTER& aAdapter );
};
