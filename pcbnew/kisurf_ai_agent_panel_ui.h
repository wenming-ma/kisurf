#ifndef KISURF_AI_AGENT_PANEL_UI_H
#define KISURF_AI_AGENT_PANEL_UI_H

#include <wx/aui/aui.h>


inline bool KisurfShouldRestoreAgentPaneWidth( int aStoredWidth )
{
    return aStoredWidth > 0;
}


inline void KisurfPrepareVisibleAgentPane( wxAuiPaneInfo& aPane )
{
    aPane.Dock().Right().Layer( 5 ).Position( 1 ).Show( true );
}


#endif
