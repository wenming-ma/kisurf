/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 */

#include <kisurf/ai/ai_editor_activity_recorder.h>

#include <json_common.h>
#include <tool/actions.h>
#include <tool/tool_event.h>

#include <cmath>
#include <string>
#include <utility>
#include <vector>


namespace
{

std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


wxString fromJson( const nlohmann::json& aJson )
{
    return wxString::FromUTF8( aJson.dump().c_str() );
}


std::string buttonName( int aButton )
{
    switch( aButton )
    {
    case BUT_LEFT:
        return "left";

    case BUT_RIGHT:
        return "right";

    case BUT_MIDDLE:
        return "middle";

    case BUT_AUX1:
        return "aux1";

    case BUT_AUX2:
        return "aux2";

    default:
        return "unknown";
    }
}


std::vector<std::string> buttonNames( int aButtons )
{
    std::vector<std::string> names;

    for( int button : { BUT_LEFT, BUT_RIGHT, BUT_MIDDLE, BUT_AUX1, BUT_AUX2 } )
    {
        if( aButtons & button )
            names.push_back( buttonName( button ) );
    }

    return names;
}


std::vector<std::string> modifierNames( int aModifiers )
{
    std::vector<std::string> names;

    const std::vector<std::pair<int, std::string>> modifiers = {
        { MD_SHIFT, "shift" },
        { MD_CTRL, "ctrl" },
        { MD_ALT, "alt" },
        { MD_SUPER, "super" },
        { MD_META, "meta" },
        { MD_ALTGR, "altgr" },
    };

    for( const auto& [mask, name] : modifiers )
    {
        if( aModifiers & mask )
            names.push_back( name );
    }

    return names;
}


wxString categoryName( const TOOL_EVENT& aEvent )
{
    switch( aEvent.Category() )
    {
    case TC_MOUSE:
        return wxS( "mouse" );

    case TC_KEYBOARD:
        return wxS( "keyboard" );

    case TC_COMMAND:
        return wxS( "command" );

    case TC_MESSAGE:
        return wxS( "message" );

    case TC_VIEW:
        return wxS( "view" );

    default:
        return wxS( "unknown" );
    }
}


wxString eventActionName( const TOOL_EVENT& aEvent )
{
    if( aEvent.IsClick() )
        return wxS( "click" );

    if( aEvent.IsDblClick() )
        return wxS( "double-click" );

    if( aEvent.IsMouseDown() )
        return wxS( "mouse-down" );

    if( aEvent.IsMouseUp() )
        return wxS( "mouse-up" );

    if( aEvent.IsDrag() )
        return wxS( "drag" );

    if( aEvent.IsMotion() )
        return wxS( "motion" );

    if( aEvent.IsKeyPressed() )
        return wxS( "key-pressed" );

    if( aEvent.IsCancel() )
        return wxS( "cancel" );

    if( aEvent.IsActivate() )
        return wxS( "activate" );

    if( aEvent.Action() == TA_ACTION )
        return wxS( "action" );

    return wxS( "event" );
}


wxString stableActionName( const TOOL_EVENT& aEvent )
{
    if( !aEvent.CommandString().empty() )
        return wxString::FromUTF8( aEvent.CommandString().c_str() );

    if( aEvent.IsClick() )
        return wxS( "mouse.click" );

    if( aEvent.IsDblClick() )
        return wxS( "mouse.doubleClick" );

    if( aEvent.IsCancel() )
        return wxS( "tool.cancel" );

    if( aEvent.IsActivate() )
        return wxS( "tool.activate" );

    return wxS( "tool.event" );
}


bool shouldRecordMessageEvent( const TOOL_EVENT& aEvent )
{
    if( aEvent.IsSelectionEvent() )
        return true;

    return aEvent.CommandString() == "common.Interactive.modified"
        || aEvent.CommandString() == "common.Interactive.moved";
}


bool shouldRecordEvent( const TOOL_EVENT& aEvent )
{
    if( aEvent.IsMotion() || aEvent.IsDrag() )
        return false;

    if( aEvent.Category() == TC_COMMAND )
        return !aEvent.CommandString().empty() || aEvent.IsActivate() || aEvent.IsCancel();

    if( aEvent.Category() == TC_MESSAGE )
        return shouldRecordMessageEvent( aEvent );

    if( aEvent.Category() == TC_MOUSE )
        return aEvent.IsClick() || aEvent.IsDblClick();

    return false;
}


wxString argumentsJson( const TOOL_EVENT& aEvent )
{
    nlohmann::json args = { { "category", toUtf8String( categoryName( aEvent ) ) },
                            { "action", toUtf8String( eventActionName( aEvent ) ) } };

    if( aEvent.Category() == TC_MOUSE && aEvent.HasPosition() )
    {
        const VECTOR2D pos = aEvent.Position();

        args["x"] = static_cast<long long>( std::llround( pos.x ) );
        args["y"] = static_cast<long long>( std::llround( pos.y ) );
    }

    if( aEvent.Category() == TC_MOUSE )
    {
        const std::vector<std::string> buttons = buttonNames( aEvent.Buttons() );

        if( buttons.size() == 1 )
            args["button"] = buttons.front();

        args["buttons"] = buttons;
        args["modifiers"] = modifierNames( aEvent.Modifier() );
    }
    else if( aEvent.Category() == TC_KEYBOARD )
    {
        args["key_code"] = aEvent.KeyCode();
        args["modifiers"] = modifierNames( aEvent.Modifier() );
    }

    return fromJson( args );
}

}


std::optional<AI_ACTIVITY_RECORD> MakeAiActivityRecordFromToolEvent(
        const TOOL_EVENT& aEvent, AI_EDITOR_KIND aEditorKind )
{
    if( !shouldRecordEvent( aEvent ) )
        return std::nullopt;

    AI_ACTIVITY_RECORD record;
    record.m_Kind = AI_ACTIVITY_KIND::UserAction;
    record.m_EditorKind = aEditorKind;
    record.m_ActionName = stableActionName( aEvent );
    record.m_ArgumentsJson = argumentsJson( aEvent );
    record.m_Allowed = true;
    record.m_Executed = true;
    record.m_Message = wxString::FromUTF8( aEvent.Format().c_str() );

    return record;
}
