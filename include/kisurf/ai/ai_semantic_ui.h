#pragma once

#include <kicommon.h>

#include <optional>
#include <vector>
#include <wx/string.h>

enum class AI_SEMANTIC_UI_TEXT_POLICY
{
    None,
    Plain,
    Redacted
};

struct KICOMMON_API AI_SEMANTIC_UI_BOUNDS
{
    bool m_Available = false;
    int  m_X = 0;
    int  m_Y = 0;
    int  m_Width = 0;
    int  m_Height = 0;
};

struct KICOMMON_API AI_SEMANTIC_UI_NODE
{
    wxString                   m_NodeId;
    wxString                   m_ParentNodeId;
    wxString                   m_Role;
    wxString                   m_Label;
    bool                       m_Enabled = true;
    bool                       m_Visible = true;
    bool                       m_Focused = false;
    wxString                   m_ActionName;
    wxString                   m_ToolActionId;
    bool                       m_RequiresUserConfirmation = false;
    AI_SEMANTIC_UI_TEXT_POLICY m_TextPolicy = AI_SEMANTIC_UI_TEXT_POLICY::None;
    wxString                   m_TextValue;
    AI_SEMANTIC_UI_BOUNDS      m_Bounds;
};

struct KICOMMON_API AI_SEMANTIC_UI_TREE
{
    wxString                         m_FrameId;
    wxString                         m_Title;
    bool                             m_ScreenshotAvailable = false;
    wxString                         m_ScreenshotUnavailableReason;
    std::vector<AI_SEMANTIC_UI_NODE> m_Nodes;

    const AI_SEMANTIC_UI_NODE* FindNode( const wxString& aNodeId ) const;
};

struct KICOMMON_API AI_SEMANTIC_UI_ACTION_REQUEST
{
    wxString            m_NodeId;
    wxString            m_Action;
    bool                m_HasText = false;
    wxString            m_Text;
    std::optional<bool> m_Checked;
    bool                m_UserConfirmed = false;
};

struct KICOMMON_API AI_SEMANTIC_UI_ACTION_RESULT
{
    bool     m_Success = false;
    wxString m_ErrorCode;
    wxString m_Message;
    wxString m_FocusedNodeId;
};

KICOMMON_API wxString RedactSemanticUiText( const wxString& aText );
