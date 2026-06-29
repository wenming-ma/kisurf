/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 */

#include <kisurf/ai/ai_suggestion_operations.h>

#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace
{
std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


bool jsonIntegerToInt( const nlohmann::json& aValue, int& aOut )
{
    if( aValue.is_number_unsigned() )
    {
        const uint64_t value = aValue.get<uint64_t>();

        if( value > static_cast<uint64_t>( std::numeric_limits<int>::max() ) )
            return false;

        aOut = static_cast<int>( value );
        return true;
    }

    if( !aValue.is_number_integer() )
        return false;

    const int64_t value = aValue.get<int64_t>();

    if( value < static_cast<int64_t>( std::numeric_limits<int>::min() )
        || value > static_cast<int64_t>( std::numeric_limits<int>::max() ) )
    {
        return false;
    }

    aOut = static_cast<int>( value );
    return true;
}


bool jsonPositiveIntegerToInt( const nlohmann::json& aValue, int& aOut )
{
    if( !jsonIntegerToInt( aValue, aOut ) )
        return false;

    return aOut > 0;
}


wxString jsonStringToWxString( const nlohmann::json& aValue )
{
    if( !aValue.is_string() )
        return wxString();

    return wxString::FromUTF8( aValue.get<std::string>().c_str() );
}


bool jsonNonEmptyStringToWxString( const nlohmann::json& aValue, wxString& aOut )
{
    if( !aValue.is_string() )
        return false;

    aOut = wxString::FromUTF8( aValue.get<std::string>().c_str() );
    aOut.Trim( true ).Trim( false );
    return !aOut.IsEmpty();
}


bool jsonPointToVector2I( const nlohmann::json& aValue, VECTOR2I& aOut )
{
    if( !aValue.is_object() || !aValue.contains( "x" ) || !aValue.contains( "y" ) )
        return false;

    int x = 0;
    int y = 0;

    if( !jsonIntegerToInt( aValue["x"], x ) || !jsonIntegerToInt( aValue["y"], y ) )
        return false;

    aOut = VECTOR2I( x, y );
    return true;
}


std::optional<std::vector<VECTOR2I>> jsonPointsToVector( const nlohmann::json& aValue )
{
    if( !aValue.is_array() || aValue.size() < 3 )
        return std::nullopt;

    std::vector<VECTOR2I> points;
    points.reserve( aValue.size() );

    for( const nlohmann::json& pointJson : aValue )
    {
        VECTOR2I point;

        if( !jsonPointToVector2I( pointJson, point ) )
            return std::nullopt;

        points.push_back( point );
    }

    return points;
}


bool parseZonePreviewHoles( const nlohmann::json& aArgs,
                            std::vector<std::vector<VECTOR2I>>& aHoles )
{
    aHoles.clear();

    if( aArgs.contains( "inner" ) )
    {
        std::optional<std::vector<VECTOR2I>> inner = jsonPointsToVector( aArgs["inner"] );

        if( !inner )
            return false;

        aHoles.push_back( std::move( *inner ) );
    }

    if( !aArgs.contains( "holes" ) )
        return true;

    if( !aArgs["holes"].is_array() )
        return false;

    for( const nlohmann::json& holeJson : aArgs["holes"] )
    {
        std::optional<std::vector<VECTOR2I>> hole = jsonPointsToVector( holeJson );

        if( !hole )
            return false;

        aHoles.push_back( std::move( *hole ) );
    }

    return true;
}


std::optional<AI_SUGGESTION_OPERATION> parseMoveOperation( const nlohmann::json& aArgs,
                                                           bool aSelected )
{
    if( !aArgs.contains( "dx" ) || !aArgs.contains( "dy" ) )
        return std::nullopt;

    int dx = 0;
    int dy = 0;

    if( !jsonIntegerToInt( aArgs["dx"], dx ) || !jsonIntegerToInt( aArgs["dy"], dy ) )
        return std::nullopt;

    AI_SUGGESTION_OPERATION operation;
    operation.m_Kind = aSelected ? AI_SUGGESTION_OPERATION_KIND::MoveSelected
                                 : AI_SUGGESTION_OPERATION_KIND::Move;
    operation.m_MoveDelta = VECTOR2I( dx, dy );
    return operation;
}


std::optional<AI_SUGGESTION_OPERATION> parseRouteSegmentPreview( const nlohmann::json& aArgs )
{
    if( !aArgs.contains( "net" ) || !aArgs.contains( "layer" ) || !aArgs.contains( "width" )
        || !aArgs.contains( "start" ) || !aArgs.contains( "end" ) )
    {
        return std::nullopt;
    }

    wxString netName = jsonStringToWxString( aArgs["net"] );
    wxString layerName = jsonStringToWxString( aArgs["layer"] );
    int      width = 0;
    VECTOR2I start;
    VECTOR2I end;

    if( netName.IsEmpty() || layerName.IsEmpty()
        || !jsonPositiveIntegerToInt( aArgs["width"], width )
        || !jsonPointToVector2I( aArgs["start"], start )
        || !jsonPointToVector2I( aArgs["end"], end ) )
    {
        return std::nullopt;
    }

    AI_SUGGESTION_OPERATION operation;
    operation.m_Kind = AI_SUGGESTION_OPERATION_KIND::RouteSegmentPreview;
    operation.m_NetName = netName;
    operation.m_LayerName = layerName;
    operation.m_Width = width;
    operation.m_Start = start;
    operation.m_End = end;
    return operation;
}


std::optional<AI_SUGGESTION_OPERATION> parsePlaceViaPreview( const nlohmann::json& aArgs )
{
    if( !aArgs.contains( "net" ) || !aArgs.contains( "diameter" )
        || !aArgs.contains( "drill" ) || !aArgs.contains( "position" ) )
    {
        return std::nullopt;
    }

    wxString netName = jsonStringToWxString( aArgs["net"] );
    int      diameter = 0;
    int      drill = 0;
    VECTOR2I position;

    if( !jsonPositiveIntegerToInt( aArgs["diameter"], diameter )
        || !jsonPositiveIntegerToInt( aArgs["drill"], drill )
        || !jsonPointToVector2I( aArgs["position"], position ) )
    {
        return std::nullopt;
    }

    AI_SUGGESTION_OPERATION operation;
    operation.m_Kind = AI_SUGGESTION_OPERATION_KIND::PlaceViaPreview;
    operation.m_NetName = netName;
    operation.m_Diameter = diameter;
    operation.m_Drill = drill;
    operation.m_Position = position;
    return operation;
}


bool isSupportedShapeKind( const wxString& aShape )
{
    return aShape.CmpNoCase( wxS( "segment" ) ) == 0
           || aShape.CmpNoCase( wxS( "line" ) ) == 0
           || aShape.CmpNoCase( wxS( "rectangle" ) ) == 0
           || aShape.CmpNoCase( wxS( "circle" ) ) == 0
           || aShape.CmpNoCase( wxS( "arc" ) ) == 0
           || aShape.CmpNoCase( wxS( "polygon" ) ) == 0
           || aShape.CmpNoCase( wxS( "poly" ) ) == 0;
}


std::optional<AI_SUGGESTION_OPERATION> parseCreateShapePreview( const nlohmann::json& aArgs )
{
    if( !aArgs.contains( "shape" ) || !aArgs.contains( "layer" )
        || !aArgs.contains( "width" ) )
    {
        return std::nullopt;
    }

    wxString shape;
    wxString layerName;
    int      width = 0;

    if( !jsonNonEmptyStringToWxString( aArgs["shape"], shape )
        || !isSupportedShapeKind( shape )
        || !jsonNonEmptyStringToWxString( aArgs["layer"], layerName )
        || !jsonPositiveIntegerToInt( aArgs["width"], width ) )
    {
        return std::nullopt;
    }

    if( shape.CmpNoCase( wxS( "line" ) ) == 0 )
        shape = wxS( "segment" );

    shape.MakeLower();

    AI_SUGGESTION_OPERATION operation;
    operation.m_Kind = AI_SUGGESTION_OPERATION_KIND::CreateShapePreview;
    operation.m_Shape = shape;
    operation.m_LayerName = layerName;
    operation.m_Width = width;

    if( shape.CmpNoCase( wxS( "circle" ) ) == 0 )
    {
        int      radius = 0;
        VECTOR2I center;

        if( !aArgs.contains( "center" ) || !aArgs.contains( "radius" )
            || !jsonPointToVector2I( aArgs["center"], center )
            || !jsonPositiveIntegerToInt( aArgs["radius"], radius ) )
        {
            return std::nullopt;
        }

        operation.m_Position = center;
        operation.m_Diameter = radius;
        return operation;
    }

    if( shape.CmpNoCase( wxS( "arc" ) ) == 0 )
    {
        VECTOR2I start;
        VECTOR2I mid;
        VECTOR2I end;

        if( !aArgs.contains( "start" ) || !aArgs.contains( "mid" )
            || !aArgs.contains( "end" )
            || !jsonPointToVector2I( aArgs["start"], start )
            || !jsonPointToVector2I( aArgs["mid"], mid )
            || !jsonPointToVector2I( aArgs["end"], end ) )
        {
            return std::nullopt;
        }

        operation.m_Start = start;
        operation.m_Position = mid;
        operation.m_End = end;
        return operation;
    }

    if( shape.CmpNoCase( wxS( "polygon" ) ) == 0 || shape.CmpNoCase( wxS( "poly" ) ) == 0 )
    {
        if( !aArgs.contains( "points" ) )
            return std::nullopt;

        std::optional<std::vector<VECTOR2I>> points = jsonPointsToVector( aArgs["points"] );

        if( !points )
            return std::nullopt;

        operation.m_Points = std::move( *points );
        return operation;
    }

    VECTOR2I start;
    VECTOR2I end;

    if( !aArgs.contains( "start" ) || !aArgs.contains( "end" )
        || !jsonPointToVector2I( aArgs["start"], start )
        || !jsonPointToVector2I( aArgs["end"], end ) )
    {
        return std::nullopt;
    }

    operation.m_Start = start;
    operation.m_End = end;
    return operation;
}


std::optional<AI_SUGGESTION_OPERATION> parseCreateCopperZonePreview( const nlohmann::json& aArgs )
{
    if( !aArgs.contains( "net" ) || !aArgs.contains( "layer" )
        || !aArgs.contains( "points" ) )
    {
        return std::nullopt;
    }

    wxString netName = jsonStringToWxString( aArgs["net"] );
    wxString layerName = jsonStringToWxString( aArgs["layer"] );
    std::optional<std::vector<VECTOR2I>> points = jsonPointsToVector( aArgs["points"] );

    if( netName.IsEmpty() || layerName.IsEmpty() || !points )
        return std::nullopt;

    std::vector<std::vector<VECTOR2I>> holes;

    if( !parseZonePreviewHoles( aArgs, holes ) )
        return std::nullopt;

    AI_SUGGESTION_OPERATION operation;
    operation.m_Kind = AI_SUGGESTION_OPERATION_KIND::CreateCopperZonePreview;
    operation.m_NetName = netName;
    operation.m_LayerName = layerName;
    operation.m_Points = std::move( *points );
    operation.m_Holes = std::move( holes );
    return operation;
}


std::optional<AI_SUGGESTION_OPERATION> parsePanelFillColumnPreview( const nlohmann::json& aArgs )
{
    if( !aArgs.contains( "panel_id" ) || !aArgs.contains( "table_id" )
        || !aArgs.contains( "column_id" ) || !aArgs.contains( "value" )
        || !aArgs.contains( "target_row_ids" ) || !aArgs["target_row_ids"].is_array()
        || aArgs["target_row_ids"].empty() )
    {
        return std::nullopt;
    }

    AI_SUGGESTION_OPERATION operation;
    operation.m_Kind = AI_SUGGESTION_OPERATION_KIND::PanelFillColumnPreview;

    if( !jsonNonEmptyStringToWxString( aArgs["panel_id"], operation.m_PanelId )
        || !jsonNonEmptyStringToWxString( aArgs["table_id"], operation.m_TableId )
        || !jsonNonEmptyStringToWxString( aArgs["column_id"], operation.m_ColumnId )
        || !jsonNonEmptyStringToWxString( aArgs["value"], operation.m_Value ) )
    {
        return std::nullopt;
    }

    operation.m_TargetRowIds.reserve( aArgs["target_row_ids"].size() );

    for( const nlohmann::json& rowIdJson : aArgs["target_row_ids"] )
    {
        wxString rowId;

        if( !jsonNonEmptyStringToWxString( rowIdJson, rowId ) )
            return std::nullopt;

        operation.m_TargetRowIds.push_back( rowId );
    }

    return operation;
}


std::optional<AI_SUGGESTION_OPERATION> parseAnchorFocusPreview( const nlohmann::json& aArgs )
{
    if( !aArgs.contains( "anchor_id" ) || !aArgs.contains( "position" ) )
        return std::nullopt;

    AI_SUGGESTION_OPERATION operation;
    operation.m_Kind = AI_SUGGESTION_OPERATION_KIND::AnchorFocusPreview;

    if( !jsonNonEmptyStringToWxString( aArgs["anchor_id"], operation.m_AnchorId )
        || !jsonPointToVector2I( aArgs["position"], operation.m_Position ) )
    {
        return std::nullopt;
    }

    if( aArgs.contains( "focus_layer" )
        && !jsonNonEmptyStringToWxString( aArgs["focus_layer"],
                                          operation.m_FocusLayer ) )
    {
        return std::nullopt;
    }

    if( aArgs.contains( "focus_net" )
        && !jsonNonEmptyStringToWxString( aArgs["focus_net"],
                                          operation.m_FocusNet ) )
    {
        return std::nullopt;
    }

    if( aArgs.contains( "dim_unfocused_layers" ) )
    {
        if( !aArgs["dim_unfocused_layers"].is_boolean() )
            return std::nullopt;

        operation.m_DimUnfocusedLayers = aArgs["dim_unfocused_layers"].get<bool>();
    }

    return operation;
}
} // namespace


bool AI_SUGGESTION_OPERATION::IsMove() const
{
    return m_Kind == AI_SUGGESTION_OPERATION_KIND::Move;
}


bool AI_SUGGESTION_OPERATION::IsMoveSelected() const
{
    return m_Kind == AI_SUGGESTION_OPERATION_KIND::MoveSelected;
}


bool AI_SUGGESTION_OPERATION::IsRouteSegmentPreview() const
{
    return m_Kind == AI_SUGGESTION_OPERATION_KIND::RouteSegmentPreview;
}


bool AI_SUGGESTION_OPERATION::IsPlaceViaPreview() const
{
    return m_Kind == AI_SUGGESTION_OPERATION_KIND::PlaceViaPreview;
}


bool AI_SUGGESTION_OPERATION::IsCreateShapePreview() const
{
    return m_Kind == AI_SUGGESTION_OPERATION_KIND::CreateShapePreview;
}


bool AI_SUGGESTION_OPERATION::IsCreateCopperZonePreview() const
{
    return m_Kind == AI_SUGGESTION_OPERATION_KIND::CreateCopperZonePreview;
}


bool AI_SUGGESTION_OPERATION::IsPanelFillColumnPreview() const
{
    return m_Kind == AI_SUGGESTION_OPERATION_KIND::PanelFillColumnPreview;
}


bool AI_SUGGESTION_OPERATION::IsAnchorFocusPreview() const
{
    return m_Kind == AI_SUGGESTION_OPERATION_KIND::AnchorFocusPreview;
}


std::optional<AI_SUGGESTION_OPERATION> ParseAiSuggestionOperation(
        const wxString& aArgumentsJson )
{
    const std::string arguments = toUtf8String( aArgumentsJson );

    if( arguments.empty() )
        return std::nullopt;

    try
    {
        const nlohmann::json args = nlohmann::json::parse( arguments );

        if( !args.is_object() || !args.contains( "operation" )
            || !args["operation"].is_string() )
        {
            return std::nullopt;
        }

        const std::string operationName = args["operation"].get<std::string>();

        if( operationName == "move" )
            return parseMoveOperation( args, false );

        if( operationName == "move_selected" )
            return parseMoveOperation( args, true );

        if( operationName == "route_segment_preview" )
            return parseRouteSegmentPreview( args );

        if( operationName == "place_via_preview" )
            return parsePlaceViaPreview( args );

        if( operationName == "create_shape_preview" )
            return parseCreateShapePreview( args );

        if( operationName == "create_copper_zone_preview" )
            return parseCreateCopperZonePreview( args );

        if( operationName == "panel_fill_column_preview" )
            return parsePanelFillColumnPreview( args );

        if( operationName == "anchor_focus_preview" )
            return parseAnchorFocusPreview( args );

        return std::nullopt;
    }
    catch( const std::exception& )
    {
        return std::nullopt;
    }
}


std::optional<VECTOR2I> ParseAiSuggestionMoveDelta( const wxString& aArgumentsJson )
{
    std::optional<AI_SUGGESTION_OPERATION> operation =
            ParseAiSuggestionOperation( aArgumentsJson );

    if( !operation || ( !operation->IsMove() && !operation->IsMoveSelected() ) )
        return std::nullopt;

    return operation->m_MoveDelta;
}
