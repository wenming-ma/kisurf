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
#include <kisurf/ai/ai_suggestion_orchestrator.h>

#include <memory>
#include <optional>
#include <vector>

class KICOMMON_API AI_VIA_PATTERN_NEXT_ACTION_PROVIDER : public AI_SUGGESTION_PROVIDER
{
public:
    std::optional<AI_SUGGESTION_RECORD> Suggest(
            const AI_SUGGESTION_TRIGGER& aTrigger ) override;
};

class KICOMMON_API AI_ROUTING_SEGMENT_NEXT_ACTION_PROVIDER : public AI_SUGGESTION_PROVIDER
{
public:
    std::optional<AI_SUGGESTION_RECORD> Suggest(
            const AI_SUGGESTION_TRIGGER& aTrigger ) override;
};

class KICOMMON_API AI_PANEL_TABLE_NEXT_ACTION_PROVIDER : public AI_SUGGESTION_PROVIDER
{
public:
    std::optional<AI_SUGGESTION_RECORD> Suggest(
            const AI_SUGGESTION_TRIGGER& aTrigger ) override;
};

class KICOMMON_API AI_NEXT_ACTION_CONTROLLER : public AI_SUGGESTION_PROVIDER
{
public:
    AI_NEXT_ACTION_CONTROLLER() = default;
    ~AI_NEXT_ACTION_CONTROLLER() override;

    AI_NEXT_ACTION_CONTROLLER( const AI_NEXT_ACTION_CONTROLLER& ) = delete;
    AI_NEXT_ACTION_CONTROLLER& operator=( const AI_NEXT_ACTION_CONTROLLER& ) = delete;

    void AddProvider( std::unique_ptr<AI_SUGGESTION_PROVIDER> aProvider );
    size_t ProviderCount() const { return m_Providers.size(); }

    std::optional<AI_SUGGESTION_RECORD> Suggest(
            const AI_SUGGESTION_TRIGGER& aTrigger ) override;

private:
    std::vector<std::unique_ptr<AI_SUGGESTION_PROVIDER>> m_Providers;
    std::optional<AI_CONTEXT_VERSION>                    m_LastContextVersion;
    AI_TOOL_STATE_KIND                                   m_LastToolStateKind =
            AI_TOOL_STATE_KIND::Unknown;
    wxString                                             m_LastFingerprint;
};
