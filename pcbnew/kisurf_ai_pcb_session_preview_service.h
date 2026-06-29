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
#include <kisurf/ai/ai_visual_snapshot.h>
#include <kisurf_ai_pcb_object_resolver.h>
#include <kisurf_ai_pcb_preview_adapter.h>

#include <functional>
#include <memory>
#include <vector>

class BOARD;
class BOARD_ITEM;
class EDA_DRAW_PANEL_GAL;

namespace KIGFX
{
class VIEW;
}

class KISURF_AI_PCB_SESSION_PREVIEW_SERVICE : public AI_SESSION_PREVIEW_SERVICE
{
public:
    using BOARD_PROVIDER = std::function<BOARD*()>;
    using VIEW_PROVIDER = std::function<KIGFX::VIEW*()>;
    using CANVAS_PROVIDER = std::function<EDA_DRAW_PANEL_GAL*()>;
    using PREVIEW_FRAME_CAPTURE_PROVIDER =
            std::function<AI_VISUAL_SNAPSHOT( uint64_t,
                                               const AI_EXECUTION_SESSION& )>;

    KISURF_AI_PCB_SESSION_PREVIEW_SERVICE(
            BOARD& aBoard, KIGFX::VIEW& aView,
            EDA_DRAW_PANEL_GAL* aCanvas = nullptr );
    KISURF_AI_PCB_SESSION_PREVIEW_SERVICE(
            BOARD_PROVIDER aBoardProvider, VIEW_PROVIDER aViewProvider,
            CANVAS_PROVIDER aCanvasProvider = CANVAS_PROVIDER() );

    AI_SESSION_PREVIEW_RESULT RenderPreview(
            const AI_EXECUTION_SESSION& aSession,
            const wxString& aArgumentsJson ) override;
    void ClearPreview( uint64_t aSessionId ) override;
    void SetPreviewFrameCaptureProvider(
            PREVIEW_FRAME_CAPTURE_PROVIDER aProvider );

    uint64_t ActiveSessionId() const { return m_ActiveSessionId; }
    const std::vector<BOARD_ITEM*>& PreviewedItems() const;

private:
    bool ensureBackend( AI_SESSION_PREVIEW_RESULT& aResult );

    BOARD_PROVIDER                 m_BoardProvider;
    VIEW_PROVIDER                  m_ViewProvider;
    CANVAS_PROVIDER                m_CanvasProvider;
    BOARD*                         m_CurrentBoard = nullptr;
    KIGFX::VIEW*                   m_CurrentView = nullptr;
    std::unique_ptr<KISURF_AI_PCB_OBJECT_RESOLVER> m_Resolver;
    std::unique_ptr<KISURF_AI_PCB_PREVIEW_ADAPTER> m_Adapter;
    std::unique_ptr<AI_PREVIEW_MANAGER>            m_PreviewManager;
    uint64_t                       m_ActiveSessionId = 0;
    PREVIEW_FRAME_CAPTURE_PROVIDER m_PreviewFrameCaptureProvider;
};
