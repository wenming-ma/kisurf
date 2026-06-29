/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#include <kisurf/ai/ai_panel_state_wx_adapter.h>

#include <kisurf/ai/ai_structured_surface_apply_adapter.h>

#include <nlohmann/json.hpp>

#include <wx/grid.h>
#include <wx/window.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <vector>


namespace
{
std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() )
                         : std::string();
}


wxString fromJson( const nlohmann::json& aJson )
{
    const std::string dumped = aJson.dump();
    return wxString::FromUTF8( dumped.c_str() );
}


wxString rowId( int aRow )
{
    return wxString::Format( wxS( "r%d" ), aRow );
}


wxString columnId( int aColumn )
{
    return wxString::Format( wxS( "c%d" ), aColumn );
}


wxString fallbackLabel( const wxString& aLabel, const wxString& aFallback )
{
    if( aLabel.IsEmpty() )
        return aFallback;

    return aLabel;
}


std::optional<std::pair<int, int>> focusedCell( const AI_STRUCTURED_SURFACE_GRID_IO& aGridIo )
{
    for( const std::pair<int, int>& cell : aGridIo.SelectedCells() )
    {
        if( cell.first >= 0 && cell.first < aGridIo.RowCount()
            && cell.second >= 0 && cell.second < aGridIo.ColumnCount() )
        {
            return cell;
        }
    }

    return std::nullopt;
}


nlohmann::json gridTableJson( const wxString& aTableId, const wxString& aTableTitle,
                              AI_STRUCTURED_SURFACE_GRID_IO& aGridIo,
                              std::optional<std::pair<int, int>>& aFocusedCell )
{
    if( aGridIo.IsCellEditControlShown() )
        aGridIo.SaveEditControlValue();

    nlohmann::json table;
    table["id"] = toUtf8String( aTableId );
    table["title"] = toUtf8String( aTableTitle.IsEmpty() ? aTableId : aTableTitle );
    table["columns"] = nlohmann::json::array();
    table["rows"] = nlohmann::json::array();

    for( int col = 0; col < aGridIo.ColumnCount(); ++col )
    {
        const wxString id = columnId( col );
        table["columns"].push_back( {
                { "id", toUtf8String( id ) },
                { "label", toUtf8String(
                            fallbackLabel( aGridIo.ColumnLabel( col ), id ) ) }
        } );
    }

    for( int row = 0; row < aGridIo.RowCount(); ++row )
    {
        const wxString id = rowId( row );
        nlohmann::json cells = nlohmann::json::object();

        for( int col = 0; col < aGridIo.ColumnCount(); ++col )
            cells[toUtf8String( columnId( col ) )] =
                    toUtf8String( aGridIo.CellValue( row, col ) );

        table["rows"].push_back( {
                { "id", toUtf8String( id ) },
                { "label", toUtf8String( fallbackLabel( aGridIo.RowLabel( row ), id ) ) },
                { "cells", std::move( cells ) }
        } );
    }

    aFocusedCell = focusedCell( aGridIo );

    if( aFocusedCell )
    {
        table["focused_cell"] = {
            { "row_id", toUtf8String( rowId( aFocusedCell->first ) ) },
            { "column_id", toUtf8String( columnId( aFocusedCell->second ) ) }
        };
    }

    return table;
}


class WX_GRID_PANEL_STATE_IO : public AI_STRUCTURED_SURFACE_GRID_IO
{
public:
    explicit WX_GRID_PANEL_STATE_IO( wxGrid& aGrid, bool aUseCursorAsFocus ) :
            m_Grid( aGrid ),
            m_UseCursorAsFocus( aUseCursorAsFocus )
    {
    }

    int RowCount() const override
    {
        return m_Grid.GetNumberRows();
    }

    int ColumnCount() const override
    {
        return m_Grid.GetNumberCols();
    }

    wxString RowLabel( int aRow ) const override
    {
        return m_Grid.GetRowLabelValue( aRow );
    }

    wxString ColumnLabel( int aColumn ) const override
    {
        return m_Grid.GetColLabelValue( aColumn );
    }

    wxString CellValue( int aRow, int aColumn ) const override
    {
        return m_Grid.GetCellValue( aRow, aColumn );
    }

    void SetCellValue( int aRow, int aColumn, const wxString& aValue ) override
    {
        m_Grid.SetCellValue( aRow, aColumn, aValue );
    }

    std::vector<std::pair<int, int>> SelectedCells() const override
    {
        std::vector<std::pair<int, int>> cells;

        if( m_UseCursorAsFocus )
        {
            const int cursorRow = m_Grid.GetGridCursorRow();
            const int cursorCol = m_Grid.GetGridCursorCol();

            if( cursorRow >= 0 && cursorCol >= 0 )
                cells.emplace_back( cursorRow, cursorCol );
        }

        const wxGridCellCoordsArray selected = m_Grid.GetSelectedCells();

        for( const wxGridCellCoords& cell : selected )
            cells.emplace_back( cell.GetRow(), cell.GetCol() );

        return cells;
    }

    bool IsCellEditControlShown() const override
    {
        return m_Grid.IsCellEditControlShown();
    }

    void SaveEditControlValue() override
    {
        m_Grid.SaveEditControlValue();
    }

private:
    wxGrid& m_Grid;
    bool    m_UseCursorAsFocus = false;
};


bool containsWindow( const wxWindow& aRoot, const wxWindow* aNeedle )
{
    for( const wxWindow* window = aNeedle; window; window = window->GetParent() )
    {
        if( window == &aRoot )
            return true;
    }

    return false;
}


void collectGrids( const wxWindow& aRoot, std::vector<wxGrid*>& aGrids )
{
    if( wxGrid* grid = dynamic_cast<wxGrid*>( const_cast<wxWindow*>( &aRoot ) ) )
        aGrids.push_back( grid );

    const wxWindowList& children = aRoot.GetChildren();

    for( wxWindowList::compatibility_iterator node = children.GetFirst();
         node; node = node->GetNext() )
    {
        if( wxWindow* child = node->GetData() )
            collectGrids( *child, aGrids );
    }
}
} // namespace


AI_PANEL_STATE_RECORD AiPanelStateRecordFromGridIo(
        const wxString& aPanelId, const wxString& aPanelTitle,
        const wxString& aTableId, const wxString& aTableTitle,
        AI_STRUCTURED_SURFACE_GRID_IO& aGridIo )
{
    AI_PANEL_STATE_RECORD record;
    record.m_Id = aPanelId;
    record.m_Title = aPanelTitle;

    std::optional<std::pair<int, int>> focus;

    nlohmann::json state;
    state["tables"] = nlohmann::json::array(
            { gridTableJson( aTableId, aTableTitle, aGridIo, focus ) } );

    if( focus )
    {
        const wxString colId = columnId( focus->second );
        const wxString row = rowId( focus->first );
        record.m_FocusedControlId =
                wxString::Format( wxS( "%s.%s.%s" ), aTableId, row, colId );
        record.m_FocusedControlLabel =
                fallbackLabel( aGridIo.ColumnLabel( focus->second ), colId );
        record.m_SelectedText = aGridIo.CellValue( focus->first, focus->second );
    }

    record.m_Summary = wxString::Format( wxS( "tables=1 rows=%d columns=%d" ),
                                         aGridIo.RowCount(), aGridIo.ColumnCount() );
    record.m_StateJson = fromJson( state );
    return record;
}


AI_PANEL_STATE_RECORD AiPanelStateRecordFromWxWindowGrids(
        const wxWindow& aRoot, const wxString& aPanelId,
        const wxString& aPanelTitle, const wxString& aTableIdPrefix,
        const wxString& aTableTitlePrefix )
{
    std::vector<wxGrid*> grids;
    collectGrids( aRoot, grids );

    if( grids.empty() )
        return AI_PANEL_STATE_RECORD();

    AI_PANEL_STATE_RECORD record;
    record.m_Id = aPanelId;
    record.m_Title = aPanelTitle;

    nlohmann::json state;
    state["tables"] = nlohmann::json::array();

    bool capturedFocus = false;
    const wxWindow* focusedWindow = wxWindow::FindFocus();

    for( size_t ii = 0; ii < grids.size(); ++ii )
    {
        wxString tableId = aTableIdPrefix;

        if( tableId.IsEmpty() )
            tableId = wxS( "grid" );

        tableId << wxS( ".grid" );

        if( grids.size() > 1 )
            tableId << ii;

        wxString tableTitle = aTableTitlePrefix;

        if( grids.size() > 1 )
            tableTitle << wxString::Format( wxS( " %zu" ), ii + 1 );

        const bool gridHasFocus =
                !focusedWindow || containsWindow( *grids[ii], focusedWindow );
        WX_GRID_PANEL_STATE_IO gridIo( *grids[ii], gridHasFocus );
        std::optional<std::pair<int, int>> focus;
        state["tables"].push_back(
                gridTableJson( tableId, tableTitle, gridIo, focus ) );

        if( focus && !capturedFocus )
        {
            const wxString colId = columnId( focus->second );
            const wxString row = rowId( focus->first );
            record.m_FocusedControlId =
                    wxString::Format( wxS( "%s.%s.%s" ), tableId, row, colId );
            record.m_FocusedControlLabel =
                    fallbackLabel( gridIo.ColumnLabel( focus->second ), colId );
            record.m_SelectedText = gridIo.CellValue( focus->first, focus->second );
            capturedFocus = true;
        }
    }

    record.m_Summary = wxString::Format( wxS( "tables=%zu" ), grids.size() );
    record.m_StateJson = fromJson( state );
    return record;
}
