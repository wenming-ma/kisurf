#pragma once

#include <kisurf/ai/ai_types.h>

#include <cstddef>
#include <vector>

class ACTION_MANAGER;
class TOOL_ACTION;

class AI_ACTION_CATALOG
{
public:
    static AI_ACTION_DESCRIPTOR DescribeAction( const TOOL_ACTION& aAction,
                                                AI_EDITOR_KIND aEditorKind,
                                                bool aEnabled = true );

    static std::vector<AI_ACTION_DESCRIPTOR> Build( const ACTION_MANAGER* aActionManager,
                                                    AI_EDITOR_KIND aEditorKind,
                                                    size_t aLimit = 0 );
};
