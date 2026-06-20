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

struct KICOMMON_API AI_ATOMIC_EXECUTION_RESULT
{
    bool                           m_Ok = false;
    wxString                       m_ErrorCode;
    wxString                       m_Message;
    std::vector<AI_SESSION_HANDLE> m_CreatedHandles;
    std::vector<AI_SESSION_HANDLE> m_ResolvedHandles;
    std::vector<uint64_t>          m_OperationIds;
    std::vector<wxString>          m_Warnings;
    wxString                       m_ResultJson = wxS( "{}" );
};

class KICOMMON_API AI_ATOMIC_OPERATION_EXECUTOR
{
public:
    static AI_ATOMIC_EXECUTION_RESULT Execute( AI_EXECUTION_SESSION& aSession,
            AI_SESSION_OPERATION_KIND aKind, const wxString& aArgumentsJson );
};
