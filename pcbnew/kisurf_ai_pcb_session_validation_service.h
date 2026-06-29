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

#include <kisurf/ai/ai_session_tool_call_handler.h>

#include <functional>

class BOARD;

class KISURF_AI_PCB_SESSION_VALIDATION_SERVICE : public AI_SESSION_VALIDATION_SERVICE
{
public:
    using BOARD_PROVIDER = std::function<BOARD*()>;

    explicit KISURF_AI_PCB_SESSION_VALIDATION_SERVICE( BOARD& aBoard );
    explicit KISURF_AI_PCB_SESSION_VALIDATION_SERVICE( BOARD_PROVIDER aBoardProvider );

    AI_SESSION_VALIDATION_RESULT RunValidation(
            const AI_EXECUTION_SESSION& aSession,
            const wxString& aArgumentsJson,
            const wxString& aCurrentResultJson ) override;

private:
    BOARD_PROVIDER m_BoardProvider;
};
