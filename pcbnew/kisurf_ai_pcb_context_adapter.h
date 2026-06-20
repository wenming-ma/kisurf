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

#include <kisurf/ai/ai_context_index.h>

class BOARD;

class KISURF_AI_PCB_CONTEXT_ADAPTER
{
public:
    explicit KISURF_AI_PCB_CONTEXT_ADAPTER( BOARD& aBoard );

    AI_CONTEXT_INDEX BuildIndex() const;

private:
    BOARD& m_Board;
};
