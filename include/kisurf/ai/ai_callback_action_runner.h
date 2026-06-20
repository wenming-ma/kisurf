#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_tool_execution.h>

#include <functional>

class KICOMMON_API AI_CALLBACK_ACTION_RUNNER : public AI_ACTION_RUNNER
{
public:
    using ACTION_CALLBACK = std::function<bool( const wxString& aActionName,
                                                wxString& aError )>;

    explicit AI_CALLBACK_ACTION_RUNNER( ACTION_CALLBACK aCallback );

    bool RunActionByName( const wxString& aActionName, wxString& aError ) override;

private:
    ACTION_CALLBACK m_Callback;
};
