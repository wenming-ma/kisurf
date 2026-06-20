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

#include <vector>

enum class AI_VALIDATION_SCOPE
{
    None,
    LocalPreflight,
    PostApplyLocal,
    FullPcbDrc,
    FullSchErc,
    HeadlessBatch
};

class KICOMMON_API AI_VALIDATION_DIFF
{
public:
    std::vector<AI_VALIDATION_ISSUE> m_Before;
    std::vector<AI_VALIDATION_ISSUE> m_After;

    AI_VALIDATION_SUMMARY Classify() const;
};

class KICOMMON_API AI_VALIDATION_POLICY
{
public:
    bool BlocksApply( AI_VALIDATION_SCOPE aScope, const AI_VALIDATION_SUMMARY& aSummary ) const;
};
