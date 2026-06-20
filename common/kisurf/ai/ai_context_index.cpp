#include <kisurf/ai/ai_context_index.h>

#include <algorithm>
#include <utility>

namespace
{
int compareString( const wxString& aLeft, const wxString& aRight )
{
    const int caseInsensitive = aLeft.CmpNoCase( aRight );

    if( caseInsensitive != 0 )
        return caseInsensitive;

    return aLeft.Cmp( aRight );
}


bool objectRefLess( const AI_OBJECT_REF& aLeft, const AI_OBJECT_REF& aRight )
{
    const int labelCompare = compareString( aLeft.m_Label, aRight.m_Label );

    if( labelCompare != 0 )
        return labelCompare < 0;

    if( aLeft.m_Type != aRight.m_Type )
        return static_cast<int>( aLeft.m_Type ) < static_cast<int>( aRight.m_Type );

    return compareString( aLeft.m_Uuid.AsString(), aRight.m_Uuid.AsString() ) < 0;
}


bool anchorLess( const AI_CONTEXT_ANCHOR& aLeft, const AI_CONTEXT_ANCHOR& aRight )
{
    const int idCompare = compareString( aLeft.m_Id, aRight.m_Id );

    if( idCompare != 0 )
        return idCompare < 0;

    if( aLeft.m_Kind != aRight.m_Kind )
        return static_cast<int>( aLeft.m_Kind ) < static_cast<int>( aRight.m_Kind );

    return compareString( aLeft.m_Label, aRight.m_Label ) < 0;
}


bool panelStateLess( const AI_PANEL_STATE_RECORD& aLeft,
                     const AI_PANEL_STATE_RECORD& aRight )
{
    const int idCompare = compareString( aLeft.m_Id, aRight.m_Id );

    if( idCompare != 0 )
        return idCompare < 0;

    return compareString( aLeft.m_Title, aRight.m_Title ) < 0;
}


void sortObjectRefs( std::vector<AI_OBJECT_REF>& aObjects )
{
    std::sort( aObjects.begin(), aObjects.end(), objectRefLess );
}
} // namespace

AI_CONTEXT_INDEX::AI_CONTEXT_INDEX( AI_EDITOR_KIND aEditorKind ) :
        m_EditorKind( aEditorKind )
{
}


AI_CONTEXT_SNAPSHOT AI_CONTEXT_INDEX::BuildSnapshot() const
{
    AI_CONTEXT_SNAPSHOT snapshot;
    snapshot.m_EditorKind = m_EditorKind;
    snapshot.m_Version = m_Version;
    snapshot.m_VisibleObjects = m_VisibleObjects;
    snapshot.m_SelectedObjects = m_SelectedObjects;
    snapshot.m_Visual = m_Visual;
    snapshot.m_Anchors = m_Anchors;
    snapshot.m_PanelStates = m_PanelStates;
    return snapshot;
}


void AI_CONTEXT_INDEX::SetVisibleObjects( std::vector<AI_OBJECT_REF> aObjects )
{
    sortObjectRefs( aObjects );
    m_VisibleObjects = std::move( aObjects );
    ++m_Version.m_DocumentRevision;
}


void AI_CONTEXT_INDEX::SetSelectedObjects( std::vector<AI_OBJECT_REF> aObjects )
{
    sortObjectRefs( aObjects );
    m_SelectedObjects = std::move( aObjects );
    ++m_Version.m_SelectionRevision;
}


void AI_CONTEXT_INDEX::SetAnchors( std::vector<AI_CONTEXT_ANCHOR> aAnchors )
{
    std::sort( aAnchors.begin(), aAnchors.end(), anchorLess );
    m_Anchors = std::move( aAnchors );
    ++m_Version.m_ViewRevision;
}


void AI_CONTEXT_INDEX::SetPanelStates( std::vector<AI_PANEL_STATE_RECORD> aPanelStates )
{
    std::sort( aPanelStates.begin(), aPanelStates.end(), panelStateLess );
    m_PanelStates = std::move( aPanelStates );
    ++m_Version.m_ViewRevision;
}


void AI_CONTEXT_INDEX::SetVisualSnapshot( AI_VISUAL_SNAPSHOT aVisual )
{
    m_Visual = std::move( aVisual );

    if( m_Visual.HasPixels() || !m_Visual.m_Source.IsEmpty()
        || !m_Visual.m_UnavailableReason.IsEmpty() )
        ++m_Version.m_ViewRevision;
}


void AI_CONTEXT_INDEX::BumpViewRevision()
{
    ++m_Version.m_ViewRevision;
}
