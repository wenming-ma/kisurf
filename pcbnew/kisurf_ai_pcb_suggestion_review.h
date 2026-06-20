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
class COMMIT;
class KISURF_AI_PCB_OBJECT_RESOLVER;

bool AcceptAiPcbSuggestion( AI_AGENT_PANEL_MODEL& aModel, uint64_t aSuggestionId,
                            KISURF_AI_PCB_OBJECT_RESOLVER& aResolver,
                            COMMIT& aCommit );
