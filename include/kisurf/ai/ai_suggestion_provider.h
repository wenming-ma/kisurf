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
#include <kisurf/ai/ai_types.h>

#include <optional>

class KICOMMON_API AI_SUGGESTION_PROVIDER
{
public:
    virtual ~AI_SUGGESTION_PROVIDER() = default;

    virtual std::optional<AI_SUGGESTION_RECORD> Suggest(
            const AI_SUGGESTION_TRIGGER& aTrigger ) = 0;
};
