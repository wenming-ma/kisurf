/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 */

#include <kisurf/ai/ai_structured_surface_preview_adapter.h>

#include <algorithm>
#include <charconv>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <wx/grid.h>
#include <wx/propgrid/propgrid.h>

namespace
{
std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


wxString fromUtf8String( const std::string& aText )
{
    return wxString::FromUTF8( aText.c_str() );
}


nlohmann::json parseObject( const wxString& aText )
{
    nlohmann::json parsed =
            nlohmann::json::parse( toUtf8String( aText ), nullptr, false );

    return parsed.is_object() ? parsed : nlohmann::json::object();
}


std::string jsonStringOrEmpty( const nlohmann::json& aObject,
                               const char* aKey )
{
    if( !aObject.is_object() || !aObject.contains( aKey )
        || !aObject[aKey].is_string() )
    {
        return std::string();
    }

    return aObject[aKey].get<std::string>();
}


wxString jsonValueToWx( const nlohmann::json& aValue )
{
    if( aValue.is_string() )
        return fromUtf8String( aValue.get<std::string>() );

    if( aValue.is_null() )
        return wxEmptyString;

    if( aValue.is_boolean() )
        return aValue.get<bool>() ? wxS( "true" ) : wxS( "false" );

    return fromUtf8String( aValue.dump() );
}


bool parseNonNegativeIndex( std::string_view aText, int& aIndex )
{
    if( aText.empty() )
        return false;

    int parsed = 0;
    const std::from_chars_result result =
            std::from_chars( aText.data(), aText.data() + aText.size(), parsed );

    if( result.ec != std::errc() || result.ptr != aText.data() + aText.size()
        || parsed < 0 )
    {
        return false;
    }

    aIndex = parsed;
    return true;
}


bool indexFromStructuredId( const wxString& aId, char aPrefix, int aUpperBound,
                            int& aIndex )
{
    const std::string id = toUtf8String( aId );

    if( id.size() > 1 && id[0] == aPrefix )
    {
        if( parseNonNegativeIndex( std::string_view( id ).substr( 1 ),
                                   aIndex ) )
        {
            return aIndex >= 0 && aIndex < aUpperBound;
        }
    }

    if( parseNonNegativeIndex( id, aIndex ) )
        return aIndex >= 0 && aIndex < aUpperBound;

    return false;
}


bool resolveGridIndexByLabel( const wxString& aId, int aCount,
                              const std::function<wxString( int )>& aLabelAt,
                              int& aIndex )
{
    if( aId.IsEmpty() )
        return false;

    for( int index = 0; index < aCount; ++index )
    {
        if( aLabelAt( index ) == aId )
        {
            aIndex = index;
            return true;
        }
    }

    return false;
}


std::optional<AI_STRUCTURED_SURFACE_PREVIEW_TARGET> targetFromOverlay(
        uint64_t aPreviewId, const AI_PREVIEW_ITEM_OVERLAY& aOverlay )
{
    if( aOverlay.m_OverlayKind != wxS( "structured_surface_patch" ) )
        return std::nullopt;

    nlohmann::json entry = parseObject( aOverlay.m_GeometryJson );

    if( entry.empty() )
        return std::nullopt;

    const nlohmann::json& visual =
            entry.contains( "visual_target" )
            && entry["visual_target"].is_object()
                    ? entry["visual_target"]
                    : entry;

    const std::string targetKind = jsonStringOrEmpty( visual, "kind" );

    if( targetKind != "table_cell" && targetKind != "field" )
        return std::nullopt;

    AI_STRUCTURED_SURFACE_PREVIEW_TARGET target;
    target.m_PreviewId = aPreviewId;
    target.m_ItemLabel = aOverlay.m_ItemLabel;
    target.m_SurfaceId =
            fromUtf8String( jsonStringOrEmpty( visual, "surface_id" ) );
    target.m_TableId =
            fromUtf8String( jsonStringOrEmpty( visual, "table_id" ) );
    target.m_RowId = fromUtf8String( jsonStringOrEmpty( visual, "row_id" ) );
    target.m_ColumnId =
            fromUtf8String( jsonStringOrEmpty( visual, "column_id" ) );
    target.m_FieldId =
            fromUtf8String( jsonStringOrEmpty( visual, "field_id" ) );
    target.m_TargetPath =
            fromUtf8String( jsonStringOrEmpty( entry, "target_path" ) );
    target.m_Severity = aOverlay.m_Severity;
    target.m_Message = aOverlay.m_Message;

    if( target.m_TargetPath.IsEmpty() )
        target.m_TargetPath = aOverlay.m_ItemLabel;

    if( entry.contains( "previous_value" ) )
    {
        target.m_PreviousValue = jsonValueToWx( entry["previous_value"] );
        target.m_HasPreviousValue = true;
    }

    if( entry.contains( "proposed_value" ) )
        target.m_ProposedValue = jsonValueToWx( entry["proposed_value"] );
    else if( entry.contains( "value" ) )
        target.m_ProposedValue = jsonValueToWx( entry["value"] );

    if( entry.contains( "value_changed" )
        && entry["value_changed"].is_boolean() )
    {
        target.m_ValueChanged = entry["value_changed"].get<bool>();
    }
    else if( target.m_HasPreviousValue )
    {
        target.m_ValueChanged =
                target.m_PreviousValue != target.m_ProposedValue;
    }

    return target;
}
} // namespace


AI_STRUCTURED_SURFACE_PREVIEW_ADAPTER::AI_STRUCTURED_SURFACE_PREVIEW_ADAPTER(
        AI_STRUCTURED_SURFACE_PREVIEW_OVERLAY_IO& aOverlayIo,
        wxString aSurfaceId, wxString aTableId ) :
        m_OverlayIo( aOverlayIo ),
        m_SurfaceId( std::move( aSurfaceId ) ),
        m_TableId( std::move( aTableId ) )
{
}


void AI_STRUCTURED_SURFACE_PREVIEW_ADAPTER::BeginPreview(
        uint64_t aPreviewId )
{
    if( m_ActivePreviewId != 0 && m_ActivePreviewId != aPreviewId )
        m_OverlayIo.ClearPreview( m_ActivePreviewId );

    m_ActivePreviewId = aPreviewId;
    m_LastError.clear();
}


void AI_STRUCTURED_SURFACE_PREVIEW_ADAPTER::ShowObject(
        uint64_t aPreviewId, const AI_OBJECT_REF& aObject )
{
    wxUnusedVar( aPreviewId );
    wxUnusedVar( aObject );
}


bool AI_STRUCTURED_SURFACE_PREVIEW_ADAPTER::targetMatchesFilter(
        const AI_STRUCTURED_SURFACE_PREVIEW_TARGET& aTarget ) const
{
    if( !m_SurfaceId.IsEmpty() && aTarget.m_SurfaceId != m_SurfaceId )
        return false;

    if( !m_TableId.IsEmpty() && aTarget.m_TableId != m_TableId )
        return false;

    return true;
}


void AI_STRUCTURED_SURFACE_PREVIEW_ADAPTER::ShowOverlay(
        uint64_t aPreviewId, const AI_PREVIEW_ITEM_OVERLAY& aOverlay )
{
    if( aPreviewId != m_ActivePreviewId )
        return;

    std::optional<AI_STRUCTURED_SURFACE_PREVIEW_TARGET> target =
            targetFromOverlay( aPreviewId, aOverlay );

    if( !target || !targetMatchesFilter( *target ) )
    {
        ++m_IgnoredOverlayCount;
        return;
    }

    wxString error;
    bool     shown = false;

    if( !target->m_RowId.IsEmpty() && !target->m_ColumnId.IsEmpty() )
        shown = m_OverlayIo.ShowCellPreview( *target, error );
    else if( !target->m_FieldId.IsEmpty() )
        shown = m_OverlayIo.ShowFieldPreview( *target, error );
    else
        error = wxS( "Structured surface preview target is missing a cell or field id." );

    if( shown )
    {
        ++m_ShownTargetCount;
        m_LastError.clear();
        return;
    }

    ++m_FailedOverlayCount;
    m_LastError = error;
}


void AI_STRUCTURED_SURFACE_PREVIEW_ADAPTER::ClearPreview(
        uint64_t aPreviewId )
{
    if( aPreviewId != m_ActivePreviewId )
        return;

    m_OverlayIo.ClearPreview( aPreviewId );
    m_ActivePreviewId = 0;
}


AI_STRUCTURED_SURFACE_WX_GRID_PREVIEW_OVERLAY_IO::
        AI_STRUCTURED_SURFACE_WX_GRID_PREVIEW_OVERLAY_IO(
                wxGrid& aGrid, wxColour aPreviewBackground,
                wxColour aPreviewText ) :
        m_Grid( aGrid ),
        m_PreviewBackground( aPreviewBackground ),
        m_PreviewText( aPreviewText )
{
}


bool AI_STRUCTURED_SURFACE_WX_GRID_PREVIEW_OVERLAY_IO::resolveCell(
        const AI_STRUCTURED_SURFACE_PREVIEW_TARGET& aTarget, int& aRow,
        int& aColumn ) const
{
    if( !indexFromStructuredId( aTarget.m_RowId, 'r',
                                m_Grid.GetNumberRows(), aRow )
        && !resolveGridIndexByLabel(
                aTarget.m_RowId, m_Grid.GetNumberRows(),
                [&]( int aIndex )
                { return m_Grid.GetRowLabelValue( aIndex ); }, aRow ) )
    {
        return false;
    }

    if( !indexFromStructuredId( aTarget.m_ColumnId, 'c',
                                m_Grid.GetNumberCols(), aColumn )
        && !resolveGridIndexByLabel(
                aTarget.m_ColumnId, m_Grid.GetNumberCols(),
                [&]( int aIndex )
                { return m_Grid.GetColLabelValue( aIndex ); }, aColumn ) )
    {
        return false;
    }

    return true;
}


bool AI_STRUCTURED_SURFACE_WX_GRID_PREVIEW_OVERLAY_IO::ShowCellPreview(
        const AI_STRUCTURED_SURFACE_PREVIEW_TARGET& aTarget,
        wxString& aError )
{
    int row = -1;
    int column = -1;

    if( !resolveCell( aTarget, row, column ) )
    {
        aError = wxS( "Structured surface grid preview target does not map to a grid cell." );
        return false;
    }

    CELL_STYLE previous;
    previous.m_PreviewId = aTarget.m_PreviewId;
    previous.m_Row = row;
    previous.m_Column = column;
    previous.m_Background = m_Grid.GetCellBackgroundColour( row, column );
    previous.m_Text = m_Grid.GetCellTextColour( row, column );
    m_AppliedCells.push_back( previous );

    m_Grid.SetCellBackgroundColour( row, column, m_PreviewBackground );
    m_Grid.SetCellTextColour( row, column, m_PreviewText );
    m_Grid.ForceRefresh();

    aError.clear();
    return true;
}


bool AI_STRUCTURED_SURFACE_WX_GRID_PREVIEW_OVERLAY_IO::ShowFieldPreview(
        const AI_STRUCTURED_SURFACE_PREVIEW_TARGET& aTarget,
        wxString& aError )
{
    wxUnusedVar( aTarget );
    aError = wxS( "Grid structured surface preview cannot show a field target." );
    return false;
}


void AI_STRUCTURED_SURFACE_WX_GRID_PREVIEW_OVERLAY_IO::ClearPreview(
        uint64_t aPreviewId )
{
    bool restored = false;

    for( const CELL_STYLE& style : m_AppliedCells )
    {
        if( style.m_PreviewId != aPreviewId )
            continue;

        if( style.m_Row >= 0 && style.m_Row < m_Grid.GetNumberRows()
            && style.m_Column >= 0 && style.m_Column < m_Grid.GetNumberCols() )
        {
            m_Grid.SetCellBackgroundColour( style.m_Row, style.m_Column,
                                            style.m_Background );
            m_Grid.SetCellTextColour( style.m_Row, style.m_Column,
                                      style.m_Text );
            restored = true;
        }
    }

    m_AppliedCells.erase(
            std::remove_if( m_AppliedCells.begin(), m_AppliedCells.end(),
                            [&]( const CELL_STYLE& aStyle )
                            { return aStyle.m_PreviewId == aPreviewId; } ),
            m_AppliedCells.end() );

    if( restored )
        m_Grid.ForceRefresh();
}


AI_STRUCTURED_SURFACE_WX_PROPERTY_GRID_PREVIEW_OVERLAY_IO::
        AI_STRUCTURED_SURFACE_WX_PROPERTY_GRID_PREVIEW_OVERLAY_IO(
                wxPropertyGrid& aPropertyGrid, wxColour aPreviewBackground,
                wxColour aPreviewText ) :
        m_PropertyGrid( aPropertyGrid ),
        m_PreviewBackground( aPreviewBackground ),
        m_PreviewText( aPreviewText )
{
}


bool AI_STRUCTURED_SURFACE_WX_PROPERTY_GRID_PREVIEW_OVERLAY_IO::ShowCellPreview(
        const AI_STRUCTURED_SURFACE_PREVIEW_TARGET& aTarget,
        wxString& aError )
{
    wxUnusedVar( aTarget );
    aError = wxS( "Property-grid structured surface preview cannot show a table cell target." );
    return false;
}


bool AI_STRUCTURED_SURFACE_WX_PROPERTY_GRID_PREVIEW_OVERLAY_IO::ShowFieldPreview(
        const AI_STRUCTURED_SURFACE_PREVIEW_TARGET& aTarget,
        wxString& aError )
{
    if( aTarget.m_FieldId.IsEmpty()
        || !m_PropertyGrid.GetProperty( aTarget.m_FieldId ) )
    {
        aError = wxS( "Structured surface property preview target does not map to a property." );
        return false;
    }

    FIELD_STYLE previous;
    previous.m_PreviewId = aTarget.m_PreviewId;
    previous.m_FieldId = aTarget.m_FieldId;
    previous.m_Background =
            m_PropertyGrid.GetPropertyBackgroundColour( aTarget.m_FieldId );
    previous.m_Text =
            m_PropertyGrid.GetPropertyTextColour( aTarget.m_FieldId );
    m_AppliedFields.push_back( previous );

    m_PropertyGrid.SetPropertyBackgroundColour(
            aTarget.m_FieldId, m_PreviewBackground,
            wxPGPropertyValuesFlags::DontRecurse );
    m_PropertyGrid.SetPropertyTextColour(
            aTarget.m_FieldId, m_PreviewText,
            wxPGPropertyValuesFlags::DontRecurse );
    m_PropertyGrid.Refresh();

    aError.clear();
    return true;
}


void AI_STRUCTURED_SURFACE_WX_PROPERTY_GRID_PREVIEW_OVERLAY_IO::ClearPreview(
        uint64_t aPreviewId )
{
    bool restored = false;

    for( const FIELD_STYLE& style : m_AppliedFields )
    {
        if( style.m_PreviewId != aPreviewId
            || !m_PropertyGrid.GetProperty( style.m_FieldId ) )
        {
            continue;
        }

        m_PropertyGrid.SetPropertyBackgroundColour(
                style.m_FieldId, style.m_Background,
                wxPGPropertyValuesFlags::DontRecurse );
        m_PropertyGrid.SetPropertyTextColour(
                style.m_FieldId, style.m_Text,
                wxPGPropertyValuesFlags::DontRecurse );
        restored = true;
    }

    m_AppliedFields.erase(
            std::remove_if( m_AppliedFields.begin(), m_AppliedFields.end(),
                            [&]( const FIELD_STYLE& aStyle )
                            { return aStyle.m_PreviewId == aPreviewId; } ),
            m_AppliedFields.end() );

    if( restored )
        m_PropertyGrid.Refresh();
}
