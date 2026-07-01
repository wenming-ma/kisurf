#include <boost/test/unit_test.hpp>

#include <kisurf/ai/ai_next_action_candidate_library.h>
#include <kisurf/ai/ai_suggestion_operations.h>

namespace
{
wxString viaDetails( int aX, int aY, const wxString& aNetName,
                     int aDiameter = 600000 )
{
    wxString details;
    details << wxS( "{\"kind\":\"via\",\"position\":{\"x\":" ) << aX
            << wxS( ",\"y\":" ) << aY << wxS( "},\"diameter\":" )
            << aDiameter << wxS( ",\"net_name\":\"" ) << aNetName << wxS( "\"}" );
    return details;
}


AI_OBJECT_REF viaRef( int aX, int aY, const wxString& aNetName = wxS( "GND" ),
                      int aDiameter = 600000 )
{
    return AI_OBJECT_REF( KIID(), PCB_VIA_T,
                          wxString::Format( wxS( "via:%d,%d" ), aX, aY ),
                          viaDetails( aX, aY, aNetName, aDiameter ) );
}


AI_SUGGESTION_TRIGGER makeViaTrigger(
        std::initializer_list<AI_OBJECT_REF> aVisibleObjects,
        AI_TOOL_STATE_KIND aToolStateKind = AI_TOOL_STATE_KIND::PlacingVia )
{
    AI_SUGGESTION_TRIGGER trigger;
    trigger.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextVersion.m_DocumentRevision = 12;
    trigger.m_ContextVersion.m_ViewRevision = 5;
    trigger.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_Version = trigger.m_ContextVersion;
    trigger.m_ContextSnapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_ToolState.m_Kind = aToolStateKind;
    trigger.m_ContextSnapshot.m_ToolState.m_ContextVersion = trigger.m_ContextVersion;
    trigger.m_ContextSnapshot.m_VisibleObjects.assign( aVisibleObjects.begin(),
                                                       aVisibleObjects.end() );
    trigger.m_Activity.m_Sequence = 44;
    trigger.m_Activity.m_ActionName = wxS( "pcbnew.Interactive.placeVia" );
    return trigger;
}


wxString viaPlacementModeContext( const wxString& aNetName = wxS( "GND" ),
                                  int aDiameter = 700000,
                                  int aDrill = 330000,
                                  bool aIncludeCursor = true )
{
    wxString context;
    context << wxS( "{\"mode\":\"placing_via\",\"net\":\"" ) << aNetName
            << wxS( "\",\"diameter\":" ) << aDiameter
            << wxS( ",\"drill\":" ) << aDrill;

    if( aIncludeCursor )
        context << wxS( ",\"cursor\":{\"x\":710,\"y\":820}" );

    context << wxS( "}" );
    return context;
}


wxString routingModeContext( const wxString& aNetName = wxS( "GND" ),
                             const wxString& aLayerName = wxS( "F.Cu" ),
                             int aWidth = 150000, bool aIncludeCursor = true )
{
    wxString context;
    context << wxS( "{\"net\":\"" ) << aNetName << wxS( "\",\"layer\":\"" )
            << aLayerName << wxS( "\",\"width\":" ) << aWidth
            << wxS( ",\"start\":{\"x\":100,\"y\":200}" );

    if( aIncludeCursor )
        context << wxS( ",\"cursor\":{\"x\":260,\"y\":200}" );

    context << wxS( "}" );
    return context;
}


AI_SUGGESTION_TRIGGER makeRoutingTrigger(
        const wxString& aModeContextJson = routingModeContext(),
        AI_TOOL_STATE_KIND aToolStateKind = AI_TOOL_STATE_KIND::RoutingTrack )
{
    AI_SUGGESTION_TRIGGER trigger;
    trigger.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextVersion.m_DocumentRevision = 31;
    trigger.m_ContextVersion.m_ViewRevision = 9;
    trigger.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_Version = trigger.m_ContextVersion;
    trigger.m_ContextSnapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_ToolState.m_Kind = aToolStateKind;
    trigger.m_ContextSnapshot.m_ToolState.m_ContextVersion = trigger.m_ContextVersion;
    trigger.m_ContextSnapshot.m_ToolState.m_ModeContextJson = aModeContextJson;
    trigger.m_ContextSnapshot.m_ToolState.m_HasCursorBoardPosition = true;
    trigger.m_ContextSnapshot.m_ToolState.m_CursorBoardPosition = VECTOR2I( 240, 220 );
    trigger.m_Activity.m_Sequence = 77;
    trigger.m_Activity.m_ActionName = wxS( "pcbnew.Interactive.routeTrack" );
    return trigger;
}


wxString drawingZoneModeContext( const wxString& aLayerName = wxS( "F.Cu" ),
                                 const wxString& aNetName = wxS( "GND" ),
                                 bool aIncludeCursor = true )
{
    wxString context;
    context << wxS( "{\"mode\":\"drawing_zone\",\"layer\":\"" ) << aLayerName
            << wxS( "\",\"net\":\"" ) << aNetName << wxS( "\"" );

    if( aIncludeCursor )
        context << wxS( ",\"cursor\":{\"x\":1000,\"y\":2000}" );

    context << wxS( "}" );
    return context;
}


AI_SUGGESTION_TRIGGER makeDrawingZoneTrigger(
        const wxString& aModeContextJson = drawingZoneModeContext(),
        AI_TOOL_STATE_KIND aToolStateKind = AI_TOOL_STATE_KIND::DrawingZone )
{
    AI_SUGGESTION_TRIGGER trigger;
    trigger.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextVersion.m_DocumentRevision = 51;
    trigger.m_ContextVersion.m_ViewRevision = 10;
    trigger.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_Version = trigger.m_ContextVersion;
    trigger.m_ContextSnapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_ToolState.m_Kind = aToolStateKind;
    trigger.m_ContextSnapshot.m_ToolState.m_ContextVersion = trigger.m_ContextVersion;
    trigger.m_ContextSnapshot.m_ToolState.m_ModeContextJson = aModeContextJson;
    trigger.m_ContextSnapshot.m_ToolState.m_HasCursorBoardPosition = true;
    trigger.m_ContextSnapshot.m_ToolState.m_CursorBoardPosition = VECTOR2I( 1100, 2100 );
    trigger.m_Activity.m_Sequence = 91;
    trigger.m_Activity.m_ActionName = wxS( "pcbnew.InteractiveDrawing.zone" );
    return trigger;
}


AI_SUGGESTION_TRIGGER makeDrawingShapeTrigger()
{
    return makeDrawingZoneTrigger(
            drawingZoneModeContext( wxS( "Dwgs.User" ), wxEmptyString ) );
}


wxString panelTableStateJson( const wxString& aFocusedValue = wxS( "0.20 mm" ),
                              int aEmptyTargetCount = 2,
                              bool aIncludeFocusedCell = true )
{
    wxString state;
    state << wxS( "{\"tables\":[{\"id\":\"clearance.rules\","
                  "\"title\":\"Clearance rules\"," );

    if( aIncludeFocusedCell )
    {
        state << wxS( "\"focused_cell\":{\"row_id\":\"row.default\","
                      "\"column_id\":\"clearance\"}," );
    }

    state << wxS( "\"columns\":[{\"id\":\"clearance\","
                  "\"label\":\"Clearance\"}],\"rows\":[" )
          << wxS( "{\"id\":\"row.default\",\"label\":\"Default\","
                  "\"cells\":{\"clearance\":\"" )
          << aFocusedValue << wxS( "\"}}" );

    for( int ii = 0; ii < aEmptyTargetCount; ++ii )
    {
        state << wxS( ",{\"id\":\"row.empty" ) << ii
              << wxS( "\",\"label\":\"Empty " ) << ii
              << wxS( "\",\"cells\":{\"clearance\":\"\"}}" );
    }

    state << wxS( "]}]}" );
    return state;
}


AI_SUGGESTION_TRIGGER makePanelTableTrigger(
        const wxString& aStateJson = panelTableStateJson() )
{
    AI_SUGGESTION_TRIGGER trigger;
    trigger.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextVersion.m_DocumentRevision = 41;
    trigger.m_ContextVersion.m_ViewRevision = 6;
    trigger.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_Version = trigger.m_ContextVersion;
    trigger.m_ContextSnapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::Unknown;

    AI_PANEL_STATE_RECORD panel;
    panel.m_Id = wxS( "board_setup.clearance" );
    panel.m_Title = wxS( "Board Setup" );
    panel.m_FocusedControlId = wxS( "clearance.rules.row.default.clearance" );
    panel.m_FocusedControlLabel = wxS( "Clearance" );
    panel.m_StateJson = aStateJson;
    trigger.m_ContextSnapshot.m_PanelStates.push_back( panel );

    trigger.m_Activity.m_Sequence = 88;
    trigger.m_Activity.m_ActionName = wxS( "panel.cell.edited" );
    return trigger;
}
} // namespace

BOOST_AUTO_TEST_SUITE( AiNextActionCandidateLibrary )


BOOST_AUTO_TEST_CASE( ThreeHorizontalViasSuggestNextViaPreview )
{
    std::optional<AI_SUGGESTION_RECORD> suggestion = AiGenerateViaPatternCandidate(
            makeViaTrigger( { viaRef( 100, 50 ), viaRef( 200, 50 ),
                              viaRef( 300, 50 ) } ) );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK_EQUAL( static_cast<int>( suggestion->m_EditorKind ),
                       static_cast<int>( AI_EDITOR_KIND::Pcb ) );
    BOOST_CHECK_EQUAL( static_cast<int>( suggestion->m_Kind ),
                       static_cast<int>( AI_SUGGESTION_KIND::Preview ) );
    BOOST_CHECK( suggestion->m_Title.Contains( wxS( "next via" ) ) );
    BOOST_CHECK( !suggestion->m_Body.Contains( wxS( "low confidence" ) ) );
    BOOST_CHECK_EQUAL( suggestion->m_ContextVersion.m_DocumentRevision, 12 );
    BOOST_CHECK_EQUAL( suggestion->m_TriggerActivitySequence, 44 );
    BOOST_CHECK_EQUAL( suggestion->m_ContextKind, wxString( wxS( "layout" ) ) );
    BOOST_CHECK( suggestion->m_ContextDetailsJson.Contains( wxS( "via_pattern" ) ) );
    BOOST_CHECK( suggestion->m_ContextDetailsJson.Contains( wxS( "placing_via" ) ) );

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( suggestion->m_ArgumentsJson );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsPlaceViaPreview() );
    BOOST_CHECK_EQUAL( operation->m_NetName, wxString( wxS( "GND" ) ) );
    BOOST_CHECK_EQUAL( operation->m_Diameter, 600000 );
    BOOST_CHECK_EQUAL( operation->m_Drill, 300000 );
    BOOST_CHECK_EQUAL( operation->m_Position.x, 400 );
    BOOST_CHECK_EQUAL( operation->m_Position.y, 50 );
}


BOOST_AUTO_TEST_CASE( ThreeVerticalViasSuggestNextViaPreview )
{
    std::optional<AI_SUGGESTION_RECORD> suggestion = AiGenerateViaPatternCandidate(
            makeViaTrigger( { viaRef( 20, 100, wxS( "/CLK" ) ),
                              viaRef( 20, 200, wxS( "/CLK" ) ),
                              viaRef( 20, 300, wxS( "/CLK" ) ) } ) );

    BOOST_REQUIRE( suggestion.has_value() );

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( suggestion->m_ArgumentsJson );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsPlaceViaPreview() );
    BOOST_CHECK_EQUAL( operation->m_NetName, wxString( wxS( "/CLK" ) ) );
    BOOST_CHECK_EQUAL( operation->m_Position.x, 20 );
    BOOST_CHECK_EQUAL( operation->m_Position.y, 400 );
}


BOOST_AUTO_TEST_CASE( TwoAlignedViasSuggestLowConfidenceCandidate )
{
    std::optional<AI_SUGGESTION_RECORD> suggestion = AiGenerateViaPatternCandidate(
            makeViaTrigger( { viaRef( 100, 50 ), viaRef( 200, 50 ) } ) );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK( suggestion->m_Body.Contains( wxS( "low confidence" ) ) );

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( suggestion->m_ArgumentsJson );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK_EQUAL( operation->m_Position.x, 300 );
    BOOST_CHECK_EQUAL( operation->m_Position.y, 50 );
}


BOOST_AUTO_TEST_CASE( ActiveViaPlacementFallsBackToCurrentToolPosition )
{
    AI_SUGGESTION_TRIGGER trigger = makeViaTrigger( {} );
    trigger.m_ContextSnapshot.m_ToolState.m_ModeContextJson =
            viaPlacementModeContext( wxS( "GND" ), 700000, 330000 );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            AiGenerateViaPlacementCandidate( trigger );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK( suggestion->m_Title.Contains( wxS( "via placement" ) ) );
    BOOST_CHECK( suggestion->m_ContextDetailsJson.Contains( wxS( "via_placement" ) ) );

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( suggestion->m_ArgumentsJson );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsPlaceViaPreview() );
    BOOST_CHECK_EQUAL( operation->m_NetName, wxString( wxS( "GND" ) ) );
    BOOST_CHECK_EQUAL( operation->m_Diameter, 700000 );
    BOOST_CHECK_EQUAL( operation->m_Drill, 330000 );
    BOOST_CHECK_EQUAL( operation->m_Position.x, 710 );
    BOOST_CHECK_EQUAL( operation->m_Position.y, 820 );
    BOOST_REQUIRE_EQUAL( suggestion->m_PreviewObjects.size(), 1 );
}


BOOST_AUTO_TEST_CASE( ActiveViaPlacementAllowsNoNetPreview )
{
    AI_SUGGESTION_TRIGGER trigger = makeViaTrigger( {} );
    trigger.m_ContextSnapshot.m_ToolState.m_ModeContextJson =
            viaPlacementModeContext( wxEmptyString, 700000, 330000 );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            AiGenerateViaPlacementCandidate( trigger );

    BOOST_REQUIRE( suggestion.has_value() );

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( suggestion->m_ArgumentsJson );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsPlaceViaPreview() );
    BOOST_CHECK( operation->m_NetName.IsEmpty() );
    BOOST_CHECK_EQUAL( operation->m_Position.x, 710 );
    BOOST_CHECK_EQUAL( operation->m_Position.y, 820 );
}


BOOST_AUTO_TEST_CASE( NonAlignedViasDoNotSuggestCandidate )
{
    BOOST_CHECK( !AiGenerateViaPatternCandidate(
                           makeViaTrigger( { viaRef( 100, 50 ), viaRef( 220, 70 ),
                                             viaRef( 300, 50 ) } ) )
                          .has_value() );
}


BOOST_AUTO_TEST_CASE( WrongNetViasDoNotSuggestCandidate )
{
    BOOST_CHECK( !AiGenerateViaPatternCandidate(
                           makeViaTrigger( { viaRef( 100, 50, wxS( "GND" ) ),
                                             viaRef( 200, 50, wxS( "VCC" ) ) } ) )
                          .has_value() );
}


BOOST_AUTO_TEST_CASE( StaleOrInactiveContextDoesNotSuggestCandidate )
{
    BOOST_CHECK( !AiGenerateViaPatternCandidate(
                           makeViaTrigger( { viaRef( 100, 50 ), viaRef( 200, 50 ) },
                                           AI_TOOL_STATE_KIND::Selecting ) )
                          .has_value() );

    AI_SUGGESTION_TRIGGER trigger = makeViaTrigger( { viaRef( 100, 50 ), viaRef( 200, 50 ) } );
    trigger.m_EditorKind = AI_EDITOR_KIND::Schematic;
    trigger.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Schematic;
    trigger.m_ContextSnapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Schematic;

    BOOST_CHECK( !AiGenerateViaPatternCandidate( trigger ).has_value() );
}


BOOST_AUTO_TEST_CASE( RoutingToolStateSuggestsSegmentPreview )
{
    std::optional<AI_SUGGESTION_RECORD> suggestion =
            AiGenerateRoutingSegmentCandidate( makeRoutingTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK_EQUAL( static_cast<int>( suggestion->m_EditorKind ),
                       static_cast<int>( AI_EDITOR_KIND::Pcb ) );
    BOOST_CHECK_EQUAL( static_cast<int>( suggestion->m_Kind ),
                       static_cast<int>( AI_SUGGESTION_KIND::Preview ) );
    BOOST_CHECK( suggestion->m_Title.Contains( wxS( "route segment" ) ) );
    BOOST_CHECK_EQUAL( suggestion->m_ContextVersion.m_DocumentRevision, 31 );
    BOOST_CHECK_EQUAL( suggestion->m_TriggerActivitySequence, 77 );
    BOOST_CHECK_EQUAL( suggestion->m_ContextKind, wxString( wxS( "routing" ) ) );
    BOOST_CHECK( suggestion->m_ContextDetailsJson.Contains( wxS( "route_segment" ) ) );
    BOOST_CHECK( suggestion->m_ContextDetailsJson.Contains( wxS( "routing_track" ) ) );

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( suggestion->m_ArgumentsJson );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsRouteSegmentPreview() );
    BOOST_CHECK_EQUAL( operation->m_NetName, wxString( wxS( "GND" ) ) );
    BOOST_CHECK_EQUAL( operation->m_LayerName, wxString( wxS( "F.Cu" ) ) );
    BOOST_CHECK_EQUAL( operation->m_Width, 150000 );
    BOOST_CHECK_EQUAL( operation->m_Start.x, 100 );
    BOOST_CHECK_EQUAL( operation->m_Start.y, 200 );
    BOOST_CHECK_EQUAL( operation->m_End.x, 260 );
    BOOST_CHECK_EQUAL( operation->m_End.y, 200 );
}


BOOST_AUTO_TEST_CASE( RoutingProviderUsesToolStateCursorWhenModeContextOmitsCursor )
{
    std::optional<AI_SUGGESTION_RECORD> suggestion =
            AiGenerateRoutingSegmentCandidate( makeRoutingTrigger(
                    routingModeContext( wxS( "GND" ), wxS( "B.Cu" ), 120000,
                                        false ) ) );

    BOOST_REQUIRE( suggestion.has_value() );

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( suggestion->m_ArgumentsJson );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsRouteSegmentPreview() );
    BOOST_CHECK_EQUAL( operation->m_LayerName, wxString( wxS( "B.Cu" ) ) );
    BOOST_CHECK_EQUAL( operation->m_End.x, 240 );
    BOOST_CHECK_EQUAL( operation->m_End.y, 220 );
}


BOOST_AUTO_TEST_CASE( RoutingProviderPlansNextLandingFromVisibleSameNetTargetWithoutCursor )
{
    AI_SUGGESTION_TRIGGER trigger = makeRoutingTrigger(
            routingModeContext( wxS( "GND" ), wxS( "F.Cu" ), 150000, false ) );
    trigger.m_ContextSnapshot.m_ToolState.m_HasCursorBoardPosition = false;
    trigger.m_ContextSnapshot.m_VisibleObjects.push_back(
            viaRef( 500, 200, wxS( "/GND" ) ) );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            AiGenerateRoutingSegmentCandidate( trigger );

    BOOST_REQUIRE( suggestion.has_value() );

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( suggestion->m_ArgumentsJson );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsRouteSegmentPreview() );
    BOOST_CHECK_EQUAL( operation->m_NetName, wxString( wxS( "GND" ) ) );
    BOOST_CHECK_EQUAL( operation->m_Start.x, 100 );
    BOOST_CHECK_EQUAL( operation->m_Start.y, 200 );
    BOOST_CHECK_EQUAL( operation->m_End.x, 500 );
    BOOST_CHECK_EQUAL( operation->m_End.y, 200 );
}


BOOST_AUTO_TEST_CASE( RoutingToolStateCanSuggestSegmentBeforeNetIsResolved )
{
    std::optional<AI_SUGGESTION_RECORD> suggestion =
            AiGenerateRoutingSegmentCandidate( makeRoutingTrigger(
                    wxS( "{\"layer\":\"F.Cu\",\"width\":150000,"
                         "\"start\":{\"x\":100,\"y\":200},"
                         "\"cursor\":{\"x\":260,\"y\":200}}" ) ) );

    BOOST_REQUIRE( suggestion.has_value() );

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( suggestion->m_ArgumentsJson );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsRouteSegmentPreview() );
    BOOST_CHECK( operation->m_NetName.IsEmpty() );
    BOOST_CHECK_EQUAL( operation->m_LayerName, wxString( wxS( "F.Cu" ) ) );
    BOOST_CHECK_EQUAL( operation->m_Start.x, 100 );
    BOOST_CHECK_EQUAL( operation->m_Start.y, 200 );
}


BOOST_AUTO_TEST_CASE( RoutingProviderRejectsInactiveOrIncompleteContext )
{
    BOOST_CHECK( !AiGenerateRoutingSegmentCandidate(
                           makeRoutingTrigger( routingModeContext(),
                                               AI_TOOL_STATE_KIND::Selecting ) )
                          .has_value() );

    BOOST_CHECK( !AiGenerateRoutingSegmentCandidate(
                           makeRoutingTrigger( wxS( "{\"width\":150000,"
                                                    "\"start\":{\"x\":100,\"y\":200},"
                                                    "\"cursor\":{\"x\":260,\"y\":200}}" ) ) )
                          .has_value() );

    AI_SUGGESTION_TRIGGER stale = makeRoutingTrigger();
    stale.m_ContextSnapshot.m_ToolState.m_ContextVersion.m_DocumentRevision = 30;

    BOOST_CHECK( !AiGenerateRoutingSegmentCandidate( stale ).has_value() );
}


BOOST_AUTO_TEST_CASE( DrawingZoneToolStateSuggestsCopperZonePreview )
{
    std::optional<AI_SUGGESTION_RECORD> suggestion =
            AiGenerateDrawingZoneCandidate( makeDrawingZoneTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK_EQUAL( static_cast<int>( suggestion->m_EditorKind ),
                       static_cast<int>( AI_EDITOR_KIND::Pcb ) );
    BOOST_CHECK_EQUAL( static_cast<int>( suggestion->m_Kind ),
                       static_cast<int>( AI_SUGGESTION_KIND::Preview ) );
    BOOST_CHECK( suggestion->m_Title.Contains( wxS( "zone" ) ) );
    BOOST_CHECK_EQUAL( suggestion->m_ContextVersion.m_DocumentRevision, 51 );
    BOOST_CHECK_EQUAL( suggestion->m_TriggerActivitySequence, 91 );
    BOOST_CHECK_EQUAL( suggestion->m_ContextKind, wxString( wxS( "layout" ) ) );
    BOOST_CHECK( suggestion->m_ContextDetailsJson.Contains( wxS( "drawing_zone" ) ) );

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( suggestion->m_ArgumentsJson );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsCreateCopperZonePreview() );
    BOOST_CHECK_EQUAL( operation->m_NetName, wxString( wxS( "GND" ) ) );
    BOOST_CHECK_EQUAL( operation->m_LayerName, wxString( wxS( "F.Cu" ) ) );
    BOOST_REQUIRE_EQUAL( operation->m_Points.size(), 4 );
}


BOOST_AUTO_TEST_CASE( DrawingZoneToolStateWithoutNetSuggestsShapePreview )
{
    std::optional<AI_SUGGESTION_RECORD> suggestion =
            AiGenerateDrawingZoneCandidate( makeDrawingShapeTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK( suggestion->m_Title.Contains( wxS( "shape" ) ) );
    BOOST_CHECK( suggestion->m_ContextDetailsJson.Contains( wxS( "drawing_zone" ) ) );

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( suggestion->m_ArgumentsJson );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsCreateShapePreview() );
    BOOST_CHECK_EQUAL( operation->m_LayerName, wxString( wxS( "Dwgs.User" ) ) );
    BOOST_CHECK_EQUAL( operation->m_Width, 100000 );
    BOOST_CHECK_EQUAL( operation->m_Start.x, -499000 );
    BOOST_CHECK_EQUAL( operation->m_Start.y, -498000 );
    BOOST_CHECK_EQUAL( operation->m_End.x, 501000 );
    BOOST_CHECK_EQUAL( operation->m_End.y, 502000 );
}


BOOST_AUTO_TEST_CASE( PanelTableStateSuggestsColumnFillPreview )
{
    std::optional<AI_SUGGESTION_RECORD> suggestion =
            AiGeneratePanelTableFillCandidate( makePanelTableTrigger() );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK_EQUAL( static_cast<int>( suggestion->m_EditorKind ),
                       static_cast<int>( AI_EDITOR_KIND::Pcb ) );
    BOOST_CHECK_EQUAL( static_cast<int>( suggestion->m_Kind ),
                       static_cast<int>( AI_SUGGESTION_KIND::Preview ) );
    BOOST_CHECK( suggestion->m_Title.Contains( wxS( "Fill" ) ) );
    BOOST_CHECK( suggestion->m_Body.Contains( wxS( "2 empty cells" ) ) );
    BOOST_CHECK_EQUAL( suggestion->m_ContextKind, wxString( wxS( "panel" ) ) );
    BOOST_CHECK( suggestion->m_ContextDetailsJson.Contains(
            wxS( "panel_table_fill" ) ) );
    BOOST_CHECK( suggestion->m_ArgumentsJson.Contains(
            wxS( "panel_fill_column_preview" ) ) );
    BOOST_CHECK( suggestion->m_ArgumentsJson.Contains( wxS( "target_row_ids" ) ) );
    BOOST_CHECK( suggestion->m_ArgumentsJson.Contains( wxS( "row.empty0" ) ) );
    BOOST_CHECK( suggestion->m_EditObjects.empty() );
    BOOST_CHECK( suggestion->m_PreviewObjects.empty() );
    BOOST_CHECK_EQUAL( suggestion->m_ContextVersion.m_DocumentRevision, 41 );
    BOOST_CHECK_EQUAL( suggestion->m_TriggerActivitySequence, 88 );
}


BOOST_AUTO_TEST_CASE( PanelTableStateSuggestsColumnFillWhenPcbToolIsIdle )
{
    AI_SUGGESTION_TRIGGER trigger = makePanelTableTrigger();
    trigger.m_ContextSnapshot.m_ToolState.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trigger.m_ContextSnapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::Idle;

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            AiGeneratePanelTableFillCandidate( trigger );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK_EQUAL( suggestion->m_ContextKind, wxString( wxS( "panel" ) ) );
    BOOST_CHECK( suggestion->m_ArgumentsJson.Contains(
            wxS( "panel_fill_column_preview" ) ) );
}


BOOST_AUTO_TEST_CASE( PanelTableProviderRejectsMalformedOrLowConfidenceState )
{
    AI_SUGGESTION_TRIGGER nonPanel = makePanelTableTrigger();
    nonPanel.m_ContextSnapshot.m_ToolState.m_Kind = AI_TOOL_STATE_KIND::Selecting;
    BOOST_CHECK( !AiGeneratePanelTableFillCandidate( nonPanel ).has_value() );

    BOOST_CHECK( !AiGeneratePanelTableFillCandidate(
                           makePanelTableTrigger( wxS( "{" ) ) )
                          .has_value() );
    BOOST_CHECK( !AiGeneratePanelTableFillCandidate( makePanelTableTrigger(
                          panelTableStateJson( wxS( "0.20 mm" ), 2, false ) ) )
                          .has_value() );
    BOOST_CHECK( !AiGeneratePanelTableFillCandidate( makePanelTableTrigger(
                          panelTableStateJson( wxEmptyString, 2 ) ) )
                          .has_value() );
    BOOST_CHECK( !AiGeneratePanelTableFillCandidate( makePanelTableTrigger(
                          panelTableStateJson( wxS( "0.20 mm" ), 1 ) ) )
                          .has_value() );
}


BOOST_AUTO_TEST_SUITE_END()
