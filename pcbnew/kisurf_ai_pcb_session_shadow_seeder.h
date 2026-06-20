#pragma once

#include <kisurf/ai/ai_session_tool_call_handler.h>

class BOARD;

class KISURF_AI_PCB_SESSION_SHADOW_SEEDER : public AI_SESSION_SHADOW_BOARD_SEEDER
{
public:
    explicit KISURF_AI_PCB_SESSION_SHADOW_SEEDER( BOARD& aBoard );

    void Seed( AI_EXECUTION_SESSION& aSession ) override;

private:
    BOARD& m_Board;
};
