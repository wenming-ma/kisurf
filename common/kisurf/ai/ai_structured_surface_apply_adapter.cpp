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
}


AI_STRUCTURED_SURFACE_APPLY_ADAPTER::AI_STRUCTURED_SURFACE_APPLY_ADAPTER(
        wxString& aSurfaceStateJson ) :
        m_SurfaceStateJson( aSurfaceStateJson )
{
}


bool AI_STRUCTURED_SURFACE_APPLY_ADAPTER::BeginTransaction(
        const AI_EXECUTION_SESSION& aSession, wxString& aError )
{
    wxUnusedVar( aSession );
    aError.clear();
    m_WorkingStateJson = m_SurfaceStateJson;
    m_InTransaction = true;
    m_SurfaceChanged = false;
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

    m_SurfaceStateJson = m_WorkingStateJson;
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
