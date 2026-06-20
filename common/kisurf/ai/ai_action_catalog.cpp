#include <kisurf/ai/ai_action_catalog.h>

#include <tool/action_manager.h>
#include <tool/tool_action.h>

#include <algorithm>

namespace
{
bool containsAny( const wxString& aValue, const std::vector<wxString>& aNeedles )
{
    const wxString lower = aValue.Lower();

    return std::any_of( aNeedles.begin(), aNeedles.end(),
                        [&]( const wxString& aNeedle )
                        {
                            return lower.Contains( aNeedle );
                        } );
}


AI_ACTION_SAFETY classifyAction( const TOOL_ACTION& aAction )
{
    const wxString name = wxString::FromUTF8( aAction.GetName().c_str() ).Lower();

    if( containsAny( name, { wxS( "delete" ), wxS( "removefile" ), wxS( "revert" ) } ) )
        return AI_ACTION_SAFETY::Destructive;

    if( containsAny( name,
                     { wxS( "paste" ), wxS( "duplicate" ), wxS( "move" ), wxS( "drag" ),
                       wxS( "route" ), wxS( "place" ), wxS( "draw" ), wxS( "add" ),
                       wxS( "edit" ), wxS( "updatepcb" ), wxS( "updateschematic" ),
                       wxS( "cleanup" ), wxS( "fill" ), wxS( "increment" ) } ) )
    {
        return AI_ACTION_SAFETY::Modifying;
    }

    if( containsAny( name,
                     { wxS( "show" ), wxS( "zoom" ), wxS( "find" ), wxS( "inspect" ),
                       wxS( "highlight" ), wxS( "measure" ), wxS( "select" ),
                       wxS( "copy" ), wxS( "toggle" ) } ) )
    {
        return AI_ACTION_SAFETY::ReadOnly;
    }

    if( aAction.IsActivation() )
        return AI_ACTION_SAFETY::Interactive;

    return AI_ACTION_SAFETY::Interactive;
}


int safetyRank( AI_ACTION_SAFETY aSafety )
{
    switch( aSafety )
    {
    case AI_ACTION_SAFETY::ReadOnly:
        return 0;

    case AI_ACTION_SAFETY::Interactive:
        return 1;

    case AI_ACTION_SAFETY::Modifying:
        return 2;

    case AI_ACTION_SAFETY::Destructive:
        return 3;
    }

    return 4;
}


int compareString( const wxString& aLeft, const wxString& aRight )
{
    const int caseInsensitive = aLeft.CmpNoCase( aRight );

    if( caseInsensitive != 0 )
        return caseInsensitive;

    return aLeft.Cmp( aRight );
}


bool descriptorLess( const AI_ACTION_DESCRIPTOR& aLeft, const AI_ACTION_DESCRIPTOR& aRight )
{
    const int leftRank = safetyRank( aLeft.m_Safety );
    const int rightRank = safetyRank( aRight.m_Safety );

    if( leftRank != rightRank )
        return leftRank < rightRank;

    const int nameCompare = compareString( aLeft.m_Name, aRight.m_Name );

    if( nameCompare != 0 )
        return nameCompare < 0;

    const int friendlyCompare = compareString( aLeft.m_FriendlyName, aRight.m_FriendlyName );

    if( friendlyCompare != 0 )
        return friendlyCompare < 0;

    return compareString( aLeft.m_Description, aRight.m_Description ) < 0;
}
} // namespace


AI_ACTION_DESCRIPTOR AI_ACTION_CATALOG::DescribeAction( const TOOL_ACTION& aAction,
                                                        AI_EDITOR_KIND aEditorKind,
                                                        bool aEnabled )
{
    AI_ACTION_DESCRIPTOR descriptor;
    descriptor.m_Name = wxString::FromUTF8( aAction.GetName().c_str() );
    descriptor.m_FriendlyName = aAction.GetFriendlyName();
    descriptor.m_Description = aAction.GetDescription();
    descriptor.m_EditorKind = aEditorKind;
    descriptor.m_Safety = classifyAction( aAction );
    descriptor.m_Enabled = aEnabled;

    if( descriptor.m_FriendlyName.IsEmpty() )
        descriptor.m_FriendlyName = aAction.GetMenuLabel();

    if( descriptor.m_Description.IsEmpty() )
        descriptor.m_Description = aAction.GetTooltip( false );

    return descriptor;
}


std::vector<AI_ACTION_DESCRIPTOR> AI_ACTION_CATALOG::Build( const ACTION_MANAGER* aActionManager,
                                                            AI_EDITOR_KIND aEditorKind,
                                                            size_t aLimit )
{
    std::vector<AI_ACTION_DESCRIPTOR> descriptors;

    if( !aActionManager )
        return descriptors;

    for( const auto& [name, action] : aActionManager->GetActions() )
    {
        wxUnusedVar( name );

        AI_ACTION_DESCRIPTOR descriptor = DescribeAction( *action, aEditorKind );

        if( descriptor.IsValid() )
            descriptors.push_back( descriptor );
    }

    std::sort( descriptors.begin(), descriptors.end(), descriptorLess );

    if( aLimit > 0 && descriptors.size() > aLimit )
        descriptors.resize( aLimit );

    return descriptors;
}
