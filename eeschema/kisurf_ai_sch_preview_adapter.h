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
#include <math/vector2d.h>

#include <cstdint>
#include <optional>
#include <vector>

class KISURF_AI_SCH_OBJECT_RESOLVER;
class SCH_ITEM;

namespace KIGFX
{
class VIEW;
}

class KISURF_AI_SCH_PREVIEW_ADAPTER : public AI_PREVIEW_ADAPTER
{
public:
    KISURF_AI_SCH_PREVIEW_ADAPTER( KISURF_AI_SCH_OBJECT_RESOLVER& aResolver,
                                   KIGFX::VIEW& aView,
                                   std::optional<VECTOR2I> aMoveDelta = std::nullopt );

    void BeginPreview( uint64_t aPreviewId ) override;
    void ShowObject( uint64_t aPreviewId, const AI_OBJECT_REF& aObject ) override;
    void ClearPreview( uint64_t aPreviewId ) override;

    uint64_t ActivePreviewId() const { return m_ActivePreviewId; }
    const std::vector<SCH_ITEM*>& PreviewedItems() const { return m_PreviewedItems; }

private:
    KISURF_AI_SCH_OBJECT_RESOLVER& m_Resolver;
    KIGFX::VIEW&                   m_View;
    std::optional<VECTOR2I>         m_MoveDelta;
    uint64_t                       m_ActivePreviewId = 0;
    std::vector<SCH_ITEM*>         m_PreviewedItems;
};
