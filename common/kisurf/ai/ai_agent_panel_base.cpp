///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version 4.2.1-0-g80c4cb6)
// http://www.wxformbuilder.org/
//
// PLEASE DO *NOT* EDIT THIS FILE!
///////////////////////////////////////////////////////////////////////////

#include "ai_agent_panel_base.h"

///////////////////////////////////////////////////////////////////////////

AI_AGENT_PANEL_BASE::AI_AGENT_PANEL_BASE( wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name ) : wxPanel( parent, id, pos, size, style, name )
{
	this->SetMinSize( wxSize( 360,420 ) );

	m_RootSizer = new wxBoxSizer( wxVERTICAL );

	m_HeaderSizer = new wxBoxSizer( wxHORIZONTAL );

	m_ModelSettingsButton = new wxButton( this, wxID_ANY, _("Model..."), wxDefaultPosition, wxDefaultSize, 0 );
	m_HeaderSizer->Add( m_ModelSettingsButton, 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 6 );

	m_NewChatButton = new wxButton( this, wxID_ANY, _("New Chat"), wxDefaultPosition, wxDefaultSize, 0 );
	m_HeaderSizer->Add( m_NewChatButton, 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 6 );

	m_BackgroundAgentToggle = new wxCheckBox( this, wxID_ANY, _("Background Agent"), wxDefaultPosition, wxDefaultSize, 0 );
	m_HeaderSizer->Add( m_BackgroundAgentToggle, 0, wxALIGN_CENTER_VERTICAL, 0 );


	m_RootSizer->Add( m_HeaderSizer, 0, wxEXPAND|wxALL, 6 );

	m_Notebook = new wxNotebook( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 );
	m_ChatPage = new wxPanel( m_Notebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	m_ChatSizer = new wxBoxSizer( wxVERTICAL );

	m_Transcript = new HTML_WINDOW( m_ChatPage, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHW_SCROLLBAR_AUTO );
	m_ChatSizer->Add( m_Transcript, 1, wxEXPAND|wxALL, 8 );


	m_ChatPage->SetSizer( m_ChatSizer );
	m_ChatPage->Layout();
	m_ChatSizer->Fit( m_ChatPage );
	m_Notebook->AddPage( m_ChatPage, _("Chat"), true );
	m_LogPage = new wxPanel( m_Notebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	m_LogSizer = new wxBoxSizer( wxVERTICAL );

	m_Log = new wxTextCtrl( m_LogPage, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE|wxTE_READONLY|wxBORDER_NONE );
	m_LogSizer->Add( m_Log, 1, wxEXPAND|wxALL, 8 );


	m_LogPage->SetSizer( m_LogSizer );
	m_LogPage->Layout();
	m_LogSizer->Fit( m_LogPage );
	m_Notebook->AddPage( m_LogPage, _("Log"), false );

	m_RootSizer->Add( m_Notebook, 1, wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM, 6 );

	m_ComposerPanel = new wxPanel( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL|wxBORDER_SIMPLE );
	m_ComposerPanel->SetMinSize( wxSize( -1,150 ) );

	m_ComposerSizer = new wxBoxSizer( wxVERTICAL );

	m_Input = new wxTextCtrl( m_ComposerPanel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE|wxTE_PROCESS_ENTER|wxBORDER_NONE );
	m_Input->SetMinSize( wxSize( -1,76 ) );

	m_ComposerSizer->Add( m_Input, 1, wxEXPAND|wxLEFT|wxRIGHT|wxTOP, 6 );

	m_ComposerFooterSizer = new wxBoxSizer( wxVERTICAL );

	m_ComposerStatus = new wxStaticText( m_ComposerPanel, wxID_ANY, _("Ready"), wxDefaultPosition, wxDefaultSize, 0 );
	m_ComposerStatus->Wrap( -1 );
	m_ComposerFooterSizer->Add( m_ComposerStatus, 0, wxEXPAND|wxBOTTOM, 6 );

	m_ComposerButtonSizer = new wxGridSizer( 0, 3, 4, 4 );

	m_PreviewButton = new wxButton( m_ComposerPanel, wxID_ANY, _("Show"), wxDefaultPosition, wxDefaultSize, 0 );
	m_ComposerButtonSizer->Add( m_PreviewButton, 0, wxEXPAND, 0 );

	m_AcceptButton = new wxButton( m_ComposerPanel, wxID_ANY, _("Accept"), wxDefaultPosition, wxDefaultSize, 0 );
	m_ComposerButtonSizer->Add( m_AcceptButton, 0, wxEXPAND, 0 );

	m_RejectButton = new wxButton( m_ComposerPanel, wxID_ANY, _("Reject"), wxDefaultPosition, wxDefaultSize, 0 );
	m_ComposerButtonSizer->Add( m_RejectButton, 0, wxEXPAND, 0 );

	m_SendButton = new wxButton( m_ComposerPanel, wxID_ANY, _("Send"), wxDefaultPosition, wxDefaultSize, 0 );
	m_ComposerButtonSizer->Add( m_SendButton, 0, wxEXPAND, 0 );

	m_StopButton = new wxButton( m_ComposerPanel, wxID_ANY, _("Stop"), wxDefaultPosition, wxDefaultSize, 0 );
	m_ComposerButtonSizer->Add( m_StopButton, 0, wxEXPAND, 0 );


	m_ComposerFooterSizer->Add( m_ComposerButtonSizer, 0, wxALIGN_RIGHT, 0 );


	m_ComposerSizer->Add( m_ComposerFooterSizer, 0, wxEXPAND|wxALL, 6 );


	m_ComposerPanel->SetSizer( m_ComposerSizer );
	m_ComposerPanel->Layout();
	m_ComposerSizer->Fit( m_ComposerPanel );
	m_RootSizer->Add( m_ComposerPanel, 0, wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM, 6 );


	this->SetSizer( m_RootSizer );
	this->Layout();

	// Connect Events
	m_ModelSettingsButton->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( AI_AGENT_PANEL_BASE::OnModelSettings ), NULL, this );
	m_NewChatButton->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( AI_AGENT_PANEL_BASE::OnNewChat ), NULL, this );
	m_BackgroundAgentToggle->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( AI_AGENT_PANEL_BASE::OnBackgroundAgentToggled ), NULL, this );
	m_Input->Connect( wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler( AI_AGENT_PANEL_BASE::OnPromptTextChanged ), NULL, this );
	m_Input->Connect( wxEVT_COMMAND_TEXT_ENTER, wxCommandEventHandler( AI_AGENT_PANEL_BASE::OnPromptEnter ), NULL, this );
	m_PreviewButton->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( AI_AGENT_PANEL_BASE::OnPreviewSuggestion ), NULL, this );
	m_AcceptButton->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( AI_AGENT_PANEL_BASE::OnAcceptSuggestion ), NULL, this );
	m_RejectButton->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( AI_AGENT_PANEL_BASE::OnRejectSuggestion ), NULL, this );
	m_SendButton->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( AI_AGENT_PANEL_BASE::OnSend ), NULL, this );
	m_StopButton->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( AI_AGENT_PANEL_BASE::OnStop ), NULL, this );
}

AI_AGENT_PANEL_BASE::~AI_AGENT_PANEL_BASE()
{
	// Disconnect Events
	m_ModelSettingsButton->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( AI_AGENT_PANEL_BASE::OnModelSettings ), NULL, this );
	m_NewChatButton->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( AI_AGENT_PANEL_BASE::OnNewChat ), NULL, this );
	m_BackgroundAgentToggle->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( AI_AGENT_PANEL_BASE::OnBackgroundAgentToggled ), NULL, this );
	m_Input->Disconnect( wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler( AI_AGENT_PANEL_BASE::OnPromptTextChanged ), NULL, this );
	m_Input->Disconnect( wxEVT_COMMAND_TEXT_ENTER, wxCommandEventHandler( AI_AGENT_PANEL_BASE::OnPromptEnter ), NULL, this );
	m_PreviewButton->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( AI_AGENT_PANEL_BASE::OnPreviewSuggestion ), NULL, this );
	m_AcceptButton->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( AI_AGENT_PANEL_BASE::OnAcceptSuggestion ), NULL, this );
	m_RejectButton->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( AI_AGENT_PANEL_BASE::OnRejectSuggestion ), NULL, this );
	m_SendButton->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( AI_AGENT_PANEL_BASE::OnSend ), NULL, this );
	m_StopButton->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( AI_AGENT_PANEL_BASE::OnStop ), NULL, this );

}
