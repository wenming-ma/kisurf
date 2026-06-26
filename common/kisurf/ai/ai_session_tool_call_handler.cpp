/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 */

#include <kisurf/ai/ai_session_tool_call_handler.h>
#include <kisurf/ai/ai_accept_applier.h>
#include <kisurf/ai/ai_atomic_operation_executor.h>
#include <kisurf/ai/ai_shadow_board.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>

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


AI_EDITOR_KIND effectiveEditorKind( const AI_PROVIDER_REQUEST& aRequest )
{
    if( aRequest.m_EditorKind != AI_EDITOR_KIND::Unknown )
        return aRequest.m_EditorKind;

    return aRequest.m_ContextSnapshot.m_EditorKind;
}


AI_CONTEXT_VERSION effectiveContextVersion( const AI_PROVIDER_REQUEST& aRequest )
{
    if( aRequest.m_ContextVersion.IsValid() )
        return aRequest.m_ContextVersion;

    return aRequest.m_ContextSnapshot.m_Version;
}


wxString defaultBoardId( AI_EDITOR_KIND aEditorKind )
{
    switch( aEditorKind )
    {
    case AI_EDITOR_KIND::Pcb:
        return wxS( "active-pcb" );

    case AI_EDITOR_KIND::Schematic:
        return wxS( "active-schematic" );

    case AI_EDITOR_KIND::Unknown:
    default:
        return wxS( "active-editor" );
    }
}


bool isSessionTool( const wxString& aToolName )
{
    return aToolName == wxS( "kisurf_open_session" )
           || aToolName == wxS( "kisurf_close_session" )
           || aToolName == wxS( "kisurf_run_cell" )
           || aToolName == wxS( "kisurf_run_atomic_operation" )
           || aToolName == wxS( "kisurf_begin_step" )
           || aToolName == wxS( "kisurf_end_step" )
           || aToolName == wxS( "kisurf_checkpoint" )
           || aToolName == wxS( "kisurf_rollback_to" )
           || aToolName == wxS( "kisurf_cancel_session" )
           || aToolName == wxS( "kisurf_reject_session" )
           || aToolName == wxS( "kisurf_accept_session" )
           || aToolName == wxS( "kisurf_observe_step" )
           || aToolName == wxS( "kisurf_query_board_summary" )
           || aToolName == wxS( "kisurf_query_items" )
           || aToolName == wxS( "kisurf_query_item" )
           || aToolName == wxS( "kisurf_query_selection" )
           || aToolName == wxS( "kisurf_query_nets" )
           || aToolName == wxS( "kisurf_query_layers" )
           || aToolName == wxS( "kisurf_query_design_rules" )
           || aToolName == wxS( "kisurf_query_viewport" )
           || aToolName == wxS( "kisurf_query_activity_timeline" )
           || aToolName == wxS( "kisurf_render_preview" )
           || aToolName == wxS( "kisurf_run_validation" );
}


nlohmann::json sessionAtomicOperationSetJson()
{
    return nlohmann::json::array(
            { "pcb.create_via",
              "pcb.create_track_segment",
              "pcb.create_track_polyline",
              "pcb.create_zone",
              "pcb.create_shape",
              "pcb.move_items",
              "pcb.delete_items",
              "pcb.update_item_geometry",
              "pcb.set_item_net",
              "pcb.set_item_layer",
              "pcb.set_item_properties",
              "pcb.set_metadata",
              "pcb.refill_zones",
              "pcb.rebuild_connectivity",
              "pcb.run_validation",
              "surface.apply_patch" } );
}


nlohmann::json catalogPointSchema( const char* aDescription )
{
    return { { "type", "object" },
             { "description", aDescription },
             { "additionalProperties", false },
             { "properties",
               { { "x", { { "type", "integer" } } },
                 { "y", { { "type", "integer" } } } } },
             { "required", nlohmann::json::array( { "x", "y" } ) } };
}


nlohmann::json catalogHandleSchema( const char* aDescription )
{
    return { { "description", aDescription },
             { "anyOf",
               nlohmann::json::array(
                       { { { "type", "string" },
                           { "description", "Session handle alias." } },
                         { { "type", "object" },
                           { "additionalProperties", false },
                           { "properties",
                             { { "session_id",
                                 { { "type", "integer" }, { "minimum", 1 } } },
                               { "handle_id",
                                 { { "type", "integer" }, { "minimum", 1 } } },
                               { "generation",
                                 { { "type", "integer" }, { "minimum", 1 } } },
                               { "alias", { { "type", "string" } } } } },
                           { "required",
                             nlohmann::json::array( { "handle_id" } ) } } } ) } };
}


nlohmann::json catalogHandleArraySchema( const char* aDescription )
{
    return { { "type", "array" },
             { "description", aDescription },
             { "items", catalogHandleSchema( "Session handle or alias." ) },
             { "minItems", 1 } };
}


nlohmann::json catalogQueryHandleFilterSchema()
{
    return { { "description",
               "Handle filter as an alias, session-local handle id, or handle object." },
             { "anyOf",
               nlohmann::json::array(
                       { { { "type", "string" },
                           { "description", "Session handle alias." } },
                         { { "type", "integer" }, { "minimum", 1 } },
                         { { "type", "object" },
                           { "additionalProperties", false },
                           { "properties",
                             { { "session_id",
                                 { { "type", "integer" }, { "minimum", 1 } } },
                               { "handle_id",
                                 { { "type", "integer" }, { "minimum", 1 } } },
                               { "generation",
                                 { { "type", "integer" }, { "minimum", 1 } } },
                               { "alias", { { "type", "string" } } } } },
                           { "required",
                             nlohmann::json::array( { "handle_id" } ) } } } ) } };
}


nlohmann::json catalogStringArraySchema( const char* aDescription )
{
    return { { "type", "array" },
             { "description", aDescription },
             { "items", { { "type", "string" } } },
             { "minItems", 1 } };
}


nlohmann::json catalogPointArraySchema( const char* aDescription, int aMinItems )
{
    return { { "type", "array" },
             { "description", aDescription },
             { "items", catalogPointSchema( "Internal-coordinate x/y point." ) },
             { "minItems", aMinItems } };
}


nlohmann::json catalogBoxSchema( const char* aDescription )
{
    return { { "description", aDescription },
             { "anyOf",
               nlohmann::json::array(
                       { { { "type", "object" },
                           { "additionalProperties", false },
                           { "properties",
                             { { "x", { { "type", "integer" } } },
                               { "y", { { "type", "integer" } } },
                               { "width",
                                 { { "type", "integer" }, { "minimum", 1 } } },
                               { "height",
                                 { { "type", "integer" }, { "minimum", 1 } } } } },
                           { "required",
                             nlohmann::json::array(
                                     { "x", "y", "width", "height" } ) } },
                         { { "type", "object" },
                           { "additionalProperties", false },
                           { "properties",
                             { { "min",
                                 catalogPointSchema( "Minimum box corner." ) },
                               { "max",
                                 catalogPointSchema( "Maximum box corner." ) } } },
                           { "required",
                             nlohmann::json::array( { "min", "max" } ) } } } ) } };
}


nlohmann::json catalogOperationScopeSchema( const char* aDescription )
{
    return { { "type", "string" },
             { "description", aDescription },
             { "enum",
               nlohmann::json::array(
                       { "session", "affected_area", "selection", "region" } ) } };
}


nlohmann::json catalogQueryItemsFilterSchema()
{
    return { { "type", "object" },
             { "description",
               "Optional semantic shadow-board filter aligned with AI_SHADOW_BOARD::QueryItems." },
             { "additionalProperties", false },
             { "properties",
               { { "type", { { "type", "string" } } },
                 { "net", { { "type", "string" } } },
                 { "layer", { { "type", "string" } } },
                 { "alias", { { "type", "string" } } },
                 { "selection", { { "type", "boolean" } } },
                 { "bbox",
                   catalogBoxSchema(
                           "Bounding box intersection filter in internal coordinates." ) },
                 { "handle", catalogQueryHandleFilterSchema() } } } };
}


nlohmann::json catalogGeometryPatchSchema()
{
    nlohmann::json schema = { { "type", "object" },
                              { "additionalProperties", true },
                              { "description",
                                "Partial shape geometry patch. Segment/rectangle use "
                                "start/end; circle uses center/radius; arc uses "
                                "start/mid/end; polygon uses points." } };

    schema["properties"] = {
        { "start", catalogPointSchema( "Patched start point." ) },
        { "end", catalogPointSchema( "Patched end point." ) },
        { "center", catalogPointSchema( "Patched circle center." ) },
        { "mid", catalogPointSchema( "Patched arc midpoint." ) },
        { "radius", { { "type", "integer" }, { "minimum", 1 } } },
        { "points",
          catalogPointArraySchema(
                  "Patched polygon outline points using internal coordinates.", 3 ) }
    };

    return schema;
}


nlohmann::json catalogZoneOutlineSchema()
{
    nlohmann::json schema = { { "type", "object" },
                              { "additionalProperties", true },
                              { "description",
                                "Zone outline geometry. Use points for the ordered "
                                "polygon outline in internal coordinates." } };

    schema["properties"] = {
        { "points",
          catalogPointArraySchema(
                  "Zone polygon outline points using internal coordinates.", 3 ) }
    };

    return schema;
}


nlohmann::json catalogTypedPropertiesSchema()
{
    nlohmann::json schema = {
        { "type", "object" },
        { "additionalProperties", true },
        { "description",
          "Typed property patch for existing PCB items. Supported fields are "
          "applied according to the target item type." }
    };

    schema["properties"] = {
        { "diameter", { { "type", "integer" }, { "minimum", 1 } } },
        { "drill", { { "type", "integer" }, { "minimum", 1 } } },
        { "width", { { "type", "integer" }, { "minimum", 0 } } },
        { "fill", { { "type", "boolean" } } },
        { "clearance", { { "type", "integer" }, { "minimum", 0 } } },
        { "priority", { { "type", "integer" }, { "minimum", 0 } } },
        { "fill_mode",
          { { "type", "string" },
            { "enum",
              nlohmann::json::array( { "solid", "hatch_pattern",
                                        "copper_thieving" } ) } } },
        { "reference", { { "type", "string" } } },
        { "value", { { "type", "string" } } },
        { "side", { { "type", "string" } } },
        { "orientation_degrees", { { "type", "number" } } }
    };

    return schema;
}


nlohmann::json sessionAtomicOperationContractsJson()
{
    return {
        { "pcb.create_via",
          { { "type", "object" },
            { "additionalProperties", true },
            { "properties",
              { { "position", catalogPointSchema( "Via center position." ) },
                { "net", { { "type", "string" } } },
                { "diameter", { { "type", "integer" }, { "minimum", 1 } } },
                { "drill", { { "type", "integer" }, { "minimum", 1 } } },
                { "layer_pair",
                  { { "description", "Via layer pair, usually [start, end]." },
                    { "anyOf",
                      nlohmann::json::array(
                              { catalogStringArraySchema( "Layer-pair names." ),
                                { { "type", "object" },
                                  { "additionalProperties", true } } } ) } } },
                { "alias", { { "type", "string" } } },
                { "metadata",
                  { { "type", "object" }, { "additionalProperties", true } } } } },
            { "required", nlohmann::json::array( { "position" } ) } } },
        { "pcb.create_track_segment",
          { { "type", "object" },
            { "additionalProperties", true },
            { "properties",
              { { "start", catalogPointSchema( "Track segment start point." ) },
                { "end", catalogPointSchema( "Track segment end point." ) },
                { "layer", { { "type", "string" } } },
                { "net", { { "type", "string" } } },
                { "width", { { "type", "integer" }, { "minimum", 1 } } },
                { "alias", { { "type", "string" } } },
                { "metadata",
                  { { "type", "object" }, { "additionalProperties", true } } } } },
            { "required", nlohmann::json::array( { "start", "end" } ) } } },
        { "pcb.create_track_polyline",
          { { "type", "object" },
            { "additionalProperties", true },
            { "properties",
              { { "points",
                  catalogPointArraySchema(
                          "Ordered route polyline points using internal coordinates.",
                          2 ) },
                { "layer", { { "type", "string" } } },
                { "net", { { "type", "string" } } },
                { "width", { { "type", "integer" }, { "minimum", 1 } } },
                { "alias", { { "type", "string" } } } } },
            { "required", nlohmann::json::array( { "points" } ) } } },
        { "pcb.create_zone",
          { { "type", "object" },
            { "additionalProperties", true },
            { "properties",
              { { "outline", catalogZoneOutlineSchema() },
                { "layer_set", catalogStringArraySchema( "Copper layers." ) },
                { "net", { { "type", "string" } } },
                { "clearance", { { "type", "number" }, { "minimum", 0 } } },
                { "priority", { { "type", "number" }, { "minimum", 0 } } },
                { "fill_mode",
                  { { "type", "string" },
                    { "enum",
                      nlohmann::json::array( { "solid", "hatch_pattern",
                                                "copper_thieving" } ) } } },
                { "alias", { { "type", "string" } } },
                { "metadata",
                  { { "type", "object" }, { "additionalProperties", true } } } } },
            { "required", nlohmann::json::array( { "outline" } ) } } },
        { "pcb.create_shape",
          { { "type", "object" },
            { "additionalProperties", true },
            { "properties",
              { { "shape_type",
                  { { "type", "string" },
                    { "enum",
                      nlohmann::json::array( { "segment", "line", "rectangle",
                                                "circle", "arc", "polygon",
                                                "poly" } ) },
                    { "description",
                      "Shape primitive to create. Polygon/poly use geometry.points." } } },
                { "geometry",
                  { { "type", "object" },
                    { "additionalProperties", true },
                    { "description",
                      "Shape geometry. Segment/rectangle use start/end; circle uses "
                      "center/radius; arc uses start/mid/end; polygon uses points." },
                    { "properties",
                      { { "start", catalogPointSchema( "Shape start point." ) },
                        { "end", catalogPointSchema( "Shape end point." ) },
                        { "center", catalogPointSchema( "Circle center point." ) },
                        { "mid", catalogPointSchema( "Arc midpoint." ) },
                        { "radius", { { "type", "integer" }, { "minimum", 1 } } },
                        { "points",
                          catalogPointArraySchema(
                                  "Polygon outline points using internal coordinates.",
                                  3 ) } } } } },
                { "layer", { { "type", "string" } } },
                { "width", { { "type", "integer" }, { "minimum", 0 } } },
                { "fill", { { "type", "boolean" } } },
                { "alias", { { "type", "string" } } },
                { "metadata",
                  { { "type", "object" }, { "additionalProperties", true } } } } },
            { "required", nlohmann::json::array( { "shape_type", "geometry" } ) } } },
        { "pcb.move_items",
          { { "type", "object" },
            { "additionalProperties", true },
            { "properties",
              { { "handles", catalogHandleArraySchema( "Items to move." ) },
                { "delta", catalogPointSchema( "Movement delta." ) },
                { "target_positions",
                  { { "description",
                      "Single target point, ordered target array, or keyed target map." },
                    { "anyOf",
                      nlohmann::json::array(
                              { catalogPointSchema( "Single target point." ),
                                { { "type", "array" },
                                  { "items",
                                    catalogPointSchema( "Ordered target point." ) } },
                                { { "type", "object" },
                                  { "additionalProperties", true } } } ) } } } } },
            { "required", nlohmann::json::array( { "handles" } ) } } },
        { "pcb.delete_items",
          { { "type", "object" },
            { "additionalProperties", false },
            { "properties",
              { { "handles", catalogHandleArraySchema( "Items to delete." ) } } },
            { "required", nlohmann::json::array( { "handles" } ) } } },
        { "pcb.update_item_geometry",
          { { "type", "object" },
            { "additionalProperties", true },
            { "properties",
              { { "handle", catalogHandleSchema( "Item to update." ) },
                { "geometry_patch", catalogGeometryPatchSchema() } } },
            { "required", nlohmann::json::array( { "handle", "geometry_patch" } ) } } },
        { "pcb.set_item_net",
          { { "type", "object" },
            { "additionalProperties", false },
            { "properties",
              { { "handle", catalogHandleSchema( "Item to update." ) },
                { "net", { { "type", "string" } } } } },
            { "required", nlohmann::json::array( { "handle", "net" } ) } } },
        { "pcb.set_item_layer",
          { { "type", "object" },
            { "additionalProperties", false },
            { "properties",
              { { "handle", catalogHandleSchema( "Item to update." ) },
                { "layer", { { "type", "string" } } },
                { "layer_set",
                  catalogStringArraySchema( "Layer set for multilayer items." ) } } },
            { "required", nlohmann::json::array( { "handle" } ) } } },
        { "pcb.set_item_properties",
          { { "type", "object" },
            { "additionalProperties", false },
            { "properties",
              { { "handle", catalogHandleSchema( "Item to update." ) },
                { "typed_props", catalogTypedPropertiesSchema() } } },
            { "required", nlohmann::json::array( { "handle", "typed_props" } ) } } },
        { "pcb.set_metadata",
          { { "type", "object" },
            { "additionalProperties", false },
            { "properties",
              { { "handle", catalogHandleSchema( "Item to update." ) },
                { "key_values",
                  { { "type", "object" }, { "additionalProperties", true } } } } },
            { "required", nlohmann::json::array( { "handle", "key_values" } ) } } },
        { "pcb.refill_zones",
          { { "type", "object" },
            { "additionalProperties", true },
            { "properties",
              { { "handles", catalogHandleArraySchema( "Zone handles to refill." ) },
                { "affected_area",
                  catalogBoxSchema(
                          "Zone refill area in internal coordinates." ) },
                { "all", { { "type", "boolean" } } } } } } },
        { "pcb.rebuild_connectivity",
          { { "type", "object" },
            { "additionalProperties", true },
            { "properties",
              { { "scope",
                  catalogOperationScopeSchema(
                          "Connectivity rebuild scope." ) } } } } },
        { "pcb.run_validation",
          { { "type", "object" },
            { "additionalProperties", true },
            { "properties",
              { { "scope",
                  catalogOperationScopeSchema( "Validation scope." ) },
                { "level",
                  { { "type", "string" },
                    { "enum",
                      nlohmann::json::array( { "geometry", "drc_lite",
                                                "full_drc" } ) } } } } } } },
        { "surface.apply_patch",
          { { "type", "object" },
            { "additionalProperties", true },
            { "properties",
              { { "surface_id", { { "type", "string" } } },
                { "table_id", { { "type", "string" } } },
                { "target_scope",
                  { { "type", "object" }, { "additionalProperties", true } } },
                { "patch", { { "type", "object" }, { "additionalProperties", true } } },
                { "write_policy",
                  { { "type", "string" },
                    { "enum",
                      nlohmann::json::array( { "fill_empty_only",
                                                "allow_overwrite" } ) } } },
                { "expected_surface_revision",
                  { { "description", "Surface revision expected at accept time." } } },
                { "expected_schema_version",
                  { { "description", "Surface schema version expected at accept time." } } },
                { "expected_selection_fingerprint",
                  { { "description", "Focused selection fingerprint expected at accept time." } } },
                { "expected_overlap_set",
                  { { "description", "Protected overlap set expected at accept time." } } },
                { "alias", { { "type", "string" } } },
                { "metadata",
                  { { "type", "object" }, { "additionalProperties", true } } } } },
            { "required", nlohmann::json::array( { "surface_id", "patch" } ) } } }
    };
}


nlohmann::json sessionToolDescriptor( const char* aName, const char* aLayer,
                                      const char* aRole, const char* aSideEffect )
{
    return { { "name", aName },
             { "layer", aLayer },
             { "role", aRole },
             { "side_effect", aSideEffect },
             { "can_publish", false },
             { "direct_publish", false },
             { "raw_board_access", false } };
}


nlohmann::json sessionToolCatalogJson()
{
    nlohmann::json tools = nlohmann::json::array(
            { sessionToolDescriptor( "kisurf_open_session", "session_control",
                                     "session_lifecycle", "session_control" ),
              sessionToolDescriptor( "kisurf_close_session", "session_control",
                                     "session_lifecycle", "session_control" ),
              sessionToolDescriptor( "kisurf_begin_step", "session_control",
                                     "step_lifecycle", "session_control" ),
              sessionToolDescriptor( "kisurf_end_step", "session_control",
                                     "step_lifecycle", "read_only" ),
              sessionToolDescriptor( "kisurf_checkpoint", "session_control",
                                     "checkpoint", "session_control" ),
              sessionToolDescriptor( "kisurf_rollback_to", "session_control",
                                     "checkpoint_rollback", "shadow_mutation" ),
              sessionToolDescriptor( "kisurf_cancel_session", "session_control",
                                     "session_lifecycle", "session_control" ),
              sessionToolDescriptor( "kisurf_reject_session", "session_control",
                                     "session_lifecycle", "session_control" ),
              sessionToolDescriptor( "kisurf_accept_session", "runtime_gate",
                                     "accept_gate", "live_commit" ),
              sessionToolDescriptor( "kisurf_observe_step", "atomic",
                                     "observation", "read_only" ),
              sessionToolDescriptor( "kisurf_query_board_summary", "atomic",
                                     "observation", "read_only" ),
              sessionToolDescriptor( "kisurf_query_items", "atomic",
                                     "observation", "read_only" ),
              sessionToolDescriptor( "kisurf_query_item", "atomic",
                                     "observation", "read_only" ),
              sessionToolDescriptor( "kisurf_query_selection", "atomic",
                                     "observation", "read_only" ),
              sessionToolDescriptor( "kisurf_query_nets", "atomic",
                                     "observation", "read_only" ),
              sessionToolDescriptor( "kisurf_query_layers", "atomic",
                                     "observation", "read_only" ),
              sessionToolDescriptor( "kisurf_query_design_rules", "atomic",
                                     "observation", "read_only" ),
              sessionToolDescriptor( "kisurf_query_viewport", "atomic",
                                     "observation", "read_only" ),
              sessionToolDescriptor( "kisurf_query_activity_timeline", "atomic",
                                     "observation", "read_only" ),
              sessionToolDescriptor( "kisurf_render_preview", "atomic",
                                     "render", "render" ),
              sessionToolDescriptor( "kisurf_run_validation", "atomic",
                                     "validation", "validate" ),
              sessionToolDescriptor( "kisurf_run_atomic_operation", "atomic",
                                     "typed_atomic_operation", "shadow_mutation" ),
              sessionToolDescriptor( "kisurf_run_cell", "script",
                                     "python_cell_batch_composition",
                                     "shadow_mutation" ) } );

    for( nlohmann::json& tool : tools )
    {
        if( !tool.is_object() )
            continue;

        const std::string name = tool.value( "name", std::string() );

        if( name == "kisurf_run_atomic_operation" )
        {
            tool["requires_journal"] = true;
            tool["supported_kinds"] = sessionAtomicOperationSetJson();
            tool["operation_contracts"] = sessionAtomicOperationContractsJson();
            tool["cannot_publish"] = true;
        }
        else if( name == "kisurf_run_cell" )
        {
            tool["requires_journal"] = true;
            tool["lowers_to_atomic_ops"] = sessionAtomicOperationSetJson();
            tool["execution_runtime"] = "python_subprocess_session_sdk";
        }
        else if( name == "kisurf_query_items" )
        {
            tool["filter_contract"] = catalogQueryItemsFilterSchema();
        }
        else if( name == "kisurf_accept_session" )
        {
            tool["requires_accept_gate"] = true;
            tool["requires_exact_journal_replay"] = true;
        }
    }

    return tools;
}


AI_TOOL_INVOCATION_RESULT makeResult( const AI_PROVIDER_REQUEST& aRequest,
                                      const AI_TOOL_CALL_RECORD& aToolCall )
{
    AI_TOOL_INVOCATION_RESULT result;
    result.m_RequestId = aRequest.m_RequestId;
    result.m_ToolCallId = aToolCall.m_ToolCallId;
    result.m_ActionName = aToolCall.m_ToolName;
    return result;
}


wxString resultJson( const AI_TOOL_INVOCATION_RESULT& aResult,
                     nlohmann::json aPayload )
{
    aPayload["tool"] = toUtf8String( aResult.m_ActionName );
    aPayload["action"] = toUtf8String( aResult.m_ActionName );
    aPayload["allowed"] = aResult.m_Allowed;
    aPayload["executed"] = aResult.m_Executed;
    aPayload["error_code"] = toUtf8String( aResult.m_ErrorCode );
    aPayload["message"] = toUtf8String( aResult.m_Message );
    return fromJson( aPayload );
}


AI_TOOL_INVOCATION_RESULT deniedResult( const AI_PROVIDER_REQUEST& aRequest,
                                        const AI_TOOL_CALL_RECORD& aToolCall,
                                        const wxString& aErrorCode,
                                        const wxString& aMessage )
{
    AI_TOOL_INVOCATION_RESULT result = makeResult( aRequest, aToolCall );
    result.m_Allowed = false;
    result.m_Executed = false;
    result.m_ErrorCode = aErrorCode;
    result.m_Message = aMessage;
    result.m_ResultJson = resultJson( result, { { "status", "denied" } } );
    return result;
}


AI_TOOL_INVOCATION_RESULT allowedResult( const AI_PROVIDER_REQUEST& aRequest,
                                         const AI_TOOL_CALL_RECORD& aToolCall,
                                         bool aExecuted, const wxString& aMessage,
                                         nlohmann::json aPayload )
{
    AI_TOOL_INVOCATION_RESULT result = makeResult( aRequest, aToolCall );
    result.m_Allowed = true;
    result.m_Executed = aExecuted;
    result.m_Message = aMessage;
    result.m_ResultJson = resultJson( result, std::move( aPayload ) );
    return result;
}


bool jsonIntegerToUint64( const nlohmann::json& aValue, uint64_t& aOut )
{
    if( !aValue.is_number_unsigned() && !aValue.is_number_integer() )
        return false;

    const int64_t value = aValue.get<int64_t>();

    if( value <= 0 )
        return false;

    aOut = static_cast<uint64_t>( value );
    return true;
}


bool getRequiredUint64( const nlohmann::json& aArguments, const char* aName,
                        uint64_t& aOut, wxString& aError )
{
    if( !aArguments.contains( aName ) || !jsonIntegerToUint64( aArguments[aName], aOut ) )
    {
        aError = wxString::Format( wxS( "%s must be a positive integer." ),
                                   wxString::FromUTF8( aName ) );
        return false;
    }

    return true;
}


bool resolveCheckpointReference( const AI_EXECUTION_SESSION& aSession,
                                 const nlohmann::json& aArguments,
                                 uint64_t& aCheckpointId,
                                 wxString& aCheckpointName,
                                 wxString& aError )
{
    aCheckpointId = 0;
    aCheckpointName.clear();

    if( aArguments.contains( "checkpoint_id" )
        && getRequiredUint64( aArguments, "checkpoint_id", aCheckpointId, aError ) )
    {
        return true;
    }

    if( aArguments.contains( "checkpoint_id" ) )
        return false;

    if( !aArguments.contains( "checkpoint_name" )
        || !aArguments["checkpoint_name"].is_string() )
    {
        aError = wxS( "checkpoint_id or checkpoint_name is required." );
        return false;
    }

    aCheckpointName = wxString::FromUTF8(
            aArguments["checkpoint_name"].get_ref<const std::string&>().c_str() );

    if( aCheckpointName.IsEmpty() )
    {
        aError = wxS( "checkpoint_name must be a non-empty string." );
        return false;
    }

    const std::vector<AI_SESSION_CHECKPOINT>& checkpoints = aSession.Checkpoints();

    for( auto it = checkpoints.rbegin(); it != checkpoints.rend(); ++it )
    {
        if( it->m_Name == aCheckpointName )
        {
            aCheckpointId = it->m_Id;
            return true;
        }
    }

    aError = wxS( "checkpoint_name is not valid for this session." );
    return false;
}


wxString optionalString( const nlohmann::json& aArguments, const char* aName )
{
    if( aArguments.contains( aName ) && aArguments[aName].is_string() )
        return wxString::FromUTF8( aArguments[aName].get_ref<const std::string&>().c_str() );

    return wxEmptyString;
}


std::optional<wxString> findNestedStringByName( const nlohmann::json& aRoot,
                                                const char* aName )
{
    if( !aRoot.is_object() )
        return std::nullopt;

    if( aRoot.contains( aName ) && aRoot[aName].is_string() )
    {
        wxString value =
                wxString::FromUTF8( aRoot[aName].get_ref<const std::string&>().c_str() );

        if( !value.IsEmpty() )
            return value;
    }

    for( const auto& [key, value] : aRoot.items() )
    {
        wxUnusedVar( key );

        if( value.is_object() )
        {
            if( std::optional<wxString> found = findNestedStringByName( value, aName ) )
                return found;
        }
    }

    return std::nullopt;
}


std::optional<wxString> contextProvidedBaseHash( const AI_PROVIDER_REQUEST& aRequest )
{
    nlohmann::json root = nlohmann::json::parse(
            toUtf8String( aRequest.m_ContextSnapshot.AsJsonText( 128, 128, 128, 128,
                                                                 16 ) ),
            nullptr, false );

    if( root.is_discarded() || !root.is_object() )
        return std::nullopt;

    const nlohmann::json& context =
            root.contains( "kisurf_context" ) && root["kisurf_context"].is_object()
                    ? root["kisurf_context"]
                    : root;

    for( const char* field : { "base_hash", "board_hash", "board_content_hash" } )
    {
        if( std::optional<wxString> hash = findNestedStringByName( context, field ) )
            return hash;
    }

    return std::nullopt;
}


wxString contextBaseHash( const AI_PROVIDER_REQUEST& aRequest )
{
    if( std::optional<wxString> provided = contextProvidedBaseHash( aRequest ) )
        return *provided;

    const AI_CONTEXT_VERSION version = effectiveContextVersion( aRequest );

    if( version.IsValid() )
        return version.AsString();

    return wxS( "unversioned-context" );
}


nlohmann::json sessionJson( const AI_EXECUTION_SESSION& aSession )
{
    return { { "session_id", aSession.SessionId() },
             { "board_id", toUtf8String( aSession.BoardId() ) },
             { "base_hash", toUtf8String( aSession.BaseHash() ) },
             { "epoch", aSession.Epoch() },
             { "journal_operation_count", aSession.Journal().Operations().size() },
             { "checkpoint_count", aSession.Checkpoints().size() } };
}


nlohmann::json handleJson( const AI_SESSION_HANDLE& aHandle )
{
    return { { "session_id", aHandle.m_SessionId },
             { "handle_id", aHandle.m_HandleId },
             { "generation", aHandle.m_Generation },
             { "alias", toUtf8String( aHandle.m_Alias ) } };
}


nlohmann::json handlesJson( const std::vector<AI_SESSION_HANDLE>& aHandles )
{
    nlohmann::json handles = nlohmann::json::array();

    for( const AI_SESSION_HANDLE& handle : aHandles )
        handles.push_back( handleJson( handle ) );

    return handles;
}


nlohmann::json warningsJson( const std::vector<wxString>& aWarnings )
{
    nlohmann::json warnings = nlohmann::json::array();

    for( const wxString& warning : aWarnings )
        warnings.push_back( toUtf8String( warning ) );

    return warnings;
}


nlohmann::json pythonEventJson( const AI_PYTHON_EVENT& aEvent )
{
    nlohmann::json payload =
            nlohmann::json::parse( toUtf8String( aEvent.m_PayloadJson ), nullptr, false );

    if( payload.is_discarded() )
        payload = toUtf8String( aEvent.m_PayloadJson );

    return { { "kind", toUtf8String( aEvent.m_Kind ) },
             { "message", toUtf8String( aEvent.m_Message ) },
             { "payload", std::move( payload ) } };
}


bool samePythonEvent( const AI_PYTHON_EVENT& aLeft, const AI_PYTHON_EVENT& aRight )
{
    return aLeft.m_Kind == aRight.m_Kind
           && aLeft.m_Message == aRight.m_Message
           && aLeft.m_PayloadJson == aRight.m_PayloadJson;
}


nlohmann::json pythonEventsJson( const std::vector<AI_PYTHON_EVENT>& aEvents )
{
    nlohmann::json events = nlohmann::json::array();

    for( const AI_PYTHON_EVENT& event : aEvents )
        events.push_back( pythonEventJson( event ) );

    return events;
}


nlohmann::json pythonSdkJson( const AI_PYTHON_CELL_RESULT& aResult )
{
    return { { "name", toUtf8String( aResult.m_SdkName ) },
             { "version", toUtf8String( aResult.m_SdkVersion ) },
             { "protocol", toUtf8String( aResult.m_SdkProtocol ) } };
}


nlohmann::json journalOperationJson( const AI_SESSION_OPERATION_RECORD& aRecord )
{
    nlohmann::json arguments =
            nlohmann::json::parse( toUtf8String( aRecord.m_ArgumentsJson ), nullptr, false );

    if( arguments.is_discarded() || !arguments.is_object() )
        arguments = nlohmann::json::object();

    return {
        { "id", aRecord.m_Id },
        { "step_id", aRecord.m_StepId },
        { "kind", toUtf8String( aRecord.OperationId() ) },
        { "arguments", std::move( arguments ) },
        { "resolved_handles", handlesJson( aRecord.m_ResolvedHandles ) },
        { "created_handles", handlesJson( aRecord.m_CreatedHandles ) },
        { "warnings", warningsJson( aRecord.m_Warnings ) },
        { "before_epoch", aRecord.m_BeforeEpoch },
        { "after_epoch", aRecord.m_AfterEpoch },
        { "mutation", aRecord.IsMutation() }
    };
}


nlohmann::json journalOperationsJson( const AI_EXECUTION_SESSION& aSession )
{
    nlohmann::json operations = nlohmann::json::array();

    for( const AI_SESSION_OPERATION_RECORD& record : aSession.Journal().Operations() )
        operations.push_back( journalOperationJson( record ) );

    return operations;
}


nlohmann::json shadowItemJson( const AI_SHADOW_ITEM& aItem )
{
    nlohmann::json layers = nlohmann::json::array();

    for( const wxString& layer : aItem.m_Layers )
        layers.push_back( toUtf8String( layer ) );

    nlohmann::json metadata = nlohmann::json::object();

    for( const auto& [key, value] : aItem.m_Metadata )
        metadata[toUtf8String( key )] = toUtf8String( value );

    nlohmann::json geometry =
            nlohmann::json::parse( toUtf8String( aItem.m_GeometryJson ), nullptr, false );

    if( geometry.is_discarded() )
        geometry = toUtf8String( aItem.m_GeometryJson );

    nlohmann::json properties =
            nlohmann::json::parse( toUtf8String( aItem.m_PropertiesJson ), nullptr, false );

    if( properties.is_discarded() || !properties.is_object() )
        properties = nlohmann::json::object();

    return { { "handle", handleJson( aItem.m_Handle ) },
             { "type", toUtf8String( aItem.m_Type ) },
             { "alias", toUtf8String( aItem.m_Alias ) },
             { "net", toUtf8String( aItem.m_Net ) },
             { "layer", toUtf8String( aItem.m_Layer ) },
             { "layers", std::move( layers ) },
             { "geometry", std::move( geometry ) },
             { "properties", std::move( properties ) },
             { "metadata", std::move( metadata ) },
             { "created_epoch", aItem.m_CreatedEpoch },
             { "updated_epoch", aItem.m_UpdatedEpoch } };
}


AI_PYTHON_CELL_REQUEST pythonCellRequest( const AI_EXECUTION_SESSION& aSession,
                                          const wxString& aCellText,
                                          const wxString& aCellId )
{
    AI_PYTHON_CELL_REQUEST request;
    request.m_SessionId = aSession.SessionId();
    request.m_BoardId = aSession.BoardId();
    request.m_BaseHash = aSession.BaseHash();
    request.m_Epoch = aSession.Epoch();
    request.m_CellText = aCellText;
    request.m_CellId = aCellId;
    return request;
}


wxString cellCheckpointName( const wxString& aCellId )
{
    return aCellId.IsEmpty() ? wxString( wxS( "before python cell" ) )
                             : wxString::Format( wxS( "before python cell %s" ), aCellId );
}


wxString cellStepLabel( const AI_PYTHON_CELL_RESULT& aWorkerResult,
                        const wxString& aCellId )
{
    if( !aWorkerResult.m_StepLabel.IsEmpty() )
        return aWorkerResult.m_StepLabel;

    return aCellId.IsEmpty() ? wxString( wxS( "python cell" ) )
                             : wxString::Format( wxS( "python cell %s" ), aCellId );
}


bool validatePythonCellResultSessionContext( const AI_PYTHON_CELL_REQUEST& aRequest,
                                             const AI_PYTHON_CELL_RESULT& aResult,
                                             wxString& aErrorCode,
                                             wxString& aMessage )
{
    if( !aResult.m_Ok && aResult.m_Operations.empty() && !aResult.m_HasSessionContext )
        return true;

    if( !aResult.m_HasSessionContext )
    {
        aErrorCode = wxS( "missing_python_session_context" );
        aMessage = wxS( "Python worker result did not include session context." );
        return false;
    }

    if( aResult.m_SessionId != aRequest.m_SessionId )
    {
        aErrorCode = wxS( "stale_python_cell_result" );
        aMessage = wxString::Format(
                wxS( "Python worker returned session %llu for active session %llu." ),
                static_cast<unsigned long long>( aResult.m_SessionId ),
                static_cast<unsigned long long>( aRequest.m_SessionId ) );
        return false;
    }

    if( aResult.m_BoardId != aRequest.m_BoardId )
    {
        aErrorCode = wxS( "stale_python_cell_result" );
        aMessage = wxS( "Python worker returned a result for a different board." );
        return false;
    }

    if( aResult.m_BaseHash != aRequest.m_BaseHash )
    {
        aErrorCode = wxS( "stale_python_cell_result" );
        aMessage = wxS( "Python worker returned a result for a stale base hash." );
        return false;
    }

    if( aResult.m_Epoch != aRequest.m_Epoch )
    {
        aErrorCode = wxS( "stale_python_cell_result" );
        aMessage = wxString::Format(
                wxS( "Python worker returned epoch %llu for requested epoch %llu." ),
                static_cast<unsigned long long>( aResult.m_Epoch ),
                static_cast<unsigned long long>( aRequest.m_Epoch ) );
        return false;
    }

    return true;
}


nlohmann::json operationIdsJson( const std::vector<uint64_t>& aIds )
{
    nlohmann::json ids = nlohmann::json::array();

    for( uint64_t id : aIds )
        ids.push_back( id );

    return ids;
}


nlohmann::json parseObjectJson( const wxString& aText )
{
    nlohmann::json parsed = nlohmann::json::parse( toUtf8String( aText ), nullptr, false );

    if( parsed.is_discarded() || !parsed.is_object() )
        return nlohmann::json::object();

    return parsed;
}


nlohmann::json contextPayloadJson( const AI_PROVIDER_REQUEST& aRequest )
{
    nlohmann::json root = parseObjectJson(
            aRequest.m_ContextSnapshot.AsJsonText( 128, 128, 128, 128, 16 ) );

    if( root.contains( "kisurf_context" ) && root["kisurf_context"].is_object() )
        return root["kisurf_context"];

    return nlohmann::json::object();
}


nlohmann::json selectionRevisionJson( const AI_EXECUTION_SESSION& aSession,
                                      const AI_PROVIDER_REQUEST& aRequest )
{
    const AI_CONTEXT_VERSION sessionVersion = aSession.ContextVersion();
    const AI_CONTEXT_VERSION currentVersion = effectiveContextVersion( aRequest );
    const bool hasSessionSelectionRevision =
            sessionVersion.m_SelectionRevision != 0;
    const bool hasCurrentSelectionRevision =
            currentVersion.m_SelectionRevision != 0;
    const bool selectionRevisionChanged =
            hasSessionSelectionRevision && hasCurrentSelectionRevision
            && sessionVersion.m_SelectionRevision != currentVersion.m_SelectionRevision;

    return {
        { "session", sessionVersion.m_SelectionRevision },
        { "current", currentVersion.m_SelectionRevision },
        { "changed", selectionRevisionChanged },
        { "conflict", selectionRevisionChanged },
        { "policy", "selection_handles_are_pinned_to_session_open" }
    };
}


bool selectionFilterRequested( const nlohmann::json& aFilter )
{
    return aFilter.is_object() && aFilter.contains( "selection" )
           && aFilter["selection"].is_boolean()
           && aFilter["selection"].get<bool>();
}


void collectStringValue( const nlohmann::json& aValue, std::set<std::string>& aOut )
{
    if( aValue.is_string() )
    {
        const std::string& value = aValue.get_ref<const std::string&>();

        if( !value.empty() )
            aOut.insert( value );

        return;
    }

    if( aValue.is_number_integer() || aValue.is_number_unsigned() )
    {
        aOut.insert( std::to_string( aValue.get<int64_t>() ) );
        return;
    }

    if( aValue.is_array() )
    {
        for( const nlohmann::json& item : aValue )
            collectStringValue( item, aOut );
    }
}


void collectNamedValues( const nlohmann::json& aObject, const char* aName,
                         std::set<std::string>& aOut )
{
    if( aObject.is_object() && aObject.contains( aName ) )
        collectStringValue( aObject[aName], aOut );
}


void collectNetAndLayerHintsFromContextObject(
        const nlohmann::json& aObject, std::set<std::string>& aNets,
        std::set<std::string>& aLayers )
{
    if( !aObject.is_object() )
        return;

    collectNamedValues( aObject, "net", aNets );
    collectNamedValues( aObject, "nets", aNets );
    collectNamedValues( aObject, "layer", aLayers );
    collectNamedValues( aObject, "layers", aLayers );
    collectNamedValues( aObject, "layer_set", aLayers );

    if( aObject.contains( "details" ) && aObject["details"].is_object() )
    {
        collectNamedValues( aObject["details"], "net", aNets );
        collectNamedValues( aObject["details"], "nets", aNets );
        collectNamedValues( aObject["details"], "layer", aLayers );
        collectNamedValues( aObject["details"], "layers", aLayers );
        collectNamedValues( aObject["details"], "layer_set", aLayers );
    }
}


nlohmann::json stringSetJson( const std::set<std::string>& aValues )
{
    nlohmann::json values = nlohmann::json::array();

    for( const std::string& value : aValues )
        values.push_back( value );

    return values;
}


std::optional<nlohmann::json> findNestedObjectByName( const nlohmann::json& aRoot,
                                                      const char* aName )
{
    if( !aRoot.is_object() )
        return std::nullopt;

    if( aRoot.contains( aName ) && aRoot[aName].is_object() )
        return aRoot[aName];

    for( const auto& [key, value] : aRoot.items() )
    {
        wxUnusedVar( key );

        if( value.is_object() )
        {
            if( std::optional<nlohmann::json> found =
                        findNestedObjectByName( value, aName ) )
            {
                return found;
            }
        }
    }

    return std::nullopt;
}


AI_SESSION_OPERATION_KIND operationKindForSessionQueryTool( const wxString& aToolName )
{
    if( aToolName == wxS( "kisurf_query_item" ) )
        return AI_SESSION_OPERATION_KIND::QueryItem;

    if( aToolName == wxS( "kisurf_query_selection" ) )
        return AI_SESSION_OPERATION_KIND::QuerySelection;

    if( aToolName == wxS( "kisurf_query_nets" ) )
        return AI_SESSION_OPERATION_KIND::QueryNets;

    if( aToolName == wxS( "kisurf_query_layers" ) )
        return AI_SESSION_OPERATION_KIND::QueryLayers;

    if( aToolName == wxS( "kisurf_query_design_rules" ) )
        return AI_SESSION_OPERATION_KIND::QueryDesignRules;

    if( aToolName == wxS( "kisurf_query_viewport" ) )
        return AI_SESSION_OPERATION_KIND::QueryViewport;

    if( aToolName == wxS( "kisurf_query_activity_timeline" ) )
        return AI_SESSION_OPERATION_KIND::QueryActivityTimeline;

    return AI_SESSION_OPERATION_KIND::Unknown;
}


AI_SESSION_OPERATION_KIND operationKindForAtomicOperationId( const std::string& aKind )
{
    if( aKind == "pcb.create_via" )
        return AI_SESSION_OPERATION_KIND::CreateVia;

    if( aKind == "pcb.create_track_segment" )
        return AI_SESSION_OPERATION_KIND::CreateTrackSegment;

    if( aKind == "pcb.create_track_polyline" )
        return AI_SESSION_OPERATION_KIND::CreateTrackPolyline;

    if( aKind == "pcb.create_zone" )
        return AI_SESSION_OPERATION_KIND::CreateZone;

    if( aKind == "pcb.create_shape" )
        return AI_SESSION_OPERATION_KIND::CreateShape;

    if( aKind == "pcb.move_items" )
        return AI_SESSION_OPERATION_KIND::MoveItems;

    if( aKind == "pcb.delete_items" )
        return AI_SESSION_OPERATION_KIND::DeleteItems;

    if( aKind == "pcb.update_item_geometry" )
        return AI_SESSION_OPERATION_KIND::UpdateItemGeometry;

    if( aKind == "pcb.set_item_net" )
        return AI_SESSION_OPERATION_KIND::SetItemNet;

    if( aKind == "pcb.set_item_layer" )
        return AI_SESSION_OPERATION_KIND::SetItemLayer;

    if( aKind == "pcb.set_item_properties" )
        return AI_SESSION_OPERATION_KIND::SetItemProperties;

    if( aKind == "pcb.set_metadata" )
        return AI_SESSION_OPERATION_KIND::SetMetadata;

    if( aKind == "pcb.refill_zones" )
        return AI_SESSION_OPERATION_KIND::RefillZones;

    if( aKind == "pcb.rebuild_connectivity" )
        return AI_SESSION_OPERATION_KIND::RebuildConnectivity;

    if( aKind == "pcb.run_validation" )
        return AI_SESSION_OPERATION_KIND::RunValidation;

    if( aKind == "surface.apply_patch" )
        return AI_SESSION_OPERATION_KIND::ApplySurfacePatch;

    return AI_SESSION_OPERATION_KIND::Unknown;
}


bool isMutationOperation( AI_SESSION_OPERATION_KIND aKind )
{
    AI_SESSION_OPERATION_RECORD record;
    record.m_Kind = aKind;
    return record.IsMutation();
}


bool isMaintenanceOperation( AI_SESSION_OPERATION_KIND aKind )
{
    return aKind == AI_SESSION_OPERATION_KIND::RunValidation
           || aKind == AI_SESSION_OPERATION_KIND::RefillZones
           || aKind == AI_SESSION_OPERATION_KIND::RebuildConnectivity;
}


std::string stringFieldOr( const nlohmann::json& aArguments, const char* aName,
                           const char* aFallback )
{
    if( aArguments.contains( aName ) && aArguments[aName].is_string() )
        return aArguments[aName].get_ref<const std::string&>();

    return aFallback;
}


nlohmann::json maintenanceOperationResult(
        const AI_PYTHON_OPERATION_REQUEST& aOperation,
        const AI_ATOMIC_EXECUTION_RESULT& aExecution )
{
    const nlohmann::json arguments = parseObjectJson( aOperation.m_ArgumentsJson );
    nlohmann::json payload = parseObjectJson( aExecution.m_ResultJson );

    if( !payload.is_object() || !payload.contains( "status" ) )
    {
        payload = {
            { "kind", toUtf8String( AiSessionOperationKindId( aOperation.m_Kind ) ) },
            { "arguments", arguments },
            { "board_mutated", false }
        };

        switch( aOperation.m_Kind )
        {
        case AI_SESSION_OPERATION_KIND::RunValidation:
            payload["status"] = "validation_completed";
            payload["validation"] = {
                { "scope", stringFieldOr( arguments, "scope", "session" ) },
                { "level", stringFieldOr( arguments, "level", "geometry" ) },
                { "status", "ok" },
                { "issue_count", 0 },
                { "warnings", warningsJson( aExecution.m_Warnings ) }
            };
            break;

        case AI_SESSION_OPERATION_KIND::RefillZones:
            payload["status"] = "zone_refill_recorded";
            payload["refill"] = {
                { "scope", arguments },
                { "status", "recorded" },
                { "warnings", warningsJson( aExecution.m_Warnings ) }
            };
            break;

        case AI_SESSION_OPERATION_KIND::RebuildConnectivity:
            payload["status"] = "connectivity_rebuild_recorded";
            payload["connectivity"] = {
                { "scope", stringFieldOr( arguments, "scope", "session" ) },
                { "status", "recorded" },
                { "warnings", warningsJson( aExecution.m_Warnings ) }
            };
            break;

        case AI_SESSION_OPERATION_KIND::Unknown:
        case AI_SESSION_OPERATION_KIND::Checkpoint:
        case AI_SESSION_OPERATION_KIND::RollbackTo:
        case AI_SESSION_OPERATION_KIND::QueryBoardSummary:
        case AI_SESSION_OPERATION_KIND::QueryItems:
        case AI_SESSION_OPERATION_KIND::QueryItem:
        case AI_SESSION_OPERATION_KIND::QuerySelection:
        case AI_SESSION_OPERATION_KIND::QueryNets:
        case AI_SESSION_OPERATION_KIND::QueryLayers:
        case AI_SESSION_OPERATION_KIND::QueryDesignRules:
        case AI_SESSION_OPERATION_KIND::QueryViewport:
        case AI_SESSION_OPERATION_KIND::QueryActivityTimeline:
        case AI_SESSION_OPERATION_KIND::RenderPreview:
        case AI_SESSION_OPERATION_KIND::ObserveStep:
        case AI_SESSION_OPERATION_KIND::CreateVia:
        case AI_SESSION_OPERATION_KIND::CreateTrackSegment:
        case AI_SESSION_OPERATION_KIND::CreateTrackPolyline:
        case AI_SESSION_OPERATION_KIND::CreateZone:
        case AI_SESSION_OPERATION_KIND::CreateShape:
        case AI_SESSION_OPERATION_KIND::MoveItems:
        case AI_SESSION_OPERATION_KIND::DeleteItems:
        case AI_SESSION_OPERATION_KIND::UpdateItemGeometry:
        case AI_SESSION_OPERATION_KIND::SetItemNet:
        case AI_SESSION_OPERATION_KIND::SetItemLayer:
        case AI_SESSION_OPERATION_KIND::SetItemProperties:
        case AI_SESSION_OPERATION_KIND::SetMetadata:
        default:
            payload["status"] = "not_reportable";
            break;
        }
    }

    if( !payload.contains( "kind" ) )
        payload["kind"] = toUtf8String( AiSessionOperationKindId( aOperation.m_Kind ) );

    if( !payload.contains( "arguments" ) )
        payload["arguments"] = arguments;

    payload["operation_ids"] = operationIdsJson( aExecution.m_OperationIds );
    payload["board_mutated"] = false;
    return payload;
}


bool applyValidationServiceResult( AI_EXECUTION_SESSION& aSession,
                                   const wxString& aArgumentsJson,
                                   AI_ATOMIC_EXECUTION_RESULT& aExecution,
                                   AI_SESSION_VALIDATION_SERVICE* aValidationService,
                                   wxString& aErrorCode, wxString& aMessage )
{
    if( !aValidationService )
        return true;

    AI_SESSION_VALIDATION_RESULT validationResult =
            aValidationService->RunValidation( aSession, aArgumentsJson,
                                               aExecution.m_ResultJson );

    if( !validationResult.m_Ok )
    {
        aErrorCode = validationResult.m_ErrorCode.IsEmpty()
                             ? wxString( wxS( "native_validation_failed" ) )
                             : validationResult.m_ErrorCode;
        aMessage = validationResult.m_Message.IsEmpty()
                           ? wxString( wxS( "Native session validation failed." ) )
                           : validationResult.m_Message;
        return false;
    }

    if( !validationResult.m_ResultJson.IsEmpty() )
        aExecution.m_ResultJson = validationResult.m_ResultJson;

    aExecution.m_Warnings = validationResult.m_Warnings;

    for( uint64_t operationId : aExecution.m_OperationIds )
    {
        aSession.UpdateOperationResult( operationId, aExecution.m_ResultJson,
                                        aExecution.m_Warnings );
    }

    return true;
}


std::optional<AI_SESSION_HANDLE> resolveQueryHandle(
        const AI_EXECUTION_SESSION& aSession, const nlohmann::json& aArguments )
{
    if( aArguments.contains( "alias" ) && aArguments["alias"].is_string() )
    {
        return aSession.ResolveAlias(
                wxString::FromUTF8(
                        aArguments["alias"].get_ref<const std::string&>().c_str() ) );
    }

    if( !aArguments.contains( "handle" ) )
        return std::nullopt;

    const nlohmann::json& handleJson = aArguments["handle"];

    if( handleJson.is_string() )
    {
        return aSession.ResolveAlias(
                wxString::FromUTF8( handleJson.get_ref<const std::string&>().c_str() ) );
    }

    AI_SESSION_HANDLE handle;
    handle.m_SessionId = aSession.SessionId();
    handle.m_Generation = 1;

    if( handleJson.is_number_unsigned() )
    {
        handle.m_HandleId = handleJson.get<uint64_t>();
    }
    else if( handleJson.is_object() && handleJson.contains( "handle_id" ) )
    {
        handle.m_HandleId = handleJson["handle_id"].get<uint64_t>();

        if( handleJson.contains( "session_id" ) && handleJson["session_id"].is_number() )
            handle.m_SessionId = handleJson["session_id"].get<uint64_t>();

        if( handleJson.contains( "generation" ) && handleJson["generation"].is_number() )
            handle.m_Generation = handleJson["generation"].get<uint64_t>();

        if( handleJson.contains( "alias" ) && handleJson["alias"].is_string() )
        {
            handle.m_Alias = wxString::FromUTF8(
                    handleJson["alias"].get_ref<const std::string&>().c_str() );
        }
    }
    else
    {
        return std::nullopt;
    }

    if( aSession.ResolveHandle( handle ) != AI_SESSION_HANDLE_STATUS::Live )
        return std::nullopt;

    if( handle.m_Alias.IsEmpty() )
    {
        if( const AI_SHADOW_ITEM* item = aSession.ShadowBoard().FindItem( handle ) )
            handle.m_Alias = item->m_Alias;
    }

    return handle;
}


std::string issueStringField( const nlohmann::json& aIssue, const char* aKey,
                              const char* aFallback = "" )
{
    if( aIssue.contains( aKey ) && aIssue[aKey].is_string() )
        return aIssue[aKey].get_ref<const std::string&>();

    return aFallback;
}


std::optional<AI_SESSION_HANDLE> resolveValidationIssueHandle(
        const AI_EXECUTION_SESSION& aSession, const nlohmann::json& aIssue )
{
    nlohmann::json handleArgs = nlohmann::json::object();

    if( aIssue.contains( "handle" ) )
        handleArgs["handle"] = aIssue["handle"];

    if( aIssue.contains( "alias" ) && aIssue["alias"].is_string() )
        handleArgs["alias"] = aIssue["alias"];

    if( !handleArgs.empty() )
    {
        if( std::optional<AI_SESSION_HANDLE> handle =
                    resolveQueryHandle( aSession, handleArgs ) )
        {
            return handle;
        }
    }

    if( aIssue.contains( "main_item_uuid" ) && aIssue["main_item_uuid"].is_string() )
    {
        const wxString uuid = wxString::FromUTF8(
                aIssue["main_item_uuid"].get_ref<const std::string&>().c_str() );

        for( const AI_SHADOW_ITEM& item : aSession.ShadowBoard().QueryItems() )
        {
            auto liveUuidIt = item.m_Metadata.find( wxS( "live_uuid" ) );

            if( liveUuidIt != item.m_Metadata.end() && liveUuidIt->second == uuid )
                return item.m_Handle;
        }
    }

    return std::nullopt;
}


std::map<wxString, wxString> clearedValidationMetadata()
{
    return {
        { wxS( "validation_status" ), wxEmptyString },
        { wxS( "validation_severity" ), wxEmptyString },
        { wxS( "validation_message" ), wxEmptyString },
        { wxS( "validation_issue_code" ), wxEmptyString },
        { wxS( "validation_geometry" ), wxEmptyString },
        { wxS( "validation_position" ), wxEmptyString },
        { wxS( "validation_layer" ), wxEmptyString }
    };
}


void clearValidationMetadata( AI_EXECUTION_SESSION& aSession )
{
    const std::map<wxString, wxString> emptyMetadata = clearedValidationMetadata();

    for( const AI_SHADOW_ITEM& item : aSession.ShadowBoard().QueryItems() )
        aSession.ShadowBoard().SetMetadata( item.m_Handle, emptyMetadata, aSession.Epoch() );
}


std::map<wxString, wxString> validationMetadataForIssue(
        const nlohmann::json& aIssue )
{
    const std::string severity = issueStringField( aIssue, "severity", "warning" );
    std::string message = issueStringField( aIssue, "message" );

    if( message.empty() )
        message = issueStringField( aIssue, "title" );

    if( message.empty() )
        message = issueStringField( aIssue, "code", severity.c_str() );

    std::map<wxString, wxString> metadata = {
        { wxS( "validation_status" ), wxString::FromUTF8( severity.c_str() ) },
        { wxS( "validation_severity" ), wxString::FromUTF8( severity.c_str() ) },
        { wxS( "validation_message" ), wxString::FromUTF8( message.c_str() ) },
        { wxS( "validation_issue_code" ),
          wxString::FromUTF8( issueStringField( aIssue, "code" ).c_str() ) }
    };

    if( aIssue.contains( "position" ) && aIssue["position"].is_object() )
        metadata[wxS( "validation_geometry" )] =
                fromJson( { { "position", aIssue["position"] } } );
    else if( aIssue.contains( "geometry" ) && aIssue["geometry"].is_object() )
        metadata[wxS( "validation_geometry" )] = fromJson( aIssue["geometry"] );

    if( aIssue.contains( "layer_name" ) && aIssue["layer_name"].is_string() )
    {
        metadata[wxS( "validation_layer" )] = wxString::FromUTF8(
                aIssue["layer_name"].get_ref<const std::string&>().c_str() );
    }
    else if( aIssue.contains( "layer" ) && aIssue["layer"].is_string() )
    {
        metadata[wxS( "validation_layer" )] = wxString::FromUTF8(
                aIssue["layer"].get_ref<const std::string&>().c_str() );
    }

    return metadata;
}


size_t projectValidationIssuesToShadowMetadata(
        AI_EXECUTION_SESSION& aSession, AI_ATOMIC_EXECUTION_RESULT& aExecution )
{
    nlohmann::json payload = parseObjectJson( aExecution.m_ResultJson );

    if( !payload.contains( "validation" ) || !payload["validation"].is_object() )
        return 0;

    clearValidationMetadata( aSession );

    nlohmann::json& validation = payload["validation"];
    const nlohmann::json issues =
            validation.contains( "issues" ) && validation["issues"].is_array()
                    ? validation["issues"]
                    : nlohmann::json::array();

    size_t projectedCount = 0;

    for( const nlohmann::json& issue : issues )
    {
        if( !issue.is_object() )
            continue;

        std::optional<AI_SESSION_HANDLE> handle =
                resolveValidationIssueHandle( aSession, issue );

        if( !handle )
            continue;

        if( aSession.ShadowBoard().SetMetadata(
                    *handle, validationMetadataForIssue( issue ), aSession.Epoch() ) )
        {
            ++projectedCount;
        }
    }

    validation["projected_overlay_count"] = projectedCount;
    aExecution.m_ResultJson = fromJson( payload );

    for( uint64_t operationId : aExecution.m_OperationIds )
    {
        aSession.UpdateOperationResult( operationId, aExecution.m_ResultJson,
                                        aExecution.m_Warnings );
    }

    return projectedCount;
}


struct SESSION_OPERATION_QUERY_RESULT
{
    bool           m_Ok = true;
    wxString       m_ErrorCode;
    wxString       m_Message;
    nlohmann::json m_Payload = nlohmann::json::object();
};


SESSION_OPERATION_QUERY_RESULT renderSessionPreviewOperation(
        AI_EXECUTION_SESSION& aSession, AI_SESSION_PREVIEW_SERVICE* aPreviewService,
        const wxString& aArgumentsJson )
{
    SESSION_OPERATION_QUERY_RESULT result;
    const nlohmann::json arguments = parseObjectJson( aArgumentsJson );
    result.m_Payload = {
        { "kind", toUtf8String( AiSessionOperationKindId(
                            AI_SESSION_OPERATION_KIND::RenderPreview ) ) },
        { "arguments", arguments },
        { "board_mutated", false }
    };

    if( !aPreviewService )
    {
        result.m_Payload["status"] = "preview_render_not_connected";
        result.m_Message = wxS( "Native preview manager is not connected in this layer yet." );
        return result;
    }

    AI_SESSION_PREVIEW_RESULT previewResult =
            aPreviewService->RenderPreview( aSession, aArgumentsJson );

    if( !previewResult.m_Ok )
    {
        result.m_Ok = false;
        result.m_ErrorCode = previewResult.m_ErrorCode.IsEmpty()
                                      ? wxString( wxS( "preview_render_failed" ) )
                                      : previewResult.m_ErrorCode;
        result.m_Message = previewResult.m_Message.IsEmpty()
                                   ? wxString( wxS( "Native preview rendering failed." ) )
                                   : previewResult.m_Message;
        result.m_Payload["status"] = "preview_render_failed";
        result.m_Payload["error_code"] = toUtf8String( result.m_ErrorCode );
        result.m_Payload["message"] = toUtf8String( result.m_Message );
        return result;
    }

    nlohmann::json servicePayload = parseObjectJson( previewResult.m_ResultJson );

    for( auto& [key, value] : servicePayload.items() )
        result.m_Payload[key] = value;

    if( !result.m_Payload.contains( "status" ) )
        result.m_Payload["status"] = "preview_rendered";

    result.m_Payload["preview_id"] = previewResult.m_PreviewId;
    result.m_Payload["rendered_item_count"] = previewResult.m_RenderedItemCount;
    result.m_Payload["board_mutated"] = false;
    result.m_Message = previewResult.m_Message.IsEmpty()
                               ? wxString( wxS( "Session preview rendered." ) )
                               : previewResult.m_Message;
    return result;
}


void clearSessionPreview( AI_SESSION_PREVIEW_SERVICE* aPreviewService, uint64_t aSessionId )
{
    if( aPreviewService && aSessionId != 0 )
        aPreviewService->ClearPreview( aSessionId );
}


void stopSessionPythonWorker( AI_PYTHON_WORKER* aPythonWorker )
{
    if( aPythonWorker )
        aPythonWorker->Cancel();
}


SESSION_OPERATION_QUERY_RESULT runSessionObservationOperation(
        AI_EXECUTION_SESSION& aSession, const AI_PYTHON_OPERATION_REQUEST& aOperation,
        AI_SESSION_PREVIEW_SERVICE* aPreviewService,
        const AI_PROVIDER_REQUEST& aRequest,
        const nlohmann::json* aPythonEventTimeline = nullptr )
{
    SESSION_OPERATION_QUERY_RESULT result;
    const nlohmann::json arguments = parseObjectJson( aOperation.m_ArgumentsJson );
    const nlohmann::json context = contextPayloadJson( aRequest );
    result.m_Payload = {
        { "kind", toUtf8String( AiSessionOperationKindId( aOperation.m_Kind ) ) },
        { "arguments", arguments },
        { "board_mutated", false }
    };

    auto finish = [&]( SESSION_OPERATION_QUERY_RESULT aResult )
    {
        if( aSession.HasOpenStep() )
        {
            AI_SESSION_OPERATION_RECORD record;
            record.m_Kind = aOperation.m_Kind;
            record.m_ArgumentsJson = aOperation.m_ArgumentsJson;
            record.m_ResultJson = fromJson( aResult.m_Payload );

            const AI_SESSION_OPERATION_RECORD& appended =
                    aSession.AppendOperation( std::move( record ) );

            aResult.m_Payload["operation_ids"] =
                    operationIdsJson( std::vector<uint64_t>{ appended.m_Id } );
            aResult.m_Payload["journaled"] = true;
        }
        else
        {
            aResult.m_Payload["journaled"] = false;
        }

        return aResult;
    };

    switch( aOperation.m_Kind )
    {
    case AI_SESSION_OPERATION_KIND::QueryBoardSummary:
        result.m_Payload["status"] = "board_summary";
        result.m_Payload["summary"] =
                parseObjectJson( aSession.ShadowBoard().QueryBoardSummary() );
        return finish( std::move( result ) );

    case AI_SESSION_OPERATION_KIND::QueryItems:
    {
        wxString filterJson = wxS( "{}" );

        if( arguments.contains( "filter" ) && arguments["filter"].is_object() )
            filterJson = fromJson( arguments["filter"] );

        nlohmann::json items = nlohmann::json::array();

        for( const AI_SHADOW_ITEM& item : aSession.ShadowBoard().QueryItems( filterJson ) )
            items.push_back( shadowItemJson( item ) );

        nlohmann::json filter = parseObjectJson( filterJson );

        result.m_Payload["status"] = "items";
        result.m_Payload["filter"] = filter;
        result.m_Payload["items"] = std::move( items );

        if( selectionFilterRequested( filter ) )
            result.m_Payload["selection_revision"] =
                    selectionRevisionJson( aSession, aRequest );

        return finish( std::move( result ) );
    }

    case AI_SESSION_OPERATION_KIND::QueryItem:
    {
        result.m_Payload["status"] = "item";

        if( std::optional<AI_SESSION_HANDLE> handle =
                    resolveQueryHandle( aSession, arguments ) )
        {
            if( const AI_SHADOW_ITEM* item = aSession.ShadowBoard().FindItem( *handle ) )
            {
                result.m_Payload["found"] = true;
                result.m_Payload["item"] = shadowItemJson( *item );
                return finish( std::move( result ) );
            }
        }

        result.m_Payload["found"] = false;
        result.m_Payload["item"] = nullptr;
        return finish( std::move( result ) );
    }

    case AI_SESSION_OPERATION_KIND::QuerySelection:
    {
        nlohmann::json selectedObjects =
                context.contains( "selected_objects" ) && context["selected_objects"].is_array()
                        ? context["selected_objects"]
                        : nlohmann::json::array();

        size_t contextSelectedCount =
                context.value( "selected_object_count", static_cast<size_t>( 0 ) );

        if( selectedObjects.is_array() && !selectedObjects.empty() )
            contextSelectedCount = selectedObjects.size();

        nlohmann::json selectedShadowItems = nlohmann::json::array();
        nlohmann::json selectedHandles = nlohmann::json::array();

        for( const AI_SHADOW_ITEM& item :
             aSession.ShadowBoard().QueryItems( wxS( "{\"selection\":true}" ) ) )
        {
            selectedShadowItems.push_back( shadowItemJson( item ) );
            selectedHandles.push_back( handleJson( item.m_Handle ) );
        }

        const size_t selectedShadowCount = selectedShadowItems.size();
        result.m_Payload["status"] = "selection";
        result.m_Payload["selected_count"] =
                contextSelectedCount + selectedShadowCount;
        result.m_Payload["selected_context_count"] = contextSelectedCount;
        result.m_Payload["selected_shadow_count"] = selectedShadowCount;
        result.m_Payload["selection_revision"] =
                selectionRevisionJson( aSession, aRequest );
        result.m_Payload["selected_objects"] = std::move( selectedObjects );
        result.m_Payload["selected_shadow_items"] = std::move( selectedShadowItems );
        result.m_Payload["selected_handles"] = std::move( selectedHandles );
        return finish( std::move( result ) );
    }

    case AI_SESSION_OPERATION_KIND::QueryNets:
    {
        std::set<std::string> nets;
        std::set<std::string> layers;

        if( context.contains( "visible_objects" ) && context["visible_objects"].is_array() )
        {
            for( const nlohmann::json& object : context["visible_objects"] )
                collectNetAndLayerHintsFromContextObject( object, nets, layers );
        }

        if( context.contains( "selected_objects" ) && context["selected_objects"].is_array() )
        {
            for( const nlohmann::json& object : context["selected_objects"] )
                collectNetAndLayerHintsFromContextObject( object, nets, layers );
        }

        for( const AI_SHADOW_ITEM& item : aSession.ShadowBoard().QueryItems( wxS( "{}" ) ) )
        {
            if( !item.m_Net.IsEmpty() )
                nets.insert( toUtf8String( item.m_Net ) );
        }

        result.m_Payload["status"] = "nets";
        result.m_Payload["nets"] = stringSetJson( nets );
        return finish( std::move( result ) );
    }

    case AI_SESSION_OPERATION_KIND::QueryLayers:
    {
        std::set<std::string> nets;
        std::set<std::string> layers;

        if( context.contains( "visible_objects" ) && context["visible_objects"].is_array() )
        {
            for( const nlohmann::json& object : context["visible_objects"] )
                collectNetAndLayerHintsFromContextObject( object, nets, layers );
        }

        if( context.contains( "selected_objects" ) && context["selected_objects"].is_array() )
        {
            for( const nlohmann::json& object : context["selected_objects"] )
                collectNetAndLayerHintsFromContextObject( object, nets, layers );
        }

        for( const AI_SHADOW_ITEM& item : aSession.ShadowBoard().QueryItems( wxS( "{}" ) ) )
        {
            if( !item.m_Layer.IsEmpty() )
                layers.insert( toUtf8String( item.m_Layer ) );

            for( const wxString& layer : item.m_Layers )
            {
                if( !layer.IsEmpty() )
                    layers.insert( toUtf8String( layer ) );
            }
        }

        result.m_Payload["status"] = "layers";
        result.m_Payload["layers"] = stringSetJson( layers );
        return finish( std::move( result ) );
    }

    case AI_SESSION_OPERATION_KIND::QueryDesignRules:
        if( std::optional<nlohmann::json> rules =
                    findNestedObjectByName( context, "design_rules" ) )
        {
            result.m_Payload["status"] = "design_rules";
            result.m_Payload["design_rules"] = *rules;
        }
        else
        {
            result.m_Payload["status"] = "design_rules_unavailable";
            result.m_Payload["design_rules"] = nlohmann::json::object();
            result.m_Payload["message"] =
                    "No design rule snapshot is attached to the current editor context.";
        }

        return finish( std::move( result ) );

    case AI_SESSION_OPERATION_KIND::QueryViewport:
    {
        nlohmann::json viewport = nlohmann::json::object();

        if( std::optional<nlohmann::json> contextViewport =
                    findNestedObjectByName( context, "viewport" ) )
        {
            viewport = *contextViewport;
        }

        result.m_Payload["status"] = "viewport";
        result.m_Payload["viewport"] = std::move( viewport );

        if( context.contains( "tool_state" ) && context["tool_state"].is_object() )
        {
            const nlohmann::json& toolState = context["tool_state"];

            if( toolState.contains( "cursor" ) )
                result.m_Payload["cursor"] = toolState["cursor"];

            if( toolState.contains( "kind" ) )
                result.m_Payload["tool_state_kind"] = toolState["kind"];

            if( toolState.contains( "active_action" ) )
                result.m_Payload["active_action"] = toolState["active_action"];
        }

        if( context.contains( "visual" ) )
            result.m_Payload["visual"] = context["visual"];

        if( context.contains( "version" ) )
            result.m_Payload["version"] = context["version"];

        return finish( std::move( result ) );
    }

    case AI_SESSION_OPERATION_KIND::QueryActivityTimeline:
        result.m_Payload["status"] = "activity_timeline";
        result.m_Payload["event_count"] =
                context.value( "recent_activity_count", static_cast<size_t>( 0 ) );
        result.m_Payload["events"] =
                context.contains( "recent_activity" ) && context["recent_activity"].is_array()
                        ? context["recent_activity"]
                        : nlohmann::json::array();
        result.m_Payload["python_events"] =
                aPythonEventTimeline && aPythonEventTimeline->is_array()
                        ? *aPythonEventTimeline
                        : nlohmann::json::array();
        result.m_Payload["python_event_count"] =
                result.m_Payload["python_events"].size();
        result.m_Payload["session_operation_count"] =
                aSession.Journal().Operations().size();
        result.m_Payload["session_operations"] = journalOperationsJson( aSession );
        return finish( std::move( result ) );

    case AI_SESSION_OPERATION_KIND::RenderPreview:
        return finish( renderSessionPreviewOperation( aSession, aPreviewService,
                                                      aOperation.m_ArgumentsJson ) );

    case AI_SESSION_OPERATION_KIND::ObserveStep:
    {
        uint64_t stepId = 0;
        wxString error;

        if( !getRequiredUint64( arguments, "step_id", stepId, error ) )
        {
            result.m_Ok = false;
            result.m_ErrorCode = wxS( "malformed_arguments" );
            result.m_Message = error;
            result.m_Payload["status"] = "malformed_arguments";
            result.m_Payload["error_code"] = toUtf8String( result.m_ErrorCode );
            result.m_Payload["message"] = toUtf8String( result.m_Message );
            return result;
        }

        AI_SESSION_OBSERVATION observation = aSession.ObserveStep( stepId );
        result.m_Payload["status"] = "step_observed";
        result.m_Payload["observation"] = parseObjectJson( observation.AsJsonText() );
        return finish( std::move( result ) );
    }

    case AI_SESSION_OPERATION_KIND::Unknown:
    default:
        result.m_Ok = false;
        result.m_ErrorCode = wxS( "unsupported_operation" );
        result.m_Message = wxString::Format(
                wxS( "Unsupported session observation operation '%s'." ),
                AiSessionOperationKindId( aOperation.m_Kind ) );
        result.m_Payload["status"] = "unsupported_operation";
        result.m_Payload["error_code"] = toUtf8String( result.m_ErrorCode );
        result.m_Payload["message"] = toUtf8String( result.m_Message );
        return finish( std::move( result ) );
    }
}
} // namespace


AI_SESSION_TOOL_CALL_HANDLER::AI_SESSION_TOOL_CALL_HANDLER(
        std::unique_ptr<AI_PYTHON_WORKER> aPythonWorker ) :
        m_PythonWorker( std::move( aPythonWorker ) )
{
}


AI_SESSION_TOOL_CALL_HANDLER::AI_SESSION_TOOL_CALL_HANDLER(
        std::unique_ptr<AI_PYTHON_WORKER> aPythonWorker,
        AI_ACCEPT_APPLY_ADAPTER* aAcceptAdapter ) :
        m_PythonWorker( std::move( aPythonWorker ) ),
        m_AcceptAdapter( aAcceptAdapter )
{
}


AI_SESSION_TOOL_CALL_HANDLER::AI_SESSION_TOOL_CALL_HANDLER(
        std::unique_ptr<AI_PYTHON_WORKER> aPythonWorker,
        AI_ACCEPT_APPLY_ADAPTER* aAcceptAdapter,
        AI_SESSION_PREVIEW_SERVICE* aPreviewService ) :
        m_PythonWorker( std::move( aPythonWorker ) ),
        m_AcceptAdapter( aAcceptAdapter ),
        m_PreviewService( aPreviewService )
{
}


AI_SESSION_TOOL_CALL_HANDLER::AI_SESSION_TOOL_CALL_HANDLER(
        std::unique_ptr<AI_PYTHON_WORKER> aPythonWorker,
        AI_ACCEPT_APPLY_ADAPTER* aAcceptAdapter,
        AI_SESSION_PREVIEW_SERVICE* aPreviewService,
        AI_SESSION_SHADOW_BOARD_SEEDER* aShadowBoardSeeder ) :
        m_PythonWorker( std::move( aPythonWorker ) ),
        m_AcceptAdapter( aAcceptAdapter ),
        m_PreviewService( aPreviewService ),
        m_ShadowBoardSeeder( aShadowBoardSeeder )
{
}


AI_SESSION_TOOL_CALL_HANDLER::AI_SESSION_TOOL_CALL_HANDLER(
        std::unique_ptr<AI_PYTHON_WORKER> aPythonWorker,
        AI_ACCEPT_APPLY_ADAPTER* aAcceptAdapter,
        AI_SESSION_PREVIEW_SERVICE* aPreviewService,
        AI_SESSION_SHADOW_BOARD_SEEDER* aShadowBoardSeeder,
        AI_SESSION_VALIDATION_SERVICE* aValidationService ) :
        m_PythonWorker( std::move( aPythonWorker ) ),
        m_AcceptAdapter( aAcceptAdapter ),
        m_PreviewService( aPreviewService ),
        m_ShadowBoardSeeder( aShadowBoardSeeder ),
        m_ValidationService( aValidationService )
{
}


void AI_SESSION_TOOL_CALL_HANDLER::rememberCheckpointPreviewState(
        uint64_t aCheckpointId )
{
    m_CheckpointPreviewStates[aCheckpointId] = m_CurrentPreviewState;
}


void AI_SESSION_TOOL_CALL_HANDLER::rememberRenderedPreview(
        const wxString& aArgumentsJson )
{
    if( !m_PreviewService )
        return;

    m_CurrentPreviewState.m_HasPreview = true;
    m_CurrentPreviewState.m_ArgumentsJson =
            aArgumentsJson.IsEmpty() ? wxString( wxS( "{}" ) ) : aArgumentsJson;
}


nlohmann::json AI_SESSION_TOOL_CALL_HANDLER::restorePreviewForCheckpoint(
        uint64_t aCheckpointId )
{
    nlohmann::json payload = {
        { "preview_restored", false },
        { "preview", nlohmann::json::object() }
    };

    if( !m_Session )
        return payload;

    clearSessionPreview( m_PreviewService, m_Session->SessionId() );

    const auto stateIt = m_CheckpointPreviewStates.find( aCheckpointId );

    if( stateIt == m_CheckpointPreviewStates.end() || !stateIt->second.m_HasPreview
        || !m_PreviewService )
    {
        m_CurrentPreviewState = PREVIEW_RESTORE_STATE();
        return payload;
    }

    SESSION_OPERATION_QUERY_RESULT previewResult =
            renderSessionPreviewOperation( *m_Session, m_PreviewService,
                                           stateIt->second.m_ArgumentsJson );

    payload["preview"] = previewResult.m_Payload;

    if( previewResult.m_Ok )
    {
        m_CurrentPreviewState = stateIt->second;
        payload["preview_restored"] = true;
    }
    else
    {
        m_CurrentPreviewState = PREVIEW_RESTORE_STATE();
        payload["preview_restore_failed"] = true;
        payload["preview_restore_error_code"] =
                toUtf8String( previewResult.m_ErrorCode );
        payload["preview_restore_message"] =
                toUtf8String( previewResult.m_Message );
    }

    return payload;
}


void AI_SESSION_TOOL_CALL_HANDLER::clearPreviewState()
{
    m_CurrentPreviewState = PREVIEW_RESTORE_STATE();
    m_CheckpointPreviewStates.clear();
}


void AI_SESSION_TOOL_CALL_HANDLER::beginPythonEventCapture( const wxString& aCellId )
{
    std::lock_guard<std::mutex> lock( m_PythonEventMutex );
    m_ActivePythonEventCellId = aCellId;
    m_CurrentPythonEvents.clear();
}


void AI_SESSION_TOOL_CALL_HANDLER::finishPythonEventCapture()
{
    std::lock_guard<std::mutex> lock( m_PythonEventMutex );
    m_ActivePythonEventCellId.clear();
}


void AI_SESSION_TOOL_CALL_HANDLER::OnPythonEvent( const AI_PYTHON_EVENT& aEvent )
{
    std::lock_guard<std::mutex> lock( m_PythonEventMutex );

    RECORDED_PYTHON_EVENT recorded;
    recorded.m_Sequence = m_NextPythonEventSequence++;
    recorded.m_CellId = m_ActivePythonEventCellId;
    recorded.m_Source = wxS( "stream" );
    recorded.m_Event = aEvent;
    m_PythonEventTimeline.push_back( recorded );
    m_CurrentPythonEvents.push_back( std::move( recorded ) );
}


void AI_SESSION_TOOL_CALL_HANDLER::recordPythonCellResultEvents(
        const wxString& aCellId, const std::vector<AI_PYTHON_EVENT>& aEvents )
{
    std::lock_guard<std::mutex> lock( m_PythonEventMutex );

    for( const AI_PYTHON_EVENT& event : aEvents )
    {
        const bool alreadyRecorded =
                std::any_of( m_CurrentPythonEvents.begin(), m_CurrentPythonEvents.end(),
                             [&]( const RECORDED_PYTHON_EVENT& aRecorded )
                             {
                                 return aRecorded.m_CellId == aCellId
                                        && samePythonEvent( aRecorded.m_Event, event );
                             } );

        if( alreadyRecorded )
            continue;

        RECORDED_PYTHON_EVENT recorded;
        recorded.m_Sequence = m_NextPythonEventSequence++;
        recorded.m_CellId = aCellId;
        recorded.m_Source = wxS( "cell_result" );
        recorded.m_Event = event;
        m_PythonEventTimeline.push_back( recorded );
        m_CurrentPythonEvents.push_back( std::move( recorded ) );
    }
}


nlohmann::json AI_SESSION_TOOL_CALL_HANDLER::currentPythonEventsJson() const
{
    std::lock_guard<std::mutex> lock( m_PythonEventMutex );
    nlohmann::json events = nlohmann::json::array();

    for( const RECORDED_PYTHON_EVENT& recorded : m_CurrentPythonEvents )
    {
        nlohmann::json event = pythonEventJson( recorded.m_Event );
        event["sequence"] = recorded.m_Sequence;
        event["cell_id"] = toUtf8String( recorded.m_CellId );
        event["source"] = toUtf8String( recorded.m_Source );
        events.push_back( std::move( event ) );
    }

    return events;
}


nlohmann::json AI_SESSION_TOOL_CALL_HANDLER::pythonEventTimelineJson() const
{
    std::lock_guard<std::mutex> lock( m_PythonEventMutex );
    nlohmann::json events = nlohmann::json::array();

    for( const RECORDED_PYTHON_EVENT& recorded : m_PythonEventTimeline )
    {
        nlohmann::json event = pythonEventJson( recorded.m_Event );
        event["sequence"] = recorded.m_Sequence;
        event["cell_id"] = toUtf8String( recorded.m_CellId );
        event["source"] = toUtf8String( recorded.m_Source );
        events.push_back( std::move( event ) );
    }

    return events;
}


AI_TOOL_INVOCATION_RESULT AI_SESSION_TOOL_CALL_HANDLER::HandleToolCall(
        const AI_PROVIDER_REQUEST& aRequest, const AI_TOOL_CALL_RECORD& aToolCall )
{
    if( !isSessionTool( aToolCall.m_ToolName ) )
    {
        return deniedResult( aRequest, aToolCall, wxS( "unknown_tool" ),
                             wxS( "Tool is not an AI execution session tool." ) );
    }

    nlohmann::json arguments;

    try
    {
        const std::string text = toUtf8String( aToolCall.m_ArgumentsJson );
        arguments = text.empty() ? nlohmann::json::object() : nlohmann::json::parse( text );
    }
    catch( const std::exception& e )
    {
        return deniedResult( aRequest, aToolCall, wxS( "malformed_arguments" ),
                             wxString::FromUTF8( e.what() ) );
    }

    if( !arguments.is_object() )
    {
        return deniedResult( aRequest, aToolCall, wxS( "malformed_arguments" ),
                             wxS( "Tool call arguments must be an object." ) );
    }

    if( aToolCall.m_ToolName == wxS( "kisurf_open_session" ) )
    {
        if( m_Session && m_Session->Status() == AI_EXECUTION_SESSION_STATUS::Open )
        {
            return deniedResult( aRequest, aToolCall, wxS( "session_already_open" ),
                                 wxS( "Only one active AI execution session is allowed." ) );
        }

        wxString boardId = optionalString( arguments, "board_id" );
        wxString baseHash = optionalString( arguments, "base_hash" );

        AI_EXECUTION_SESSION& session = openSessionFromRequest(
                aRequest, boardId, baseHash.IsEmpty() ? contextBaseHash( aRequest ) : baseHash );

        return allowedResult( aRequest, aToolCall, true, wxS( "AI session opened." ),
                              { { "status", "session_open" },
                                { "session", sessionJson( session ) },
                                { "board_mutated", false } } );
    }

    if( aToolCall.m_ToolName == wxS( "kisurf_run_cell" ) )
    {
        if( !arguments.contains( "cell_text" ) || !arguments["cell_text"].is_string()
            || arguments["cell_text"].get_ref<const std::string&>().empty() )
        {
            return deniedResult( aRequest, aToolCall, wxS( "malformed_arguments" ),
                                 wxS( "kisurf_run_cell requires non-empty cell_text." ) );
        }

        if( !m_Session || m_Session->Status() != AI_EXECUTION_SESSION_STATUS::Open )
            openSessionFromRequest( aRequest, wxEmptyString, contextBaseHash( aRequest ) );

        if( m_Session->SelectionRevisionConflicts( effectiveContextVersion( aRequest ) ) )
        {
            return deniedResult( aRequest, aToolCall, wxS( "selection_conflict" ),
                                 wxS( "Session selection changed after it was opened. "
                                      "Query the current selection, roll back, or reject "
                                      "the session before running another cell." ) );
        }

        wxString cellId = optionalString( arguments, "cell_id" );
        wxString cellText = optionalString( arguments, "cell_text" );

        if( m_PythonWorker && m_PythonWorker->IsConnected() )
        {
            const uint64_t checkpointId =
                    m_Session->Checkpoint( cellCheckpointName( cellId ) );
            rememberCheckpointPreviewState( checkpointId );
            const AI_PYTHON_CELL_REQUEST cellRequest =
                    pythonCellRequest( *m_Session, cellText, cellId );
            beginPythonEventCapture( cellId );
            m_PythonWorker->SetEventSink( this );
            AI_PYTHON_CELL_RESULT workerResult =
                    m_PythonWorker->RunCell( *m_Session, cellRequest );
            m_PythonWorker->SetEventSink( nullptr );
            recordPythonCellResultEvents( cellId, workerResult.m_Events );

            wxString sessionContextErrorCode;
            wxString sessionContextMessage;

            if( !validatePythonCellResultSessionContext( cellRequest, workerResult,
                                                          sessionContextErrorCode,
                                                          sessionContextMessage ) )
            {
                workerResult.m_Ok = false;
                workerResult.m_ErrorCode = sessionContextErrorCode;
                workerResult.m_Message = sessionContextMessage;
                workerResult.m_RollbackOnError = true;
                workerResult.m_Operations.clear();
            }

            const bool hadOpenStep = m_Session->HasOpenStep();
            uint64_t stepId = 0;

            auto ensureCellStepOpen =
                    [&]() -> bool
                    {
                        if( m_Session->HasOpenStep() )
                            return true;

                        if( hadOpenStep )
                            return false;

                        stepId = m_Session->BeginStep( cellStepLabel( workerResult,
                                                                      cellId ) );
                        return stepId != 0;
                    };

            std::vector<uint64_t> operationIds;
            nlohmann::json operationResults = nlohmann::json::array();
            nlohmann::json stepResults = nlohmann::json::array();
            size_t appliedOperationCount = 0;
            bool hasBoardMutationInStep = false;
            bool hasExplicitValidation = false;
            bool hasExplicitPreview = false;
            wxString operationErrorCode;
            wxString operationMessage;

            for( const AI_PYTHON_OPERATION_REQUEST& operation : workerResult.m_Operations )
            {
                if( operation.m_Kind == AI_SESSION_OPERATION_KIND::RunValidation )
                    hasExplicitValidation = true;

                if( operation.m_Kind == AI_SESSION_OPERATION_KIND::RenderPreview )
                    hasExplicitPreview = true;

                if( operation.m_Kind == AI_SESSION_OPERATION_KIND::Checkpoint )
                {
                    const nlohmann::json controlArgs =
                            parseObjectJson( operation.m_ArgumentsJson );
                    wxString name = optionalString( controlArgs, "name" );

                    if( name.IsEmpty() )
                    {
                        operationErrorCode = wxS( "malformed_arguments" );
                        operationMessage = wxS( "session.checkpoint requires name." );
                        break;
                    }

                    const uint64_t controlCheckpointId =
                            m_Session->Checkpoint( name );
                    rememberCheckpointPreviewState( controlCheckpointId );
                    operationResults.push_back(
                            { { "kind", toUtf8String( AiSessionOperationKindId(
                                                  operation.m_Kind ) ) },
                              { "arguments", controlArgs },
                              { "status", "checkpoint_created" },
                              { "checkpoint_id", controlCheckpointId },
                              { "board_mutated", false } } );
                    continue;
                }

                if( operation.m_Kind == AI_SESSION_OPERATION_KIND::RollbackTo )
                {
                    const nlohmann::json controlArgs =
                            parseObjectJson( operation.m_ArgumentsJson );
                    uint64_t controlCheckpointId = 0;
                    wxString checkpointName;
                    wxString error;

                    if( !resolveCheckpointReference( *m_Session, controlArgs,
                                                     controlCheckpointId,
                                                     checkpointName, error ) )
                    {
                        operationErrorCode = wxS( "malformed_arguments" );
                        operationMessage = error;
                        break;
                    }

                    if( !m_Session->RollbackTo( controlCheckpointId ) )
                    {
                        operationErrorCode = wxS( "rollback_failed" );
                        operationMessage =
                                wxS( "Checkpoint id is not valid for this session." );
                        break;
                    }

                    nlohmann::json previewRestore =
                            restorePreviewForCheckpoint( controlCheckpointId );
                    nlohmann::json rollbackResult =
                            { { "kind", toUtf8String( AiSessionOperationKindId(
                                                  operation.m_Kind ) ) },
                              { "arguments", controlArgs },
                              { "status", "rolled_back" },
                              { "checkpoint_id", controlCheckpointId },
                              { "preview_restored",
                                previewRestore["preview_restored"] },
                              { "preview", previewRestore["preview"] },
                              { "board_mutated", false } };

                    if( !checkpointName.IsEmpty() )
                        rollbackResult["checkpoint_name"] = toUtf8String( checkpointName );

                    stepId = 0;
                    operationResults.push_back( std::move( rollbackResult ) );
                    continue;
                }

                if( !ensureCellStepOpen() )
                {
                    operationErrorCode = wxS( "step_not_started" );
                    operationMessage = wxS(
                            "Python cell operation requires an open session step." );
                    break;
                }

                if( !isMutationOperation( operation.m_Kind ) )
                {
                    nlohmann::json pythonEvents = pythonEventTimelineJson();
                    SESSION_OPERATION_QUERY_RESULT queryResult =
                            runSessionObservationOperation( *m_Session, operation,
                                                            m_PreviewService, aRequest,
                                                            &pythonEvents );

                    operationResults.push_back( queryResult.m_Payload );

                    if( !queryResult.m_Ok )
                    {
                        operationErrorCode = queryResult.m_ErrorCode;
                        operationMessage = queryResult.m_Message;
                        break;
                    }

                    if( operation.m_Kind == AI_SESSION_OPERATION_KIND::RenderPreview )
                        rememberRenderedPreview( operation.m_ArgumentsJson );

                    continue;
                }

                AI_ATOMIC_EXECUTION_RESULT execution = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
                        *m_Session, operation.m_Kind, operation.m_ArgumentsJson );

                if( !execution.m_Ok )
                {
                    operationErrorCode = execution.m_ErrorCode;
                    operationMessage = execution.m_Message;
                    break;
                }

                if( operation.m_Kind == AI_SESSION_OPERATION_KIND::RunValidation
                    && !applyValidationServiceResult( *m_Session,
                                                      operation.m_ArgumentsJson,
                                                      execution,
                                                      m_ValidationService,
                                                      operationErrorCode,
                                                      operationMessage ) )
                {
                    break;
                }

                if( operation.m_Kind == AI_SESSION_OPERATION_KIND::RunValidation )
                    projectValidationIssuesToShadowMetadata( *m_Session, execution );

                ++appliedOperationCount;
                operationIds.insert( operationIds.end(), execution.m_OperationIds.begin(),
                                     execution.m_OperationIds.end() );

                if( !isMaintenanceOperation( operation.m_Kind ) )
                    hasBoardMutationInStep = true;

                if( isMaintenanceOperation( operation.m_Kind ) )
                    operationResults.push_back( maintenanceOperationResult( operation, execution ) );
            }

            AI_SESSION_OBSERVATION observation;

            if( stepId != 0 && operationErrorCode.IsEmpty() && hasBoardMutationInStep )
            {
                if( !hasExplicitValidation && m_ValidationService )
                {
                    AI_PYTHON_OPERATION_REQUEST validationOperation;
                    validationOperation.m_Kind = AI_SESSION_OPERATION_KIND::RunValidation;
                    validationOperation.m_ArgumentsJson =
                            wxS( "{\"scope\":\"session\",\"level\":\"geometry\","
                                 "\"automatic\":true}" );

                    AI_ATOMIC_EXECUTION_RESULT execution =
                            AI_ATOMIC_OPERATION_EXECUTOR::Execute(
                                    *m_Session, validationOperation.m_Kind,
                                    validationOperation.m_ArgumentsJson );

                    if( !execution.m_Ok )
                    {
                        operationErrorCode = execution.m_ErrorCode;
                        operationMessage = execution.m_Message;
                    }
                    else if( !applyValidationServiceResult( *m_Session,
                                                            validationOperation.m_ArgumentsJson,
                                                            execution,
                                                            m_ValidationService,
                                                            operationErrorCode,
                                                            operationMessage ) )
                    {
                        // Error state is already populated by the validation helper.
                    }
                    else
                    {
                        projectValidationIssuesToShadowMetadata( *m_Session, execution );
                        ++appliedOperationCount;
                        operationIds.insert( operationIds.end(),
                                             execution.m_OperationIds.begin(),
                                             execution.m_OperationIds.end() );
                        stepResults.push_back(
                                maintenanceOperationResult( validationOperation, execution ) );
                    }
                }

                if( operationErrorCode.IsEmpty() && !hasExplicitPreview && m_PreviewService )
                {
                    AI_PYTHON_OPERATION_REQUEST previewOperation;
                    previewOperation.m_Kind = AI_SESSION_OPERATION_KIND::RenderPreview;
                    previewOperation.m_ArgumentsJson =
                            wxS( "{\"mode\":\"native\",\"automatic\":true}" );

                    SESSION_OPERATION_QUERY_RESULT previewResult =
                            runSessionObservationOperation( *m_Session, previewOperation,
                                                            m_PreviewService, aRequest );

                    stepResults.push_back( previewResult.m_Payload );

                    if( !previewResult.m_Ok )
                    {
                        operationErrorCode = previewResult.m_ErrorCode;
                        operationMessage = previewResult.m_Message;
                    }
                    else
                    {
                        if( previewResult.m_Payload.contains( "operation_ids" )
                            && previewResult.m_Payload["operation_ids"].is_array() )
                        {
                            for( const nlohmann::json& idJson :
                                 previewResult.m_Payload["operation_ids"] )
                            {
                                if( idJson.is_number_unsigned() )
                                    operationIds.push_back( idJson.get<uint64_t>() );
                            }
                        }

                        rememberRenderedPreview( previewOperation.m_ArgumentsJson );
                    }
                }
            }

            if( stepId != 0 && operationErrorCode.IsEmpty() )
                observation = m_Session->EndStep( stepId );

            const bool failed = !workerResult.m_Ok || !operationErrorCode.IsEmpty();
            nlohmann::json previewRestore = {
                { "preview_restored", false },
                { "preview", nlohmann::json::object() }
            };

            if( failed && workerResult.m_RollbackOnError )
            {
                m_Session->RollbackTo( checkpointId );
                previewRestore = restorePreviewForCheckpoint( checkpointId );
            }

            nlohmann::json recordedPythonEvents = currentPythonEventsJson();
            finishPythonEventCapture();

            if( failed )
            {
                wxString errorCode = !operationErrorCode.IsEmpty()
                                             ? operationErrorCode
                                             : workerResult.m_ErrorCode;

                if( errorCode.IsEmpty() )
                    errorCode = wxS( "python_cell_failed" );

                wxString message = !operationMessage.IsEmpty() ? operationMessage
                                                               : workerResult.m_Message;

                if( message.IsEmpty() )
                    message = wxS( "Python worker failed while running the cell." );

                AI_TOOL_INVOCATION_RESULT result =
                        allowedResult( aRequest, aToolCall, false, message,
                                       { { "status", "cell_failed" },
                                         { "cell_id", toUtf8String( cellId ) },
                                         { "python_worker", "connected" },
                                         { "sdk", pythonSdkJson( workerResult ) },
                                         { "error_code", toUtf8String( errorCode ) },
                                         { "rolled_back", workerResult.m_RollbackOnError },
                                         { "checkpoint_id", checkpointId },
                                         { "applied_operation_count",
                                          appliedOperationCount },
                                         { "operation_ids", operationIdsJson( operationIds ) },
                                         { "operation_results", operationResults },
                                         { "step_results", stepResults },
                                         { "events", pythonEventsJson( workerResult.m_Events ) },
                                         { "recorded_events", recordedPythonEvents },
                                         { "preview_restored",
                                           previewRestore["preview_restored"] },
                                         { "preview", previewRestore["preview"] },
                                         { "stdout", toUtf8String( workerResult.m_Stdout ) },
                                         { "stderr", toUtf8String( workerResult.m_Stderr ) },
                                         { "session", sessionJson( *m_Session ) },
                                         { "shadow_board_mutated", false },
                                         { "board_mutated", false } } );
                result.m_ErrorCode = errorCode;
                result.m_ResultJson = resultJson(
                        result,
                        { { "status", "cell_failed" },
                          { "cell_id", toUtf8String( cellId ) },
                          { "python_worker", "connected" },
                          { "sdk", pythonSdkJson( workerResult ) },
                          { "error_code", toUtf8String( errorCode ) },
                          { "rolled_back", workerResult.m_RollbackOnError },
                          { "checkpoint_id", checkpointId },
                          { "applied_operation_count", appliedOperationCount },
                          { "operation_ids", operationIdsJson( operationIds ) },
                          { "operation_results", operationResults },
                          { "step_results", stepResults },
                          { "events", pythonEventsJson( workerResult.m_Events ) },
                          { "recorded_events", recordedPythonEvents },
                          { "preview_restored", previewRestore["preview_restored"] },
                          { "preview", previewRestore["preview"] },
                          { "stdout", toUtf8String( workerResult.m_Stdout ) },
                          { "stderr", toUtf8String( workerResult.m_Stderr ) },
                          { "session", sessionJson( *m_Session ) },
                          { "shadow_board_mutated", false },
                          { "board_mutated", false } } );
                return result;
            }

            nlohmann::json observationJson = nlohmann::json::object();

            if( stepId != 0 )
                observationJson = nlohmann::json::parse( toUtf8String( observation.AsJsonText() ) );

            return allowedResult(
                    aRequest, aToolCall, true, wxS( "Python cell executed." ),
                    { { "status", "cell_executed" },
                      { "cell_id", toUtf8String( cellId ) },
                      { "python_worker", "connected" },
                      { "sdk", pythonSdkJson( workerResult ) },
                      { "step_id", stepId },
                      { "observation", std::move( observationJson ) },
                      { "applied_operation_count", appliedOperationCount },
                      { "operation_ids", operationIdsJson( operationIds ) },
                      { "operation_results", operationResults },
                      { "step_results", stepResults },
                      { "events", pythonEventsJson( workerResult.m_Events ) },
                      { "recorded_events", recordedPythonEvents },
                      { "session", sessionJson( *m_Session ) },
                      { "shadow_board_mutated", appliedOperationCount > 0 },
                      { "board_mutated", false } } );
        }

        return allowedResult(
                aRequest, aToolCall, false,
                wxS( "Python cell accepted by the session control plane." ),
                { { "status", "cell_received" },
                  { "cell_id", toUtf8String( cellId ) },
                  { "session", sessionJson( *m_Session ) },
                  { "python_worker", "not_connected" },
                  { "board_mutated", false } } );
    }

    if( aToolCall.m_ToolName == wxS( "kisurf_run_atomic_operation" ) )
    {
        if( !arguments.contains( "kind" ) || !arguments["kind"].is_string()
            || arguments["kind"].get_ref<const std::string&>().empty()
            || !arguments.contains( "arguments" ) || !arguments["arguments"].is_object() )
        {
            return deniedResult( aRequest, aToolCall, wxS( "malformed_arguments" ),
                                 wxS( "kisurf_run_atomic_operation requires kind and "
                                      "object arguments." ) );
        }

        const std::string kindName = arguments["kind"].get_ref<const std::string&>();
        const AI_SESSION_OPERATION_KIND operationKind =
                operationKindForAtomicOperationId( kindName );

        if( operationKind == AI_SESSION_OPERATION_KIND::Unknown )
        {
            return deniedResult(
                    aRequest, aToolCall, wxS( "unsupported_operation_kind" ),
                    wxString::Format( wxS( "Unsupported KiSurf atomic operation '%s'." ),
                                      wxString::FromUTF8( kindName.c_str() ) ) );
        }

        if( !m_Session || m_Session->Status() != AI_EXECUTION_SESSION_STATUS::Open )
            openSessionFromRequest( aRequest, wxEmptyString, contextBaseHash( aRequest ) );

        if( m_Session->SelectionRevisionConflicts( effectiveContextVersion( aRequest ) ) )
        {
            return deniedResult( aRequest, aToolCall, wxS( "selection_conflict" ),
                                 wxS( "Session selection changed after it was opened. "
                                      "Query the current selection, roll back, or reject "
                                      "the session before running another atomic operation." ) );
        }

        const bool hadOpenStep = m_Session->HasOpenStep();
        uint64_t   stepId = 0;

        if( !hadOpenStep )
        {
            stepId = m_Session->BeginStep(
                    wxString::Format( wxS( "atomic %s" ),
                                      wxString::FromUTF8( kindName.c_str() ) ) );

            if( stepId == 0 )
            {
                return deniedResult( aRequest, aToolCall, wxS( "step_not_started" ),
                                     wxS( "Unable to start an AI execution step for the "
                                          "atomic operation." ) );
            }
        }

        const wxString operationArgumentsJson = fromJson( arguments["arguments"] );
        AI_ATOMIC_EXECUTION_RESULT execution = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
                *m_Session, operationKind, operationArgumentsJson );

        if( !execution.m_Ok )
        {
            if( stepId != 0 )
                m_Session->FailStep( stepId, execution.m_Message );

            return deniedResult( aRequest, aToolCall, execution.m_ErrorCode,
                                 execution.m_Message );
        }

        wxString validationErrorCode;
        wxString validationMessage;

        if( operationKind == AI_SESSION_OPERATION_KIND::RunValidation
            && !applyValidationServiceResult( *m_Session, operationArgumentsJson,
                                              execution, m_ValidationService,
                                              validationErrorCode, validationMessage ) )
        {
            if( stepId != 0 )
                m_Session->FailStep( stepId, validationMessage );

            return deniedResult( aRequest, aToolCall, validationErrorCode,
                                 validationMessage );
        }

        if( operationKind == AI_SESSION_OPERATION_KIND::RunValidation )
            projectValidationIssuesToShadowMetadata( *m_Session, execution );

        AI_SESSION_OBSERVATION observation;

        if( stepId != 0 )
            observation = m_Session->EndStep( stepId );

        nlohmann::json payload = {
            { "status", "atomic_operation_executed" },
            { "kind", kindName },
            { "arguments", arguments["arguments"] },
            { "applied_operation_count", execution.m_OperationIds.size() },
            { "operation_ids", operationIdsJson( execution.m_OperationIds ) },
            { "created_handles", handlesJson( execution.m_CreatedHandles ) },
            { "resolved_handles", handlesJson( execution.m_ResolvedHandles ) },
            { "warnings", warningsJson( execution.m_Warnings ) },
            { "result", parseObjectJson( execution.m_ResultJson ) },
            { "step_id", stepId },
            { "observation",
              stepId != 0 ? parseObjectJson( observation.AsJsonText() )
                          : nlohmann::json::object() },
            { "session", sessionJson( *m_Session ) },
            { "shadow_board_mutated",
              isMutationOperation( operationKind ) && !execution.m_OperationIds.empty() },
            { "board_mutated", false }
        };

        return allowedResult( aRequest, aToolCall, true,
                              wxS( "KiSurf atomic operation executed in the session." ),
                              std::move( payload ) );
    }

    if( !m_Session || m_Session->Status() != AI_EXECUTION_SESSION_STATUS::Open )
    {
        return deniedResult( aRequest, aToolCall, wxS( "no_active_session" ),
                             wxS( "Open a KiSurf AI execution session first." ) );
    }

    if( aToolCall.m_ToolName == wxS( "kisurf_begin_step" ) )
    {
        wxString label = optionalString( arguments, "label" );

        if( label.IsEmpty() )
        {
            return deniedResult( aRequest, aToolCall, wxS( "malformed_arguments" ),
                                 wxS( "kisurf_begin_step requires label." ) );
        }

        wxString optionsJson = wxS( "{}" );

        if( arguments.contains( "options" ) && arguments["options"].is_object() )
            optionsJson = fromJson( arguments["options"] );

        const uint64_t stepId = m_Session->BeginStep( label, optionsJson );

        if( stepId == 0 )
        {
            return deniedResult( aRequest, aToolCall, wxS( "step_not_started" ),
                                 wxS( "The session already has an open step." ) );
        }

        return allowedResult( aRequest, aToolCall, true, wxS( "Step started." ),
                              { { "status", "step_open" },
                                { "step_id", stepId },
                                { "session", sessionJson( *m_Session ) },
                                { "board_mutated", false } } );
    }

    if( aToolCall.m_ToolName == wxS( "kisurf_end_step" )
        || aToolCall.m_ToolName == wxS( "kisurf_observe_step" ) )
    {
        uint64_t stepId = 0;
        wxString error;

        if( !getRequiredUint64( arguments, "step_id", stepId, error ) )
            return deniedResult( aRequest, aToolCall, wxS( "malformed_arguments" ), error );

        const bool endStep = aToolCall.m_ToolName == wxS( "kisurf_end_step" );
        AI_SESSION_OBSERVATION observation = endStep ? m_Session->EndStep( stepId )
                                                     : m_Session->ObserveStep( stepId );

        return allowedResult(
                aRequest, aToolCall, true, wxS( "Step observed." ),
                { { "status", endStep ? "step_completed" : "step_observed" },
                  { "observation",
                    nlohmann::json::parse( toUtf8String( observation.AsJsonText() ) ) },
                  { "session", sessionJson( *m_Session ) },
                  { "board_mutated", false } } );
    }

    if( aToolCall.m_ToolName == wxS( "kisurf_query_board_summary" ) )
    {
        return allowedResult(
                aRequest, aToolCall, false, wxS( "Shadow board summary returned." ),
                { { "status", "board_summary" },
                  { "summary",
                    nlohmann::json::parse(
                            toUtf8String( m_Session->ShadowBoard().QueryBoardSummary() ) ) },
                  { "session", sessionJson( *m_Session ) },
                  { "board_mutated", false } } );
    }

    if( aToolCall.m_ToolName == wxS( "kisurf_query_items" ) )
    {
        wxString filterJson = wxS( "{}" );

        if( arguments.contains( "filter" ) && arguments["filter"].is_object() )
            filterJson = fromJson( arguments["filter"] );

        nlohmann::json items = nlohmann::json::array();

        for( const AI_SHADOW_ITEM& item : m_Session->ShadowBoard().QueryItems( filterJson ) )
            items.push_back( shadowItemJson( item ) );

        nlohmann::json filter = nlohmann::json::parse( toUtf8String( filterJson ) );
        nlohmann::json payload = {
            { "status", "items" },
            { "filter", filter },
            { "items", std::move( items ) },
            { "session", sessionJson( *m_Session ) },
            { "board_mutated", false }
        };

        if( selectionFilterRequested( filter ) )
            payload["selection_revision"] =
                    selectionRevisionJson( *m_Session, aRequest );

        return allowedResult( aRequest, aToolCall, false,
                              wxS( "Shadow board items returned." ),
                              std::move( payload ) );
    }

    const AI_SESSION_OPERATION_KIND queryKind =
            operationKindForSessionQueryTool( aToolCall.m_ToolName );

    if( queryKind != AI_SESSION_OPERATION_KIND::Unknown )
    {
        AI_PYTHON_OPERATION_REQUEST operation;
        operation.m_Kind = queryKind;
        operation.m_ArgumentsJson = fromJson( arguments );
        nlohmann::json pythonEvents = pythonEventTimelineJson();
        SESSION_OPERATION_QUERY_RESULT queryResult =
                runSessionObservationOperation( *m_Session, operation, m_PreviewService,
                                                aRequest, &pythonEvents );
        nlohmann::json payload = queryResult.m_Payload;
        payload["session"] = sessionJson( *m_Session );

        if( !queryResult.m_Ok )
        {
            AI_TOOL_INVOCATION_RESULT result =
                    deniedResult( aRequest, aToolCall, queryResult.m_ErrorCode,
                                  queryResult.m_Message );
            result.m_ResultJson = resultJson( result, std::move( payload ) );
            return result;
        }

        return allowedResult( aRequest, aToolCall, false,
                              wxS( "Session observation returned." ),
                              std::move( payload ) );
    }

    if( aToolCall.m_ToolName == wxS( "kisurf_checkpoint" ) )
    {
        wxString name = optionalString( arguments, "name" );

        if( name.IsEmpty() )
        {
            return deniedResult( aRequest, aToolCall, wxS( "malformed_arguments" ),
                                 wxS( "kisurf_checkpoint requires name." ) );
        }

        const uint64_t checkpointId = m_Session->Checkpoint( name );
        rememberCheckpointPreviewState( checkpointId );

        return allowedResult( aRequest, aToolCall, true, wxS( "Checkpoint created." ),
                              { { "status", "checkpoint_created" },
                                { "checkpoint_id", checkpointId },
                                { "session", sessionJson( *m_Session ) },
                                { "board_mutated", false } } );
    }

    if( aToolCall.m_ToolName == wxS( "kisurf_rollback_to" ) )
    {
        uint64_t checkpointId = 0;
        wxString checkpointName;
        wxString error;

        if( !resolveCheckpointReference( *m_Session, arguments, checkpointId,
                                         checkpointName, error ) )
            return deniedResult( aRequest, aToolCall, wxS( "malformed_arguments" ), error );

        if( !m_Session->RollbackTo( checkpointId ) )
        {
            return deniedResult( aRequest, aToolCall, wxS( "rollback_failed" ),
                                 wxS( "Checkpoint id is not valid for this session." ) );
        }

        nlohmann::json previewRestore = restorePreviewForCheckpoint( checkpointId );
        nlohmann::json payload = { { "status", "rolled_back" },
                                   { "checkpoint_id", checkpointId },
                                   { "preview_restored",
                                     previewRestore["preview_restored"] },
                                   { "preview", previewRestore["preview"] },
                                   { "session", sessionJson( *m_Session ) },
                                   { "board_mutated", false } };

        if( !checkpointName.IsEmpty() )
            payload["checkpoint_name"] = toUtf8String( checkpointName );

        return allowedResult( aRequest, aToolCall, true, wxS( "Rolled back." ),
                              std::move( payload ) );
    }

    if( aToolCall.m_ToolName == wxS( "kisurf_render_preview" ) )
    {
        SESSION_OPERATION_QUERY_RESULT previewResult =
                renderSessionPreviewOperation( *m_Session, m_PreviewService,
                                               fromJson( arguments ) );
        nlohmann::json payload = previewResult.m_Payload;
        payload["session"] = sessionJson( *m_Session );

        if( !previewResult.m_Ok )
        {
            AI_TOOL_INVOCATION_RESULT result =
                    deniedResult( aRequest, aToolCall, previewResult.m_ErrorCode,
                                  previewResult.m_Message );
            result.m_ResultJson = resultJson( result, std::move( payload ) );
            return result;
        }

        const bool executed = m_PreviewService != nullptr;

        if( executed )
            rememberRenderedPreview( fromJson( arguments ) );

        wxString message = previewResult.m_Message.IsEmpty()
                                   ? wxString( wxS( "Session preview rendered." ) )
                                   : previewResult.m_Message;
        return allowedResult( aRequest, aToolCall, executed, message,
                              std::move( payload ) );
    }

    if( aToolCall.m_ToolName == wxS( "kisurf_run_validation" ) )
    {
        const bool hadOpenStep = m_Session->HasOpenStep();
        uint64_t stepId = 0;

        if( !hadOpenStep )
            stepId = m_Session->BeginStep( wxS( "session validation" ) );

        AI_ATOMIC_EXECUTION_RESULT execution = AI_ATOMIC_OPERATION_EXECUTOR::Execute(
                *m_Session, AI_SESSION_OPERATION_KIND::RunValidation,
                fromJson( arguments ) );

        if( !execution.m_Ok )
        {
            if( stepId != 0 )
                m_Session->FailStep( stepId, execution.m_Message );

            return deniedResult( aRequest, aToolCall, execution.m_ErrorCode,
                                 execution.m_Message );
        }

        wxString validationErrorCode;
        wxString validationMessage;

        if( !applyValidationServiceResult( *m_Session, fromJson( arguments ), execution,
                                           m_ValidationService, validationErrorCode,
                                           validationMessage ) )
        {
            if( stepId != 0 )
                m_Session->FailStep( stepId, validationMessage );

            return deniedResult( aRequest, aToolCall, validationErrorCode,
                                 validationMessage );
        }

        projectValidationIssuesToShadowMetadata( *m_Session, execution );

        AI_SESSION_OBSERVATION observation;

        if( stepId != 0 )
            observation = m_Session->EndStep( stepId );

        AI_PYTHON_OPERATION_REQUEST operation;
        operation.m_Kind = AI_SESSION_OPERATION_KIND::RunValidation;
        operation.m_ArgumentsJson = fromJson( arguments );

        nlohmann::json payload = maintenanceOperationResult( operation, execution );
        payload["step_id"] = stepId;
        payload["observation"] =
                stepId != 0 ? parseObjectJson( observation.AsJsonText() )
                            : nlohmann::json::object();
        payload["session"] = sessionJson( *m_Session );

        return allowedResult( aRequest, aToolCall, true,
                              wxS( "Session validation completed." ),
                              std::move( payload ) );
    }

    if( aToolCall.m_ToolName == wxS( "kisurf_accept_session" ) )
    {
        wxString baseHash = optionalString( arguments, "base_hash" );

        if( baseHash.IsEmpty() )
            baseHash = contextBaseHash( aRequest );

        const AI_CONTEXT_VERSION currentVersion = effectiveContextVersion( aRequest );

        if( m_AcceptAdapter )
        {
            const uint64_t sessionId = m_Session->SessionId();
            AI_ACCEPT_APPLY_RESULT applyResult = AI_ACCEPT_APPLIER::Apply(
                    *m_Session, baseHash, currentVersion, *m_AcceptAdapter );

            if( !applyResult.m_Ok )
            {
                return deniedResult( aRequest, aToolCall, applyResult.m_ErrorCode,
                                     applyResult.m_Message );
            }

            nlohmann::json payload = {
                { "status", "accepted" },
                { "session_id", sessionId },
                { "board_mutated", applyResult.m_BoardMutated },
                { "accept_replay", "applied" },
                { "applied_operation_count", applyResult.m_AppliedOperationCount }
            };
            clearSessionPreview( m_PreviewService, sessionId );
            clearPreviewState();
            stopSessionPythonWorker( m_PythonWorker.get() );
            m_Session.reset();
            return allowedResult( aRequest, aToolCall, true, applyResult.m_Message,
                                  std::move( payload ) );
        }

        if( !m_Session->AcceptSession( baseHash, currentVersion ) )
        {
            if( m_Session->Status() == AI_EXECUTION_SESSION_STATUS::Open
                && m_Session->BaseHash() == baseHash
                && m_Session->SelectionRevisionConflicts( currentVersion ) )
            {
                return deniedResult( aRequest, aToolCall, wxS( "selection_conflict" ),
                                     wxS( "Session selection changed after it was opened." ) );
            }

            return deniedResult( aRequest, aToolCall, wxS( "stale_session" ),
                                 wxS( "Session base hash does not match the live board." ) );
        }

        const uint64_t sessionId = m_Session->SessionId();
        nlohmann::json payload = { { "status", "accepted" },
                                   { "session_id", sessionId },
                                   { "board_mutated", false },
                                   { "accept_replay", "not_connected" } };
        clearSessionPreview( m_PreviewService, sessionId );
        clearPreviewState();
        stopSessionPythonWorker( m_PythonWorker.get() );
        m_Session.reset();
        return allowedResult( aRequest, aToolCall, true,
                              wxS( "Session accepted by the control plane." ),
                              std::move( payload ) );
    }

    if( aToolCall.m_ToolName == wxS( "kisurf_reject_session" ) )
    {
        const uint64_t sessionId = m_Session->SessionId();
        m_Session->RejectSession();
        clearSessionPreview( m_PreviewService, sessionId );
        clearPreviewState();
        stopSessionPythonWorker( m_PythonWorker.get() );
        m_Session.reset();
        return allowedResult( aRequest, aToolCall, true, wxS( "Session rejected." ),
                              { { "status", "rejected" },
                                { "session_id", sessionId },
                                { "board_mutated", false } } );
    }

    if( aToolCall.m_ToolName == wxS( "kisurf_cancel_session" ) )
    {
        const uint64_t sessionId = m_Session->SessionId();
        m_Session->CancelSession( optionalString( arguments, "reason" ) );
        clearSessionPreview( m_PreviewService, sessionId );
        clearPreviewState();
        stopSessionPythonWorker( m_PythonWorker.get() );
        m_Session.reset();
        return allowedResult( aRequest, aToolCall, true, wxS( "Session cancelled." ),
                              { { "status", "cancelled" },
                                { "session_id", sessionId },
                                { "board_mutated", false } } );
    }

    if( aToolCall.m_ToolName == wxS( "kisurf_close_session" ) )
    {
        const uint64_t sessionId = m_Session->SessionId();
        m_Session->CloseSession();
        clearSessionPreview( m_PreviewService, sessionId );
        clearPreviewState();
        stopSessionPythonWorker( m_PythonWorker.get() );
        m_Session.reset();
        return allowedResult( aRequest, aToolCall, true, wxS( "Session closed." ),
                              { { "status", "closed" },
                                { "session_id", sessionId },
                                { "board_mutated", false } } );
    }

    return deniedResult( aRequest, aToolCall, wxS( "unknown_tool" ),
                         wxS( "Tool is not an AI execution session tool." ) );
}


const AI_EXECUTION_SESSION* AI_SESSION_TOOL_CALL_HANDLER::ActiveSession() const
{
    return m_Session ? &( *m_Session ) : nullptr;
}


wxString AI_SESSION_TOOL_CALL_HANDLER::ToolCatalogJson() const
{
    return fromJson( sessionToolCatalogJson() );
}


AI_EXECUTION_SESSION& AI_SESSION_TOOL_CALL_HANDLER::openSessionFromRequest(
        const AI_PROVIDER_REQUEST& aRequest, const wxString& aBoardId,
        const wxString& aBaseHash )
{
    AI_EXECUTION_SESSION::OPEN_OPTIONS options;
    options.m_SessionId = m_NextSessionId++;
    options.m_EditorKind = effectiveEditorKind( aRequest );
    options.m_ContextVersion = effectiveContextVersion( aRequest );
    options.m_BoardId = aBoardId.IsEmpty() ? defaultBoardId( options.m_EditorKind ) : aBoardId;
    options.m_BaseHash = aBaseHash.IsEmpty() ? contextBaseHash( aRequest ) : aBaseHash;
    clearPreviewState();
    m_Session.emplace( std::move( options ) );

    if( m_ShadowBoardSeeder )
        m_ShadowBoardSeeder->Seed( *m_Session );

    return *m_Session;
}
