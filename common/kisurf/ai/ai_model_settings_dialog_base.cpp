///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version 4.2.1-0-g80c4cb6)
// http://www.wxformbuilder.org/
//
// PLEASE DO *NOT* EDIT THIS FILE!
///////////////////////////////////////////////////////////////////////////

#include "ai_model_settings_dialog_base.h"

///////////////////////////////////////////////////////////////////////////

AI_MODEL_SETTINGS_DIALOG_BASE::AI_MODEL_SETTINGS_DIALOG_BASE( wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style ) : wxDialog( parent, id, title, pos, size, style )
{
	this->SetSizeHints( wxSize( 520,-1 ), wxDefaultSize );

	m_RootSizer = new wxBoxSizer( wxVERTICAL );

	m_FormSizer = new wxFlexGridSizer( 0, 2, 8, 8 );
	m_FormSizer->AddGrowableCol( 1 );
	m_FormSizer->SetFlexibleDirection( wxBOTH );
	m_FormSizer->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_SPECIFIED );

	m_ProviderLabel = new wxStaticText( this, wxID_ANY, _("Provider"), wxDefaultPosition, wxDefaultSize, 0 );
	m_ProviderLabel->Wrap( -1 );
	m_FormSizer->Add( m_ProviderLabel, 0, wxALIGN_CENTER_VERTICAL, 0 );

	wxArrayString m_ProviderChoiceChoices;
	m_ProviderChoice = new wxChoice( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, m_ProviderChoiceChoices, 0 );
	m_ProviderChoice->SetSelection( 0 );
	m_FormSizer->Add( m_ProviderChoice, 0, wxEXPAND, 0 );

	m_BaseUrlLabel = new wxStaticText( this, wxID_ANY, _("Base URL"), wxDefaultPosition, wxDefaultSize, 0 );
	m_BaseUrlLabel->Wrap( -1 );
	m_FormSizer->Add( m_BaseUrlLabel, 0, wxALIGN_CENTER_VERTICAL, 0 );

	m_BaseUrl = new wxTextCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	m_BaseUrl->SetMinSize( wxSize( 360,-1 ) );

	m_FormSizer->Add( m_BaseUrl, 0, wxEXPAND, 0 );

	m_ModelLabel = new wxStaticText( this, wxID_ANY, _("Model"), wxDefaultPosition, wxDefaultSize, 0 );
	m_ModelLabel->Wrap( -1 );
	m_FormSizer->Add( m_ModelLabel, 0, wxALIGN_CENTER_VERTICAL, 0 );

	m_Model = new wxTextCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	m_FormSizer->Add( m_Model, 0, wxEXPAND, 0 );

	m_ApiKeyLabel = new wxStaticText( this, wxID_ANY, _("API key"), wxDefaultPosition, wxDefaultSize, 0 );
	m_ApiKeyLabel->Wrap( -1 );
	m_FormSizer->Add( m_ApiKeyLabel, 0, wxALIGN_CENTER_VERTICAL, 0 );

	m_ApiKey = new wxTextCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD );
	m_FormSizer->Add( m_ApiKey, 0, wxEXPAND, 0 );


	m_RootSizer->Add( m_FormSizer, 1, wxEXPAND|wxALL, 12 );

	m_HelpText = new wxStaticText( this, wxID_ANY, _("Keys are stored locally. Changes apply to the next Agent request."), wxDefaultPosition, wxDefaultSize, 0 );
	m_HelpText->Wrap( 480 );
	m_RootSizer->Add( m_HelpText, 0, wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM, 12 );

	m_StdDialogButtons = new wxStdDialogButtonSizer();
	m_StdDialogButtonsOK = new wxButton( this, wxID_OK );
	m_StdDialogButtons->AddButton( m_StdDialogButtonsOK );
	m_StdDialogButtonsCancel = new wxButton( this, wxID_CANCEL );
	m_StdDialogButtons->AddButton( m_StdDialogButtonsCancel );
	m_StdDialogButtons->Realize();

	m_RootSizer->Add( m_StdDialogButtons, 0, wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM, 12 );


	this->SetSizer( m_RootSizer );
	this->Layout();

	this->Centre( wxBOTH );
}

AI_MODEL_SETTINGS_DIALOG_BASE::~AI_MODEL_SETTINGS_DIALOG_BASE()
{
}
