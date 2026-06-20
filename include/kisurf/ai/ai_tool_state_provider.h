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


class KICOMMON_API AI_TOOL_STATE_PROVIDER
{
public:
    AI_TOOL_STATE_PROVIDER();
    virtual ~AI_TOOL_STATE_PROVIDER();

    virtual AI_TOOL_STATE_SNAPSHOT BuildToolState(
            const AI_CONTEXT_VERSION& aContextVersion ) const = 0;
};
