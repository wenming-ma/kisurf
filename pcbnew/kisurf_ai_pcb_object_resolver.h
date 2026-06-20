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

#include <kisurf/ai/ai_types.h>

#include <vector>

class BOARD;
class BOARD_ITEM;

class KISURF_AI_PCB_OBJECT_RESOLVER
{
public:
    explicit KISURF_AI_PCB_OBJECT_RESOLVER( BOARD& aBoard );

    BOARD& Board() const { return m_Board; }
    BOARD_ITEM* Resolve( const AI_OBJECT_REF& aRef ) const;
    std::vector<BOARD_ITEM*> ResolveAll( const std::vector<AI_OBJECT_REF>& aRefs ) const;

private:
    BOARD& m_Board;
};
