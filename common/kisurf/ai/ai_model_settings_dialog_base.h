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
#include <wx/string.h>
#include <wx/stattext.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/choice.h>
#include <wx/textctrl.h>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/dialog.h>

#include "kicommon.h"

///////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
/// Class AI_MODEL_SETTINGS_DIALOG_BASE
///////////////////////////////////////////////////////////////////////////////
class KICOMMON_API AI_MODEL_SETTINGS_DIALOG_BASE : public wxDialog
{
	private:

	protected:
		wxBoxSizer* m_RootSizer;
		wxFlexGridSizer* m_FormSizer;
		wxStaticText* m_ProviderLabel;
		wxChoice* m_ProviderChoice;
		wxStaticText* m_BaseUrlLabel;
		wxTextCtrl* m_BaseUrl;
		wxStaticText* m_ModelLabel;
		wxTextCtrl* m_Model;
		wxStaticText* m_ApiKeyLabel;
		wxTextCtrl* m_ApiKey;
		wxStaticText* m_HelpText;
		wxStdDialogButtonSizer* m_StdDialogButtons;
		wxButton* m_StdDialogButtonsOK;
		wxButton* m_StdDialogButtonsCancel;

	public:

		AI_MODEL_SETTINGS_DIALOG_BASE( wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = _("Model Settings"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize( 560,-1 ), long style = wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER );

		~AI_MODEL_SETTINGS_DIALOG_BASE();

};
