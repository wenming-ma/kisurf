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

#include <kisurf/ai/ai_preview_manager.h>
#include <kisurf/ai/ai_session_tool_call_handler.h>
#include <kisurf_ai_pcb_object_resolver.h>
#include <kisurf_ai_pcb_preview_adapter.h>

#include <vector>

class BOARD;
class BOARD_ITEM;

namespace KIGFX
{
class VIEW;
}

class KISURF_AI_PCB_SESSION_PREVIEW_SERVICE : public AI_SESSION_PREVIEW_SERVICE
{
public:
    KISURF_AI_PCB_SESSION_PREVIEW_SERVICE( BOARD& aBoard, KIGFX::VIEW& aView );

    AI_SESSION_PREVIEW_RESULT RenderPreview(
            const AI_EXECUTION_SESSION& aSession,
            const wxString& aArgumentsJson ) override;
    void ClearPreview( uint64_t aSessionId ) override;

    uint64_t ActiveSessionId() const { return m_ActiveSessionId; }
    const std::vector<BOARD_ITEM*>& PreviewedItems() const
    {
        return m_Adapter.PreviewedItems();
    }

private:
    BOARD&                         m_Board;
    KISURF_AI_PCB_OBJECT_RESOLVER  m_Resolver;
    KISURF_AI_PCB_PREVIEW_ADAPTER  m_Adapter;
    AI_PREVIEW_MANAGER             m_PreviewManager;
    uint64_t                       m_ActiveSessionId = 0;
};
