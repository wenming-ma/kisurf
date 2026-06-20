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
#include <kisurf/ai/ai_edit_session.h>
#include <kisurf/ai/ai_preview_manager.h>
#include <kisurf/ai/ai_types.h>

#include <cstddef>
#include <optional>
#include <vector>

class KICOMMON_API AI_SUGGESTION_PROVIDER
{
public:
    virtual ~AI_SUGGESTION_PROVIDER() = default;

    virtual std::optional<AI_SUGGESTION_RECORD> Suggest(
            const AI_SUGGESTION_TRIGGER& aTrigger ) = 0;
};

class KICOMMON_API AI_SUGGESTION_ORCHESTRATOR
{
public:
    explicit AI_SUGGESTION_ORCHESTRATOR( AI_SUGGESTION_PROVIDER& aProvider,
                                         size_t aCapacity = 8 );

    std::optional<AI_SUGGESTION_RECORD> Update( AI_SUGGESTION_TRIGGER aTrigger );
    std::optional<AI_SUGGESTION_RECORD> AddSuggestion( AI_SUGGESTION_RECORD aSuggestion );
    std::vector<AI_SUGGESTION_RECORD> Records() const;
    std::optional<AI_SUGGESTION_RECORD> Find( uint64_t aSuggestionId ) const;

    bool CanPreview( uint64_t aSuggestionId ) const;
    bool CanAccept( uint64_t aSuggestionId ) const;
    bool BeginPreview( uint64_t aSuggestionId, AI_PREVIEW_MANAGER& aPreviewManager );
    bool Accept( uint64_t aSuggestionId, AI_EDIT_SESSION& aEditSession );
    bool MarkAccepted( uint64_t aSuggestionId );
    bool Reject( uint64_t aSuggestionId );
    size_t ExpireStale( const AI_CONTEXT_VERSION& aCurrentVersion );

private:
    AI_SUGGESTION_RECORD* findMutable( uint64_t aSuggestionId );
    void trimToCapacity();

    AI_SUGGESTION_PROVIDER&          m_Provider;
    size_t                           m_Capacity = 0;
    uint64_t                         m_NextId = 1;
    uint64_t                         m_NextSequence = 1;
    std::vector<AI_SUGGESTION_RECORD> m_Records;
};
