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

#include <cstdint>

class AI_AGENT_PANEL_MODEL;
class AI_ACCEPT_APPLY_ADAPTER;
class AI_SESSION_VALIDATION_SERVICE;
struct AI_NEXT_ACTION_CONTEXT_VERSION;
class COMMIT;
class KISURF_AI_PCB_OBJECT_RESOLVER;

bool AcceptAiPcbSuggestion( AI_AGENT_PANEL_MODEL& aModel, uint64_t aSuggestionId,
                            AI_ACCEPT_APPLY_ADAPTER& aSessionApplyAdapter,
                            AI_SESSION_VALIDATION_SERVICE& aValidationService,
                            const AI_NEXT_ACTION_CONTEXT_VERSION& aCurrentContextVersion );

bool AcceptAiPcbSuggestion( AI_AGENT_PANEL_MODEL& aModel, uint64_t aSuggestionId,
                            KISURF_AI_PCB_OBJECT_RESOLVER& aResolver,
                            COMMIT& aCommit,
                            const AI_NEXT_ACTION_CONTEXT_VERSION& aCurrentContextVersion );
