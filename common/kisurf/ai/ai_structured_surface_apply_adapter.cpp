/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 */

#include <kisurf/ai/ai_structured_surface_apply_adapter.h>

#include <algorithm>
#include <nlohmann/json.hpp>
#include <wx/grid.h>
#include <wx/propgrid/propgrid.h>

#include <charconv>
#include <memory>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}


nlohmann::json objectFromJsonText( const wxString& aText )
{
    nlohmann::json parsed =
            nlohmann::json::parse( toUtf8String( aText ), nullptr, false );

    return parsed.is_object() ? parsed : nlohmann::json::object();
}


const nlohmann::json* patchOperations( const nlohmann::json& aPatch )
{
    for( const char* key : { "operations", "ops", "changes" } )
    {
        if( aPatch.contains( key ) && aPatch[key].is_array() )
            return &aPatch[key];
    }

    return nullptr;
}


std::string stringField( const nlohmann::json& aObject, const char* aKey )
{
    if( !aObject.is_object() || !aObject.contains( aKey )
        || !aObject[aKey].is_string() )
    {
        return std::string();
    }

    return aObject[aKey].get<std::string>();
}


std::string surfacePatchWritePolicy( const nlohmann::json& aArgs )
{
    std::string policy = stringField( aArgs, "write_policy" );

    if( !policy.empty() )
        return policy;

    policy = stringField( aArgs, "fill_policy" );

    if( !policy.empty() )
        return policy;

    if( aArgs.contains( "patch" ) && aArgs["patch"].is_object() )
    {
        policy = stringField( aArgs["patch"], "write_policy" );

        if( !policy.empty() )
            return policy;

        policy = stringField( aArgs["patch"], "fill_policy" );
    }

    return policy;
}


bool provenanceAllowsFillEmptyOnlyOverwrite( const std::string& aProvenance )
{
    return aProvenance == "project_default"
           || aProvenance == "inherited"
           || aProvenance == "deterministic_propagated"
           || aProvenance == "ai_accepted";
}


bool surfaceValueAllowsFillEmptyOnlyWrite( const nlohmann::json& aCurrentValue )
{
    if( aCurrentValue.is_null() )
        return true;

    if( aCurrentValue.is_string() )
        return aCurrentValue.get<std::string>().empty();

    if( aCurrentValue.is_object() )
    {
        const std::string provenance =
                stringField( aCurrentValue, "value_provenance" ).empty()
                        ? stringField( aCurrentValue, "provenance" )
                        : stringField( aCurrentValue, "value_provenance" );

        if( provenanceAllowsFillEmptyOnlyOverwrite( provenance ) )
            return true;

        if( aCurrentValue.contains( "value" ) )
            return surfaceValueAllowsFillEmptyOnlyWrite( aCurrentValue["value"] );
    }

    return false;
}


std::string provenanceFromMap( const nlohmann::json& aOwner,
                               const std::vector<std::string>& aKeys )
{
    for( const char* field : { "value_provenance", "provenance", "cell_provenance" } )
    {
        if( !aOwner.contains( field ) || !aOwner[field].is_object() )
            continue;

        const nlohmann::json& provenance = aOwner[field];

        for( const std::string& key : aKeys )
        {
            if( provenance.contains( key ) && provenance[key].is_string() )
                return provenance[key].get<std::string>();
        }
    }

    return std::string();
}


std::string surfaceFieldProvenance( const nlohmann::json& aSurface,
                                    const std::string& aFieldId )
{
    return provenanceFromMap( aSurface, { aFieldId } );
}


std::string surfaceCellProvenance( const nlohmann::json& aSurface,
                                   const std::string& aTableId,
                                   const std::string& aRowId,
                                   const std::string& aColumnId )
{
    const std::vector<std::string> keys = {
        aRowId + "." + aColumnId,
        aRowId + ":" + aColumnId,
        aColumnId
    };

    if( aSurface.contains( "tables" ) && aSurface["tables"].is_object()
        && aSurface["tables"].contains( aTableId )
        && aSurface["tables"][aTableId].is_object() )
    {
        const nlohmann::json& table = aSurface["tables"][aTableId];

        if( table.contains( "rows" ) && table["rows"].is_object()
            && table["rows"].contains( aRowId )
            && table["rows"][aRowId].is_object() )
        {
            const std::string rowProvenance =
                    provenanceFromMap( table["rows"][aRowId], keys );

            if( !rowProvenance.empty() )
                return rowProvenance;
        }

        const std::string tableProvenance = provenanceFromMap( table, keys );

        if( !tableProvenance.empty() )
            return tableProvenance;
    }

    return provenanceFromMap( aSurface, keys );
}


nlohmann::json valueWithFallbackProvenance( const nlohmann::json& aCurrentValue,
                                            const std::string& aProvenance )
{
    if( aProvenance.empty() || !provenanceAllowsFillEmptyOnlyOverwrite( aProvenance ) )
        return aCurrentValue;

    if( aCurrentValue.is_object()
        && ( !stringField( aCurrentValue, "value_provenance" ).empty()
             || !stringField( aCurrentValue, "provenance" ).empty() ) )
    {
        return aCurrentValue;
    }

    return nlohmann::json{
        { "value", aCurrentValue },
        { "value_provenance", aProvenance }
    };
}


const nlohmann::json* currentSurfaceCellValue(
        const nlohmann::json& aSurface,
        const std::string& aTableId,
        const std::string& aRowId,
        const std::string& aColumnId )
{
    if( !aSurface.contains( "tables" ) || !aSurface["tables"].is_object()
        || !aSurface["tables"].contains( aTableId )
        || !aSurface["tables"][aTableId].is_object() )
    {
        return nullptr;
    }

    const nlohmann::json& table = aSurface["tables"][aTableId];

    if( !table.contains( "rows" ) || !table["rows"].is_object()
        || !table["rows"].contains( aRowId )
        || !table["rows"][aRowId].is_object() )
    {
        return nullptr;
    }

    const nlohmann::json& row = table["rows"][aRowId];

    if( !row.contains( "cells" ) || !row["cells"].is_object()
        || !row["cells"].contains( aColumnId ) )
    {
        return nullptr;
    }

    return &row["cells"][aColumnId];
}


const nlohmann::json* currentSurfaceFieldValue(
        const nlohmann::json& aSurface,
        const std::string& aFieldId )
{
    if( !aSurface.contains( "fields" ) || !aSurface["fields"].is_object()
        || !aSurface["fields"].contains( aFieldId ) )
    {
        return nullptr;
    }

    return &aSurface["fields"][aFieldId];
}


bool scalarValuesEqual( const nlohmann::json& aExpected,
                        const nlohmann::json& aActual )
{
    if( aExpected.type() == aActual.type() )
        return aExpected == aActual;

    if( aExpected.is_number() && aActual.is_number() )
        return aExpected.get<double>() == aActual.get<double>();

    return false;
}


std::vector<std::string> sortedArrayValueKeys( const nlohmann::json& aArray )
{
    std::vector<std::string> keys;

    for( const nlohmann::json& value : aArray )
        keys.push_back( value.dump() );

    std::sort( keys.begin(), keys.end() );
    return keys;
}


bool jsonValuesEqual( const nlohmann::json& aExpected,
                      const nlohmann::json& aActual )
{
    if( scalarValuesEqual( aExpected, aActual ) )
        return true;

    if( aExpected.is_array() && aActual.is_array() )
        return sortedArrayValueKeys( aExpected ) == sortedArrayValueKeys( aActual );

    return aExpected == aActual;
}


std::vector<std::pair<int, int>> normalizedSelectedCells(
        const std::vector<std::pair<int, int>>& aCells,
        int aRowCount, int aColumnCount )
{
    std::set<std::pair<int, int>> uniqueCells;

    for( const std::pair<int, int>& cell : aCells )
    {
        if( cell.first >= 0 && cell.first < aRowCount
            && cell.second >= 0 && cell.second < aColumnCount )
        {
            uniqueCells.insert( cell );
        }
    }

    return { uniqueCells.begin(), uniqueCells.end() };
}


wxString gridSelectionFingerprint(
        const std::vector<std::pair<int, int>>& aCells )
{
    if( aCells.empty() )
        return wxEmptyString;

    wxArrayString cellIds;

    for( const std::pair<int, int>& cell : aCells )
    {
        cellIds.Add( wxString::Format( wxS( "r%d:c%d" ), cell.first,
                                       cell.second ) );
    }

    if( cellIds.GetCount() == 1 )
        return wxS( "cell:" ) + cellIds[0];

    return wxS( "cells:" ) + wxJoin( cellIds, '|' );
}


std::vector<wxString> gridOverlapSet(
        const std::vector<std::pair<int, int>>& aCells )
{
    std::set<int> rows;

    for( const std::pair<int, int>& cell : aCells )
        rows.insert( cell.first );

    std::vector<wxString> overlap;

    for( int row : rows )
        overlap.push_back( wxString::Format( wxS( "r%d" ), row ) );

    return overlap;
}


void addOverlapSetJson( nlohmann::json& aSurface,
                        const std::vector<wxString>& aOverlapSet )
{
    if( aOverlapSet.empty() )
        return;

    nlohmann::json overlap = nlohmann::json::array();

    for( const wxString& item : aOverlapSet )
        overlap.push_back( toUtf8String( item ) );

    aSurface["overlap_set"] = overlap;
}


const nlohmann::json* surfaceRevisionField( const nlohmann::json& aSurface )
{
    for( const char* key : { "revision", "surface_revision" } )
    {
        if( aSurface.contains( key )
            && ( aSurface[key].is_string() || aSurface[key].is_number() ) )
        {
            return &aSurface[key];
        }
    }

    return nullptr;
}


void advanceNumericSurfaceRevision( nlohmann::json& aSurface )
{
    for( const char* key : { "revision", "surface_revision" } )
    {
        if( !aSurface.contains( key ) )
            continue;

        if( aSurface[key].is_number_unsigned() )
        {
            aSurface[key] = aSurface[key].get<uint64_t>() + 1;
            continue;
        }

        if( aSurface[key].is_number_integer() )
            aSurface[key] = aSurface[key].get<int64_t>() + 1;
    }
}


const nlohmann::json* jsonMetadataField( const nlohmann::json& aObject,
                                         const char* aKey )
{
    if( aObject.contains( aKey ) )
        return &aObject[aKey];

    return nullptr;
}


const nlohmann::json* scalarMetadataField( const nlohmann::json& aObject,
                                           const char* aKey )
{
    if( aObject.contains( aKey )
        && ( aObject[aKey].is_string() || aObject[aKey].is_number() ) )
    {
        return &aObject[aKey];
    }

    return nullptr;
}


const nlohmann::json* schemaVersionField( const nlohmann::json& aSurface,
                                          const nlohmann::json& aRoot )
{
    for( const char* key : { "schema_version", "schemaVersion" } )
    {
        if( const nlohmann::json* value = scalarMetadataField( aSurface, key ) )
            return value;
    }

    for( const char* key : { "schema_version", "schemaVersion" } )
    {
        if( const nlohmann::json* value = scalarMetadataField( aRoot, key ) )
            return value;
    }

    return nullptr;
}


const nlohmann::json* selectionFingerprintField( const nlohmann::json& aSurface,
                                                 const nlohmann::json& aRoot )
{
    for( const char* key : { "selection_fingerprint", "selectionFingerprint" } )
    {
        if( const nlohmann::json* value = scalarMetadataField( aSurface, key ) )
            return value;
    }

    for( const char* key : { "selection_fingerprint", "selectionFingerprint" } )
    {
        if( const nlohmann::json* value = scalarMetadataField( aRoot, key ) )
            return value;
    }

    return nullptr;
}


const nlohmann::json* overlapSetField( const nlohmann::json& aSurface,
                                       const nlohmann::json& aRoot )
{
    for( const char* key : { "overlap_set", "overlapSet" } )
    {
        if( const nlohmann::json* value = jsonMetadataField( aSurface, key ) )
            return value;
    }

    for( const char* key : { "overlap_set", "overlapSet" } )
    {
        if( const nlohmann::json* value = jsonMetadataField( aRoot, key ) )
            return value;
    }

    return nullptr;
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


bool indexFromStructuredId( const std::string& aId, char aPrefix, int& aIndex )
{
    if( aId.size() > 1 && aId[0] == aPrefix )
        return parseNonNegativeIndex( std::string_view( aId ).substr( 1 ), aIndex );

    return parseNonNegativeIndex( aId, aIndex );
}


bool indexFromStructuredObject( const std::string& aId,
                                const nlohmann::json& aObject,
                                char aPrefix,
                                int aUpperBound,
                                int& aIndex )
{
    if( aObject.is_object() && aObject.contains( "index" )
        && aObject["index"].is_number_integer() )
    {
        const int index = aObject["index"].get<int>();

        if( index >= 0 && index < aUpperBound )
        {
            aIndex = index;
            return true;
        }

        return false;
    }

    if( !indexFromStructuredId( aId, aPrefix, aIndex ) )
        return false;

    return aIndex >= 0 && aIndex < aUpperBound;
}


nlohmann::json jsonStringFromWx( const wxString& aText )
{
    return toUtf8String( aText );
}


wxString jsonCellValueToWx( const nlohmann::json& aValue )
{
    if( aValue.is_string() )
        return wxString::FromUTF8( aValue.get<std::string>().c_str() );

    if( aValue.is_null() )
        return wxEmptyString;

    if( aValue.is_boolean() )
        return aValue.get<bool>() ? wxS( "1" ) : wxS( "0" );

    const std::string dumped = aValue.dump();
    return wxString::FromUTF8( dumped.c_str() );
}


bool mergeStructuredSurfaceState( nlohmann::json& aTarget,
                                  const nlohmann::json& aSource,
                                  wxString& aError )
{
    if( !aSource.is_object() )
        return true;

    for( auto it = aSource.begin(); it != aSource.end(); ++it )
    {
        if( it.key() == "surfaces" )
        {
            if( !it.value().is_object() )
            {
                aError = wxS( "Composite structured surface child has invalid surfaces." );
                return false;
            }

            for( auto surfaceIt = it.value().begin();
                 surfaceIt != it.value().end(); ++surfaceIt )
            {
                if( aTarget["surfaces"].contains( surfaceIt.key() ) )
                {
                    aError = wxS( "Composite structured surface child duplicates surface_id." );
                    return false;
                }

                aTarget["surfaces"][surfaceIt.key()] = surfaceIt.value();
            }

            continue;
        }

        if( aTarget.contains( it.key() ) && aTarget[it.key()] != it.value() )
        {
            aError = wxS( "Composite structured surface child has conflicting metadata." );
            return false;
        }

        aTarget[it.key()] = it.value();
    }

    return true;
}


bool collectStructuredSurfaceIds( const nlohmann::json& aState,
                                  std::vector<std::string>& aSurfaceIds,
                                  wxString& aError )
{
    aSurfaceIds.clear();

    if( !aState.is_object() || !aState.contains( "surfaces" ) )
        return true;

    if( !aState["surfaces"].is_object() )
    {
        aError = wxS( "Structured surface state has invalid surfaces." );
        return false;
    }

    for( auto surfaceIt = aState["surfaces"].begin();
         surfaceIt != aState["surfaces"].end(); ++surfaceIt )
    {
        aSurfaceIds.push_back( surfaceIt.key() );
    }

    return true;
}


bool ownsStructuredSurfaceId(
        const std::vector<std::vector<std::string>>& aBackendSurfaceIds,
        const std::string& aSurfaceId )
{
    for( const std::vector<std::string>& childSurfaceIds : aBackendSurfaceIds )
    {
        if( std::find( childSurfaceIds.begin(), childSurfaceIds.end(),
                       aSurfaceId ) != childSurfaceIds.end() )
        {
            return true;
        }
    }

    return false;
}


nlohmann::json scopedStructuredSurfaceState(
        const nlohmann::json& aState,
        const std::vector<std::string>& aSurfaceIds )
{
    nlohmann::json scopedState = aState;
    scopedState["surfaces"] = nlohmann::json::object();

    if( !aState.contains( "surfaces" ) || !aState["surfaces"].is_object() )
        return scopedState;

    for( const std::string& surfaceId : aSurfaceIds )
    {
        if( aState["surfaces"].contains( surfaceId ) )
            scopedState["surfaces"][surfaceId] = aState["surfaces"][surfaceId];
    }

    return scopedState;
}


class WX_GRID_STRUCTURED_SURFACE_IO : public AI_STRUCTURED_SURFACE_GRID_IO
{
public:
    explicit WX_GRID_STRUCTURED_SURFACE_IO( wxGrid& aGrid ) :
            m_Grid( aGrid )
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

    void SetCellValue( int aRow, int aColumn,
                       const wxString& aValue ) override
    {
        m_Grid.SetCellValue( aRow, aColumn, aValue );
    }

    bool IsCellEditControlShown() const override
    {
        return m_Grid.IsCellEditControlShown();
    }

    void SaveEditControlValue() override
    {
        m_Grid.SaveEditControlValue();
    }

    std::vector<std::pair<int, int>> SelectedCells() const override
    {
        std::vector<std::pair<int, int>> cells;

        const wxGridCellCoordsArray selectedCells = m_Grid.GetSelectedCells();

        for( const wxGridCellCoords& cell : selectedCells )
            cells.emplace_back( cell.GetRow(), cell.GetCol() );

        const wxGridCellCoordsArray topLeft =
                m_Grid.GetSelectionBlockTopLeft();
        const wxGridCellCoordsArray bottomRight =
                m_Grid.GetSelectionBlockBottomRight();

        for( size_t i = 0; i < topLeft.size() && i < bottomRight.size(); ++i )
        {
            for( int row = topLeft[i].GetRow(); row <= bottomRight[i].GetRow(); ++row )
            {
                for( int col = topLeft[i].GetCol(); col <= bottomRight[i].GetCol(); ++col )
                    cells.emplace_back( row, col );
            }
        }

        const wxArrayInt selectedRows = m_Grid.GetSelectedRows();

        for( unsigned int rowIndex = 0; rowIndex < selectedRows.GetCount(); ++rowIndex )
        {
            const int row = selectedRows[rowIndex];

            for( int col = 0; col < m_Grid.GetNumberCols(); ++col )
                cells.emplace_back( row, col );
        }

        const wxArrayInt selectedCols = m_Grid.GetSelectedCols();

        for( unsigned int colIndex = 0; colIndex < selectedCols.GetCount(); ++colIndex )
        {
            const int col = selectedCols[colIndex];

            for( int row = 0; row < m_Grid.GetNumberRows(); ++row )
                cells.emplace_back( row, col );
        }

        if( cells.empty() && m_Grid.GetGridCursorRow() >= 0
            && m_Grid.GetGridCursorCol() >= 0 )
        {
            cells.emplace_back( m_Grid.GetGridCursorRow(),
                                m_Grid.GetGridCursorCol() );
        }

        return normalizedSelectedCells( cells, RowCount(), ColumnCount() );
    }

private:
    wxGrid& m_Grid;
};


std::unique_ptr<AI_STRUCTURED_SURFACE_GRID_IO> makeWxGridSurfaceIo(
        wxGrid& aGrid )
{
    return std::make_unique<WX_GRID_STRUCTURED_SURFACE_IO>( aGrid );
}


class WX_PROPERTY_GRID_STRUCTURED_SURFACE_IO :
        public AI_STRUCTURED_SURFACE_FIELD_IO
{
public:
    explicit WX_PROPERTY_GRID_STRUCTURED_SURFACE_IO(
            wxPropertyGrid& aPropertyGrid ) :
            m_PropertyGrid( aPropertyGrid )
    {
    }

    wxArrayString FieldIds() const override
    {
        wxArrayString ids;

        for( wxPropertyGridIterator it = m_PropertyGrid.GetIterator();
             !it.AtEnd(); ++it )
        {
            wxPGProperty* property = *it;

            if( property && !property->GetName().empty() )
                ids.Add( property->GetName() );
        }

        return ids;
    }

    wxString FieldValue( const wxString& aFieldId ) const override
    {
        wxPGProperty* property = m_PropertyGrid.GetProperty( aFieldId );

        if( !property )
            return wxEmptyString;

        return property->GetValueAsString();
    }

    void SetFieldValue( const wxString& aFieldId,
                        const wxString& aValue ) override
    {
        m_PropertyGrid.SetPropertyValue( aFieldId, aValue );
    }

    bool HasField( const wxString& aFieldId ) const override
    {
        return m_PropertyGrid.GetProperty( aFieldId ) != nullptr;
    }

    wxString FocusedFieldId() const override
    {
        wxPGProperty* property = m_PropertyGrid.GetSelection();

        if( !property )
            return wxEmptyString;

        return property->GetName();
    }

private:
    wxPropertyGrid& m_PropertyGrid;
};


std::unique_ptr<AI_STRUCTURED_SURFACE_FIELD_IO> makeWxPropertyGridSurfaceIo(
        wxPropertyGrid& aPropertyGrid )
{
    return std::make_unique<WX_PROPERTY_GRID_STRUCTURED_SURFACE_IO>(
            aPropertyGrid );
}
}


AI_STRUCTURED_SURFACE_STRING_STATE_BACKEND::
        AI_STRUCTURED_SURFACE_STRING_STATE_BACKEND( wxString& aSurfaceStateJson ) :
        m_SurfaceStateJson( aSurfaceStateJson )
{
}


bool AI_STRUCTURED_SURFACE_STRING_STATE_BACKEND::BeginSurfaceTransaction(
        const AI_EXECUTION_SESSION& aSession, wxString& aSurfaceStateJson,
        wxString& aError )
{
    wxUnusedVar( aSession );
    aSurfaceStateJson = m_SurfaceStateJson;
    aError.clear();
    return true;
}


bool AI_STRUCTURED_SURFACE_STRING_STATE_BACKEND::CommitSurfaceTransaction(
        const wxString& aSurfaceStateJson, bool aChanged, wxString& aError )
{
    wxUnusedVar( aChanged );
    m_SurfaceStateJson = aSurfaceStateJson;
    aError.clear();
    return true;
}


AI_STRUCTURED_SURFACE_GRID_STATE_BACKEND::AI_STRUCTURED_SURFACE_GRID_STATE_BACKEND(
        std::unique_ptr<AI_STRUCTURED_SURFACE_GRID_IO> aGridIo,
        const wxString& aSurfaceId, const wxString& aTableId ) :
        m_GridIo( std::move( aGridIo ) ),
        m_SurfaceId( aSurfaceId ),
        m_TableId( aTableId )
{
}


void AI_STRUCTURED_SURFACE_GRID_STATE_BACKEND::SetSurfaceRevision(
        uint64_t aRevision )
{
    m_SurfaceRevision = aRevision;
}


uint64_t AI_STRUCTURED_SURFACE_GRID_STATE_BACKEND::SurfaceRevision() const
{
    return m_SurfaceRevision;
}


void AI_STRUCTURED_SURFACE_GRID_STATE_BACKEND::SetSchemaVersion(
        const wxString& aSchemaVersion )
{
    m_SchemaVersion = aSchemaVersion;
}


void AI_STRUCTURED_SURFACE_GRID_STATE_BACKEND::SetSelectionFingerprint(
        const wxString& aSelectionFingerprint )
{
    m_SelectionFingerprint = aSelectionFingerprint;
}


void AI_STRUCTURED_SURFACE_GRID_STATE_BACKEND::SetOverlapSet(
        const std::vector<wxString>& aOverlapSet )
{
    m_OverlapSet = aOverlapSet;
}


bool AI_STRUCTURED_SURFACE_GRID_STATE_BACKEND::BeginSurfaceTransaction(
        const AI_EXECUTION_SESSION& aSession, wxString& aSurfaceStateJson,
        wxString& aError )
{
    wxUnusedVar( aSession );

    if( !m_GridIo || m_SurfaceId.empty() || m_TableId.empty() )
    {
        aError = wxS( "Grid structured surface backend requires grid_io, surface_id, and table_id." );
        return false;
    }

    if( m_GridIo->IsCellEditControlShown() )
        m_GridIo->SaveEditControlValue();

    const std::vector<std::pair<int, int>> selectedCells =
            normalizedSelectedCells( m_GridIo->SelectedCells(),
                                     m_GridIo->RowCount(),
                                     m_GridIo->ColumnCount() );

    nlohmann::json state = nlohmann::json::object();
    nlohmann::json& surface = state["surfaces"][toUtf8String( m_SurfaceId )];
    surface["kind"] = "grid";
    surface["revision"] = m_SurfaceRevision;

    if( !m_SchemaVersion.empty() )
        surface["schema_version"] = toUtf8String( m_SchemaVersion );

    if( !m_SelectionFingerprint.empty() )
    {
        surface["selection_fingerprint"] =
                toUtf8String( m_SelectionFingerprint );
    }
    else
    {
        const wxString fingerprint = gridSelectionFingerprint( selectedCells );

        if( !fingerprint.empty() )
            surface["selection_fingerprint"] = toUtf8String( fingerprint );
    }

    if( !m_OverlapSet.empty() )
        addOverlapSetJson( surface, m_OverlapSet );
    else
        addOverlapSetJson( surface, gridOverlapSet( selectedCells ) );

    nlohmann::json& table = surface["tables"][toUtf8String( m_TableId )];
    table["kind"] = "grid";

    for( int col = 0; col < m_GridIo->ColumnCount(); ++col )
    {
        const std::string colId = "c" + std::to_string( col );
        table["column_order"].push_back( colId );
        table["columns"][colId]["index"] = col;
        table["columns"][colId]["label"] =
                jsonStringFromWx( m_GridIo->ColumnLabel( col ) );
    }

    for( int row = 0; row < m_GridIo->RowCount(); ++row )
    {
        const std::string rowId = "r" + std::to_string( row );
        table["row_order"].push_back( rowId );
        nlohmann::json& rowJson = table["rows"][rowId];
        rowJson["index"] = row;
        rowJson["label"] = jsonStringFromWx( m_GridIo->RowLabel( row ) );

        for( int col = 0; col < m_GridIo->ColumnCount(); ++col )
        {
            const std::string colId = "c" + std::to_string( col );
            rowJson["cells"][colId] =
                    jsonStringFromWx( m_GridIo->CellValue( row, col ) );
        }
    }

    const std::string dumpedState = state.dump();
    aSurfaceStateJson = wxString::FromUTF8( dumpedState.c_str() );
    aError.clear();
    m_InTransaction = true;
    return true;
}


bool AI_STRUCTURED_SURFACE_GRID_STATE_BACKEND::CommitSurfaceTransaction(
        const wxString& aSurfaceStateJson, bool aChanged, wxString& aError )
{
    if( !m_InTransaction )
    {
        aError = wxS( "Grid structured surface transaction has not started." );
        return false;
    }

    if( !aChanged )
    {
        m_InTransaction = false;
        aError.clear();
        return true;
    }

    nlohmann::json state = objectFromJsonText( aSurfaceStateJson );
    const std::string surfaceId = toUtf8String( m_SurfaceId );
    const std::string tableId = toUtf8String( m_TableId );

    if( !state.contains( "surfaces" ) || !state["surfaces"].is_object()
        || !state["surfaces"].contains( surfaceId )
        || !state["surfaces"][surfaceId].is_object() )
    {
        aError = wxS( "Grid structured surface commit missing surface state." );
        return false;
    }

    nlohmann::json& surface = state["surfaces"][surfaceId];

    if( !surface.contains( "tables" ) || !surface["tables"].is_object()
        || !surface["tables"].contains( tableId )
        || !surface["tables"][tableId].is_object() )
    {
        aError = wxS( "Grid structured surface commit missing table state." );
        return false;
    }

    const nlohmann::json& table = surface["tables"][tableId];

    if( !table.contains( "rows" ) || !table["rows"].is_object() )
    {
        aError = wxS( "Grid structured surface commit missing rows." );
        return false;
    }

    struct CELL_CHANGE
    {
        int      m_Row = 0;
        int      m_Col = 0;
        wxString m_Value;
    };

    std::vector<CELL_CHANGE> changes;

    for( auto rowIt = table["rows"].begin(); rowIt != table["rows"].end();
         ++rowIt )
    {
        int row = -1;

        if( !indexFromStructuredObject( rowIt.key(), rowIt.value(), 'r',
                                        m_GridIo->RowCount(), row ) )
        {
            aError = wxS( "Grid structured surface commit has an invalid row." );
            return false;
        }

        if( !rowIt.value().is_object()
            || !rowIt.value().contains( "cells" )
            || !rowIt.value()["cells"].is_object() )
        {
            continue;
        }

        const nlohmann::json& cells = rowIt.value()["cells"];

        for( auto cellIt = cells.begin(); cellIt != cells.end(); ++cellIt )
        {
            int col = -1;

            const nlohmann::json* colMetadata = nullptr;

            if( table.contains( "columns" ) && table["columns"].is_object()
                && table["columns"].contains( cellIt.key() )
                && table["columns"][cellIt.key()].is_object() )
            {
                colMetadata = &table["columns"][cellIt.key()];
            }

            const nlohmann::json emptyObject = nlohmann::json::object();
            const nlohmann::json& colObject =
                    colMetadata ? *colMetadata : emptyObject;

            if( !indexFromStructuredObject( cellIt.key(), colObject, 'c',
                                            m_GridIo->ColumnCount(), col ) )
            {
                aError = wxS( "Grid structured surface commit has an invalid column." );
                return false;
            }

            changes.push_back( { row, col, jsonCellValueToWx( cellIt.value() ) } );
        }
    }

    for( const CELL_CHANGE& change : changes )
    {
        if( m_GridIo->CellValue( change.m_Row, change.m_Col )
            != change.m_Value )
        {
            m_GridIo->SetCellValue( change.m_Row, change.m_Col,
                                    change.m_Value );
        }
    }

    if( const nlohmann::json* revision = surfaceRevisionField( surface ) )
    {
        if( revision->is_number_unsigned() )
            m_SurfaceRevision = revision->get<uint64_t>();
        else if( revision->is_number_integer() && revision->get<int64_t>() >= 0 )
            m_SurfaceRevision = static_cast<uint64_t>( revision->get<int64_t>() );
    }

    m_InTransaction = false;
    aError.clear();
    return true;
}


void AI_STRUCTURED_SURFACE_GRID_STATE_BACKEND::AbortSurfaceTransaction()
{
    m_InTransaction = false;
}


AI_STRUCTURED_SURFACE_WX_GRID_BACKEND::AI_STRUCTURED_SURFACE_WX_GRID_BACKEND(
        wxGrid& aGrid, const wxString& aSurfaceId, const wxString& aTableId ) :
        AI_STRUCTURED_SURFACE_GRID_STATE_BACKEND(
                makeWxGridSurfaceIo( aGrid ), aSurfaceId, aTableId )
{
}


bool AI_STRUCTURED_SURFACE_FIELD_IO::HasField(
        const wxString& aFieldId ) const
{
    const wxArrayString fieldIds = FieldIds();

    for( unsigned int i = 0; i < fieldIds.GetCount(); ++i )
    {
        if( fieldIds[i] == aFieldId )
            return true;
    }

    return false;
}


AI_STRUCTURED_SURFACE_FIELD_STATE_BACKEND::
        AI_STRUCTURED_SURFACE_FIELD_STATE_BACKEND(
                std::unique_ptr<AI_STRUCTURED_SURFACE_FIELD_IO> aFieldIo,
                const wxString& aSurfaceId ) :
        m_FieldIo( std::move( aFieldIo ) ),
        m_SurfaceId( aSurfaceId )
{
}


void AI_STRUCTURED_SURFACE_FIELD_STATE_BACKEND::SetSurfaceRevision(
        uint64_t aRevision )
{
    m_SurfaceRevision = aRevision;
}


uint64_t AI_STRUCTURED_SURFACE_FIELD_STATE_BACKEND::SurfaceRevision() const
{
    return m_SurfaceRevision;
}


void AI_STRUCTURED_SURFACE_FIELD_STATE_BACKEND::SetSchemaVersion(
        const wxString& aSchemaVersion )
{
    m_SchemaVersion = aSchemaVersion;
}


void AI_STRUCTURED_SURFACE_FIELD_STATE_BACKEND::SetSelectionFingerprint(
        const wxString& aSelectionFingerprint )
{
    m_SelectionFingerprint = aSelectionFingerprint;
}


void AI_STRUCTURED_SURFACE_FIELD_STATE_BACKEND::SetOverlapSet(
        const std::vector<wxString>& aOverlapSet )
{
    m_OverlapSet = aOverlapSet;
}


bool AI_STRUCTURED_SURFACE_FIELD_STATE_BACKEND::BeginSurfaceTransaction(
        const AI_EXECUTION_SESSION& aSession, wxString& aSurfaceStateJson,
        wxString& aError )
{
    wxUnusedVar( aSession );

    if( !m_FieldIo || m_SurfaceId.empty() )
    {
        aError = wxS( "Field structured surface backend requires field_io and surface_id." );
        return false;
    }

    if( m_FieldIo->IsFieldEditControlShown() )
        m_FieldIo->SaveFieldEditControlValue();

    nlohmann::json state = nlohmann::json::object();
    nlohmann::json& surface = state["surfaces"][toUtf8String( m_SurfaceId )];
    surface["kind"] = "fields";
    surface["revision"] = m_SurfaceRevision;

    if( !m_SchemaVersion.empty() )
        surface["schema_version"] = toUtf8String( m_SchemaVersion );

    if( !m_SelectionFingerprint.empty() )
    {
        surface["selection_fingerprint"] =
                toUtf8String( m_SelectionFingerprint );
    }
    else if( !m_FieldIo->FocusedFieldId().empty() )
    {
        surface["selection_fingerprint"] =
                toUtf8String( wxS( "field:" ) + m_FieldIo->FocusedFieldId() );
    }

    if( !m_OverlapSet.empty() )
        addOverlapSetJson( surface, m_OverlapSet );
    else if( !m_FieldIo->FocusedFieldId().empty() )
        addOverlapSetJson( surface, { m_FieldIo->FocusedFieldId() } );

    const wxArrayString fieldIds = m_FieldIo->FieldIds();

    for( unsigned int i = 0; i < fieldIds.GetCount(); ++i )
    {
        const wxString& fieldId = fieldIds[i];
        const std::string fieldIdUtf8 = toUtf8String( fieldId );

        surface["field_order"].push_back( fieldIdUtf8 );
        surface["schema"]["fields"].push_back(
                { { "id", fieldIdUtf8 }, { "index", i } } );
        surface["fields"][fieldIdUtf8] =
                jsonStringFromWx( m_FieldIo->FieldValue( fieldId ) );
    }

    const std::string dumpedState = state.dump();
    aSurfaceStateJson = wxString::FromUTF8( dumpedState.c_str() );
    aError.clear();
    m_InTransaction = true;
    return true;
}


bool AI_STRUCTURED_SURFACE_FIELD_STATE_BACKEND::CommitSurfaceTransaction(
        const wxString& aSurfaceStateJson, bool aChanged, wxString& aError )
{
    if( !m_InTransaction )
    {
        aError = wxS( "Field structured surface transaction has not started." );
        return false;
    }

    if( !aChanged )
    {
        m_InTransaction = false;
        aError.clear();
        return true;
    }

    nlohmann::json state = objectFromJsonText( aSurfaceStateJson );
    const std::string surfaceId = toUtf8String( m_SurfaceId );

    if( !state.contains( "surfaces" ) || !state["surfaces"].is_object()
        || !state["surfaces"].contains( surfaceId )
        || !state["surfaces"][surfaceId].is_object() )
    {
        aError = wxS( "Field structured surface commit missing surface state." );
        return false;
    }

    nlohmann::json& surface = state["surfaces"][surfaceId];

    if( !surface.contains( "fields" ) || !surface["fields"].is_object() )
    {
        aError = wxS( "Field structured surface commit missing fields." );
        return false;
    }

    struct FIELD_CHANGE
    {
        wxString m_FieldId;
        wxString m_Value;
    };

    std::vector<FIELD_CHANGE> changes;

    for( auto fieldIt = surface["fields"].begin();
         fieldIt != surface["fields"].end(); ++fieldIt )
    {
        const wxString fieldId = wxString::FromUTF8( fieldIt.key().c_str() );

        if( !m_FieldIo->HasField( fieldId ) )
        {
            aError = wxS( "Field structured surface commit has an invalid field." );
            return false;
        }

        changes.push_back( { fieldId, jsonCellValueToWx( fieldIt.value() ) } );
    }

    for( const FIELD_CHANGE& change : changes )
    {
        if( m_FieldIo->FieldValue( change.m_FieldId ) != change.m_Value )
            m_FieldIo->SetFieldValue( change.m_FieldId, change.m_Value );
    }

    if( const nlohmann::json* revision = surfaceRevisionField( surface ) )
    {
        if( revision->is_number_unsigned() )
            m_SurfaceRevision = revision->get<uint64_t>();
        else if( revision->is_number_integer() && revision->get<int64_t>() >= 0 )
            m_SurfaceRevision = static_cast<uint64_t>( revision->get<int64_t>() );
    }

    m_InTransaction = false;
    aError.clear();
    return true;
}


void AI_STRUCTURED_SURFACE_FIELD_STATE_BACKEND::AbortSurfaceTransaction()
{
    m_InTransaction = false;
}


AI_STRUCTURED_SURFACE_WX_PROPERTY_GRID_BACKEND::
        AI_STRUCTURED_SURFACE_WX_PROPERTY_GRID_BACKEND(
                wxPropertyGrid& aPropertyGrid,
                const wxString& aSurfaceId ) :
        AI_STRUCTURED_SURFACE_FIELD_STATE_BACKEND(
                makeWxPropertyGridSurfaceIo( aPropertyGrid ), aSurfaceId )
{
}


void AI_STRUCTURED_SURFACE_COMPOSITE_STATE_BACKEND::AddBackend(
        std::unique_ptr<AI_STRUCTURED_SURFACE_STATE_BACKEND> aBackend )
{
    if( aBackend )
        m_Backends.push_back( std::move( aBackend ) );
}


bool AI_STRUCTURED_SURFACE_COMPOSITE_STATE_BACKEND::BeginSurfaceTransaction(
        const AI_EXECUTION_SESSION& aSession, wxString& aSurfaceStateJson,
        wxString& aError )
{
    if( m_Backends.empty() )
    {
        aError = wxS( "Composite structured surface backend has no children." );
        return false;
    }

    nlohmann::json mergedState = nlohmann::json::object();
    m_BegunCount = 0;
    m_BackendSurfaceIds.clear();

    for( std::unique_ptr<AI_STRUCTURED_SURFACE_STATE_BACKEND>& backend :
         m_Backends )
    {
        wxString childStateJson;

        if( !backend->BeginSurfaceTransaction( aSession, childStateJson,
                                               aError ) )
        {
            AbortSurfaceTransaction();
            return false;
        }

        ++m_BegunCount;

        nlohmann::json childState = objectFromJsonText( childStateJson );
        std::vector<std::string> childSurfaceIds;

        if( !collectStructuredSurfaceIds( childState, childSurfaceIds,
                                          aError ) )
        {
            AbortSurfaceTransaction();
            return false;
        }

        m_BackendSurfaceIds.push_back( childSurfaceIds );

        if( !mergeStructuredSurfaceState( mergedState, childState, aError ) )
        {
            AbortSurfaceTransaction();
            return false;
        }
    }

    const std::string dumpedState = mergedState.dump();
    aSurfaceStateJson = wxString::FromUTF8( dumpedState.c_str() );
    aError.clear();
    m_InTransaction = true;
    return true;
}


bool AI_STRUCTURED_SURFACE_COMPOSITE_STATE_BACKEND::CommitSurfaceTransaction(
        const wxString& aSurfaceStateJson, bool aChanged, wxString& aError )
{
    if( !m_InTransaction )
    {
        aError = wxS( "Composite structured surface transaction has not started." );
        return false;
    }

    if( m_BackendSurfaceIds.size() != m_Backends.size() )
    {
        aError = wxS( "Composite structured surface ownership is incomplete." );
        return false;
    }

    nlohmann::json state = objectFromJsonText( aSurfaceStateJson );
    std::vector<std::string> surfaceIds;

    if( !collectStructuredSurfaceIds( state, surfaceIds, aError ) )
        return false;

    for( const std::string& surfaceId : surfaceIds )
    {
        if( !ownsStructuredSurfaceId( m_BackendSurfaceIds, surfaceId ) )
        {
            aError = wxS( "Composite structured surface commit has no child backend for surface." );
            return false;
        }
    }

    for( size_t i = 0; i < m_Backends.size(); ++i )
    {
        nlohmann::json childState =
                scopedStructuredSurfaceState( state, m_BackendSurfaceIds[i] );
        const std::string dumpedState = childState.dump();
        wxString childStateJson = wxString::FromUTF8( dumpedState.c_str() );

        if( !m_Backends[i]->CommitSurfaceTransaction( childStateJson,
                                                      aChanged, aError ) )
        {
            return false;
        }
    }

    m_BackendSurfaceIds.clear();
    m_BegunCount = 0;
    m_InTransaction = false;
    aError.clear();
    return true;
}


void AI_STRUCTURED_SURFACE_COMPOSITE_STATE_BACKEND::AbortSurfaceTransaction()
{
    const size_t count = m_InTransaction ? m_Backends.size() : m_BegunCount;

    for( size_t i = 0; i < count && i < m_Backends.size(); ++i )
        m_Backends[i]->AbortSurfaceTransaction();

    m_BegunCount = 0;
    m_BackendSurfaceIds.clear();
    m_InTransaction = false;
}


bool AI_STRUCTURED_SURFACE_UI_TRANSACTION_HOOK::BeginUiTransaction(
        const AI_EXECUTION_SESSION& aSession, wxString& aError )
{
    wxUnusedVar( aSession );
    aError.clear();
    return true;
}


bool AI_STRUCTURED_SURFACE_UI_TRANSACTION_HOOK::BeforeUiCommit(
        const wxString& aSurfaceStateJson, bool aChanged, wxString& aError )
{
    wxUnusedVar( aSurfaceStateJson );
    wxUnusedVar( aChanged );
    aError.clear();
    return true;
}


bool AI_STRUCTURED_SURFACE_UI_TRANSACTION_HOOK::AfterUiCommit(
        const wxString& aSurfaceStateJson, bool aChanged, wxString& aError )
{
    wxUnusedVar( aSurfaceStateJson );
    wxUnusedVar( aChanged );
    aError.clear();
    return true;
}


void AI_STRUCTURED_SURFACE_UI_TRANSACTION_HOOK::AbortUiTransaction()
{
}


AI_STRUCTURED_SURFACE_UI_TRANSACTION_BACKEND::
        AI_STRUCTURED_SURFACE_UI_TRANSACTION_BACKEND(
                std::unique_ptr<AI_STRUCTURED_SURFACE_STATE_BACKEND> aBackend,
                std::unique_ptr<AI_STRUCTURED_SURFACE_UI_TRANSACTION_HOOK> aHook ) :
        m_Backend( std::move( aBackend ) ),
        m_Hook( std::move( aHook ) )
{
}


bool AI_STRUCTURED_SURFACE_UI_TRANSACTION_BACKEND::BeginSurfaceTransaction(
        const AI_EXECUTION_SESSION& aSession, wxString& aSurfaceStateJson,
        wxString& aError )
{
    if( !m_Backend || !m_Hook )
    {
        aError = wxS( "Structured surface UI transaction backend requires "
                      "a child backend and UI hook." );
        return false;
    }

    if( !m_Backend->BeginSurfaceTransaction( aSession, aSurfaceStateJson,
                                             aError ) )
    {
        return false;
    }

    if( !m_Hook->BeginUiTransaction( aSession, aError ) )
    {
        m_Backend->AbortSurfaceTransaction();
        return false;
    }

    m_InTransaction = true;
    aError.clear();
    return true;
}


bool AI_STRUCTURED_SURFACE_UI_TRANSACTION_BACKEND::CommitSurfaceTransaction(
        const wxString& aSurfaceStateJson, bool aChanged, wxString& aError )
{
    if( !m_InTransaction )
    {
        aError = wxS( "Structured surface UI transaction has not started." );
        return false;
    }

    if( !m_Hook->BeforeUiCommit( aSurfaceStateJson, aChanged, aError ) )
        return false;

    if( !m_Backend->CommitSurfaceTransaction( aSurfaceStateJson, aChanged,
                                              aError ) )
    {
        return false;
    }

    if( !m_Hook->AfterUiCommit( aSurfaceStateJson, aChanged, aError ) )
        return false;

    m_InTransaction = false;
    aError.clear();
    return true;
}


void AI_STRUCTURED_SURFACE_UI_TRANSACTION_BACKEND::AbortSurfaceTransaction()
{
    if( m_Backend )
        m_Backend->AbortSurfaceTransaction();

    if( m_Hook )
        m_Hook->AbortUiTransaction();

    m_InTransaction = false;
}


AI_STRUCTURED_SURFACE_APPLY_ADAPTER::AI_STRUCTURED_SURFACE_APPLY_ADAPTER(
        wxString& aSurfaceStateJson ) :
        m_OwnedBackend(
                std::make_unique<AI_STRUCTURED_SURFACE_STRING_STATE_BACKEND>(
                        aSurfaceStateJson ) ),
        m_Backend( *m_OwnedBackend )
{
}


AI_STRUCTURED_SURFACE_APPLY_ADAPTER::AI_STRUCTURED_SURFACE_APPLY_ADAPTER(
        AI_STRUCTURED_SURFACE_STATE_BACKEND& aBackend ) :
        m_Backend( aBackend )
{
}


bool AI_STRUCTURED_SURFACE_APPLY_ADAPTER::BeginTransaction(
        const AI_EXECUTION_SESSION& aSession, wxString& aError )
{
    aError.clear();
    m_SurfaceChanged = false;

    if( !m_Backend.BeginSurfaceTransaction( aSession, m_WorkingStateJson,
                                            aError ) )
    {
        m_WorkingStateJson.clear();
        m_InTransaction = false;
        return false;
    }

    m_InTransaction = true;
    return true;
}


bool AI_STRUCTURED_SURFACE_APPLY_ADAPTER::ApplyOperation(
        const AI_SESSION_OPERATION_RECORD& aOperation, wxString& aError )
{
    if( !m_InTransaction )
    {
        aError = wxS( "Structured surface transaction has not started." );
        return false;
    }

    if( aOperation.m_Kind != AI_SESSION_OPERATION_KIND::ApplySurfacePatch )
    {
        aError = wxS( "Structured surface adapter only accepts SurfacePatch operations." );
        return false;
    }

    return applySurfacePatch( aOperation, aError );
}


bool AI_STRUCTURED_SURFACE_APPLY_ADAPTER::CommitTransaction( wxString& aError )
{
    if( !m_InTransaction )
    {
        aError = wxS( "Structured surface transaction has not started." );
        return false;
    }

    if( !m_Backend.CommitSurfaceTransaction( m_WorkingStateJson,
                                             m_SurfaceChanged, aError ) )
    {
        return false;
    }

    m_WorkingStateJson.clear();
    m_InTransaction = false;
    aError.clear();
    return true;
}


bool AI_STRUCTURED_SURFACE_APPLY_ADAPTER::HasBoardChanges() const
{
    return false;
}


void AI_STRUCTURED_SURFACE_APPLY_ADAPTER::AbortTransaction()
{
    if( m_InTransaction )
        m_Backend.AbortSurfaceTransaction();

    m_WorkingStateJson.clear();
    m_InTransaction = false;
    m_SurfaceChanged = false;
}


bool AI_STRUCTURED_SURFACE_APPLY_ADAPTER::HasSurfaceChanges() const
{
    return m_SurfaceChanged;
}


bool AI_STRUCTURED_SURFACE_APPLY_ADAPTER::applySurfacePatch(
        const AI_SESSION_OPERATION_RECORD& aOperation, wxString& aError )
{
    nlohmann::json args = objectFromJsonText( aOperation.m_ArgumentsJson );
    const std::string surfaceId = stringField( args, "surface_id" );

    if( surfaceId.empty() )
    {
        aError = wxS( "SurfacePatch replay requires surface_id." );
        return false;
    }

    if( !args.contains( "patch" ) || !args["patch"].is_object() )
    {
        aError = wxS( "SurfacePatch replay requires a patch object." );
        return false;
    }

    const nlohmann::json* operations = patchOperations( args["patch"] );

    if( !operations )
    {
        aError = wxS( "SurfacePatch replay requires patch operations." );
        return false;
    }

    const std::string tableId = stringField( args, "table_id" );
    const std::string writePolicy = surfacePatchWritePolicy( args );

    if( !writePolicy.empty()
        && writePolicy != "fill_empty_only"
        && writePolicy != "allow_overwrite" )
    {
        aError = wxS( "SurfacePatch write_policy must be fill_empty_only or "
                      "allow_overwrite." );
        return false;
    }

    const bool fillEmptyOnly = writePolicy == "fill_empty_only";
    nlohmann::json workingState = objectFromJsonText( m_WorkingStateJson );

    if( args.contains( "expected_surface_revision" ) )
    {
        const nlohmann::json* currentRevision = nullptr;

        if( workingState.contains( "surfaces" )
            && workingState["surfaces"].is_object()
            && workingState["surfaces"].contains( surfaceId )
            && workingState["surfaces"][surfaceId].is_object() )
        {
            currentRevision =
                    surfaceRevisionField( workingState["surfaces"][surfaceId] );
        }

        if( !currentRevision
            || !scalarValuesEqual( args["expected_surface_revision"],
                                   *currentRevision ) )
        {
            aError = wxS( "SurfacePatch stale surface revision." );
            return false;
        }
    }

    if( args.contains( "expected_schema_version" ) )
    {
        const nlohmann::json* currentSchemaVersion = nullptr;

        if( workingState.contains( "surfaces" )
            && workingState["surfaces"].is_object()
            && workingState["surfaces"].contains( surfaceId )
            && workingState["surfaces"][surfaceId].is_object() )
        {
            currentSchemaVersion = schemaVersionField(
                    workingState["surfaces"][surfaceId], workingState );
        }

        if( !currentSchemaVersion
            || !scalarValuesEqual( args["expected_schema_version"],
                                   *currentSchemaVersion ) )
        {
            aError = wxS( "SurfacePatch stale schema version." );
            return false;
        }
    }

    if( args.contains( "expected_selection_fingerprint" ) )
    {
        const nlohmann::json* currentSelectionFingerprint = nullptr;

        if( workingState.contains( "surfaces" )
            && workingState["surfaces"].is_object()
            && workingState["surfaces"].contains( surfaceId )
            && workingState["surfaces"][surfaceId].is_object() )
        {
            currentSelectionFingerprint = selectionFingerprintField(
                    workingState["surfaces"][surfaceId], workingState );
        }

        if( !currentSelectionFingerprint
            || !scalarValuesEqual( args["expected_selection_fingerprint"],
                                   *currentSelectionFingerprint ) )
        {
            aError = wxS( "SurfacePatch stale selection fingerprint." );
            return false;
        }
    }

    if( args.contains( "expected_overlap_set" ) )
    {
        const nlohmann::json* currentOverlapSet = nullptr;

        if( workingState.contains( "surfaces" )
            && workingState["surfaces"].is_object()
            && workingState["surfaces"].contains( surfaceId )
            && workingState["surfaces"][surfaceId].is_object() )
        {
            currentOverlapSet = overlapSetField(
                    workingState["surfaces"][surfaceId], workingState );
        }

        if( !currentOverlapSet
            || !jsonValuesEqual( args["expected_overlap_set"],
                                 *currentOverlapSet ) )
        {
            aError = wxS( "SurfacePatch stale overlap set." );
            return false;
        }
    }

    nlohmann::json& surface = workingState["surfaces"][surfaceId];
    bool changed = false;

    for( const nlohmann::json& op : *operations )
    {
        const std::string opName = stringField( op, "op" );

        if( opName == "set_cell" )
        {
            const std::string rowId = stringField( op, "row_id" );
            const std::string columnId = stringField( op, "column_id" );
            const std::string tableOverride = stringField( op, "table_id" );
            const std::string opTableId =
                    tableOverride.empty() ? tableId : tableOverride;

            if( opTableId.empty() || rowId.empty() || columnId.empty()
                || !op.contains( "value" ) )
            {
                aError = wxS( "SurfacePatch set_cell requires table_id, row_id, "
                              "column_id, and value." );
                return false;
            }

            const nlohmann::json* currentValue =
                    currentSurfaceCellValue( surface, opTableId, rowId,
                                             columnId );

            nlohmann::json currentPolicyValue;

            if( currentValue )
            {
                currentPolicyValue = valueWithFallbackProvenance(
                        *currentValue,
                        surfaceCellProvenance( surface, opTableId, rowId,
                                               columnId ) );
            }

            if( fillEmptyOnly && currentValue
                && !surfaceValueAllowsFillEmptyOnlyWrite( currentPolicyValue ) )
            {
                aError = wxS( "SurfacePatch fill_empty_only cannot overwrite "
                              "a non-empty cell." );
                return false;
            }

            surface["tables"][opTableId]["rows"][rowId]["cells"][columnId] =
                    op["value"];
            changed = true;
            continue;
        }

        if( opName == "set_field" )
        {
            const std::string fieldId = stringField( op, "field_id" );

            if( fieldId.empty() || !op.contains( "value" ) )
            {
                aError = wxS( "SurfacePatch set_field requires field_id and value." );
                return false;
            }

            const nlohmann::json* currentValue =
                    currentSurfaceFieldValue( surface, fieldId );

            nlohmann::json currentPolicyValue;

            if( currentValue )
            {
                currentPolicyValue = valueWithFallbackProvenance(
                        *currentValue,
                        surfaceFieldProvenance( surface, fieldId ) );
            }

            if( fillEmptyOnly && currentValue
                && !surfaceValueAllowsFillEmptyOnlyWrite( currentPolicyValue ) )
            {
                aError = wxS( "SurfacePatch fill_empty_only cannot overwrite "
                              "a non-empty field." );
                return false;
            }

            surface["fields"][fieldId] = op["value"];
            changed = true;
            continue;
        }

        const wxString opLabel = opName.empty()
                                 ? wxString( wxS( "<missing>" ) )
                                 : wxString::FromUTF8( opName.c_str() );
        aError = wxString::Format( wxS( "Unsupported SurfacePatch op: %s" ),
                                   opLabel );
        return false;
    }

    if( changed )
    {
        advanceNumericSurfaceRevision( surface );

        const std::string dumpedState = workingState.dump();
        m_WorkingStateJson = wxString::FromUTF8( dumpedState.c_str() );
        m_SurfaceChanged = true;
    }

    aError.clear();
    return true;
}
