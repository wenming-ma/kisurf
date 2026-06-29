#pragma once

#include <kisurf/ai/ai_session_tool_call_handler.h>

#include <functional>

class BOARD;

class KISURF_AI_PCB_SESSION_SHADOW_SEEDER : public AI_SESSION_SHADOW_BOARD_SEEDER
{
public:
    using BOARD_PROVIDER = std::function<BOARD*()>;

    struct SEED_OPTIONS
    {
        bool m_IncludeFootprints = true;
    };

    explicit KISURF_AI_PCB_SESSION_SHADOW_SEEDER( BOARD& aBoard,
                                                  SEED_OPTIONS aOptions = {} );
    explicit KISURF_AI_PCB_SESSION_SHADOW_SEEDER( BOARD_PROVIDER aBoardProvider,
                                                  SEED_OPTIONS aOptions = {} );

    void Seed( AI_EXECUTION_SESSION& aSession ) override;

private:
    BOARD_PROVIDER m_BoardProvider;
    SEED_OPTIONS   m_Options;
};
