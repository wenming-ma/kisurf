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

#include <kisurf/ai/ai_tool_state_provider.h>

#include <cstdint>
#include <wx/string.h>

class TOOL_EVENT;
class TOOL_MANAGER;


class KISURF_AI_PCB_TOOL_STATE_PROVIDER : public AI_TOOL_STATE_PROVIDER
{
public:
    explicit KISURF_AI_PCB_TOOL_STATE_PROVIDER( TOOL_MANAGER* aToolManager );
    ~KISURF_AI_PCB_TOOL_STATE_PROVIDER() override;

    KISURF_AI_PCB_TOOL_STATE_PROVIDER( const KISURF_AI_PCB_TOOL_STATE_PROVIDER& ) = delete;
    KISURF_AI_PCB_TOOL_STATE_PROVIDER& operator=( const KISURF_AI_PCB_TOOL_STATE_PROVIDER& ) = delete;

    AI_TOOL_STATE_SNAPSHOT BuildToolState(
            const AI_CONTEXT_VERSION& aContextVersion ) const override;

    void RecordToolEvent( const TOOL_EVENT& aEvent );

private:
    AI_TOOL_STATE_KIND classifyActiveState() const;

private:
    TOOL_MANAGER* m_ToolManager = nullptr;
    uint64_t      m_EventObserverId = 0;
    wxString      m_LastActionName;
    VECTOR2I      m_LastCursorBoardPosition = VECTOR2I( 0, 0 );
    bool          m_HasLastCursorBoardPosition = false;
};
