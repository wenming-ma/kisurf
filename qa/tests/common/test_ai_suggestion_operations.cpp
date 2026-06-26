#include <boost/test/unit_test.hpp>

#include <kisurf/ai/ai_suggestion_operations.h>

BOOST_AUTO_TEST_SUITE( AiSuggestionOperations )


BOOST_AUTO_TEST_CASE( ParsesMoveOperation )
{
    const wxString payload = wxS( "{\"operation\":\"move\",\"dx\":100,\"dy\":-25}" );

    std::optional<AI_SUGGESTION_OPERATION> operation = ParseAiSuggestionOperation( payload );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsMove() );
    BOOST_CHECK_EQUAL( operation->m_MoveDelta.x, 100 );
    BOOST_CHECK_EQUAL( operation->m_MoveDelta.y, -25 );

    std::optional<VECTOR2I> delta = ParseAiSuggestionMoveDelta( payload );

    BOOST_REQUIRE( delta.has_value() );
    BOOST_CHECK_EQUAL( delta->x, 100 );
    BOOST_CHECK_EQUAL( delta->y, -25 );
}


BOOST_AUTO_TEST_CASE( ParsesMoveSelectedOperation )
{
    const wxString payload = wxS( "{\"operation\":\"move_selected\",\"dx\":-30,\"dy\":45}" );

    std::optional<AI_SUGGESTION_OPERATION> operation = ParseAiSuggestionOperation( payload );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsMoveSelected() );
    BOOST_CHECK_EQUAL( operation->m_MoveDelta.x, -30 );
    BOOST_CHECK_EQUAL( operation->m_MoveDelta.y, 45 );

    std::optional<VECTOR2I> delta = ParseAiSuggestionMoveDelta( payload );

    BOOST_REQUIRE( delta.has_value() );
    BOOST_CHECK_EQUAL( delta->x, -30 );
    BOOST_CHECK_EQUAL( delta->y, 45 );
}


BOOST_AUTO_TEST_CASE( ParsesRouteSegmentPreviewOperation )
{
    const wxString payload = wxS(
            "{\"operation\":\"route_segment_preview\",\"net\":\"GND\",\"layer\":\"F.Cu\","
            "\"width\":150000,\"start\":{\"x\":10,\"y\":20},\"end\":{\"x\":110,\"y\":20}}" );

    std::optional<AI_SUGGESTION_OPERATION> operation = ParseAiSuggestionOperation( payload );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsRouteSegmentPreview() );
    BOOST_CHECK_EQUAL( operation->m_NetName, wxString( wxS( "GND" ) ) );
    BOOST_CHECK_EQUAL( operation->m_LayerName, wxString( wxS( "F.Cu" ) ) );
    BOOST_CHECK_EQUAL( operation->m_Width, 150000 );
    BOOST_CHECK_EQUAL( operation->m_Start.x, 10 );
    BOOST_CHECK_EQUAL( operation->m_Start.y, 20 );
    BOOST_CHECK_EQUAL( operation->m_End.x, 110 );
    BOOST_CHECK_EQUAL( operation->m_End.y, 20 );
}


BOOST_AUTO_TEST_CASE( ParsesPlaceViaPreviewOperation )
{
    const wxString payload = wxS(
            "{\"operation\":\"place_via_preview\",\"net\":\"GND\",\"diameter\":600000,"
            "\"drill\":300000,\"position\":{\"x\":50,\"y\":70}}" );

    std::optional<AI_SUGGESTION_OPERATION> operation = ParseAiSuggestionOperation( payload );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsPlaceViaPreview() );
    BOOST_CHECK_EQUAL( operation->m_NetName, wxString( wxS( "GND" ) ) );
    BOOST_CHECK_EQUAL( operation->m_Diameter, 600000 );
    BOOST_CHECK_EQUAL( operation->m_Drill, 300000 );
    BOOST_CHECK_EQUAL( operation->m_Position.x, 50 );
    BOOST_CHECK_EQUAL( operation->m_Position.y, 70 );
}


BOOST_AUTO_TEST_CASE( ParsesCreateShapePreviewOperation )
{
    const wxString payload = wxS(
            "{\"operation\":\"create_shape_preview\",\"shape\":\"rectangle\","
            "\"layer\":\"F.SilkS\",\"width\":120000,"
            "\"start\":{\"x\":10,\"y\":20},\"end\":{\"x\":110,\"y\":220}}" );

    std::optional<AI_SUGGESTION_OPERATION> operation = ParseAiSuggestionOperation( payload );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsCreateShapePreview() );
    BOOST_CHECK_EQUAL( operation->m_Shape, wxString( wxS( "rectangle" ) ) );
    BOOST_CHECK_EQUAL( operation->m_LayerName, wxString( wxS( "F.SilkS" ) ) );
    BOOST_CHECK_EQUAL( operation->m_Width, 120000 );
    BOOST_CHECK_EQUAL( operation->m_Start.x, 10 );
    BOOST_CHECK_EQUAL( operation->m_Start.y, 20 );
    BOOST_CHECK_EQUAL( operation->m_End.x, 110 );
    BOOST_CHECK_EQUAL( operation->m_End.y, 220 );
}


BOOST_AUTO_TEST_CASE( ParsesCreateCircleShapePreviewOperation )
{
    const wxString payload = wxS(
            "{\"operation\":\"create_shape_preview\",\"shape\":\"circle\","
            "\"layer\":\"F.SilkS\",\"width\":50000,"
            "\"center\":{\"x\":400,\"y\":500},\"radius\":125000}" );

    std::optional<AI_SUGGESTION_OPERATION> operation = ParseAiSuggestionOperation( payload );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsCreateShapePreview() );
    BOOST_CHECK_EQUAL( operation->m_Shape, wxString( wxS( "circle" ) ) );
    BOOST_CHECK_EQUAL( operation->m_LayerName, wxString( wxS( "F.SilkS" ) ) );
    BOOST_CHECK_EQUAL( operation->m_Width, 50000 );
    BOOST_CHECK_EQUAL( operation->m_Position.x, 400 );
    BOOST_CHECK_EQUAL( operation->m_Position.y, 500 );
    BOOST_CHECK_EQUAL( operation->m_Diameter, 125000 );
}


BOOST_AUTO_TEST_CASE( ParsesCreateArcShapePreviewOperation )
{
    const wxString payload = wxS(
            "{\"operation\":\"create_shape_preview\",\"shape\":\"arc\","
            "\"layer\":\"F.SilkS\",\"width\":50000,"
            "\"start\":{\"x\":0,\"y\":0},"
            "\"mid\":{\"x\":50,\"y\":100},"
            "\"end\":{\"x\":100,\"y\":0}}" );

    std::optional<AI_SUGGESTION_OPERATION> operation = ParseAiSuggestionOperation( payload );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsCreateShapePreview() );
    BOOST_CHECK_EQUAL( operation->m_Shape, wxString( wxS( "arc" ) ) );
    BOOST_CHECK_EQUAL( operation->m_LayerName, wxString( wxS( "F.SilkS" ) ) );
    BOOST_CHECK_EQUAL( operation->m_Width, 50000 );
    BOOST_CHECK_EQUAL( operation->m_Start.x, 0 );
    BOOST_CHECK_EQUAL( operation->m_Start.y, 0 );
    BOOST_CHECK_EQUAL( operation->m_Position.x, 50 );
    BOOST_CHECK_EQUAL( operation->m_Position.y, 100 );
    BOOST_CHECK_EQUAL( operation->m_End.x, 100 );
    BOOST_CHECK_EQUAL( operation->m_End.y, 0 );
}


BOOST_AUTO_TEST_CASE( ParsesCreatePolygonShapePreviewOperation )
{
    const wxString payload = wxS(
            "{\"operation\":\"create_shape_preview\",\"shape\":\"polygon\","
            "\"layer\":\"F.SilkS\",\"width\":50000,"
            "\"points\":[{\"x\":0,\"y\":0},{\"x\":100,\"y\":0},"
            "{\"x\":100,\"y\":100},{\"x\":0,\"y\":100}]}" );

    std::optional<AI_SUGGESTION_OPERATION> operation = ParseAiSuggestionOperation( payload );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsCreateShapePreview() );
    BOOST_CHECK_EQUAL( operation->m_Shape, wxString( wxS( "polygon" ) ) );
    BOOST_CHECK_EQUAL( operation->m_LayerName, wxString( wxS( "F.SilkS" ) ) );
    BOOST_CHECK_EQUAL( operation->m_Width, 50000 );
    BOOST_REQUIRE_EQUAL( operation->m_Points.size(), 4 );
    BOOST_CHECK_EQUAL( operation->m_Points[2].x, 100 );
    BOOST_CHECK_EQUAL( operation->m_Points[2].y, 100 );
}


BOOST_AUTO_TEST_CASE( ParsesCreateCopperZonePreviewOperation )
{
    const wxString payload = wxS(
            "{\"operation\":\"create_copper_zone_preview\",\"net\":\"GND\",\"layer\":\"B.Cu\","
            "\"points\":[{\"x\":0,\"y\":0},{\"x\":100,\"y\":0},{\"x\":100,\"y\":100}]}" );

    std::optional<AI_SUGGESTION_OPERATION> operation = ParseAiSuggestionOperation( payload );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsCreateCopperZonePreview() );
    BOOST_CHECK_EQUAL( operation->m_NetName, wxString( wxS( "GND" ) ) );
    BOOST_CHECK_EQUAL( operation->m_LayerName, wxString( wxS( "B.Cu" ) ) );
    BOOST_REQUIRE_EQUAL( operation->m_Points.size(), 3 );
    BOOST_CHECK_EQUAL( operation->m_Points[2].x, 100 );
    BOOST_CHECK_EQUAL( operation->m_Points[2].y, 100 );
}


BOOST_AUTO_TEST_CASE( ParsesCreateCopperZonePreviewOperationWithHoles )
{
    const wxString payload = wxS(
            "{\"operation\":\"create_copper_zone_preview\",\"net\":\"GND\",\"layer\":\"F.Cu\","
            "\"points\":[{\"x\":0,\"y\":0},{\"x\":1000,\"y\":0},"
            "{\"x\":1000,\"y\":1000},{\"x\":0,\"y\":1000}],"
            "\"holes\":[[{\"x\":250,\"y\":250},{\"x\":750,\"y\":250},"
            "{\"x\":750,\"y\":750},{\"x\":250,\"y\":750}]]}" );

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( payload );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsCreateCopperZonePreview() );
    BOOST_REQUIRE_EQUAL( operation->m_Holes.size(), 1 );
    BOOST_REQUIRE_EQUAL( operation->m_Holes.front().size(), 4 );
    BOOST_CHECK_EQUAL( operation->m_Holes.front()[2].x, 750 );
    BOOST_CHECK_EQUAL( operation->m_Holes.front()[2].y, 750 );
}


BOOST_AUTO_TEST_CASE( ParsesPanelFillColumnPreviewOperation )
{
    const wxString payload = wxS(
            "{\"operation\":\"panel_fill_column_preview\","
            "\"panel_id\":\"board_setup.clearance\","
            "\"table_id\":\"clearance.rules\","
            "\"column_id\":\"clearance\","
            "\"value\":\"0.20 mm\","
            "\"target_row_ids\":[\"row.power\",\"row.signal\"]}" );

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( payload );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsPanelFillColumnPreview() );
    BOOST_CHECK_EQUAL( operation->m_PanelId,
                       wxString( wxS( "board_setup.clearance" ) ) );
    BOOST_CHECK_EQUAL( operation->m_TableId,
                       wxString( wxS( "clearance.rules" ) ) );
    BOOST_CHECK_EQUAL( operation->m_ColumnId, wxString( wxS( "clearance" ) ) );
    BOOST_CHECK_EQUAL( operation->m_Value, wxString( wxS( "0.20 mm" ) ) );
    BOOST_REQUIRE_EQUAL( operation->m_TargetRowIds.size(), 2 );
    BOOST_CHECK_EQUAL( operation->m_TargetRowIds[1],
                       wxString( wxS( "row.signal" ) ) );
}


BOOST_AUTO_TEST_CASE( ParsesAnchorFocusPreviewOperation )
{
    const wxString payload = wxS(
            "{\"operation\":\"anchor_focus_preview\","
            "\"anchor_id\":\"tool.routing.orthogonal.horizontal\","
            "\"position\":{\"x\":500,\"y\":200},"
            "\"focus_layer\":\"F.Cu\","
            "\"focus_net\":\"/GPIO\","
            "\"dim_unfocused_layers\":true}" );

    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( payload );

    BOOST_REQUIRE( operation.has_value() );
    BOOST_CHECK( operation->IsAnchorFocusPreview() );
    BOOST_CHECK_EQUAL( operation->m_AnchorId,
                       wxString( wxS( "tool.routing.orthogonal.horizontal" ) ) );
    BOOST_CHECK_EQUAL( operation->m_Position.x, 500 );
    BOOST_CHECK_EQUAL( operation->m_Position.y, 200 );
    BOOST_CHECK_EQUAL( operation->m_FocusLayer, wxString( wxS( "F.Cu" ) ) );
    BOOST_CHECK_EQUAL( operation->m_FocusNet, wxString( wxS( "/GPIO" ) ) );
    BOOST_CHECK( operation->m_DimUnfocusedLayers );
}


BOOST_AUTO_TEST_CASE( RejectsMalformedOrUnsupportedPayloads )
{
    BOOST_CHECK( !ParseAiSuggestionOperation( wxS( "not-json" ) ).has_value() );
    BOOST_CHECK( !ParseAiSuggestionOperation( wxS( "[]" ) ).has_value() );
    BOOST_CHECK( !ParseAiSuggestionOperation( wxS( "{\"operation\":\"route\",\"dx\":1,\"dy\":2}" ) )
                          .has_value() );
    BOOST_CHECK( !ParseAiSuggestionOperation( wxS( "{\"dx\":1,\"dy\":2}" ) ).has_value() );
    BOOST_CHECK( !ParseAiSuggestionOperation( wxS( "{\"operation\":\"move\",\"dx\":1}" ) )
                          .has_value() );
    BOOST_CHECK( !ParseAiSuggestionOperation(
                           wxS( "{\"operation\":\"route_segment_preview\",\"net\":\"GND\","
                                "\"layer\":\"F.Cu\",\"width\":1,\"start\":{\"x\":0},"
                                "\"end\":{\"x\":10,\"y\":0}}" ) )
                          .has_value() );
    BOOST_CHECK( !ParseAiSuggestionOperation(
                           wxS( "{\"operation\":\"place_via_preview\",\"net\":\"GND\","
                                "\"diameter\":0,\"drill\":1,\"position\":{\"x\":0,\"y\":0}}" ) )
                          .has_value() );
    BOOST_CHECK( !ParseAiSuggestionOperation(
                           wxS( "{\"operation\":\"create_copper_zone_preview\",\"net\":\"GND\","
                                "\"layer\":\"F.Cu\",\"points\":[{\"x\":0,\"y\":0},"
                                "{\"x\":1,\"y\":0}]}" ) )
                          .has_value() );
    BOOST_CHECK( !ParseAiSuggestionOperation(
                           wxS( "{\"operation\":\"create_shape_preview\",\"shape\":\"polygon\","
                                "\"layer\":\"F.SilkS\",\"width\":120000,"
                                "\"start\":{\"x\":0,\"y\":0},\"end\":{\"x\":1,\"y\":1}}" ) )
                          .has_value() );
    BOOST_CHECK( !ParseAiSuggestionOperation(
                           wxS( "{\"operation\":\"create_shape_preview\",\"shape\":\"rectangle\","
                                "\"layer\":\"F.SilkS\",\"width\":0,"
                                "\"start\":{\"x\":0,\"y\":0},\"end\":{\"x\":1,\"y\":1}}" ) )
                          .has_value() );
    BOOST_CHECK( !ParseAiSuggestionOperation(
                           wxS( "{\"operation\":\"panel_fill_column_preview\","
                                "\"panel_id\":\"board_setup.clearance\","
                                "\"table_id\":\"clearance.rules\","
                                "\"column_id\":\"clearance\","
                                "\"value\":\"0.20 mm\","
                                "\"target_row_ids\":[]}" ) )
                          .has_value() );
    BOOST_CHECK( !ParseAiSuggestionOperation(
                           wxS( "{\"operation\":\"panel_fill_column_preview\","
                                "\"panel_id\":\"\","
                                "\"table_id\":\"clearance.rules\","
                                "\"column_id\":\"clearance\","
                                "\"value\":\"0.20 mm\","
                                "\"target_row_ids\":[\"row.power\"]}" ) )
                          .has_value() );
    BOOST_CHECK( !ParseAiSuggestionOperation(
                           wxS( "{\"operation\":\"panel_fill_column_preview\","
                                "\"panel_id\":\"board_setup.clearance\","
                                "\"table_id\":\"clearance.rules\","
                                "\"column_id\":\"clearance\","
                                "\"value\":\"0.20 mm\","
                                "\"target_row_ids\":[\"\"]}" ) )
                          .has_value() );
    BOOST_CHECK( !ParseAiSuggestionOperation(
                           wxS( "{\"operation\":\"anchor_focus_preview\","
                                "\"anchor_id\":\"\","
                                "\"position\":{\"x\":500,\"y\":200}}" ) )
                          .has_value() );
    BOOST_CHECK( !ParseAiSuggestionOperation(
                           wxS( "{\"operation\":\"anchor_focus_preview\","
                                "\"anchor_id\":\"tool.routing.current_end\","
                                "\"position\":{\"x\":500}}" ) )
                          .has_value() );
}


BOOST_AUTO_TEST_CASE( RejectsNonIntegerAndOutOfRangeDeltas )
{
    BOOST_CHECK( !ParseAiSuggestionMoveDelta(
                           wxS( "{\"operation\":\"move\",\"dx\":1.5,\"dy\":2}" ) )
                          .has_value() );
    BOOST_CHECK( !ParseAiSuggestionMoveDelta(
                           wxS( "{\"operation\":\"move\",\"dx\":\"1\",\"dy\":2}" ) )
                          .has_value() );
    BOOST_CHECK( !ParseAiSuggestionMoveDelta(
                           wxS( "{\"operation\":\"move\",\"dx\":2147483648,\"dy\":2}" ) )
                          .has_value() );
    BOOST_CHECK( !ParseAiSuggestionMoveDelta(
                           wxS( "{\"operation\":\"move\",\"dx\":1,\"dy\":-2147483649}" ) )
                          .has_value() );
}


BOOST_AUTO_TEST_SUITE_END()
