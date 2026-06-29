/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 */

#include <kisurf/ai/ai_next_action_candidate_library.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{
struct VIA_PATTERN_POINT
{
    VECTOR2I m_Position = VECTOR2I( 0, 0 );
    wxString m_NetName;
    int      m_Diameter = 0;
    int      m_Drill = 0;
};

struct VIA_PATTERN_CANDIDATE
{
    VECTOR2I m_Position = VECTOR2I( 0, 0 );
    wxString m_NetName;
    int      m_Diameter = 0;
    int      m_Drill = 0;
    bool     m_LowConfidence = true;
};

struct ROUTING_SEGMENT_CANDIDATE
{
    VECTOR2I m_Start = VECTOR2I( 0, 0 );
    VECTOR2I m_End = VECTOR2I( 0, 0 );
    wxString m_NetName;
    wxString m_LayerName;
    int      m_Width = 0;
};


struct DRAWING_ZONE_CANDIDATE
{
    VECTOR2I          m_Position = VECTOR2I( 0, 0 );
    std::vector<VECTOR2I> m_Points;
    wxString          m_NetName;
    wxString          m_LayerName;
    int               m_Width = 100000;
};


struct PANEL_TABLE_FILL_CANDIDATE
{
    wxString              m_PanelId;
    wxString              m_PanelTitle;
    wxString              m_TableId;
    wxString              m_TableTitle;
    wxString              m_ColumnId;
    wxString              m_ColumnLabel;
    wxString              m_SourceRowId;
    wxString              m_SourceRowLabel;
    wxString              m_Value;
    std::vector<wxString> m_TargetRowIds;
};


std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


wxString fromUtf8String( const std::string& aText )
{
    return wxString::FromUTF8( aText.c_str() );
}


AI_CONTEXT_VERSION effectiveVersion( const AI_SUGGESTION_TRIGGER& aTrigger )
{
    if( aTrigger.m_ContextVersion.IsValid() )
        return aTrigger.m_ContextVersion;

    return aTrigger.m_ContextSnapshot.m_Version;
}


bool sameVersion( const AI_CONTEXT_VERSION& aLeft, const AI_CONTEXT_VERSION& aRight )
{
    return aLeft.m_DocumentRevision == aRight.m_DocumentRevision
        && aLeft.m_SelectionRevision == aRight.m_SelectionRevision
        && aLeft.m_ViewRevision == aRight.m_ViewRevision;
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

    return fromUtf8String( aValue.get<std::string>() );
}


bool jsonNonEmptyStringToWxString( const nlohmann::json& aValue, wxString& aOut )
{
    if( !aValue.is_string() )
        return false;

    aOut = fromUtf8String( aValue.get<std::string>() );
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


bool samePoint( const VECTOR2I& aLeft, const VECTOR2I& aRight )
{
    return aLeft.x == aRight.x && aLeft.y == aRight.y;
}


std::optional<VIA_PATTERN_POINT> parseViaPoint( const AI_OBJECT_REF& aRef )
{
    if( aRef.m_DetailsJson.IsEmpty() )
        return std::nullopt;

    const nlohmann::json details =
            nlohmann::json::parse( toUtf8String( aRef.m_DetailsJson ), nullptr, false );

    if( details.is_discarded() || !details.is_object()
        || !details.contains( "kind" ) || !details["kind"].is_string()
        || details["kind"].get<std::string>() != "via"
        || !details.contains( "position" )
        || !details.contains( "net_name" ) || !details["net_name"].is_string()
        || !details.contains( "diameter" ) )
    {
        return std::nullopt;
    }

    VIA_PATTERN_POINT point;
    point.m_NetName = jsonStringToWxString( details["net_name"] );

    if( point.m_NetName.IsEmpty()
        || !jsonPointToVector2I( details["position"], point.m_Position )
        || !jsonPositiveIntegerToInt( details["diameter"], point.m_Diameter )
        || point.m_Diameter <= 0 )
    {
        return std::nullopt;
    }

    if( details.contains( "drill" ) && !details["drill"].is_null() )
    {
        if( !jsonPositiveIntegerToInt( details["drill"], point.m_Drill ) )
            return std::nullopt;
    }
    else
    {
        point.m_Drill = std::max( 1, point.m_Diameter / 2 );
    }

    return point;
}


void appendViaPoints( const std::vector<AI_OBJECT_REF>& aObjects,
                      std::vector<VIA_PATTERN_POINT>& aOut,
                      std::set<std::string>& aSeenLabels )
{
    for( const AI_OBJECT_REF& object : aObjects )
    {
        const std::string label = toUtf8String( object.m_Label );

        if( !label.empty() && !aSeenLabels.insert( label ).second )
            continue;

        if( std::optional<VIA_PATTERN_POINT> point = parseViaPoint( object ) )
            aOut.push_back( *point );
    }
}


std::optional<ROUTING_SEGMENT_CANDIDATE> parseRoutingCandidate(
        const AI_TOOL_STATE_SNAPSHOT& aToolState )
{
    const nlohmann::json modeContext =
            nlohmann::json::parse( toUtf8String( aToolState.m_ModeContextJson ),
                                   nullptr, false );

    if( modeContext.is_discarded() || !modeContext.is_object()
        || !modeContext.contains( "net" ) || !modeContext.contains( "layer" )
        || !modeContext.contains( "width" ) || !modeContext.contains( "start" ) )
    {
        return std::nullopt;
    }

    ROUTING_SEGMENT_CANDIDATE candidate;
    candidate.m_NetName = jsonStringToWxString( modeContext["net"] );
    candidate.m_LayerName = jsonStringToWxString( modeContext["layer"] );

    if( candidate.m_NetName.IsEmpty() || candidate.m_LayerName.IsEmpty()
        || !jsonPositiveIntegerToInt( modeContext["width"], candidate.m_Width )
        || !jsonPointToVector2I( modeContext["start"], candidate.m_Start ) )
    {
        return std::nullopt;
    }

    bool hasEnd = false;

    if( modeContext.contains( "cursor" ) )
        hasEnd = jsonPointToVector2I( modeContext["cursor"], candidate.m_End );

    if( !hasEnd && aToolState.m_HasCursorBoardPosition )
    {
        candidate.m_End = aToolState.m_CursorBoardPosition;
        hasEnd = true;
    }

    if( !hasEnd || samePoint( candidate.m_Start, candidate.m_End ) )
        return std::nullopt;

    return candidate;
}


std::vector<VECTOR2I> rectangleAroundPoint( const VECTOR2I& aCenter,
                                            int aHalfSize )
{
    return {
        VECTOR2I( aCenter.x - aHalfSize, aCenter.y - aHalfSize ),
        VECTOR2I( aCenter.x + aHalfSize, aCenter.y - aHalfSize ),
        VECTOR2I( aCenter.x + aHalfSize, aCenter.y + aHalfSize ),
        VECTOR2I( aCenter.x - aHalfSize, aCenter.y + aHalfSize )
    };
}


std::optional<DRAWING_ZONE_CANDIDATE> parseDrawingZoneCandidate(
        const AI_TOOL_STATE_SNAPSHOT& aToolState )
{
    const nlohmann::json modeContext =
            nlohmann::json::parse( toUtf8String( aToolState.m_ModeContextJson ),
                                   nullptr, false );

    if( modeContext.is_discarded() || !modeContext.is_object()
        || !modeContext.contains( "layer" ) )
    {
        return std::nullopt;
    }

    DRAWING_ZONE_CANDIDATE candidate;
    candidate.m_LayerName = jsonStringToWxString( modeContext["layer"] );

    if( candidate.m_LayerName.IsEmpty() )
        return std::nullopt;

    if( modeContext.contains( "net" ) )
        candidate.m_NetName = jsonStringToWxString( modeContext["net"] );

    if( modeContext.contains( "width" ) )
        jsonPositiveIntegerToInt( modeContext["width"], candidate.m_Width );

    bool hasPosition = false;

    if( modeContext.contains( "cursor" ) )
        hasPosition = jsonPointToVector2I( modeContext["cursor"],
                                           candidate.m_Position );

    if( !hasPosition && aToolState.m_HasCursorBoardPosition )
    {
        candidate.m_Position = aToolState.m_CursorBoardPosition;
        hasPosition = true;
    }

    if( !hasPosition )
        return std::nullopt;

    candidate.m_Points = rectangleAroundPoint( candidate.m_Position, 500000 );
    return candidate;
}


bool panelHasFocus( const AI_PANEL_STATE_RECORD& aPanel )
{
    return !aPanel.m_FocusedControlId.IsEmpty()
           || !aPanel.m_FocusedControlLabel.IsEmpty()
           || !aPanel.m_SelectedText.IsEmpty();
}


bool parsePanelCellValue( const nlohmann::json& aCell, wxString& aOut )
{
    if( aCell.is_string() )
    {
        aOut = fromUtf8String( aCell.get<std::string>() );
        aOut.Trim( true ).Trim( false );
        return true;
    }

    if( aCell.is_object() && aCell.contains( "value" )
        && aCell["value"].is_string() )
    {
        aOut = fromUtf8String( aCell["value"].get<std::string>() );
        aOut.Trim( true ).Trim( false );
        return true;
    }

    return false;
}


bool rowCellValue( const nlohmann::json& aRow, const wxString& aColumnId,
                   wxString& aOut )
{
    aOut.clear();

    if( !aRow.is_object() || !aRow.contains( "cells" ) || !aRow["cells"].is_object() )
        return true;

    const nlohmann::json& cells = aRow["cells"];
    const std::string     columnId = toUtf8String( aColumnId );

    if( !cells.contains( columnId ) )
        return true;

    return parsePanelCellValue( cells[columnId], aOut );
}


bool focusedCellForTable( const nlohmann::json& aState, const nlohmann::json& aTable,
                          const wxString& aTableId, wxString& aRowId,
                          wxString& aColumnId )
{
    const nlohmann::json* focused = nullptr;

    if( aTable.contains( "focused_cell" ) )
    {
        focused = &aTable["focused_cell"];
    }
    else if( aState.contains( "focused_cell" ) )
    {
        focused = &aState["focused_cell"];

        wxString focusedTableId;

        if( ( *focused ).contains( "table_id" )
            && !jsonNonEmptyStringToWxString( ( *focused )["table_id"],
                                              focusedTableId ) )
        {
            return false;
        }

        if( !focusedTableId.IsEmpty() && focusedTableId != aTableId )
            return false;
    }

    if( !focused || !focused->is_object() )
        return false;

    return focused->contains( "row_id" ) && focused->contains( "column_id" )
           && jsonNonEmptyStringToWxString( ( *focused )["row_id"], aRowId )
           && jsonNonEmptyStringToWxString( ( *focused )["column_id"], aColumnId );
}


wxString panelColumnLabel( const nlohmann::json& aTable, const wxString& aColumnId )
{
    if( !aTable.contains( "columns" ) || !aTable["columns"].is_array() )
        return aColumnId;

    for( const nlohmann::json& column : aTable["columns"] )
    {
        wxString id;

        if( !column.is_object() || !column.contains( "id" )
            || !jsonNonEmptyStringToWxString( column["id"], id )
            || id != aColumnId )
        {
            continue;
        }

        wxString label;

        if( column.contains( "label" )
            && jsonNonEmptyStringToWxString( column["label"], label ) )
        {
            return label;
        }

        return aColumnId;
    }

    return aColumnId;
}


std::optional<PANEL_TABLE_FILL_CANDIDATE> panelTableCandidate(
        const AI_PANEL_STATE_RECORD& aPanel, const nlohmann::json& aState,
        const nlohmann::json& aTable )
{
    if( !aTable.is_object() || !aTable.contains( "rows" )
        || !aTable["rows"].is_array() )
    {
        return std::nullopt;
    }

    PANEL_TABLE_FILL_CANDIDATE candidate;
    candidate.m_PanelId = aPanel.m_Id;
    candidate.m_PanelTitle = aPanel.m_Title;

    if( !aTable.contains( "id" )
        || !jsonNonEmptyStringToWxString( aTable["id"], candidate.m_TableId ) )
    {
        return std::nullopt;
    }

    if( !aTable.contains( "title" )
        || !jsonNonEmptyStringToWxString( aTable["title"], candidate.m_TableTitle ) )
    {
        candidate.m_TableTitle = candidate.m_TableId;
    }

    if( !focusedCellForTable( aState, aTable, candidate.m_TableId,
                              candidate.m_SourceRowId, candidate.m_ColumnId ) )
    {
        return std::nullopt;
    }

    candidate.m_ColumnLabel = panelColumnLabel( aTable, candidate.m_ColumnId );
    bool foundSource = false;

    for( const nlohmann::json& row : aTable["rows"] )
    {
        wxString rowId;

        if( !row.is_object() || !row.contains( "id" )
            || !jsonNonEmptyStringToWxString( row["id"], rowId ) )
        {
            return std::nullopt;
        }

        wxString value;

        if( !rowCellValue( row, candidate.m_ColumnId, value ) )
            return std::nullopt;

        if( rowId == candidate.m_SourceRowId )
        {
            foundSource = true;
            candidate.m_Value = value;

            if( !row.contains( "label" )
                || !jsonNonEmptyStringToWxString( row["label"],
                                                  candidate.m_SourceRowLabel ) )
            {
                candidate.m_SourceRowLabel = rowId;
            }

            continue;
        }

        if( value.IsEmpty() )
            candidate.m_TargetRowIds.push_back( rowId );
    }

    if( !foundSource || candidate.m_Value.IsEmpty()
        || candidate.m_TargetRowIds.size() < 2 )
    {
        return std::nullopt;
    }

    return candidate;
}


std::optional<PANEL_TABLE_FILL_CANDIDATE> detectPanelTableFillCandidate(
        const AI_CONTEXT_SNAPSHOT& aContext )
{
    for( const AI_PANEL_STATE_RECORD& panel : aContext.m_PanelStates )
    {
        if( !panelHasFocus( panel ) || panel.m_StateJson.IsEmpty() )
            continue;

        const nlohmann::json state =
                nlohmann::json::parse( toUtf8String( panel.m_StateJson ),
                                       nullptr, false );

        if( state.is_discarded() || !state.is_object()
            || !state.contains( "tables" ) || !state["tables"].is_array() )
        {
            continue;
        }

        for( const nlohmann::json& table : state["tables"] )
        {
            if( std::optional<PANEL_TABLE_FILL_CANDIDATE> candidate =
                        panelTableCandidate( panel, state, table ) )
            {
                return candidate;
            }
        }
    }

    return std::nullopt;
}


std::vector<VIA_PATTERN_POINT> collectViaPoints( const AI_CONTEXT_SNAPSHOT& aContext )
{
    std::vector<VIA_PATTERN_POINT> points;
    std::set<std::string>          seenLabels;

    appendViaPoints( aContext.m_VisibleObjects, points, seenLabels );
    appendViaPoints( aContext.m_SelectedObjects, points, seenLabels );
    return points;
}


bool findLinearCandidate( std::vector<VIA_PATTERN_POINT> aPoints, bool aHorizontal,
                          VIA_PATTERN_CANDIDATE& aCandidate )
{
    if( aPoints.size() < 2 )
        return false;

    if( aHorizontal )
    {
        const int y = aPoints.front().m_Position.y;

        if( std::any_of( aPoints.begin(), aPoints.end(),
                         [y]( const VIA_PATTERN_POINT& aPoint )
                         {
                             return aPoint.m_Position.y != y;
                         } ) )
        {
            return false;
        }

        std::sort( aPoints.begin(), aPoints.end(),
                   []( const VIA_PATTERN_POINT& aLeft, const VIA_PATTERN_POINT& aRight )
                   {
                       return aLeft.m_Position.x < aRight.m_Position.x;
                   } );

        const int delta = aPoints[1].m_Position.x - aPoints[0].m_Position.x;

        if( delta == 0 )
            return false;

        for( size_t i = 2; i < aPoints.size(); ++i )
        {
            if( aPoints[i].m_Position.x - aPoints[i - 1].m_Position.x != delta )
                return false;
        }

        aCandidate.m_Position = VECTOR2I( aPoints.back().m_Position.x + delta, y );
    }
    else
    {
        const int x = aPoints.front().m_Position.x;

        if( std::any_of( aPoints.begin(), aPoints.end(),
                         [x]( const VIA_PATTERN_POINT& aPoint )
                         {
                             return aPoint.m_Position.x != x;
                         } ) )
        {
            return false;
        }

        std::sort( aPoints.begin(), aPoints.end(),
                   []( const VIA_PATTERN_POINT& aLeft, const VIA_PATTERN_POINT& aRight )
                   {
                       return aLeft.m_Position.y < aRight.m_Position.y;
                   } );

        const int delta = aPoints[1].m_Position.y - aPoints[0].m_Position.y;

        if( delta == 0 )
            return false;

        for( size_t i = 2; i < aPoints.size(); ++i )
        {
            if( aPoints[i].m_Position.y - aPoints[i - 1].m_Position.y != delta )
                return false;
        }

        aCandidate.m_Position = VECTOR2I( x, aPoints.back().m_Position.y + delta );
    }

    aCandidate.m_NetName = aPoints.front().m_NetName;
    aCandidate.m_Diameter = aPoints.front().m_Diameter;
    aCandidate.m_Drill = aPoints.front().m_Drill;
    aCandidate.m_LowConfidence = aPoints.size() == 2;
    return true;
}


std::optional<VIA_PATTERN_CANDIDATE> detectViaPattern(
        const std::vector<VIA_PATTERN_POINT>& aPoints )
{
    std::map<std::string, std::vector<VIA_PATTERN_POINT>> groups;

    for( const VIA_PATTERN_POINT& point : aPoints )
    {
        wxString key;
        key << point.m_NetName << wxS( "|" ) << point.m_Diameter
            << wxS( "|" ) << point.m_Drill;
        groups[toUtf8String( key )].push_back( point );
    }

    std::optional<VIA_PATTERN_CANDIDATE> lowConfidenceCandidate;

    for( const auto& [key, points] : groups )
    {
        if( points.size() < 2 )
            continue;

        VIA_PATTERN_CANDIDATE candidate;

        if( !findLinearCandidate( points, true, candidate )
            && !findLinearCandidate( points, false, candidate ) )
        {
            continue;
        }

        if( !candidate.m_LowConfidence )
            return candidate;

        if( !lowConfidenceCandidate )
            lowConfidenceCandidate = candidate;
    }

    return lowConfidenceCandidate;
}


std::optional<VIA_PATTERN_CANDIDATE> fallbackViaPlacementCandidate(
        const AI_TOOL_STATE_SNAPSHOT& aToolState )
{
    const nlohmann::json modeContext =
            nlohmann::json::parse( toUtf8String( aToolState.m_ModeContextJson ),
                                   nullptr, false );

    if( modeContext.is_discarded() || !modeContext.is_object() )
        return std::nullopt;

    VIA_PATTERN_CANDIDATE candidate;

    if( modeContext.contains( "net" ) )
        candidate.m_NetName = jsonStringToWxString( modeContext["net"] );

    if( !modeContext.contains( "diameter" ) || !modeContext.contains( "drill" )
        || !jsonPositiveIntegerToInt( modeContext["diameter"], candidate.m_Diameter )
        || !jsonPositiveIntegerToInt( modeContext["drill"], candidate.m_Drill ) )
    {
        return std::nullopt;
    }

    bool hasPosition = false;

    if( modeContext.contains( "cursor" ) )
        hasPosition = jsonPointToVector2I( modeContext["cursor"], candidate.m_Position );

    if( !hasPosition && aToolState.m_HasCursorBoardPosition )
    {
        candidate.m_Position = aToolState.m_CursorBoardPosition;
        hasPosition = true;
    }

    if( !hasPosition )
        return std::nullopt;

    candidate.m_LowConfidence = true;
    return candidate;
}


wxString buildArgumentsJson( const VIA_PATTERN_CANDIDATE& aCandidate )
{
    nlohmann::json operation =
            { { "operation", "place_via_preview" },
              { "net", toUtf8String( aCandidate.m_NetName ) },
              { "diameter", aCandidate.m_Diameter },
              { "drill", aCandidate.m_Drill },
              { "position",
                { { "x", aCandidate.m_Position.x }, { "y", aCandidate.m_Position.y } } } };

    return fromUtf8String( operation.dump() );
}


wxString buildArgumentsJson( const ROUTING_SEGMENT_CANDIDATE& aCandidate )
{
    nlohmann::json operation =
            { { "operation", "route_segment_preview" },
              { "net", toUtf8String( aCandidate.m_NetName ) },
              { "layer", toUtf8String( aCandidate.m_LayerName ) },
              { "width", aCandidate.m_Width },
              { "start",
                { { "x", aCandidate.m_Start.x }, { "y", aCandidate.m_Start.y } } },
              { "end", { { "x", aCandidate.m_End.x }, { "y", aCandidate.m_End.y } } } };

    return fromUtf8String( operation.dump() );
}


wxString buildArgumentsJson( const DRAWING_ZONE_CANDIDATE& aCandidate )
{
    nlohmann::json points = nlohmann::json::array();

    for( const VECTOR2I& point : aCandidate.m_Points )
        points.push_back( { { "x", point.x }, { "y", point.y } } );

    if( !aCandidate.m_NetName.IsEmpty() )
    {
        nlohmann::json operation =
                { { "operation", "create_copper_zone_preview" },
                  { "net", toUtf8String( aCandidate.m_NetName ) },
                  { "layer", toUtf8String( aCandidate.m_LayerName ) },
                  { "points", std::move( points ) } };

        return fromUtf8String( operation.dump() );
    }

    nlohmann::json operation =
            { { "operation", "create_shape_preview" },
              { "shape", "rectangle" },
              { "layer", toUtf8String( aCandidate.m_LayerName ) },
              { "width", aCandidate.m_Width },
              { "start",
                { { "x", aCandidate.m_Points.front().x },
                  { "y", aCandidate.m_Points.front().y } } },
              { "end",
                { { "x", aCandidate.m_Points[2].x },
                  { "y", aCandidate.m_Points[2].y } } } };

    return fromUtf8String( operation.dump() );
}


wxString buildArgumentsJson( const PANEL_TABLE_FILL_CANDIDATE& aCandidate )
{
    nlohmann::json targetRowIds = nlohmann::json::array();

    for( const wxString& rowId : aCandidate.m_TargetRowIds )
        targetRowIds.push_back( toUtf8String( rowId ) );

    nlohmann::json operation =
            { { "operation", "panel_fill_column_preview" },
              { "panel_id", toUtf8String( aCandidate.m_PanelId ) },
              { "table_id", toUtf8String( aCandidate.m_TableId ) },
              { "column_id", toUtf8String( aCandidate.m_ColumnId ) },
              { "value", toUtf8String( aCandidate.m_Value ) },
              { "target_row_ids", std::move( targetRowIds ) } };

    return fromUtf8String( operation.dump() );
}


AI_OBJECT_REF buildSyntheticPreviewRef( const VIA_PATTERN_CANDIDATE& aCandidate )
{
    wxString label = wxString::Format( wxS( "preview:via:%d,%d" ),
                                       aCandidate.m_Position.x,
                                       aCandidate.m_Position.y );
    return AI_OBJECT_REF( KIID(), PCB_VIA_T, label, buildArgumentsJson( aCandidate ) );
}


AI_OBJECT_REF buildSyntheticPreviewRef( const ROUTING_SEGMENT_CANDIDATE& aCandidate )
{
    wxString label = wxString::Format( wxS( "preview:route:%d,%d:%d,%d" ),
                                       aCandidate.m_Start.x, aCandidate.m_Start.y,
                                       aCandidate.m_End.x, aCandidate.m_End.y );
    return AI_OBJECT_REF( KIID(), PCB_TRACE_T, label, buildArgumentsJson( aCandidate ) );
}


AI_OBJECT_REF buildSyntheticPreviewRef( const DRAWING_ZONE_CANDIDATE& aCandidate )
{
    wxString label = wxString::Format( wxS( "preview:zone:%d,%d" ),
                                       aCandidate.m_Position.x,
                                       aCandidate.m_Position.y );

    if( !aCandidate.m_NetName.IsEmpty() )
        return AI_OBJECT_REF( KIID(), PCB_ZONE_T, label, buildArgumentsJson( aCandidate ) );

    return AI_OBJECT_REF( KIID(), PCB_SHAPE_T, label, buildArgumentsJson( aCandidate ) );
}


bool triggerIsActiveViaPlacement( const AI_SUGGESTION_TRIGGER& aTrigger )
{
    const AI_TOOL_STATE_SNAPSHOT& toolState = aTrigger.m_ContextSnapshot.m_ToolState;
    const AI_CONTEXT_VERSION      version = effectiveVersion( aTrigger );

    if( aTrigger.m_EditorKind != AI_EDITOR_KIND::Pcb
        || aTrigger.m_ContextSnapshot.m_EditorKind != AI_EDITOR_KIND::Pcb
        || toolState.m_EditorKind != AI_EDITOR_KIND::Pcb
        || toolState.m_Kind != AI_TOOL_STATE_KIND::PlacingVia )
    {
        return false;
    }

    if( toolState.m_ContextVersion.IsValid() && version.IsValid()
        && !sameVersion( toolState.m_ContextVersion, version ) )
    {
        return false;
    }

    return true;
}


bool triggerIsActiveRouting( const AI_SUGGESTION_TRIGGER& aTrigger )
{
    const AI_TOOL_STATE_SNAPSHOT& toolState = aTrigger.m_ContextSnapshot.m_ToolState;
    const AI_CONTEXT_VERSION      version = effectiveVersion( aTrigger );

    if( aTrigger.m_EditorKind != AI_EDITOR_KIND::Pcb
        || aTrigger.m_ContextSnapshot.m_EditorKind != AI_EDITOR_KIND::Pcb
        || toolState.m_EditorKind != AI_EDITOR_KIND::Pcb
        || toolState.m_Kind != AI_TOOL_STATE_KIND::RoutingTrack )
    {
        return false;
    }

    if( toolState.m_ContextVersion.IsValid() && version.IsValid()
        && !sameVersion( toolState.m_ContextVersion, version ) )
    {
        return false;
    }

    return true;
}


bool triggerIsActiveDrawingZone( const AI_SUGGESTION_TRIGGER& aTrigger )
{
    const AI_TOOL_STATE_SNAPSHOT& toolState = aTrigger.m_ContextSnapshot.m_ToolState;
    const AI_CONTEXT_VERSION      version = effectiveVersion( aTrigger );

    if( aTrigger.m_EditorKind != AI_EDITOR_KIND::Pcb
        || aTrigger.m_ContextSnapshot.m_EditorKind != AI_EDITOR_KIND::Pcb
        || toolState.m_EditorKind != AI_EDITOR_KIND::Pcb
        || toolState.m_Kind != AI_TOOL_STATE_KIND::DrawingZone )
    {
        return false;
    }

    if( toolState.m_ContextVersion.IsValid() && version.IsValid()
        && !sameVersion( toolState.m_ContextVersion, version ) )
    {
        return false;
    }

    return true;
}


bool triggerIsActivePanel( const AI_SUGGESTION_TRIGGER& aTrigger )
{
    if( aTrigger.m_EditorKind == AI_EDITOR_KIND::Unknown
        || aTrigger.m_ContextSnapshot.m_EditorKind == AI_EDITOR_KIND::Unknown )
    {
        return false;
    }

    return AiDynamicContextKind( aTrigger.m_ContextSnapshot ) == wxS( "panel" );
}
} // namespace


std::optional<AI_SUGGESTION_RECORD> AiGenerateViaPatternCandidate(
        const AI_SUGGESTION_TRIGGER& aTrigger )
{
    if( !triggerIsActiveViaPlacement( aTrigger ) )
        return std::nullopt;

    std::optional<VIA_PATTERN_CANDIDATE> candidate =
            detectViaPattern( collectViaPoints( aTrigger.m_ContextSnapshot ) );

    if( !candidate )
        return std::nullopt;

    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_EditorKind = AI_EDITOR_KIND::Pcb;
    suggestion.m_Kind = AI_SUGGESTION_KIND::Preview;
    suggestion.m_ContextVersion = effectiveVersion( aTrigger );
    suggestion.m_TriggerActivitySequence = aTrigger.m_Activity.m_Sequence;
    suggestion.m_Title = wxS( "Preview next via" );
    suggestion.m_Body = candidate->m_LowConfidence
                              ? wxS( "low confidence: two aligned vias suggest a possible spacing pattern." )
                              : wxS( "Detected equal via spacing. Preview the next via before applying it." );
    suggestion.m_ContextKind = AiDynamicContextKind( aTrigger.m_ContextSnapshot );
    suggestion.m_ContextDetailsJson =
            AiDynamicContextDetailsJson( aTrigger.m_ContextSnapshot,
                                         wxS( "via_pattern" ) );
    suggestion.m_ArgumentsJson = buildArgumentsJson( *candidate );
    suggestion.m_Fingerprint << wxS( "via-pattern|" ) << candidate->m_NetName
                             << wxS( "|" ) << candidate->m_Position.x << wxS( "," )
                             << candidate->m_Position.y << wxS( "|" )
                             << candidate->m_Diameter << wxS( "|" )
                             << candidate->m_Drill << wxS( "|" )
                             << ( candidate->m_LowConfidence ? wxS( "low" )
                                                             : wxS( "normal" ) );
    suggestion.m_PreviewObjects.push_back( buildSyntheticPreviewRef( *candidate ) );
    suggestion.m_EditObjects = suggestion.m_PreviewObjects;
    return suggestion;
}


std::optional<AI_SUGGESTION_RECORD> AiGenerateViaPlacementCandidate(
        const AI_SUGGESTION_TRIGGER& aTrigger )
{
    if( std::optional<AI_SUGGESTION_RECORD> pattern =
                AiGenerateViaPatternCandidate( aTrigger ) )
    {
        return pattern;
    }

    if( !triggerIsActiveViaPlacement( aTrigger ) )
        return std::nullopt;

    std::optional<VIA_PATTERN_CANDIDATE> candidate =
            fallbackViaPlacementCandidate( aTrigger.m_ContextSnapshot.m_ToolState );

    if( !candidate )
        return std::nullopt;

    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_EditorKind = AI_EDITOR_KIND::Pcb;
    suggestion.m_Kind = AI_SUGGESTION_KIND::Preview;
    suggestion.m_ContextVersion = effectiveVersion( aTrigger );
    suggestion.m_TriggerActivitySequence = aTrigger.m_Activity.m_Sequence;
    suggestion.m_Title = wxS( "Preview via placement" );
    suggestion.m_Body =
            wxS( "Preview the next via at the current placement point." );
    suggestion.m_ContextKind = AiDynamicContextKind( aTrigger.m_ContextSnapshot );
    suggestion.m_ContextDetailsJson =
            AiDynamicContextDetailsJson( aTrigger.m_ContextSnapshot,
                                         wxS( "via_placement" ) );
    suggestion.m_ArgumentsJson = buildArgumentsJson( *candidate );
    suggestion.m_Fingerprint << wxS( "via-placement|" )
                             << suggestion.m_ContextVersion.AsString()
                             << wxS( "|" ) << candidate->m_NetName
                             << wxS( "|" ) << candidate->m_Position.x << wxS( "," )
                             << candidate->m_Position.y << wxS( "|" )
                             << candidate->m_Diameter << wxS( "|" )
                             << candidate->m_Drill;
    suggestion.m_PreviewObjects.push_back( buildSyntheticPreviewRef( *candidate ) );
    suggestion.m_EditObjects = suggestion.m_PreviewObjects;
    return suggestion;
}


std::optional<AI_SUGGESTION_RECORD> AiGenerateRoutingSegmentCandidate(
        const AI_SUGGESTION_TRIGGER& aTrigger )
{
    if( !triggerIsActiveRouting( aTrigger ) )
        return std::nullopt;

    std::optional<ROUTING_SEGMENT_CANDIDATE> candidate =
            parseRoutingCandidate( aTrigger.m_ContextSnapshot.m_ToolState );

    if( !candidate )
        return std::nullopt;

    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_EditorKind = AI_EDITOR_KIND::Pcb;
    suggestion.m_Kind = AI_SUGGESTION_KIND::Preview;
    suggestion.m_ContextVersion = effectiveVersion( aTrigger );
    suggestion.m_TriggerActivitySequence = aTrigger.m_Activity.m_Sequence;
    suggestion.m_Title = wxS( "Preview route segment" );
    suggestion.m_Body = wxS( "Preview the next routing segment before applying it." );
    suggestion.m_ContextKind = AiDynamicContextKind( aTrigger.m_ContextSnapshot );
    suggestion.m_ContextDetailsJson =
            AiDynamicContextDetailsJson( aTrigger.m_ContextSnapshot,
                                         wxS( "route_segment" ) );
    suggestion.m_ArgumentsJson = buildArgumentsJson( *candidate );
    suggestion.m_Fingerprint << wxS( "route-segment|" ) << candidate->m_NetName
                             << wxS( "|" ) << candidate->m_LayerName << wxS( "|" )
                             << candidate->m_Width << wxS( "|" )
                             << candidate->m_Start.x << wxS( "," )
                             << candidate->m_Start.y << wxS( "|" )
                             << candidate->m_End.x << wxS( "," )
                             << candidate->m_End.y;
    suggestion.m_PreviewObjects.push_back( buildSyntheticPreviewRef( *candidate ) );
    suggestion.m_EditObjects = suggestion.m_PreviewObjects;
    return suggestion;
}


std::optional<AI_SUGGESTION_RECORD> AiGenerateDrawingZoneCandidate(
        const AI_SUGGESTION_TRIGGER& aTrigger )
{
    if( !triggerIsActiveDrawingZone( aTrigger ) )
        return std::nullopt;

    std::optional<DRAWING_ZONE_CANDIDATE> candidate =
            parseDrawingZoneCandidate( aTrigger.m_ContextSnapshot.m_ToolState );

    if( !candidate )
        return std::nullopt;

    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_EditorKind = AI_EDITOR_KIND::Pcb;
    suggestion.m_Kind = AI_SUGGESTION_KIND::Preview;
    suggestion.m_ContextVersion = effectiveVersion( aTrigger );
    suggestion.m_TriggerActivitySequence = aTrigger.m_Activity.m_Sequence;
    suggestion.m_Title = candidate->m_NetName.IsEmpty()
                                 ? wxS( "Preview drawing shape" )
                                 : wxS( "Preview copper zone" );
    suggestion.m_Body =
            candidate->m_NetName.IsEmpty()
                    ? wxS( "Preview a starter shape at the current drawing point." )
                    : wxS( "Preview a starter copper zone at the current drawing point." );
    suggestion.m_ContextKind = AiDynamicContextKind( aTrigger.m_ContextSnapshot );
    suggestion.m_ContextDetailsJson =
            AiDynamicContextDetailsJson( aTrigger.m_ContextSnapshot,
                                         wxS( "drawing_zone" ) );
    suggestion.m_ArgumentsJson = buildArgumentsJson( *candidate );
    suggestion.m_Fingerprint << wxS( "drawing-zone|" )
                             << suggestion.m_ContextVersion.AsString()
                             << wxS( "|" ) << candidate->m_LayerName
                             << wxS( "|" ) << candidate->m_NetName
                             << wxS( "|" ) << candidate->m_Position.x
                             << wxS( "," ) << candidate->m_Position.y;
    suggestion.m_PreviewObjects.push_back( buildSyntheticPreviewRef( *candidate ) );
    suggestion.m_EditObjects = suggestion.m_PreviewObjects;
    return suggestion;
}


std::optional<AI_SUGGESTION_RECORD> AiGeneratePanelTableFillCandidate(
        const AI_SUGGESTION_TRIGGER& aTrigger )
{
    if( !triggerIsActivePanel( aTrigger ) )
        return std::nullopt;

    std::optional<PANEL_TABLE_FILL_CANDIDATE> candidate =
            detectPanelTableFillCandidate( aTrigger.m_ContextSnapshot );

    if( !candidate )
        return std::nullopt;

    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_EditorKind = aTrigger.m_EditorKind;
    suggestion.m_Kind = AI_SUGGESTION_KIND::Preview;
    suggestion.m_ContextVersion = effectiveVersion( aTrigger );
    suggestion.m_TriggerActivitySequence = aTrigger.m_Activity.m_Sequence;
    suggestion.m_Title = wxString::Format( wxS( "Fill %s column" ),
                                           candidate->m_ColumnLabel );
    suggestion.m_Body = wxString::Format(
            wxS( "Detected %s in %s. Review filling %zu empty cells in %s." ),
            candidate->m_Value,
            candidate->m_SourceRowLabel,
            candidate->m_TargetRowIds.size(),
            candidate->m_PanelTitle.IsEmpty() ? candidate->m_TableTitle
                                              : candidate->m_PanelTitle );
    suggestion.m_ContextKind = AiDynamicContextKind( aTrigger.m_ContextSnapshot );
    suggestion.m_ContextDetailsJson =
            AiDynamicContextDetailsJson( aTrigger.m_ContextSnapshot,
                                         wxS( "panel_table_fill" ) );
    suggestion.m_ArgumentsJson = buildArgumentsJson( *candidate );
    suggestion.m_Fingerprint << wxS( "panel-table-fill|" )
                             << suggestion.m_ContextVersion.AsString()
                             << wxS( "|" ) << candidate->m_PanelId
                             << wxS( "|" ) << candidate->m_TableId
                             << wxS( "|" ) << candidate->m_ColumnId
                             << wxS( "|" ) << candidate->m_Value;

    for( const wxString& rowId : candidate->m_TargetRowIds )
        suggestion.m_Fingerprint << wxS( "|" ) << rowId;

    return suggestion;
}
