#include <boost/test/unit_test.hpp>

#include <json_common.h>
#include <kisurf/ai/ai_editor_activity_recorder.h>

#include <tool/actions.h>
#include <tool/tool_event.h>

BOOST_AUTO_TEST_SUITE( AiEditorActivityRecorder )


BOOST_AUTO_TEST_CASE( CommandEventMapsToUserAction )
{
    TOOL_EVENT event( TC_COMMAND, TA_ACTION, "pcbnew.Route.routeSingleTrack" );

    std::optional<AI_ACTIVITY_RECORD> record =
            MakeAiActivityRecordFromToolEvent( event, AI_EDITOR_KIND::Pcb );

    BOOST_REQUIRE( record.has_value() );
    BOOST_CHECK( record->m_Kind == AI_ACTIVITY_KIND::UserAction );
    BOOST_CHECK( record->m_EditorKind == AI_EDITOR_KIND::Pcb );
    BOOST_CHECK_EQUAL( record->m_ActionName,
                       wxString( wxS( "pcbnew.Route.routeSingleTrack" ) ) );
    BOOST_CHECK( record->m_Message.Contains( wxS( "pcbnew.Route.routeSingleTrack" ) ) );
    BOOST_CHECK( record->m_ArgumentsJson.Contains( wxS( "\"category\":\"command\"" ) ) );
}


BOOST_AUTO_TEST_CASE( SelectionEventMapsToSelectionAction )
{
    std::optional<AI_ACTIVITY_RECORD> record =
            MakeAiActivityRecordFromToolEvent( EVENTS::SelectedEvent,
                                               AI_EDITOR_KIND::Schematic );

    BOOST_REQUIRE( record.has_value() );
    BOOST_CHECK( record->m_Kind == AI_ACTIVITY_KIND::UserAction );
    BOOST_CHECK( record->m_EditorKind == AI_EDITOR_KIND::Schematic );
    BOOST_CHECK_EQUAL( record->m_ActionName,
                       wxString( wxS( "common.Interactive.selected" ) ) );
}


BOOST_AUTO_TEST_CASE( MoveEventMapsToMoveAction )
{
    std::optional<AI_ACTIVITY_RECORD> record =
            MakeAiActivityRecordFromToolEvent( EVENTS::SelectedItemsMoved,
                                               AI_EDITOR_KIND::Pcb );

    BOOST_REQUIRE( record.has_value() );
    BOOST_CHECK_EQUAL( record->m_ActionName,
                       wxString( wxS( "common.Interactive.moved" ) ) );
}


BOOST_AUTO_TEST_CASE( MouseClickMapsWithPosition )
{
    TOOL_EVENT event( TC_MOUSE, TA_MOUSE_CLICK, BUT_LEFT | MD_SHIFT | MD_CTRL );
    event.SetMousePosition( VECTOR2D( 10.4, 20.6 ) );

    std::optional<AI_ACTIVITY_RECORD> record =
            MakeAiActivityRecordFromToolEvent( event, AI_EDITOR_KIND::Pcb );

    BOOST_REQUIRE( record.has_value() );
    BOOST_CHECK_EQUAL( record->m_ActionName, wxString( wxS( "mouse.click" ) ) );
    BOOST_CHECK( record->m_ArgumentsJson.Contains( wxS( "\"x\":10" ) ) );
    BOOST_CHECK( record->m_ArgumentsJson.Contains( wxS( "\"y\":21" ) ) );

    nlohmann::json args = nlohmann::json::parse( record->m_ArgumentsJson.ToStdString() );
    BOOST_CHECK_EQUAL( args["button"].get<std::string>(), "left" );
    BOOST_REQUIRE_EQUAL( args["buttons"].size(), 1 );
    BOOST_CHECK_EQUAL( args["buttons"][0].get<std::string>(), "left" );
    BOOST_REQUIRE_EQUAL( args["modifiers"].size(), 2 );
    BOOST_CHECK_EQUAL( args["modifiers"][0].get<std::string>(), "shift" );
    BOOST_CHECK_EQUAL( args["modifiers"][1].get<std::string>(), "ctrl" );
}


BOOST_AUTO_TEST_CASE( RightMouseClickCapturesPrimaryButton )
{
    TOOL_EVENT event( TC_MOUSE, TA_MOUSE_CLICK, BUT_RIGHT );
    event.SetMousePosition( VECTOR2D( 4.0, 8.0 ) );

    std::optional<AI_ACTIVITY_RECORD> record =
            MakeAiActivityRecordFromToolEvent( event, AI_EDITOR_KIND::Pcb );

    BOOST_REQUIRE( record.has_value() );

    nlohmann::json args = nlohmann::json::parse( record->m_ArgumentsJson.ToStdString() );
    BOOST_CHECK_EQUAL( args["button"].get<std::string>(), "right" );
    BOOST_REQUIRE_EQUAL( args["buttons"].size(), 1 );
    BOOST_CHECK_EQUAL( args["buttons"][0].get<std::string>(), "right" );
}


BOOST_AUTO_TEST_CASE( MouseMotionIsIgnored )
{
    TOOL_EVENT event( TC_MOUSE, TA_MOUSE_MOTION, BUT_NONE );

    std::optional<AI_ACTIVITY_RECORD> record =
            MakeAiActivityRecordFromToolEvent( event, AI_EDITOR_KIND::Pcb );

    BOOST_CHECK( !record.has_value() );
}


BOOST_AUTO_TEST_SUITE_END()
