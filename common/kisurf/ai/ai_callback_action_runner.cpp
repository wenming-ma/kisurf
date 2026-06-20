#include <kisurf/ai/ai_callback_action_runner.h>

#include <utility>


AI_CALLBACK_ACTION_RUNNER::AI_CALLBACK_ACTION_RUNNER( ACTION_CALLBACK aCallback ) :
        m_Callback( std::move( aCallback ) )
{
}


bool AI_CALLBACK_ACTION_RUNNER::RunActionByName( const wxString& aActionName,
                                                 wxString& aError )
{
    if( !m_Callback )
    {
        aError = wxS( "Action callback is not available." );
        return false;
    }

    return m_Callback( aActionName, aError );
}
