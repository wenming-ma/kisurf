#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_types.h>

#include <vector>

class KICOMMON_API AI_CONTEXT_INDEX
{
public:
    explicit AI_CONTEXT_INDEX( AI_EDITOR_KIND aEditorKind );

    AI_EDITOR_KIND EditorKind() const { return m_EditorKind; }
    const AI_CONTEXT_VERSION& Version() const { return m_Version; }
    const std::vector<AI_OBJECT_REF>& VisibleObjects() const { return m_VisibleObjects; }
    const std::vector<AI_OBJECT_REF>& SelectedObjects() const { return m_SelectedObjects; }
    const std::vector<AI_CONTEXT_ANCHOR>& Anchors() const { return m_Anchors; }
    const std::vector<AI_PANEL_STATE_RECORD>& PanelStates() const { return m_PanelStates; }

    AI_CONTEXT_SNAPSHOT BuildSnapshot() const;

    void SetVisibleObjects( std::vector<AI_OBJECT_REF> aObjects );
    void SetSelectedObjects( std::vector<AI_OBJECT_REF> aObjects );
    void SetAnchors( std::vector<AI_CONTEXT_ANCHOR> aAnchors );
    void SetPanelStates( std::vector<AI_PANEL_STATE_RECORD> aPanelStates );
    void SetVisualSnapshot( AI_VISUAL_SNAPSHOT aVisual );
    void SetSummary( wxString aSummary );
    void BumpViewRevision();

private:
    AI_EDITOR_KIND                       m_EditorKind = AI_EDITOR_KIND::Unknown;
    AI_CONTEXT_VERSION                   m_Version;
    std::vector<AI_OBJECT_REF>           m_VisibleObjects;
    std::vector<AI_OBJECT_REF>           m_SelectedObjects;
    std::vector<AI_CONTEXT_ANCHOR>       m_Anchors;
    std::vector<AI_PANEL_STATE_RECORD>   m_PanelStates;
    AI_VISUAL_SNAPSHOT                   m_Visual;
    wxString                             m_Summary;
};
