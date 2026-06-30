///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version 4.2.1-0-g80c4cb6)
// http://www.wxformbuilder.org/
//
// PLEASE DO *NOT* EDIT THIS FILE!
///////////////////////////////////////////////////////////////////////////

#pragma once

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/intl.h>
#include "widgets/html_window.h"
#include <wx/button.h>
#include <wx/string.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/checkbox.h>
#include <wx/sizer.h>
#include <wx/html/htmlwin.h>
#include <wx/panel.h>
#include <wx/textctrl.h>
#include <wx/notebook.h>
#include <wx/stattext.h>

#include "kicommon.h"

///////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
/// Class AI_AGENT_PANEL_BASE
///////////////////////////////////////////////////////////////////////////////
class KICOMMON_API AI_AGENT_PANEL_BASE : public wxPanel
{
	private:

	protected:
		wxBoxSizer* m_RootSizer;
		wxBoxSizer* m_HeaderSizer;
		wxButton* m_ModelSettingsButton;
		wxButton* m_NewChatButton;
		wxCheckBox* m_BackgroundAgentToggle;
		wxNotebook* m_Notebook;
		wxPanel* m_ChatPage;
		wxBoxSizer* m_ChatSizer;
		HTML_WINDOW* m_Transcript;
		wxPanel* m_LogPage;
		wxBoxSizer* m_LogSizer;
		wxTextCtrl* m_Log;
		wxPanel* m_ComposerPanel;
		wxBoxSizer* m_ComposerSizer;
		wxTextCtrl* m_Input;
		wxBoxSizer* m_ComposerFooterSizer;
		wxStaticText* m_ComposerStatus;
		wxGridSizer* m_ComposerButtonSizer;
		wxButton* m_SendButton;
		wxButton* m_StopButton;

		// Virtual event handlers, override them in your derived class
		virtual void OnModelSettings( wxCommandEvent& event ) { event.Skip(); }
		virtual void OnNewChat( wxCommandEvent& event ) { event.Skip(); }
		virtual void OnBackgroundAgentToggled( wxCommandEvent& event ) { event.Skip(); }
		virtual void OnPromptTextChanged( wxCommandEvent& event ) { event.Skip(); }
		virtual void OnPromptEnter( wxCommandEvent& event ) { event.Skip(); }
		virtual void OnSend( wxCommandEvent& event ) { event.Skip(); }
		virtual void OnStop( wxCommandEvent& event ) { event.Skip(); }


	public:

		AI_AGENT_PANEL_BASE( wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize( 420,640 ), long style = wxTAB_TRAVERSAL, const wxString& name = wxEmptyString );

		~AI_AGENT_PANEL_BASE();

};
